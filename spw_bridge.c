/*
 * Copyright (C) 2018-2026 Armin Luntzer (armin.luntzer@univie.ac.at),
 * Roland Ottensamer (roland.ottensamer@univie.ac.at)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/*
 *
 *
 * supports a pseudo-RMAP protocol via a separate port
 * format:
 * 	1 byte    :1byte:4 bytes                  :4 bytes    :1 byte        :1 byte
 * 	<spw addr><read>:<32-bit starting address>:<data size>:<data 1>: ... :<data n>
 * notes:
 *   - because of restrictions in the RMAP protocol, data size cannot be
 *     larger than 2^24-1 bytes, i.e. the upper byte will be ignored
 *   - byte order of fields is is big endian
 *   - to generate a "write" command, set the second byte to, if 0, "read"
 *     is assumed
 *   - only incremental read/write operations are supported in this simplified
 *     protocol (we can just pass on RMAP packets for more complex stuff...)
 *
 *
 *
 * BUGS:
 *  - in raw mode, one recv() defines one SpW packet, so back-to-back
 *    packets may be merged and a partially delivered packet may be split
 *  - RMAP packets received on the shared data connection are framed the
 *    same way
 *  - maybe lots of others
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <time.h>

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <stdbool.h>
#include <stdint.h>

#include <spw_bridge.h>
#include <debug.h>
#include <kbd.h>

#include <ccsds_pkt.h>
#include <ecss_pkt.h>
#include <crc16.h>


static void sigint_handler(__attribute__((unused)) int s)
{
	printf("\nCaught signal %d\n", s);
}


static void print_usage(const char *prog, struct bridge_cfg *cfg)
{
	printf("\nUsage: %s [OPTIONS]\n", prog);
	printf("  -i DEVNUM                 SpW device id to use (default %u)\n", cfg->dev_num);
	printf("  -c CHANNEL                SpW channel to use (default %u)\n", cfg->channel);
	printf("  -n 01:1a:cc:..            SpW address nodes route/path in hex bytes (1 byte per node)\n");
	printf("  -p PORT                   local port number (default %u)\n", cfg->port);
	printf("  -s ADDRESS                local source address (default: %s:%u\n", cfg->host, cfg->port);
	printf("  -r ADDRESS:PORT           client mode: address and port of remote target\n");
	printf("  -u                        use UDP instead of TCP/IP\n");
	printf("  -d NUM                    number of header bytes to drop from incoming SpW packet (default %zu)\n", cfg->skip_header_bytes);
	printf("  -t timeout (µs)           throttle transmission of SpW packets by inserting a delay between packets (default %u)\n", cfg->pkt_throttle_usec);
	printf("  -S LINKSPEED              link speed in Mbit/s (default %g)\n", DEFAULT_LINK_SPEED);
	printf("  -L LINKID                 id of link to set speed for; needed with Brick Mk2 port 2; (default link_id = channel). \n");
	printf("  -P                        parse network byte stream for PUS packets\n");
	printf("  -D                        print decoded PUS-C packet headers and payload (requires -P; PUS-C types only)\n");
	printf("  -C                        disable the PUS CRC16 check (PUS packets always carry a CRC16 per ECSS-E-ST-70-41C, disable only for tailored streams without one)\n");
	printf("  -F                        parse network byte stream for FEE data packets\n");
	printf("  -R RMAP_PORT              exchange RMAP via RMAP_PORT\n");
	printf("  -M CHANNEL1:CHANNEL2     monitor mode: bridge the given SpW channels, copying packets verbatim between them\n");
	printf("  -E                        decode RMAP packets in the debug printout (requires -M and -D)\n");
	printf("  -G                        use GRESB protocol for network exchange\n");
	printf("  -X                        execute a device reset\n");
	printf("  -h, --help                print this help and exit\n");
	printf("\n");
}


static int parse_port(uint32_t *port, const char *arg)
{
	long tmp;

	tmp = strtol(arg, NULL, 0);
	if (tmp < 1 || tmp > 65535) {
		printf("invalid port %ld, valid ones are 1-65535\n", tmp);
		return -1;
	}

	(*port) = (uint32_t)tmp;
	return 0;
}


static void parse_path(char *list, uint8_t *path, uint16_t *len, size_t max_nodes)
{
	long tmp;

	char *node_addr;


	node_addr = strtok(list, ":");
	while (node_addr) {
		if ((*len) >= max_nodes) {
			printf("too many path nodes for -n, keeping the first %zu\n",
			       max_nodes);
			break;
		}

		tmp = strtol(node_addr, NULL, 16);
		if (tmp < 0 || tmp > 255)
			printf("path node %s out of range, truncating\n", node_addr);

		path[(*len)] = (uint8_t)tmp;
		(*len)++;
		node_addr = strtok(NULL, ":");
	}
}


static void resolve_host_addr(char *arg, char *host, size_t host_sz)
{
	int ret;

	struct addrinfo *res;
	char *node_addr;


	ret = getaddrinfo(arg, NULL, NULL, &res);
	if (ret) {
		printf("error in getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	node_addr = strtok(arg, ":");
	if (node_addr)
		snprintf(host, host_sz, "%s", node_addr);

	while (res) {
		if (res->ai_family == AF_INET) {
			inet_ntop(res->ai_family, &((struct sockaddr_in *)res->ai_addr)->sin_addr, host, (socklen_t)host_sz);
			break;	/* just take the first one and hope for the best */
		}

		res = res->ai_next;
	}
	freeaddrinfo(res);
}


