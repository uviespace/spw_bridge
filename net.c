/**
 * @file net.c
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
 * @brief network subsystem of the SpaceWire bridge: socket handling, receive
 *        framing and the packet sink invoked by the SpW subsystem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <stdbool.h>
#include <stdint.h>

#include <byteorder.h>

#include <spw_bridge.h>
#include <rmap.h>
#include <gresb.h>
#include <debug.h>


/* a peer that takes longer than this to deliver one packet is dropped */
#define NET_RECV_TIMEOUT_S	30

#define FEE_DATA_PROTOCOL	0xF0

__extension__
struct fee_data_hdr {
	uint8_t logical_addr;
	uint8_t proto_id;
	uint16_t data_len;
	uint16_t fee_pkt_type;
	uint16_t frame_cntr;	/* increments per-frame, see Draft A.14 MSSL-IF-109 */
	uint16_t seq_cntr;	/* packet seq. in frame transfer, see Draft A.14 MSSL-IF-110 */
} __attribute__((packed));


/* one service is shared by all its clients, the connection set is multiplexed */
struct net_service {
	struct net_state	*state;
	int			sock_fd;
	fd_set			conn_set;
	int			nfds;
	pthread_mutex_t		lock;
	void			(*handle_pkt)(struct net_service *svc, int sockfd);
	struct sockaddr_in	*clients;
	size_t			nclients;
	size_t			clients_cap;
};


struct net_state {
	struct bridge_cfg	*cfg;
	bool			is_client;
	int			client_sock;
	uint16_t		pus_seq_tx;
	uint16_t		pus_seq_rx;
	pthread_t		th_main;	/* accept in server mode, poll in client mode */
	pthread_t		th_poll;
	pthread_t		th_rmap_accept;
	pthread_t		th_rmap_poll;
	struct net_service	data;
	struct net_service	rmap;
};


static int send_all(int sockfd, const uint8_t *buf, size_t len)
{
	ssize_t n;

	while (len) {

		n = send(sockfd, buf, len, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;	/* a signal interrupted us, retry */

			perror("send_all");
			return -1;
		}

		len -= (size_t)n;
		buf += n;
	}

	return 0;
}


static void set_net_recv_timeout(int sockfd)
{
	struct timeval timeout;

	/* inactivity-based: data flowing in clears the clock, only a socket
	 * that stays quiet for the full period is timed out
	 */
	timeout.tv_sec  = NET_RECV_TIMEOUT_S;
	timeout.tv_usec = 0;

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
		perror("setsockopt(SO_RCVTIMEO)");
}


static void set_net_send_timeout(int sockfd)
{
	struct timeval timeout;

	timeout.tv_sec  = NET_RECV_TIMEOUT_S;
	timeout.tv_usec = 0;

	if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
		perror("setsockopt(SO_SNDTIMEO)");
}


static void deadline_add(struct timespec *ts, int s)
{
	ts->tv_sec += s;
}


static bool deadline_expired(const struct timespec *deadline)
{
	struct timespec now;


	clock_gettime(CLOCK_MONOTONIC, &now);

	if (now.tv_sec > deadline->tv_sec)
		return true;

	if (now.tv_sec == deadline->tv_sec) {
		if (now.tv_nsec >= deadline->tv_nsec)
			return true;
	}

	return false;
}


static ssize_t recv_exact(int sockfd, uint8_t *buf, size_t n)
{
	size_t got = 0;

	struct timespec deadline;


	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline_add(&deadline, NET_RECV_TIMEOUT_S);

	while (got < n) {
		ssize_t r = recv(sockfd, buf + got, n - got, 0);
		if (r <= 0)
			return r;

		got += (size_t)r;

		/* the per-call SO_RCVTIMEO window is inactivity-based, so a
		 * peer trickling the packet in tiny pieces never trips it;
		 * bound the whole transfer by the same deadline instead
		 */
		if (deadline_expired(&deadline)) {
			errno = ETIMEDOUT;
			return -1;
		}
	}

	return (ssize_t)got;
}


