/**
 * @file spw.c
 *
 * @copyright GPLv2
 * Copyright (C) 2018-2026 Armin Luntzer (armin.luntzer@univie.ac.at)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * @brief SpaceWire subsystem of the SpaceWire bridge: link setup, packet
 *        transmission and the receive loop that hands packets to the net
 *        subsystem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <stdbool.h>
#include <stdint.h>

#include <pthread.h>

#include <spw_bridge.h>
#include <rmap.h>
#include <debug.h>

#include <star/star-api.h>
#include <star/cfg_api_mk2.h>
#include <star/cfg_api_brick_mk2.h>
#include <star/cfg_api_brick_mk3.h>
#include <star/cfg_api_pci_mk2.h>
#include <star/cfg_api_pcie_mk2.h>
#include <star/cfg_api_generic.h>
#include <star/rmap_packet_library.h>


/* RMAP replies are sent from this logical address to the target key below */
#define RMAP_DEFAULT_SRC	0x30
#define RMAP_DEFAULT_KEY	0xab


struct spw_poll_arg {
	struct bridge_cfg	*cfg;
	uint32_t		chan;
};


/* everything the SpW subsystem keeps across its threads, allocated with the
 * device in spw_setup_device() and freed by spw_release(); the channel-indexed
 * members carry state for the two channels used in monitor mode
 */
struct spw_state {
	STAR_DEVICE_ID		dev_id;
	STAR_SPACEWIRE_ADDRESS	*p_address;
	STAR_CHANNEL_ID		spw_chan_id[2];
	STAR_TRANSFER_STATUS	tx_status;
	volatile bool		shutdown;
	pthread_t		th_spw_poll[2];
	pthread_mutex_t		pus_op_lock;
	struct spw_poll_arg	poll_arg[2];
	STAR_TRANSFER_OPERATION *pus_tx_transfer_op[2];
	STAR_TRANSFER_OPERATION *pus_rx_transfer_op[2];
};


static STAR_TRANSFER_OPERATION *spw_setup_rx_op(struct bridge_cfg *cfg, uint32_t chan)
{
	STAR_TRANSFER_OPERATION *p_rx_transfer_op;

	p_rx_transfer_op = STAR_createRxOperation(1, STAR_RECEIVE_PACKETS);
	if (!p_rx_transfer_op) {
		printf("Error creating transfer operation\n");
		exit(EXIT_FAILURE);
	}

	pthread_mutex_lock(&cfg->spw->pus_op_lock);

	if (cfg->spw->pus_rx_transfer_op[chan])
		STAR_disposeTransferOperation(cfg->spw->pus_rx_transfer_op[chan]);

	cfg->spw->pus_rx_transfer_op[chan] = p_rx_transfer_op;
	pthread_mutex_unlock(&cfg->spw->pus_op_lock);

	if (!STAR_submitTransferOperation(cfg->spw->spw_chan_id[chan], p_rx_transfer_op)) {
		printf("Error during transfer submission\n");
		exit(EXIT_FAILURE);
	}

	return p_rx_transfer_op;
}