static void bridge_cfg_init(struct bridge_cfg *cfg)
{
	cfg->mode = MODE_SERVER;
	cfg->port = DEFAULT_PORT;
	cfg->rmap_port = DEFAULT_RMAP_PORT;
	cfg->channel = DEFAULT_CHAN;
	cfg->link_id = cfg->channel;
	cfg->dev_num = 0;
	cfg->reset_dev = false;
	cfg->sig_rate = DEFAULT_LINK_SPEED;
	cfg->path_len = 0;
	cfg->skip_header_bytes = 0;
	cfg->pkt_throttle_usec = 0;
	cfg->interpret_pus = false;
	cfg->interpret_fee = false;
	cfg->interpret_rmap = false;
	cfg->enable_monitor = false;
	cfg->channel2 = 0;
	cfg->enable_rmap = false;
	cfg->enable_gresb = false;
	cfg->pus_debug = false;
	cfg->crc_check = true;
	snprintf(cfg->host, sizeof(cfg->host), "%s", DEFAULT_ADDR);
}


static void opt_dev_num(struct bridge_cfg *cfg)
{
	cfg->dev_num = (uint32_t)strtol(optarg, NULL, 0);
}


static void opt_channel(struct bridge_cfg *cfg)
{
	cfg->channel = (uint32_t)strtol(optarg, NULL, 0);
	cfg->link_id = cfg->channel;
}


static void opt_path(struct bridge_cfg *cfg)
{
	parse_path(optarg, cfg->path, &cfg->path_len, sizeof(cfg->path));
}


static void opt_port(struct bridge_cfg *cfg)
{
	if (parse_port(&cfg->port, optarg))
		exit(EXIT_FAILURE);
}


static void opt_source(struct bridge_cfg *cfg)
{
	resolve_host_addr(optarg, cfg->host, sizeof(cfg->host));
}


static void opt_client(struct bridge_cfg *cfg)
{
	char *node_addr;

	cfg->mode = MODE_CLIENT;

	/* redundant, the resolved address below repeats this split */
	node_addr = strtok(optarg, ":");
	if (!node_addr) {
		printf("error: -r requires a host, use <address>:<port>\n");
		exit(EXIT_FAILURE);
	}

	snprintf(cfg->host, sizeof(cfg->host), "%s", node_addr);
	node_addr = strtok(NULL, ":");
	if (!node_addr) {
		printf("error: -r requires a port, use <address>:<port>\n");
		exit(EXIT_FAILURE);
	}

	if (parse_port(&cfg->port, node_addr))
		exit(EXIT_FAILURE);

	resolve_host_addr(optarg, cfg->host, sizeof(cfg->host));
}


static void opt_skip_header(struct bridge_cfg *cfg)
{
	long tmp;

	tmp = strtol(optarg, NULL, 0);
	if (tmp < 0)
		tmp = 0;

	cfg->skip_header_bytes = (size_t)tmp;
}


static void opt_throttle(struct bridge_cfg *cfg)
{
	long tmp;

	tmp = strtol(optarg, NULL, 0);
	if (tmp < 0) {
		printf("invalid throttle delay %ld us, using 0\n", tmp);
		tmp = 0;
	}

	cfg->pkt_throttle_usec = (useconds_t)tmp;
}


