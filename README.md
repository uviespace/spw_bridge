# spw_bridge

A software bridge between a SpaceWire link and a TCP/IP or UDP connection to a
workstation. It operates a SpaceWire interface device through the STAR API and
relays packets between the SpW link and the network endpoint in either
direction. It is intended for ground support equipment, e.g. to operate the
front-end electronics of an instrument through a SpaceWire-to-Ethernet path, or
to debug a LEON3-FT-based target such as a GR712RC through RMAP.

## Building

The build requires the STAR API support software from STAR-Dundee Ltd. (the
headers under `star-system/inc/star`, plus the STAR API, the RMAP packet
library and the device configuration libraries). A GCC toolchain, GNU make and
a bash shell are otherwise sufficient.

	make
	make DEBUG=2     # enable per-packet DBG traces (default 1)

`make` produces the `spw_bridge` executable in the current directory;
`make clean` removes the build output.

## Synopsis

	./spw_bridge [OPTIONS]

The bridge runs until it is interrupted; SIGINT, SIGTERM and SIGHUP terminate
the program and shut down the device cleanly.

	-i DEVNUM        SpW device id (default 0)
	-c CHANNEL       SpW channel (default 1)
	-n PATH          routing path, colon-separated hex bytes, one per node (up to 256)
	-p PORT          local data port (default 1234)
	-s ADDRESS       local source address (default 0.0.0.0)
	-r HOST:PORT     client mode: address and port of the remote target
	-u               use UDP instead of TCP
	-d NUM           header bytes to drop from incoming SpW packets (default 0)
	-t USEC          throttle delay before each SpW packet (default 0)
	-S MBPS          link speed in Mbit/s (default 10, valid range 2-400)
	-L LINKID        id of the link whose speed is set (default: the channel)
	-P               parse the network byte stream for PUS packets
	-D               print decoded PUS-C headers and payloads (requires -P)
	-N               suppress payload bytes in the debug printout (short form)
	-C               disable the PUS CRC16 check
	-F               parse the network byte stream for FEE data packets
	-R [PORT]        enable the pseudo-RMAP service on PORT (default 2345)
	-M C1:C2         monitor mode: bridge the given SpW channels, copying
	                 packets verbatim between them
	-E               decode RMAP packets in the debug printout (requires -M and -D)
	-G               use the GRESB protocol for the network exchange
	-X               perform a device reset before opening the channel
	-h, --help       print the usage message and exit

A routing path given with `-n` is a colon-separated list of hex byte values,
one per node (e.g. `01:24:00:00:00`); out-of-range values are truncated to
8 bits.

## Operating Modes

* server mode (default): the bridge listens for multiple TCP connections;
  every connected peer receives the SpW traffic and data from any peer is
  transmitted on the SpW link.

* client mode (`-r`): the bridge connects to `HOST:PORT` and operates a single
  connection; if the link is lost, the bridge terminates.

* UDP mode (`-u`): one datagram per SpW packet; peers are registered as they
  are seen, i.e. the first datagram from a host registers it as a recipient.

## PUS Mode

With `-P` the network stream is framed as PUS packets, the CCSDS sequence
counter is checked per APID in both directions and the PUS CRC16 is verified; a failed
CRC check is reported on stderr, a sequence mismatch is reported in the `-D`
debug printout. Packets are never dropped. `-D` additionally
prints a decoded CCSDS/PUS-C header and payload dump for every packet in both
directions; the printout can be toggled on and off from the terminal with
`d`/`D` while the bridge is running (requires stdin to be a terminal). `-N`
restricts the printout to the decoded header lines (no payload bytes); the
short form is toggled with `s`/`S`.

## Monitor Mode

Monitor mode turns the bridge into a passive tap between two SpW channels of
the same device:

	./spw_bridge -M 1:2

Every packet received on one channel is copied verbatim to the other; no
routing header is added or stripped, no header bytes are dropped and the
forwarded SpW transactions stay atomic. The network interface keeps serving
clients but only observes: everything received from the network is discarded,
and the connected peers see a mirror of the copied traffic in both directions.
`-S` (link speed) is applied to both channels; `-c` and `-d` are ignored.

`-D` prints the copied direction (`SPW[1]->SPW[2]`) and a raw payload dump; add
`-E` to decode the packets as RMAP (CMD/REPLY, WRITE/READ, destination and
initiator addresses, transaction id, data address and length, and the header
and data CRCs):

	./spw_bridge -M 1:2 -D -E

`-N` switches the printout to the short form: only the direction line (and,
with `-E`, the decoded RMAP header line) is printed, payload bytes and the
"not an RMAP packet" announcement are suppressed. The short form can also be
toggled with `s`/`S` from the terminal while the bridge is running.

## Examples

Server mode with a PCIe card, transmit/receive on channel 2:

	./spw_bridge -c 2

Brick MK II (the channel is always 1), configure the signalling rate of link 2
and send packets via route 2 to the remote SpW device with node address 0x14:

	./spw_bridge -c 1 -n 2:14 -L 2
	./spw_bridge -c 1 -n 2:14 -L 2 -r localhost:1234  # client mode

Use in the CHEOPS EGSE, with the real DPU connected to the Brick, for link 1
and link 2:

	./spw_bridge -c 1 -n 01:24:00:00:00 -d 4 -L 1 -p 5573 -P
	./spw_bridge -c 1 -n 02:24:00:00:00 -d 4 -L 2 -p 5573 -P

The first route byte is consumed by the Brick as the link number, the next is
the address of the DPU (0x24) and the remaining three bytes are consumed by the
DPU; `-d 4` drops the four-byte SpW address header attached by the real DPU.

Use in the ARIEL mission (PCIe MK II card): PUS framing on port 5573 at
60 Mbit/s on channel 3. The channel flag selects the physical SpW port of the
card and may need adjustment depending on which port is wired to the target:

	./spw_bridge -i0 -P -n52:02:00:00 -p5573 -S60 -d4 -c3

GRESB protocol with grmon for RMAP access: connect the target SpW link to a
port of the device that supports RMAP (on the GR712RC only link 0 or 1), start
the bridge with the GRESB option at 10 Mbit/s and forward the GRESB base ports
(3000, 3001) to the data port of the bridge:

	./spw_bridge -c 1 -S 10 -G
	socat tcp-l:3000,fork,reuseaddr tcp:127.0.0.1:1234
	socat tcp-l:3001,fork,reuseaddr tcp:127.0.0.1:1234
	grmon -gresb 127.0.0.1

If the GRSPW2 SpW port is not initialised (no boot ROM present), initialise the
core first through JTAG or another debug interface, e.g. GRSPW0:

	grmon> wmem 0x80100800 0xA0010006
	grmon> wmem 0x8010080C 0x00000909

## Documentation

The full user manual is maintained under `um/` (`SPW_BRIDGE-UVIE-UM-001.pdf`).