static void *poll_spw(void *ptr)
{
	uint32_t i;

	uint32_t chan;

	uint32_t spw_recv_bytes;

	STAR_TRANSFER_STATUS rx_status;

	uint8_t *spw_recv_buffer;
	struct bridge_cfg *cfg;
	struct spw_poll_arg *arg;

	STAR_SPACEWIRE_PACKET *p_spw_packet;
	STAR_TRANSFER_OPERATION *p_rx_transfer_op;


	arg = (struct spw_poll_arg *)ptr;
	cfg = arg->cfg;
	chan = arg->chan;

	spw_recv_buffer = NULL;

	while (1) {

		if (cfg->spw->shutdown)
			break;

		if (spw_recv_buffer)
			STAR_destroyPacketData(spw_recv_buffer);

		spw_recv_buffer = NULL;

		p_rx_transfer_op = spw_setup_rx_op(cfg, chan);

		if (cfg->spw->shutdown)
			break;

		rx_status = STAR_waitOnTransferOperationCompletion(p_rx_transfer_op, -1);
		if (rx_status == STAR_TRANSFER_STATUS_CANCELLED) {
			printf("TRANSFER CANCELLED!\n");
			break;
		}

		if (cfg->spw->shutdown)
			break;

		if (rx_status != STAR_TRANSFER_STATUS_COMPLETE) {
			printf("TRANSFER INCOMPLETE!\n");
			continue;
		}

		p_spw_packet = (STAR_SPACEWIRE_PACKET *) STAR_getTransferItem(p_rx_transfer_op, 0)->item;

		spw_recv_buffer = STAR_getPacketData(p_spw_packet, &spw_recv_bytes);

		DBG("SPW->PC: ");
		for (i = 0; i < spw_recv_bytes; i++)
			DBG("%02x", spw_recv_buffer[i]);
		DBG("\n");

		if (cfg->pkt_sink)
			cfg->pkt_sink(cfg, chan, spw_recv_buffer, spw_recv_bytes);
	}

	/* reached on shutdown: the current packet and transfer operation are
	 * left for the shutdown path, which disposes them after this thread
	 * has been joined; a cancelled operation is freed by the API already
	 */
	return NULL;
}


static STAR_DEVICE_ID select_device(uint32_t dev_num)
{

	uint32_t i;
	U32 num_devs;
	STAR_DEVICE_ID dev;

	char *p_str;

	STAR_DEVICE_ID *dev_list;


	dev_list = STAR_getDeviceList(&num_devs);
	if (!dev_list) {
		printf("No SpaceWire devices detected!");
		exit(EXIT_FAILURE);
	}

	printf("%u device(s) detected:\n", num_devs);

	for (i = 0; i < num_devs; i++) {
		if (dev_list[i]) {
			p_str = STAR_getDeviceName(dev_list[i]);
			if (p_str) {
				printf("\t[%u]: %s\n", i, p_str);
				STAR_destroyString(p_str);
			} else {
				printf("\t[%u]: invalid device descriptor string\n", i);
			}
		} else {
			printf("\t[%u]: non-fatal error: cannot enumerate device\n", i);
		}
	}

	if (dev_num >= num_devs) {
		printf("Cannot select device with number %u, only %u available\n", dev_num, num_devs);
		STAR_destroyDeviceList(dev_list);
		exit(EXIT_FAILURE);
	} else {
		dev = dev_list[dev_num];
		if (!dev) {
			printf("Cannot select device with number %u, failed to enumerate it\n", dev_num);
			STAR_destroyDeviceList(dev_list);
			exit(EXIT_FAILURE);
		}

		STAR_destroyDeviceList(dev_list);
		return dev;
	}
}


static uint32_t select_channel(STAR_DEVICE_ID dev, uint32_t channel)
{
	uint32_t i;

	STAR_CHANNEL_MASK channel_mask;

	channel_mask = STAR_getDeviceChannels(dev);
	if (!channel_mask || (channel_mask == 0x1)) {
		printf("Error: no valid channels on device\n");
		exit(EXIT_FAILURE);
	}

	if (channel > 31) {
		printf("Invalid channel number %u, valid ones are 0-31\n", channel);
		exit(EXIT_FAILURE);
	}

	if (channel_mask & (0x1u << channel)) {

		return channel;

	} else {

		printf("Non-existent channel, available channels are: ");

		for (i = 1; i < 32; i++) {
			if (channel_mask & (0x1u << i))
				printf("%u ", i);
		}

		printf("\n");
		exit(EXIT_FAILURE);
	}
}


static void set_link_speed(STAR_DEVICE_ID dev, uint32_t link_id, double sig_rate)
{
	U32 sig_rate_u32;

	printf("Setting link speed to %g Mbps\n", sig_rate);
	sig_rate_u32 = (U32)sig_rate;
	if (CFG_setTransmitSignallingRate(dev, (U8)link_id, sig_rate_u32)) {
		printf("Failed to set link speed for link %u\n", link_id);
		exit(EXIT_FAILURE);
	}

	CFG_getTransmitSignallingRate(dev, (U8)link_id, &sig_rate_u32);
	printf("Actual link speed is %u Mbps\n", sig_rate_u32);
}