/* expects url in <ip>:<port> form */
static struct sockaddr_in sockaddr_from_url(const char *url)
{
	struct sockaddr_in sockaddr;

	char *str;


	str = (char *)malloc(strlen(url) + 1);
	if (!str)  {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	strcpy(str, url);

	sockaddr.sin_addr.s_addr = inet_addr(strtok(str, ":"));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons((uint16_t)atoi(strtok(NULL, ":")));

	free(str);

	return sockaddr;
}


static int connect_client_socket(struct net_service *svc, const char *url)
{
	int ret;
	int sockfd;

	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return sockfd;

	set_net_recv_timeout(sockfd);
	set_net_send_timeout(sockfd);

	ret = connect(sockfd, (struct sockaddr *)&server, sizeof(server));
	if (ret < 0)
		return ret;

	FD_ZERO(&svc->conn_set);
	FD_SET(sockfd, &svc->conn_set);

	if (sockfd > svc->nfds)
		svc->nfds = sockfd + 1;

	return sockfd;
}


static int bind_server_socket(const char *url, fd_set *set)
{
	int sockfd;
	int endpoint;

	int optval = 1;

	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET , SOCK_STREAM , 0);

	FD_ZERO(set);

	if (sockfd < 0) {
		printf("Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}


	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	endpoint = bind(sockfd, (struct sockaddr *)&server, sizeof(server));
	if (endpoint < 0) {
		close(sockfd);
		printf("could not bind endpoint %s: %s\n", url, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (listen(sockfd, SOMAXCONN) < 0) {
		close(sockfd);
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("Listening on %s\n", url);

	return sockfd;
}


static int bind_dgram_socket(const char *url)
{
	int sockfd;
	int endpoint;

	int optval = 1;

	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		printf("Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	endpoint = bind(sockfd, (struct sockaddr *)&server, sizeof(server));
	if (endpoint < 0) {
		close(sockfd);
		printf("could not bind endpoint %s: %s\n", url, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Listening on %s (UDP)\n", url);

	return sockfd;
}


static void *accept_connections(void *ptr)
{
	int fd = 0;

	socklen_t client_addr_len;

	struct sockaddr_storage client_addr;

	struct net_service *svc;


	svc = (struct net_service *)ptr;

	client_addr_len = sizeof(client_addr);

	while (1) {

		fd = accept(svc->sock_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (fd < 0) {
			if (errno == EINTR)
				continue;

			perror("accept");
			usleep(100000);
			continue;
		}

		printf("New incoming connection\n");

		if (fd >= FD_SETSIZE) {
			printf("connection refused: fd %d above FD_SETSIZE\n", fd);
			close(fd);
			continue;
		}

		set_net_recv_timeout(fd);
		set_net_send_timeout(fd);

		pthread_mutex_lock(&svc->lock);
		FD_SET(fd, &svc->conn_set);

		if (fd > svc->nfds)
			svc->nfds = fd + 1;

		pthread_mutex_unlock(&svc->lock);
	}

	return NULL;
}


static void conn_del(struct net_service *svc, int sockfd)
{
	pthread_mutex_lock(&svc->lock);
	FD_CLR(sockfd, &svc->conn_set);
	pthread_mutex_unlock(&svc->lock);
}


static void conn_drop(struct net_service *svc, int sockfd, bool error)
{
	/* a recv() EOF is a clean peer close, not an error */
	if (error)
		printf("Connection %d error: %s\n", sockfd, strerror(errno));
	else
		printf("Connection %d closed\n", sockfd);

	close(sockfd);
	conn_del(svc, sockfd);

	if (svc->state->is_client) {
		printf("Client link lost, shutting down\n");
		exit(EXIT_FAILURE);
	}
}


static void conn_drop_locked(struct net_service *svc, int sockfd, bool error)
{
	if (error)
		printf("Connection %d error: %s\n", sockfd, strerror(errno));
	else
		printf("Connection %d closed\n", sockfd);

	close(sockfd);
	/* caller holds svc->lock, so clear the fd set directly */
	FD_CLR(sockfd, &svc->conn_set);

	if (svc->state->is_client) {
		printf("Client link lost, shutting down\n");
		exit(EXIT_FAILURE);
	}
}


static void rmap_conn_drop(struct net_service *svc, int sockfd, bool error)
{
	if (error)
		printf("RMAP connection %d error: %s\n", sockfd, strerror(errno));
	else
		printf("RMAP connection %d closed\n", sockfd);

	close(sockfd);
	conn_del(svc, sockfd);
}


static void rmap_conn_drop_locked(struct net_service *svc, int sockfd, bool error)
{
	if (error)
		printf("RMAP connection %d error: %s\n", sockfd, strerror(errno));
	else
		printf("RMAP connection %d closed\n", sockfd);

	close(sockfd);
	/* caller holds svc->lock, so clear the fd set directly */
	FD_CLR(sockfd, &svc->conn_set);
}


static ssize_t recv_pus_packet(struct net_service *svc, int sockfd, uint8_t **buf, size_t *len)
{
	uint16_t sctr;
	ssize_t recv_bytes;
	size_t packet_length;

	struct bridge_cfg *cfg;

	uint8_t recv_buffer_small[6];


	/* assume the recv queue always starts with a valid PUS header */
	recv_bytes = recv(sockfd, recv_buffer_small, 6, MSG_PEEK);
	if (recv_bytes != 6) {
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	packet_length = (((uint16_t)recv_buffer_small[4]) << 8) |
		((uint16_t)recv_buffer_small[5]);
	packet_length++;

	packet_length += 6;

	(*buf) = (uint8_t *)malloc(packet_length);
	if (!(*buf)) {
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	/* pull in the whole packet, it may span multiple TCP segments */
	recv_bytes = recv_exact(sockfd, (*buf), packet_length);
	if (recv_bytes != (ssize_t)packet_length) {
		free(*buf);
		(*buf) = NULL;
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	sctr = (((uint16_t)(*buf)[2] << 8) | (*buf)[3]) & 0x3fff;
	if (sctr != svc->state->pus_seq_tx)
		fprintf(stderr, "Sequence expected: %d is: %d\n", svc->state->pus_seq_tx, sctr);

	svc->state->pus_seq_tx = (sctr + 1) & 0x3fff;

	cfg = svc->state->cfg;
	if (cfg->crc_check)
		if (!pus_pkt_crc_valid((*buf), packet_length))
			fprintf(stderr, "CRC error on NET->SPW packet\n");

	(*len) = packet_length;
	return recv_bytes;
}


static ssize_t recv_raw_packet(struct net_service *svc, int sockfd, uint8_t **buf, size_t *len)
{
	uint32_t avail = 0;

	ssize_t recv_bytes;

	if (ioctl(sockfd, FIONREAD, &avail) < 0 || avail == 0) {
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	(*buf) = (uint8_t *)malloc(avail);
	if (!(*buf)) {
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	/* one recv() defines one SpW packet: back-to-back packets may be
	 * merged, a partially delivered packet may be split
	 */
	recv_bytes = recv(sockfd, (*buf), avail, 0);
	if (recv_bytes <= 0) {
		free(*buf);
		(*buf) = NULL;
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	(*len) = (size_t)recv_bytes;
	return recv_bytes;
}


static ssize_t recv_dgram_packet(int sockfd, uint8_t **buf, size_t *len,
				 struct sockaddr_in *client)
{
	uint32_t avail = 0;

	ssize_t recv_bytes;

	socklen_t client_len;


	client_len = sizeof(struct sockaddr_in);

	if (ioctl(sockfd, FIONREAD, &avail) < 0 || avail == 0)
		return -1;

	(*buf) = (uint8_t *)malloc(avail);
	if (!(*buf))
		return -1;

	/* one datagram defines one SpW packet */
	recv_bytes = recvfrom(sockfd, (*buf), avail, 0,
			      (struct sockaddr *)client, &client_len);
	if (recv_bytes <= 0) {
		free(*buf);
		(*buf) = NULL;
		return -1;
	}

	(*len) = (size_t)recv_bytes;
	return recv_bytes;
}


static void dgram_add_client(struct net_service *svc, const struct sockaddr_in *client)
{
	size_t i;
	size_t cap;

	struct sockaddr_in *tmp;


	pthread_mutex_lock(&svc->lock);

	for (i = 0; i < svc->nclients; i++)
		if (svc->clients[i].sin_addr.s_addr == client->sin_addr.s_addr &&
		    svc->clients[i].sin_port == client->sin_port) {
			pthread_mutex_unlock(&svc->lock);
			return;
		}

	if (svc->nclients == svc->clients_cap) {
		cap = svc->clients_cap ? svc->clients_cap * 2 : 4;
		tmp = realloc(svc->clients, cap * sizeof(struct sockaddr_in));
		if (!tmp) {
			pthread_mutex_unlock(&svc->lock);
			return;
		}

		svc->clients = tmp;
		svc->clients_cap = cap;
	}

	svc->clients[svc->nclients] = *client;
	svc->nclients++;

	pthread_mutex_unlock(&svc->lock);
}


static void dgram_send_all(struct net_service *svc, const uint8_t *buf, size_t len)
{
	size_t i;


	pthread_mutex_lock(&svc->lock);
	for (i = 0; i < svc->nclients; i++)
		if (sendto(svc->sock_fd, buf, len, 0,
			   (struct sockaddr *)&svc->clients[i],
			   sizeof(struct sockaddr_in)) < 0)
			perror("sendto");
	pthread_mutex_unlock(&svc->lock);
}


static ssize_t recv_fee_packet(struct net_service *svc, int sockfd, uint8_t **buf, size_t *len)
{
	ssize_t recv_bytes;
	size_t packet_length;

	struct fee_data_hdr fee_hdr;


	recv_bytes = recv(sockfd, &fee_hdr, sizeof(struct fee_data_hdr), MSG_PEEK);
	if (recv_bytes != (ssize_t)sizeof(struct fee_data_hdr)) {
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	if (fee_hdr.proto_id != FEE_DATA_PROTOCOL)
		/* otherwise treat as rmap */
		return recv_raw_packet(svc, sockfd, buf, len);

	packet_length = __be16_to_cpu(fee_hdr.data_len) + sizeof(struct fee_data_hdr);

	(*buf) = (uint8_t *)malloc(packet_length);
	if (!(*buf)) {
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	/* pull in the whole packet, it may span multiple TCP segments */
	recv_bytes = recv_exact(sockfd, (*buf), packet_length);
	if (recv_bytes != (ssize_t)packet_length) {
		free(*buf);
		(*buf) = NULL;
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	(*len) = packet_length;
	return recv_bytes;
}


static ssize_t recv_gresb_packet(struct net_service *svc, int sockfd, uint8_t **buf, size_t *len)
{
	size_t pkt_size;
	size_t gsize;
	ssize_t recv_bytes;

	uint8_t *pkt_buf;

	uint8_t gresb_hdr[4];	/* host-to-gresb header is 4 bytes */


	recv_bytes = recv(sockfd, gresb_hdr, 4, MSG_PEEK | MSG_DONTWAIT);
	if (recv_bytes < 4) {
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	gsize = gresb_get_spw_data_size(gresb_hdr);
	if (gsize > MAX_SPW_PACKET_SIZE) {
		printf("Oversized GRESB request, %zu bytes\n", gsize);
		conn_drop(svc, sockfd, 0);
		return -1;
	}

	pkt_size = gsize + 4;

	pkt_buf = malloc(pkt_size);
	if (!pkt_buf) {
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	/* pull in the whole packet, it may span multiple TCP segments */
	recv_bytes = recv_exact(sockfd, pkt_buf, pkt_size);
	if (recv_bytes != (ssize_t)pkt_size) {
		free(pkt_buf);
		conn_drop(svc, sockfd, recv_bytes < 0);
		return -1;
	}

	(*buf) = malloc(gsize);
	if (!(*buf)) {
		free(pkt_buf);
		conn_drop(svc, sockfd, 1);
		return -1;
	}

	memcpy((*buf), gresb_get_spw_data(pkt_buf), gsize);

	free(pkt_buf);

	(*len) = gsize;
	return (ssize_t)gsize;
}


static void net_to_spw(struct net_service *svc, int sockfd)
{
	size_t packet_length;

	ssize_t recv_bytes;

	struct sockaddr_in client;

	uint8_t *recv_buffer;

	struct bridge_cfg *cfg;


	cfg = svc->state->cfg;
	if (cfg->mode == MODE_DGRAM)
		recv_bytes = recv_dgram_packet(sockfd, &recv_buffer, &packet_length, &client);
	else if (cfg->interpret_pus)
		recv_bytes = recv_pus_packet(svc, sockfd, &recv_buffer, &packet_length);
	else if (cfg->interpret_fee)
		recv_bytes = recv_fee_packet(svc, sockfd, &recv_buffer, &packet_length);
	else if (cfg->enable_gresb)
		recv_bytes = recv_gresb_packet(svc, sockfd, &recv_buffer, &packet_length);
	else
		recv_bytes = recv_raw_packet(svc, sockfd, &recv_buffer, &packet_length);
	if (recv_bytes < 0)
		return;

	if (cfg->mode == MODE_DGRAM)
		dgram_add_client(svc, &client);

	if (cfg->enable_monitor) {
		/* monitor mode: the network side only observes and sends, anything
		 * received from the net is discarded to keep the links untouched
		 */
		free(recv_buffer);
		return;
	}

	pus_debug_print(cfg, "NET->SPW", recv_buffer, packet_length);

	rmap_parse_pkt(recv_buffer, packet_length);

	spw_send_packet(cfg, recv_buffer, packet_length);

	free(recv_buffer);
}


static void *poll_socket(void *ptr)
{
	int32_t sockfd;
	int n;

	struct timeval timeout;

	fd_set readset;

	struct net_service *svc;


	svc = (struct net_service *)ptr;

	while (1) {

		if (!spw_link_ready(svc->state->cfg)) {
			usleep(10000);
			continue;
		}

		pthread_mutex_lock(&svc->lock);
		readset = svc->conn_set;
		n = svc->nfds;
		pthread_mutex_unlock(&svc->lock);

		timeout.tv_sec  = 0;
		timeout.tv_usec = 10000;

		if (select(n, &readset, NULL, NULL, &timeout) <= 0)
			continue;

		/* fds may have been dropped and their numbers reused while we
		 * were blocked in select(); re-check membership against the
		 * live set so a handler never runs on a closed connection
		 */
		pthread_mutex_lock(&svc->lock);
		for (sockfd = 0; sockfd < n; sockfd++)
			if (!FD_ISSET(sockfd, &svc->conn_set))
				FD_CLR(sockfd, &readset);

		pthread_mutex_unlock(&svc->lock);

		for (sockfd = 0; sockfd < n; sockfd++) {

			if (!FD_ISSET(sockfd, &readset))
				continue;

			svc->handle_pkt(svc, sockfd);
		}
	}

	return NULL;
}


static void rmap_net_to_spw(struct net_service *svc, int sockfd)
{
	uint8_t dst;
	uint8_t op;
	uint32_t addr;
	size_t size;

	ssize_t n;

	uint8_t *rec;

	uint8_t tmp[10];

	struct bridge_cfg *cfg;


	cfg = svc->state->cfg;

	bzero(tmp, 10);

	n = recv(sockfd, tmp, 10, MSG_PEEK);
	if (n != 10) {
		rmap_conn_drop(svc, sockfd, n < 0);
		return;
	}

	dst = tmp[0];
	op  = tmp[1];

	addr = ((uint32_t)tmp[2]) << 24;
	addr|= ((uint32_t)tmp[3]) << 16;
	addr|= ((uint32_t)tmp[4]) <<  8;
	addr|=  (uint32_t)tmp[5];


	size = ((uint32_t)tmp[6]) << 24;
	size|= ((uint32_t)tmp[7]) << 16;
	size|= ((uint32_t)tmp[8]) <<  8;
	size|=  (uint32_t)tmp[9];
	if (size > MAX_SPW_PACKET_SIZE) {
		printf("Oversized RMAP request, %zu bytes\n", size);
		rmap_conn_drop(svc, sockfd, 0);
		return;
	}

	rec = (uint8_t *)malloc(size + 10);
	if (!rec) {
		rmap_conn_drop(svc, sockfd, 1);
		goto cleanup;
	}

	n = recv_exact(sockfd, rec, size + 10);
	if (n <= 0) {
		rmap_conn_drop(svc, sockfd, n < 0);
		goto cleanup;
	}

	if (n != (ssize_t) (size + 10)) {
		rmap_conn_drop(svc, sockfd, 0);
		goto cleanup;
	}

	if (!op)
		spw_rmap_cmd(cfg, dst, 0, addr, NULL, 0);
	else
		/* the write payload follows the 10-byte command header */
		spw_rmap_cmd(cfg, dst, 1, addr, &rec[10], size);


cleanup:
	free(rec);
}


/* lacks size checks! */
static void rmap_reply_to_clients(struct bridge_cfg *cfg, uint8_t *buf, size_t len)
{
	int fd;
	uint32_t i;
	size_t n;

	struct rmap_pkt *pkt;

	uint8_t *tmp;

	struct net_service *svc;


	svc = &cfg->net->rmap;

	pkt = rmap_pkt_from_buffer(&buf[cfg->skip_header_bytes], len - cfg->skip_header_bytes);
	if (!pkt)
		return;


	rmap_parse_pkt(&buf[cfg->skip_header_bytes], len - cfg->skip_header_bytes);

	/* logical spw address, op, starting address, data size */
	n = 1 + 1 + 4 + 4;

	/* data only on write */
	if (pkt->ri.cmd & RMAP_CMD_BIT_WRITE)
		n += pkt->data_len;

	tmp = (uint8_t *)calloc(n, 1);
	if (!tmp) {
		rmap_erase_packet(pkt);
		return;
	}

	tmp[0] = pkt->dst;

	tmp[1] = pkt->ri.cmd & RMAP_CMD_BIT_WRITE;

	tmp[2] = (uint8_t) (pkt->addr >> 24);
	tmp[3] = (uint8_t) (pkt->addr >> 16);
	tmp[4] = (uint8_t) (pkt->addr >>  8);
	tmp[5] = (uint8_t)pkt->addr;

	tmp[6] = (uint8_t) (pkt->data_len >> 24);
	tmp[7] = (uint8_t) (pkt->data_len >> 16);
	tmp[8] = (uint8_t) (pkt->data_len >>  8);
	tmp[9] = (uint8_t)pkt->data_len;

	for (i = 0; i < pkt->data_len; i++)
		tmp[10 + i] = pkt->data[i];


	pthread_mutex_lock(&svc->lock);
	for (fd = 0; fd < svc->nfds; fd++) {
		if (!FD_ISSET(fd, &svc->conn_set))
			continue;

		if (send_all(fd, tmp, n) == -1)
			rmap_conn_drop_locked(svc, fd, 1);
	}
	pthread_mutex_unlock(&svc->lock);

	free(tmp);
	rmap_erase_packet(pkt);
}


static void check_pus_sequence(struct bridge_cfg *cfg, const uint8_t *buf, size_t len)
{
	uint16_t sctr;

	if (len < 8)
		return;

	/* the counter sits two bytes into the CCSDS packet, after any skipped header */
	sctr = (((uint16_t)buf[cfg->skip_header_bytes + 2] << 8) |
		buf[cfg->skip_header_bytes + 3]) & 0x3fff;
	if (sctr != cfg->net->pus_seq_rx)
		fprintf(stderr, "Sequence expected: %d is: %d\n", cfg->net->pus_seq_rx, sctr);

	cfg->net->pus_seq_rx = (sctr + 1) & 0x3fff;
}


static void forward_to_clients(struct bridge_cfg *cfg, const uint8_t *buf, size_t len)
{
	int fd;

	uint8_t *gresb_pkt;

	struct net_service *svc;


	svc = &cfg->net->data;

	gresb_pkt = NULL;

	if (cfg->mode == MODE_DGRAM) {

		if (cfg->enable_gresb) {
			gresb_pkt = gresb_create_host_data_pkt(buf, len);
			if (!gresb_pkt) {
				printf("Error creating GRESB packet, dropping the SpW packet\n");
				return;
			}

			dgram_send_all(svc, gresb_pkt,
				       gresb_get_host_data_pkt_size(gresb_pkt));

			gresb_destroy_host_data_pkt((struct host_to_gresb_pkt *)gresb_pkt);
		} else {
			dgram_send_all(svc, buf, len);
		}

		return;
	}

	if (cfg->enable_gresb) {
		gresb_pkt = gresb_create_host_data_pkt(buf, len);
		if (!gresb_pkt) {
			printf("Error creating GRESB packet, dropping the SpW packet\n");
			return;
		}
	}

	pthread_mutex_lock(&svc->lock);
	for (fd = 0; fd < svc->nfds; fd++) {
		if (!FD_ISSET(fd, &svc->conn_set))
			continue;

		if (!cfg->enable_gresb) {
			if (send_all(fd, buf, len) == -1)
				conn_drop_locked(svc, fd, 1);
		} else {
			if (send_all(fd, gresb_pkt,
				     gresb_get_host_data_pkt_size(gresb_pkt)) == -1)
				conn_drop_locked(svc, fd, 1);
		}
	}
	pthread_mutex_unlock(&svc->lock);

	if (cfg->enable_gresb)
		gresb_destroy_host_data_pkt((struct host_to_gresb_pkt *)gresb_pkt);
}


/**
 * @brief handle a complete packet received on the SpW link
 *
 * @param cfg the bridge configuration
 * @param buf received packet bytes, including the leading path header
 * @param len size of the packet in bytes
 */

void net_pkt_sink(struct bridge_cfg *cfg, uint32_t chan, uint8_t *buf, size_t len)
{
	char dir[64];

	uint32_t other;

	if (cfg->enable_monitor) {
		/* monitor mode: copy the packet verbatim to the other link and
		 * hand the plain bytes to the clients for observation; nothing
		 * is routed, interpreted or stripped on the way
		 */
		other = 1 - chan;

		snprintf(dir, sizeof(dir), "SPW[%u]->SPW[%u]",
			 chan == 0 ? cfg->channel : cfg->channel2,
			 chan == 0 ? cfg->channel2 : cfg->channel);

		pus_debug_print(cfg, dir, buf, len);

		if (spw_link_ready(cfg))
			spw_send_packet_chan(cfg, other, buf, len);

		forward_to_clients(cfg, buf, len);

		return;
	}

	if (len <= cfg->skip_header_bytes) {
		printf("skip_header_bytes is %zu, dropping packet\n", cfg->skip_header_bytes);
		return;
	}

	pus_debug_print(cfg, "SPW->NET",
			buf + cfg->skip_header_bytes, len - cfg->skip_header_bytes);

	if (cfg->interpret_pus)
		if (cfg->crc_check &&
		    !pus_pkt_crc_valid(buf + cfg->skip_header_bytes,
				       len - cfg->skip_header_bytes))
			fprintf(stderr, "CRC error on SPW->NET packet\n");

	rmap_parse_pkt(buf + cfg->skip_header_bytes, len - cfg->skip_header_bytes);

	if (cfg->enable_rmap &&
	    len > cfg->skip_header_bytes + 1 &&
	    buf[cfg->skip_header_bytes + 1] == 0x1) {
		rmap_reply_to_clients(cfg, buf, len);
		return;
	}

	if (cfg->interpret_pus)
		check_pus_sequence(cfg, buf, len);

	forward_to_clients(cfg, buf + cfg->skip_header_bytes,
			   len - cfg->skip_header_bytes);
}


/**
 * @brief set up the network side of the bridge
 *
 * @param cfg the bridge configuration
 */

void net_start(struct bridge_cfg *cfg)
{
	int ret;

	char url[256];

	struct net_state *st;


	st = (struct net_state *)calloc(1, sizeof(struct net_state));
	if (!st) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	cfg->net = st;
	st->cfg = cfg;

	st->pus_seq_tx = 1;
	st->pus_seq_rx = 1;

	pthread_mutex_init(&st->data.lock, NULL);
	pthread_mutex_init(&st->rmap.lock, NULL);

	st->data.state = st;
	st->data.handle_pkt = net_to_spw;
	st->rmap.state = st;
	st->rmap.handle_pkt = rmap_net_to_spw;

	snprintf(url, sizeof(url), "%s:%u", cfg->host, cfg->port);

	if (cfg->mode == MODE_SERVER) {

		st->data.sock_fd = bind_server_socket(url, &st->data.conn_set);

		if ((ret = pthread_create(&st->th_main, NULL, accept_connections, &st->data))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		if ((ret = pthread_create(&st->th_poll, NULL, poll_socket, &st->data))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		printf("Started in SERVER mode\n");

	} else if (cfg->mode == MODE_DGRAM) {

		st->data.sock_fd = bind_dgram_socket(url);

		FD_ZERO(&st->data.conn_set);
		FD_SET(st->data.sock_fd, &st->data.conn_set);
		st->data.nfds = st->data.sock_fd + 1;

		if ((ret = pthread_create(&st->th_main, NULL, poll_socket, &st->data))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		printf("Started in DGRAM mode\n");

	} else {

		st->client_sock = connect_client_socket(&st->data, url);
		if (st->client_sock < 0) {
			printf("Failed to connect to %s\n", url);
			exit(EXIT_FAILURE);
		}

		st->is_client = true;

		if ((ret = pthread_create(&st->th_main, NULL, poll_socket, &st->data))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		printf("Started in CLIENT mode\n");
	}

	if (cfg->enable_rmap) {

		snprintf(url, sizeof(url), "%s:%u", cfg->host, cfg->rmap_port);
		st->rmap.sock_fd = bind_server_socket(url, &st->rmap.conn_set);

		if ((ret = pthread_create(&st->th_rmap_accept, NULL, accept_connections, &st->rmap))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		if ((ret = pthread_create(&st->th_rmap_poll, NULL, poll_socket, &st->rmap))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		printf("Started RMAP SERVER\n");
	}
}


/**
 * @brief cancel and join the network threads
 *
 * @param cfg the bridge configuration
 */

void net_stop(struct bridge_cfg *cfg)
{
	struct net_state *st;


	st = cfg->net;

	pthread_cancel(st->th_main);

	if (st->th_poll)
		pthread_cancel(st->th_poll);

	if (cfg->enable_rmap) {
		pthread_cancel(st->th_rmap_accept);
		pthread_cancel(st->th_rmap_poll);
	}

	if (st->data.sock_fd)
		close(st->data.sock_fd);

	if (st->rmap.sock_fd)
		close(st->rmap.sock_fd);

	if (st->client_sock)
		close(st->client_sock);

	/* collect the network threads, they may be mid-transmit when killed,
	 * so do not dispose anything until they have wound down
	 */
	pthread_join(st->th_main, NULL);

	if (st->th_poll)
		pthread_join(st->th_poll, NULL);

	if (cfg->enable_rmap) {
		pthread_join(st->th_rmap_accept, NULL);
		pthread_join(st->th_rmap_poll, NULL);
	}
}


/**
 * @brief release the network side of the bridge
 *
 * @param cfg the bridge configuration
 *
 * @note call only after the SpW polling thread has wound down, its receive
 *	 callback dereferences the network state until then
 */

void net_release(struct bridge_cfg *cfg)
{
	pthread_mutex_destroy(&cfg->net->data.lock);
	pthread_mutex_destroy(&cfg->net->rmap.lock);

	free(cfg->net->data.clients);
	free(cfg->net->rmap.clients);

	free(cfg->net);
	cfg->net = NULL;
}
