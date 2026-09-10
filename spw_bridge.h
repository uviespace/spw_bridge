/**
 * @file spw_bridge.h
 *
 * @brief public interface of the SpaceWire bridge: the shared configuration
 *        and the interfaces of the net and SpW subsystems
 */

#ifndef SPW_BRIDGE_H
#define SPW_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <unistd.h>

#include <rmap.h>


#define DEFAULT_PORT		1234
#define DEFAULT_ADDR		"0.0.0.0"
#define DEFAULT_CHAN		1
#define DEFAULT_RMAP_PORT	2345

#define DEFAULT_LINK_SPEED	10.0

/* largest single SpW packet the bridge will forward; RMAP data is at most
 * 2^24-1 bytes, the biggest packet the RMAP protocol can express
 */
#define MAX_SPW_PACKET_SIZE	(RMAP_MAX_DATA_LEN)


enum net_mode {MODE_SERVER, MODE_CLIENT, MODE_DGRAM};

struct bridge_cfg;
struct net_state;
struct spw_state;


/**
 * @brief callback invoked for every complete packet received on the SpW link
 *
 * @param cfg the bridge configuration
 * @param chan index of the SpW channel the packet was received on
 * @param buf received packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

typedef void (*bridge_pkt_sink)(struct bridge_cfg *cfg, uint32_t chan, uint8_t *buf,
				size_t len);


struct bridge_cfg {
	enum net_mode	mode;
	uint32_t	port;
	uint32_t	rmap_port;
	uint32_t	channel;
	uint32_t	channel2;
	uint32_t	link_id;
	uint32_t	dev_num;
	bool		reset_dev;
	double		sig_rate;
	uint16_t	path_len;
	size_t		skip_header_bytes;
	useconds_t	pkt_throttle_usec;
	bool		interpret_pus;
	bool		interpret_fee;
	bool		interpret_rmap;
	bool		enable_monitor;
	bool		enable_rmap;
	bool		enable_gresb;
	bool		pus_debug;
	bool		crc_check;
	char		host[128];
	uint8_t		path[256];
	bridge_pkt_sink		pkt_sink;
	struct net_state		*net;
	struct spw_state		*spw;
};


/**
 * @brief check if the SpW channel is open
 *
 * @param cfg the bridge configuration
 *
 * @return true when the SpW link is ready to carry traffic
 */

bool spw_link_ready(struct bridge_cfg *cfg);


/**
 * @brief transmit a packet on the SpaceWire link
 *
 * @param cfg bridge configuration
 * @param buf packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void spw_send_packet(struct bridge_cfg *cfg, uint8_t *buf, size_t len);


/**
 * @brief transmit a packet on one of the two SpaceWire links
 *
 * @param cfg bridge configuration
 * @param chan index of the SpW channel to transmit on (0 or 1)
 * @param buf packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void spw_send_packet_chan(struct bridge_cfg *cfg, uint32_t chan, uint8_t *buf,
			  size_t len);


/**
 * @brief issue a simplified RMAP read or write command on the SpW link
 *
 * @param cfg  bridge configuration
 * @param dst  target logical address
 * @param op   zero for a read, non-zero for a write
 * @param addr 32-bit starting address
 * @param data write payload, unused on reads
 * @param size size of the write payload, unused on reads
 */

void spw_rmap_cmd(struct bridge_cfg *cfg, uint8_t dst, uint8_t op, uint32_t addr,
		  uint8_t *data, size_t size);


/**
 * @brief set up the SpaceWire device, channel and routing path
 *
 * @param cfg the bridge configuration; the channel is updated in place
 */

void spw_setup_device(struct bridge_cfg *cfg);


/**
 * @brief bring the SpW link into a running state
 *
 * @param cfg the bridge configuration
 */

void spw_start_link(struct bridge_cfg *cfg);


/**
 * @brief flag the SpW polling thread to stop
 *
 * @param cfg the bridge configuration
 *
 * @note set it before tearing down the network threads so no further packets
 *	 are forwarded while the net side winds down
 */

void spw_request_shutdown(struct bridge_cfg *cfg);


/**
 * @brief wake the SpW polling thread and wait for it to wind down
 *
 * @param cfg the bridge configuration
 */

void spw_stop_poll(struct bridge_cfg *cfg);


/**
 * @brief dispose the transfer operations handed back by the SpW thread
 *
 * @param cfg the bridge configuration
 *
 * @note cancelled transfer operations are already freed by the API
 */

void spw_dispose_ops(struct bridge_cfg *cfg);


/**
 * @brief release the SpaceWire device resources
 *
 * @param cfg the bridge configuration
 */

void spw_release(struct bridge_cfg *cfg);


/**
 * @brief set up the network side of the bridge and start its threads
 *
 * @param cfg the bridge configuration
 */

void net_start(struct bridge_cfg *cfg);


/**
 * @brief cancel and join the network threads
 *
 * @param cfg the bridge configuration
 */

void net_stop(struct bridge_cfg *cfg);


/**
 * @brief release the network side of the bridge
 *
 * @param cfg the bridge configuration
 *
 * @note call only after the SpW polling thread has wound down, its receive
 *	 callback dereferences the network state until then
 */

void net_release(struct bridge_cfg *cfg);


/**
 * @brief handle a complete packet received on the SpW link
 *
 * @param cfg the bridge configuration
 * @param chan index of the SpW channel the packet was received on
 * @param buf received packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void net_pkt_sink(struct bridge_cfg *cfg, uint32_t chan, uint8_t *buf, size_t len);


/**
 * @brief print a decoded PUS-C header and payload of a packet
 *
 * @param cfg the bridge configuration; printing is only active when enabled
 *	      on the command line and PUS interpretation is turned on
 * @param dir direction string of the packet flow, i.e. NET->SPW or SPW->NET
 * @param pkt packet bytes at the CCSDS packet start, past any path header
 * @param len size of the packet in bytes
 */

void pus_debug_print(struct bridge_cfg *cfg, const char *dir, const uint8_t *pkt,
		     size_t len);


/**
 * @brief verify the CRC16 field of a PUS packet
 *
 * @param pkt packet bytes at the CCSDS packet start
 * @param len size of the packet in bytes
 *
 * @return 1 when the trailing CRC16 matches the packet content, 0 otherwise
 *
 * @note the packet is assumed to carry the two CRC16 bytes mandated by
 *	 ECSS-E-ST-70-41C 7.4.3.2d; calls crc16_buf(), so the lookup table
 *	 must be initialised with crc_init_lookup_table() before first use
 */

int pus_pkt_crc_valid(const uint8_t *pkt, size_t len);


#endif /* SPW_BRIDGE_H */