static void opt_link_speed(struct bridge_cfg *cfg)
{
	cfg->sig_rate = (double)strtoul(optarg, NULL, 0);
	if (cfg->sig_rate < 2.0 || cfg->sig_rate > 400.) {
		printf("\nInvalid link rate %g Mbps, must be between 2 and 400\n\n",
		       cfg->sig_rate);
		exit(-1);
	}
}


static void opt_link_id(struct bridge_cfg *cfg)
{
	long tmp;

	tmp = strtol(optarg, NULL, 0);
	if (tmp < 0 || tmp > 255) {
		printf("invalid link id %ld, valid ones are 0-255\n", tmp);
		exit(EXIT_FAILURE);
	}

	cfg->link_id = (uint32_t)tmp;
}


static void opt_pus(struct bridge_cfg *cfg)
{
	cfg->interpret_pus = true;
}


static void opt_pus_debug(struct bridge_cfg *cfg)
{
	cfg->pus_debug = true;
}


static void opt_no_crc(struct bridge_cfg *cfg)
{
	cfg->crc_check = false;
}


static void opt_fee(struct bridge_cfg *cfg)
{
	cfg->interpret_fee = true;
}


static void opt_gresb(struct bridge_cfg *cfg)
{
	cfg->enable_gresb = true;
}


static void opt_rmap(struct bridge_cfg *cfg, char **argv)
{
	long p;

	char *endp;

	if (argv[optind]) {
		p = strtol(argv[optind], &endp, 0);
		if ((*endp) == '\0' && p > 0 && p <= 65535)
			cfg->rmap_port = (uint32_t)p;
	}

	cfg->enable_rmap = true;
}


static void opt_udp(struct bridge_cfg *cfg)
{
	cfg->mode = MODE_DGRAM;
}


static void opt_monitor(struct bridge_cfg *cfg)
{
	long chan1;
	long chan2;

	char *endp;

	char *chans;


	chans = optarg;
	chan1 = strtol(chans, &endp, 0);
	if ((*endp) != ':' || endp == chans) {
		printf("error: -M requires CHANNEL1:CHANNEL2, use e.g. -M 1:2\n");
		exit(EXIT_FAILURE);
	}

	chans = endp + 1;
	chan2 = strtol(chans, &endp, 0);
	if ((*endp) != '\0' || endp == chans) {
		printf("error: -M requires CHANNEL1:CHANNEL2, use e.g. -M 1:2\n");
		exit(EXIT_FAILURE);
	}

	cfg->channel = (uint32_t)chan1;
	cfg->channel2 = (uint32_t)chan2;
	cfg->link_id = (uint32_t)chan1;
	cfg->enable_monitor = true;
}


static void opt_rmap_debug(struct bridge_cfg *cfg)
{
	cfg->interpret_rmap = true;
}


static void opt_reset(struct bridge_cfg *cfg)
{
	cfg->reset_dev = true;
}


static void setup_signals(void)
{
	struct sigaction SIGINT_handler;

	SIGINT_handler.sa_handler = sigint_handler;
	sigemptyset(&SIGINT_handler.sa_mask);
	SIGINT_handler.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_handler, NULL);
	sigaction(SIGTERM, &SIGINT_handler, NULL);
	sigaction(SIGHUP, &SIGINT_handler, NULL);

	signal(SIGPIPE, SIG_IGN);
}


static void parse_options(struct bridge_cfg *cfg, int argc, char **argv)
{
	int opt;


	while ((opt = getopt(argc, argv, "i:c:n:p:s:r:d:t:L:S:uCDPFGXRM:Eh")) != -1) {
		switch (opt) {
		case 'i':
			opt_dev_num(cfg);
			break;
		case 'c':
			opt_channel(cfg);
			break;
		case 'n':
			opt_path(cfg);
			break;
		case 'p':
			opt_port(cfg);
			break;
		case 's':
			opt_source(cfg);
			break;
		case 'r':
			opt_client(cfg);
			break;
		case 'd':
			opt_skip_header(cfg);
			break;
		case 't':
			opt_throttle(cfg);
			break;
		case 'S':
			opt_link_speed(cfg);
			break;
		case 'L':
			opt_link_id(cfg);
			break;
		case 'P':
			opt_pus(cfg);
			break;
		case 'D':
			opt_pus_debug(cfg);
			break;
		case 'C':
			opt_no_crc(cfg);
			break;
		case 'F':
			opt_fee(cfg);
			break;
		case 'G':
			opt_gresb(cfg);
			break;
		case 'u':
			opt_udp(cfg);
			break;
		case 'R':
			opt_rmap(cfg, argv);
			break;
		case 'M':
			opt_monitor(cfg);
			break;
		case 'E':
			opt_rmap_debug(cfg);
			break;
		case 'X':
			opt_reset(cfg);
			break;

		case 'h':
		default:
			print_usage(argv[0], cfg);
			exit(0);
		}
	}
}