static void print_path(const uint8_t *path, uint16_t path_len)
{
	int i;

	printf("Using path of %u nodes: ", path_len);
	for (i = 0; i < path_len; i++)
		printf(":%x", path[i]);
	printf(":\n");
}


static void start_link(struct bridge_cfg *cfg, uint32_t chan, uint32_t link_id)
{
	U16 link_speed_u16;

	PORT_STATUS_CONTROL port_status;
	STAR_CFG_SPW_LINK_STATUS link_status;


	/* make sure the link is running */
	if (CFG_getPortStatusControl(cfg->spw->dev_id, (U8)chan, &port_status)) {
		printf("Failed to read port status control\n");
		port_status = 0;
	}

	if (CFG_getSpaceWireLinkStatus(port_status, &link_status)) {
		printf("Failed to read link status, forcing a clean slate\n");
		memset(&link_status, 0, sizeof(link_status));
	}

	link_status.start = 1;
	link_status.running = 1;

	if (CFG_setSpaceWireLinkStatus(cfg->spw->dev_id, (U8)chan, &link_status))
		printf("Failed to set link to running state\n");

	if (CFG_getMeasuredLinkSpeed(cfg->spw->dev_id, (U8)link_id, &link_speed_u16))
		printf("Failed to read measured link speed\n");
	else
		printf("Measured RX link speed %g Mbps\n", (double)link_speed_u16 / 10.0);
}


/**
 * @brief check if the SpW channel is open
 *
 * @param cfg the bridge configuration
 *
 * @return true when the SpW link is ready to carry traffic
 */

bool spw_link_ready(struct bridge_cfg *cfg)
{
	if (!cfg->spw)
		return false;

	if (cfg->enable_monitor)
		return cfg->spw->spw_chan_id[0] && cfg->spw->spw_chan_id[1];

	return cfg->spw->spw_chan_id[0] != 0;
}


/**
 * @brief transmit a packet on the SpaceWire link
 *
 * @param cfg bridge configuration
 * @param buf packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void spw_send_packet_chan(struct bridge_cfg *cfg, uint32_t chan, uint8_t *buf,
			  size_t len)
{
	STAR_STREAM_ITEM *p_tx_stream_item;
	STAR_TRANSFER_OPERATION *p_tx_transfer_op;

	struct spw_state *st;


	st = cfg->spw;

	p_tx_stream_item = STAR_createPacket(st->p_address, buf, (uint32_t)len, STAR_EOP_TYPE_EOP);
	if (!p_tx_stream_item) {
		printf("Error creating stream item\n");
		exit(EXIT_FAILURE);
	}

	p_tx_transfer_op = STAR_createTxOperation(&p_tx_stream_item, 1);
	if (!p_tx_transfer_op) {
		printf("Error creating transfer operation\n");
		exit(EXIT_FAILURE);
	}

	pthread_mutex_lock(&st->pus_op_lock);
	st->pus_tx_transfer_op[chan] = p_tx_transfer_op;
	pthread_mutex_unlock(&st->pus_op_lock);

	if (!STAR_submitTransferOperation(st->spw_chan_id[chan], p_tx_transfer_op)) {
		printf("Error during transfer submission\n");
		exit(EXIT_FAILURE);
	}

	st->tx_status = STAR_waitOnTransferOperationCompletion(p_tx_transfer_op, 1);
	if (st->tx_status != STAR_TRANSFER_STATUS_COMPLETE) {
		printf("Error during transfer\n");
		exit(EXIT_FAILURE);
	}

	/* optional delay to throttle packet submission (high rates might knock
	 * over the Mk II Brick)
	 */
	usleep(cfg->pkt_throttle_usec);

	pthread_mutex_lock(&st->pus_op_lock);

	if (st->pus_tx_transfer_op[chan] == p_tx_transfer_op)
		st->pus_tx_transfer_op[chan] = NULL;

	pthread_mutex_unlock(&st->pus_op_lock);
	STAR_disposeTransferOperation(p_tx_transfer_op);

	STAR_destroyStreamItem(p_tx_stream_item);
}