static void pus_debug_print_monitor(struct bridge_cfg *cfg, const uint8_t *pkt,
				    size_t len)
{
	size_t i;

	struct rmap_pkt *rmap_pkt;


	/* monitor mode: no PUS framing is expected on the copied packets,
	 * optionally decode them as RMAP, otherwise show the raw payload
	 */
	if (cfg->interpret_rmap) {
		rmap_pkt = rmap_pkt_from_buffer((uint8_t *)pkt, len);
		if (!rmap_pkt) {
			printf("  not an RMAP packet\n");
		} else {
			printf("  RMAP: %s %s dst=0x%02x key=0x%02x "
			       "src=0x%02x tr_id=%u addr=0x%08x "
			       "data_len=%u hdr_crc=0x%02x data_crc=0x%02x\n",
			       rmap_pkt->ri.cmd_resp ? "CMD" : "REPLY",
			       (rmap_pkt->ri.cmd & RMAP_CMD_BIT_WRITE) ? "WRITE" : "READ",
			       (uint32_t)rmap_pkt->dst,
			       (uint32_t)rmap_pkt->key,
			       (uint32_t)rmap_pkt->src,
			       (uint32_t)rmap_pkt->tr_id,
			       (uint32_t)rmap_pkt->addr,
			       (uint32_t)rmap_pkt->data_len,
			       (uint32_t)rmap_pkt->hdr_crc,
			       (uint32_t)rmap_pkt->data_crc);

			rmap_erase_packet(rmap_pkt);
		}
	}

	printf("  payload (%zu bytes):\n", len);
	for (i = 0; i < len; i++) {
		printf("%02x ", (uint32_t)pkt[i]);

		if ((i & 0xf) == 0xf)
			printf("\n");
	}

	if (len && (len & 0xf))
		printf("\n");

	printf("\n");
}