/**
 * @brief transmit a packet on the SpaceWire link
 *
 * @param cfg bridge configuration
 * @param buf packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void spw_send_packet(struct bridge_cfg *cfg, uint8_t *buf, size_t len)
{
	spw_send_packet_chan(cfg, 0, buf, len);
}


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
		  uint8_t *data, size_t size)
{
	uint8_t src = RMAP_DEFAULT_SRC;
	uint8_t key = RMAP_DEFAULT_KEY;

	unsigned long len;

	STAR_TRANSFER_STATUS transmit_status;

	void *pkt;


	DBG("Here I generate a %s packet at address %x of size %zu\n",
	    op ? "WRITE" : "READ", addr, size);

	if (op)
		pkt = RMAP_BuildWriteCommandPacket(&dst, 1, &src, 1, 0, 0, 1, key, 0,
						  addr, 0, data, (U32)size, &len, NULL, 1);
	else
		pkt = RMAP_BuildReadCommandPacket(&dst, 1, &src, 1, 1, key, 0, addr, 0, 5, &len, NULL, 1);

	if (!pkt) {
		printf("Failure in  RMAP_Build%sCommandPacket()\n", op ? "Write" : "Read");
		return;
	}

	transmit_status = STAR_transmitPacket(cfg->spw->spw_chan_id[0], pkt, (uint32_t)len,
					      STAR_EOP_TYPE_EOP, 5);
	if (transmit_status != STAR_TRANSFER_STATUS_COMPLETE)
		printf("Error transmitting RMAP %s command\n", op ? "write" : "read");

	RMAP_FreeBuffer(pkt);
}


/**
 * @brief set up the SpaceWire device, channel and routing path
 *
 * @param cfg the bridge configuration; the channel is updated in place
 */

void spw_setup_device(struct bridge_cfg *cfg)
{
	int ret;

	uint32_t nchans;
	uint32_t i;

	struct spw_state *st;


	nchans = cfg->enable_monitor ? 2 : 1;

	st = (struct spw_state *)calloc(1, sizeof(struct spw_state));
	if (!st) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	cfg->spw = st;

	pthread_mutex_init(&st->pus_op_lock, NULL);

	for (i = 0; i < nchans; i++) {
		st->poll_arg[i].cfg = cfg;
		st->poll_arg[i].chan = i;
	}

	st->dev_id = select_device(cfg->dev_num);

	if (cfg->reset_dev) {
		printf("Attempting to reset the device, this will have an effect on other ports!\n");
		STAR_resetDevice(st->dev_id);
	}

	cfg->channel = select_channel(st->dev_id, cfg->channel);

	if (cfg->enable_monitor) {
		if (cfg->channel2 == cfg->channel) {
			printf("monitor mode requires two different channels, refusing to proceed\n");
			exit(EXIT_FAILURE);
		}

		cfg->channel2 = select_channel(st->dev_id, cfg->channel2);
	}

	set_link_speed(st->dev_id, cfg->link_id, cfg->sig_rate);

	if (cfg->enable_monitor)
		set_link_speed(st->dev_id, cfg->channel2, cfg->sig_rate);

	st->spw_chan_id[0] = STAR_openChannelToLocalDevice(st->dev_id, STAR_CHANNEL_DIRECTION_INOUT, (uint8_t)cfg->channel, TRUE);
	if (!st->spw_chan_id[0]) {
		printf("Error opening channel\n");
		exit(EXIT_FAILURE);
	}

	printf("Selected channel %u\n", cfg->channel);

	if (cfg->enable_monitor) {
		st->spw_chan_id[1] = STAR_openChannelToLocalDevice(st->dev_id, STAR_CHANNEL_DIRECTION_INOUT, (uint8_t)cfg->channel2, TRUE);
		if (!st->spw_chan_id[1]) {
			printf("Error opening channel\n");
			exit(EXIT_FAILURE);
		}

		printf("Selected channel %u\n", cfg->channel2);
	}

	if (cfg->enable_monitor)
		/* monitor mode copies packets verbatim, no routing header on TX */
		st->p_address = STAR_createAddress(cfg->path, 0);
	else {
		print_path(cfg->path, cfg->path_len);
		st->p_address = STAR_createAddress(cfg->path, cfg->path_len);
	}

	for (i = 0; i < nchans; i++) {
		ret = pthread_create(&st->th_spw_poll[i], NULL, poll_spw, &st->poll_arg[i]);
		if (ret) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}
	}
}


/**
 * @brief bring the SpW link into a running state
 *
 * @param cfg the bridge configuration
 */

void spw_start_link(struct bridge_cfg *cfg)
{
	start_link(cfg, cfg->channel, cfg->link_id);

	if (cfg->enable_monitor)
		start_link(cfg, cfg->channel2, cfg->channel2);
}


/**
 * @brief flag the SpW polling thread to stop
 *
 * @param cfg the bridge configuration
 *
 * @note set it before tearing down the network threads so no further packets
 *	 are forwarded while the net side winds down
 */

void spw_request_shutdown(struct bridge_cfg *cfg)
{
	cfg->spw->shutdown = true;
}


/**
 * @brief wake the SpW polling thread and wait for it to wind down
 *
 * @param cfg the bridge configuration
 */

void spw_stop_poll(struct bridge_cfg *cfg)
{
	uint32_t i;
	uint32_t nchans;


	nchans = cfg->enable_monitor ? 2 : 1;

	cfg->spw->shutdown = true;

	pthread_mutex_lock(&cfg->spw->pus_op_lock);

	for (i = 0; i < nchans; i++)
		if (cfg->spw->pus_rx_transfer_op[i])
			STAR_cancelTransferOperationWaits(cfg->spw->pus_rx_transfer_op[i]);

	pthread_mutex_unlock(&cfg->spw->pus_op_lock);

	for (i = 0; i < nchans; i++)
		pthread_join(cfg->spw->th_spw_poll[i], NULL);
}


/**
 * @brief dispose the transfer operations handed back by the SpW thread
 *
 * @param cfg the bridge configuration
 *
 * @note cancelled transfer operations are already freed by the API
 */

void spw_dispose_ops(struct bridge_cfg *cfg)
{
	uint32_t i;
	uint32_t nchans;


	nchans = cfg->enable_monitor ? 2 : 1;

	pthread_mutex_lock(&cfg->spw->pus_op_lock);

	for (i = 0; i < nchans; i++) {
		if (cfg->spw->pus_rx_transfer_op[i] &&
		    STAR_getTransferOperationStatus(cfg->spw->pus_rx_transfer_op[i]) != STAR_TRANSFER_STATUS_CANCELLED)
			STAR_disposeTransferOperation(cfg->spw->pus_rx_transfer_op[i]);

		if (cfg->spw->pus_tx_transfer_op[i] &&
		    STAR_getTransferOperationStatus(cfg->spw->pus_tx_transfer_op[i]) != STAR_TRANSFER_STATUS_CANCELLED)
			STAR_disposeTransferOperation(cfg->spw->pus_tx_transfer_op[i]);
	}

	pthread_mutex_unlock(&cfg->spw->pus_op_lock);
}


/**
 * @brief release the SpaceWire device resources
 *
 * @param cfg the bridge configuration
 */

void spw_release(struct bridge_cfg *cfg)
{
	uint32_t i;
	uint32_t nchans;


	nchans = cfg->enable_monitor ? 2 : 1;

	if (cfg->spw->p_address)
		STAR_destroyAddress(cfg->spw->p_address);

	for (i = 0; i < nchans; i++)
		if (cfg->spw->spw_chan_id[i])
			STAR_closeChannel(cfg->spw->spw_chan_id[i]);

	pthread_mutex_destroy(&cfg->spw->pus_op_lock);

	free(cfg->spw);
	cfg->spw = NULL;
}