void pus_debug_print(struct bridge_cfg *cfg, const char *dir, const uint8_t *pkt,
		     size_t len)
{
	uint8_t tmtc;
	uint8_t tref;

	size_t avail;
	size_t pld_len;
	size_t pld_off;
	size_t i;

	uint32_t ms;

	struct timespec now;
	struct tm tmv;

	uint8_t *pld;
	uint8_t ts[PUS_C_TM_TIME_OCTETS];
	char tsbuf[64];


	if (!cfg->pus_debug)
		return;

	if (!cfg->enable_monitor && !cfg->interpret_pus)
		return;

	clock_gettime(CLOCK_REALTIME, &now);
	ms = (uint32_t)(now.tv_nsec / 1000000);
	localtime_r(&now.tv_sec, &tmv);
	strftime(tsbuf, sizeof(tsbuf), "%F %T", &tmv);
	printf("[%s.%03u] %s\n", tsbuf, ms, dir);

	if (cfg->enable_monitor) {
		pus_debug_print_monitor(cfg, pkt, len);
		return;
	}

	if (len < 6) {
		printf("  short packet (%zu bytes), not a CCSDS packet\n", len);
		return;
	}

	tmtc = ccsds_get_pkt_type((uint8_t *)pkt);

	printf("  CCSDS: type=%s APID=0x%03X seq_flags=%u seq_count=%u "
	       "data_len=%u\n",
	       tmtc ? "TC" : "TM",
	       (uint32_t)ccsds_get_apid((uint8_t *)pkt),
	       (uint32_t)ccsds_get_seq_flags((uint8_t *)pkt),
	       (uint32_t)ccsds_get_seq_cnt((uint8_t *)pkt),
	       (uint32_t)ccsds_get_data_len((uint8_t *)pkt));

	if (!ecss_is_pkt_valid((uint8_t *)pkt, NULL)) {
		printf("  not a valid ECSS PUS packet\n");
		return;
	}

	if (ecss_get_pkt_version((uint8_t *)pkt) != PUS_C_VERSION) {
		printf("  not a PUS-C packet (PUS version %u)\n",
		       (uint32_t)ecss_get_pkt_version((uint8_t *)pkt));
		return;
	}

	if (tmtc) {
		printf("  PUS-C: ST=%u SST=%u source_id=0x%04x ACK=0b%u%u%u%u\n",
		       (uint32_t)ecss_get_service_type((uint8_t *)pkt),
		       (uint32_t)ecss_get_sub_service_type((uint8_t *)pkt),
		       (uint32_t)ecss_get_source_id((uint8_t *)pkt),
		       (uint32_t)ecss_get_exec_compl_ack((uint8_t *)pkt),
		       (uint32_t)ecss_get_exec_progr_ack((uint8_t *)pkt),
		       (uint32_t)ecss_get_exec_start_ack((uint8_t *)pkt),
		       (uint32_t)ecss_get_pkt_accept_ack((uint8_t *)pkt));
	} else {
		tref = ecss_get_sc_time_ref_status((uint8_t *)pkt);
		printf("  PUS-C: ST=%u SST=%u MTC=%u DEST_ID=0x%04X "
		       "T_REF_STS=%u%u%u%u TS=",
		       (uint32_t)ecss_get_service_type((uint8_t *)pkt),
		       (uint32_t)ecss_get_sub_service_type((uint8_t *)pkt),
		       (uint32_t)ecss_get_msg_type_cntr((uint8_t *)pkt),
		       (uint32_t)ecss_get_destination_id((uint8_t *)pkt),
		       (uint32_t)((tref >> 3) & 1),
		       (uint32_t)((tref >> 2) & 1),
		       (uint32_t)((tref >> 1) & 1),
		       (uint32_t)(tref & 1));

		if (ecss_get_timestamp((uint8_t *)pkt, ts) < 0)
			printf("unavailable");
		else
			for (i = 0; i < sizeof(ts); i++)
				printf("%02x%s", (uint32_t)ts[i],
				       i + 1 < sizeof(ts) ? ":" : "");

		printf("\n");
	}

	if (cfg->crc_check)
		printf("  CRC=%s\n", pus_pkt_crc_valid((uint8_t *)pkt, len) ? "OK" : "NOK");

	pld_len = ecss_get_pld_size((uint8_t *)pkt);
	pld_off = ecss_get_payload_offset((uint8_t *)pkt) / 8;
	if (pld_off >= len) {
		printf("  payload missing (%zu declared, header offset %zu)\n",
		       pld_len, pld_off);
		return;
	}

	avail = len - pld_off;
	if (pld_len > avail) {
		printf("  payload truncated: %zu declared, %zu received\n", pld_len, avail);
		pld_len = avail;
	}

	pld = (uint8_t *)pkt + pld_off;
	printf("  payload (%zu bytes):\n", pld_len);
	for (i = 0; i < pld_len; i++) {
		printf("%02x ", (uint32_t)pld[i]);

		if ((i & 0xf) == 0xf)
			printf("\n");
	}

	if (pld_len && (pld_len & 0xf))
		printf("\n");

	printf("\n");
}


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

int pus_pkt_crc_valid(const uint8_t *pkt, size_t len)
{
	uint16_t crc;
	uint16_t check;

	if (len <= PUS_ECSS_CRC_LEN)
		return 0;

	crc = crc16_buf((uint8_t *)pkt, len - PUS_ECSS_CRC_LEN);
	check = ((uint16_t)pkt[len - 2] << 8) | pkt[len - 1];

	return crc == check;
}


int main(int argc, char **argv)
{
	struct bridge_cfg cfg;


	bridge_cfg_init(&cfg);

	parse_options(&cfg, argc, argv);

	crc_init_lookup_table();

	cfg.pkt_sink = net_pkt_sink;

	net_start(&cfg);

	setup_signals();

	kbd_start(&cfg);

	spw_setup_device(&cfg);
	spw_start_link(&cfg);

	printf("Ready...\n");

	pause();

	kbd_stop();

	spw_request_shutdown(&cfg);
	net_stop(&cfg);
	spw_stop_poll(&cfg);
	net_release(&cfg);
	spw_dispose_ops(&cfg);
	spw_release(&cfg);

	return 0;
}