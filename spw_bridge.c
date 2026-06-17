/**
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
 *  - this is still a hacked together mess (and there's lots of duplicate code)
 *  - SpW packets are be at most MTU bytes (define below)
 *  - does not detect remote disconnect in client mode
 *  - race conditions may be present when accessing data between threads
 *  - maybe lots of others
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <rmap.h>
#include <gresb.h>


#include <signal.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>

#include <byteorder.h>


#include <star/star-api.h>
#include <star/cfg_api_mk2.h>
#include <star/cfg_api_brick_mk2.h>
#include <star/cfg_api_brick_mk3.h>
#include <star/cfg_api_pci_mk2.h>
#include <star/cfg_api_pci_mk2.h>
#include <star/cfg_api_pcie_mk2.h>
#include <star/cfg_api_generic.h>
#include <star/rmap_packet_library.h>



#define DEFAULT_RMAP_PORT 2345

#define DEFAULT_PORT 1234
#define DEFAULT_ADDR "0.0.0.0"
#define DEFAULT_CHAN 1


#define DEFAULT_LINK_SPEED 10.0

#define STAR_DEVICE_TYPE_BRICK_MKII	 9
#define STAR_DEVICE_TYPE_PCIE		11
#define STAR_DEVICE_TYPE_BRICK_MKIV	36
#define STAR_DEVICE_TYPE_PCIE_MKII      38

#define MTU		16384

/* global fun! */
int server_socket, server_socket_connection;

pthread_t th_spw_poll, th_server, th_rmap_server, th_server_poll, th_rmap_server_poll;

STAR_DEVICE_ID dev_id;
STAR_CHANNEL_ID spw_chan_id;
STAR_SPACEWIRE_ADDRESS *p_address;
STAR_TRANSFER_STATUS tx_status;

unsigned int skip_header_bytes;
int pkt_throttle_usec;
int interpret_pus;

int enable_rmap;
int enable_gresb;

int interpret_pus;
int interpret_fee;

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



/* yay (and technically incorrect) */
volatile fd_set conn_set;
volatile int nfds;

volatile fd_set rmap_conn_set;
volatile int rmap_nfds;

struct th_arg
{
	int *sock_fd;
	int *conn_fd;
} server_accept_thread;

struct th_arg rmap_server_accept_thread;

/* XXX */
STAR_TRANSFER_OPERATION *pus_tx_transfer_op;
STAR_TRANSFER_OPERATION *pus_rx_transfer_op;


/**
 * @brief a sigint handler, should we ever need it
 */

static void sigint_handler(__attribute__((unused)) int s)
{
	printf("\nCaught signal %d\n", s);
}


/**
 * @brief transmits the contents of a buffer from a socket to its peer
 */

static int send_all(int sockfd, unsigned char *buf, int len)
{
	int n;

	while (len) {

		n = send(sockfd, buf, len, 0);

		if (n == -1) {
			perror("send_all");
			return len;
		}

		len -= n;
		buf += n;
	}

	return len;
}


/**
 * @brief create socket address from url
 *
 * @note url expected to be <ip>:<port>
 */

static struct sockaddr_in sockaddr_from_url(const char *url)
{
	char *str;

	struct sockaddr_in sockaddr;


	str = (char *) malloc(strlen(url) + 1);
	if (!str)  {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	strcpy(str, url);

	sockaddr.sin_addr.s_addr = inet_addr(strtok(str, ":"));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(atoi(strtok(NULL, ":")));

	free(str);

	return sockaddr;
}


/**
 * @brief establish a client socket connection
 */

static int connect_client_socket(const char *url)
{
	int ret;
	int sockfd;

	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return sockfd;

	ret = connect(sockfd, (struct sockaddr *) &server, sizeof(server));
	if (ret < 0)
		return ret;

	FD_ZERO((fd_set *)&conn_set);
	FD_SET(sockfd, &conn_set);

	if (sockfd > nfds)
		nfds = sockfd + 1;

	return sockfd;
}


/**
 * @brief bind a socket for a listening server
 *
 * @note url expected to be <ip>:<port>
 */

static int bind_server_socket(const char* url)
{
	int sockfd;
	int endpoint;

	int optval = 1;
	
	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET , SOCK_STREAM , 0);

	FD_ZERO((fd_set *)&conn_set);

	if (sockfd < 0) {
		printf("Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}


	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	endpoint = bind(sockfd, (struct sockaddr *) &server, sizeof(server));
	if (endpoint < 0) {
		close(sockfd);
		printf("could not bind endpoint %s: %s\n", url, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (listen(sockfd, 0) < 0) {
		close(sockfd);
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("Listening on %s\n", url);

	return sockfd;
}


/**
 * @brief bind a socket for a listening server
 *
 * @note url expected to be <ip>:<port>
 */

static int rmap_bind_server_socket(const char* url)
{
	int sockfd;
	int endpoint;

	int optval = 1;
	
	struct sockaddr_in server;


	server = sockaddr_from_url(url);

	sockfd = socket(AF_INET , SOCK_STREAM , 0);

	FD_ZERO((fd_set *)&rmap_conn_set);

	if (sockfd < 0) {
		printf("Socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}


	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	endpoint = bind(sockfd, (struct sockaddr *) &server, sizeof(server));
	if (endpoint < 0) {
		close(sockfd);
		printf("could not bind endpoint %s: %s\n", url, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (listen(sockfd, 0) < 0) {
		close(sockfd);
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("Listening on %s\n", url);

	return sockfd;
}



/**
 * @brief thread function accepting incoming connections on a server socket
 */

static void *accept_connections(void *ptr)
{
	int fd = 0;

	struct th_arg arg, *p_arg;

	struct sockaddr_storage client_addr;

	socklen_t client_addr_len;


	p_arg = (struct th_arg *) ptr;

	arg.sock_fd = p_arg->sock_fd;
	arg.conn_fd = p_arg->conn_fd;

	client_addr_len = sizeof(client_addr);

	while(1) {

		fd = accept((*arg.sock_fd), (struct sockaddr *) &client_addr,
			    &client_addr_len);

		if (fd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}

		printf("New incoming connection\n");

		FD_SET(fd, &conn_set);

		if (fd > nfds)
			nfds = fd + 1;
	}
}


/**
 * @brief thread function accepting incoming connections on a server socket
 */

static void *rmap_accept_connections(void *ptr)
{
	int fd = 0;

	struct th_arg arg, *p_arg;

	struct sockaddr_storage client_addr;

	socklen_t client_addr_len;


	p_arg = (struct th_arg *) ptr;

	arg.sock_fd = p_arg->sock_fd;
	arg.conn_fd = p_arg->conn_fd;

	client_addr_len = sizeof(client_addr);

	while(1) {

		fd = accept((*arg.sock_fd), (struct sockaddr *) &client_addr,
			    &client_addr_len);

		if (fd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}

		printf("New incoming connection\n");

		FD_SET(fd, &rmap_conn_set);

		if (fd > rmap_nfds)
			rmap_nfds = fd + 1;
	}
}


static void net_to_spw(int sockfd)
{
	int i;

	unsigned char *recv_buffer = NULL;

	ssize_t recv_bytes;
	unsigned char recv_buffer_small[6];

	unsigned short packet_length;
	struct timespec now;

	struct fee_data_hdr fee_hdr;



	STAR_STREAM_ITEM 	*p_tx_stream_item = NULL;
	STAR_TRANSFER_OPERATION *p_tx_transfer_op = NULL;



	/* check if there's at least a valid PUS header in the
	 * recv stream we obviously assume that the top of the
	 * recv queue always starts with a valid PUS header
	 */
	if (interpret_pus) {
		static unsigned short expseq = 1;
		unsigned short sctr;

		recv_bytes = recv(sockfd, recv_buffer_small, 6, MSG_PEEK);

		if (recv_bytes <= 0) {
			FD_CLR(sockfd, &conn_set);
			perror("poll_client_socket");
			goto cleanup;
		}

		packet_length = (((unsigned short) recv_buffer_small[4]) << 8)
 			 	| ((unsigned short) recv_buffer_small[5]);
		packet_length++;

		packet_length += 6;

		recv_buffer = (uint8_t *) malloc(packet_length);

		recv_bytes  = recv(sockfd, recv_buffer, packet_length, 0);

		if (recv_bytes <= 0) {
			FD_CLR(sockfd, &conn_set);
			perror("poll_client_socket");
			goto cleanup;
		}

		sctr = (((uint16_t)recv_buffer[2] << 8) | recv_buffer[3]) & 0x3fff;
		if (sctr != expseq)
			fprintf(stderr, "Sequence expected: %d is: %d\n", expseq, sctr);

		expseq = (sctr + 1) & 0x3fff;

	} else if (interpret_fee) {
		recv_bytes = recv(sockfd, &fee_hdr, sizeof(struct fee_data_hdr), MSG_PEEK);

		if (recv_bytes <= 0) {
			FD_CLR(sockfd, &conn_set);
			perror("poll_client_socket");
			goto cleanup;
		}

		if (fee_hdr.proto_id == FEE_DATA_PROTOCOL) {

			packet_length = __be16_to_cpu(fee_hdr.data_len) + sizeof(struct fee_data_hdr);

			recv_buffer = (uint8_t *) malloc(packet_length);

			recv_bytes  = recv(sockfd, recv_buffer, packet_length, 0);

			if (recv_bytes <= 0) {
				FD_CLR(sockfd, &conn_set);
				perror("poll_client_socket");
				goto cleanup;
			}
		}


		/* otherwise treat as rmap */
		recv_buffer = (uint8_t *) malloc(MTU);
		recv_bytes  = recv(sockfd, recv_buffer, MTU, 0);
		packet_length = recv_bytes;

		if (recv_bytes <= 0) {
			FD_CLR(sockfd, &conn_set);
			perror("poll_client_socket");
			goto cleanup;
		}

	} else if (enable_gresb) {

		ssize_t recv_left;
		ssize_t pkt_size;
		uint8_t gresb_hdr[4];	/* host-to-gresb header is 4 bytes */
		unsigned char *pkt_buf = NULL;

		/* try to grab a header */
		recv_bytes = recv(sockfd, gresb_hdr, 4, MSG_PEEK | MSG_DONTWAIT);
		if (recv_bytes < 4)
			goto cleanup;

		/* we got at least a header, now allocate space for a host-to-gresb
		 * packet (data + header) and start receiving
		 * note the lack of basic sanity checks...
		 */
		pkt_size = gresb_get_spw_data_size(gresb_hdr) + 4;
		pkt_buf = malloc(pkt_size);


		/* pull in the whole packet */
		recv_bytes = 0;
		recv_left  = pkt_size ;
		while (recv_left) {
			ssize_t rb;
			rb = recv(sockfd, pkt_buf + recv_bytes, recv_left, 0);
			recv_bytes += rb;
			recv_left  -= rb;
		}

		packet_length = gresb_get_spw_data_size(gresb_hdr);
		recv_buffer  =  malloc(packet_length);

		/* update */
		recv_bytes = packet_length;

		memcpy(recv_buffer, gresb_get_spw_data(pkt_buf), packet_length);

		free(pkt_buf);

	} else {

		recv_buffer = (uint8_t *) malloc(MTU);
		recv_bytes  = recv(sockfd, recv_buffer, MTU, 0);
		packet_length = recv_bytes;

		if (recv_bytes <= 0) {
			FD_CLR(sockfd, &conn_set);
			perror("poll_client_socket");
			goto cleanup;
		}
	}

	clock_gettime (CLOCK_MONOTONIC, &now);
#if 0
	printf("[%ld.%09ld] Server %d got %ld bytes\b", now.tv_sec, now.tv_nsec,
						        sockfd, recv_bytes);
#endif
	/* disconnect while receiving ? */
	if (((ssize_t) packet_length) != recv_bytes)
		goto cleanup;


	/* send packet via SpW*/
#if 0
	printf("creating stream item of %d bytes\n", packet_length);
#endif
	printf("NET->PC: ");
	for (i = 0; i < packet_length; i++)
		printf("%02x", recv_buffer[i]);
	printf("\n");

	rmap_parse_pkt(recv_buffer);

	p_tx_stream_item = STAR_createPacket(p_address, recv_buffer,
					     packet_length, STAR_EOP_TYPE_EOP);
	if (!p_tx_stream_item){
		printf("Error creating stream item\n");
		exit(EXIT_FAILURE);
	}


	p_tx_transfer_op = STAR_createTxOperation(&p_tx_stream_item, 1);

	if (!p_tx_transfer_op){
		printf("Error creating transfer operation\n");
		exit(EXIT_FAILURE);
	}

	pus_tx_transfer_op = p_tx_transfer_op; /* XXX */
	if (!STAR_submitTransferOperation(spw_chan_id, p_tx_transfer_op)) {
		printf("Error during transfer submission\n");
		exit(EXIT_FAILURE);
	}

	tx_status = STAR_waitOnTransferOperationCompletion(p_tx_transfer_op, 1);
	if (tx_status != STAR_TRANSFER_STATUS_COMPLETE) {
		printf("Error during transfer\n");
		exit(EXIT_FAILURE);
	}


	/* optional delay to throttle packet submission (high rates might knock
	 * over the Mk II Brick)
	 */
	usleep(pkt_throttle_usec);


cleanup:
	if (recv_buffer)
		free(recv_buffer);

	if (p_tx_transfer_op)
		STAR_disposeTransferOperation(p_tx_transfer_op);

	if (p_tx_stream_item)
		STAR_destroyStreamItem(p_tx_stream_item);

}


/**
 * @brief thread function polling a network socket
 */

static void *poll_socket(__attribute__((unused)) void *arg)
{
	int32_t sockfd;

	struct timeval timeout;

	fd_set readset;


	/* wait 10 ms in select() */
	timeout.tv_sec  = 0;
	timeout.tv_usec = 10000;

	while (1) {

		if (!spw_chan_id) {
			usleep(1000);
			continue;
		}

		readset = conn_set;

		/* select ready sockets */
		if (select(nfds, &readset, NULL, NULL, &timeout) <= 0)  {
			usleep(1000);
			continue;
		}


		for (sockfd = 0; sockfd < nfds; sockfd++) {

			if (!FD_ISSET(sockfd, &readset))
				continue;

			net_to_spw(sockfd);
		}
	}

	return NULL;
}

static void rmap_read_cmd(unsigned char dst, uint32_t addr)
{
	unsigned long len;

	void *pkt;

	/** XXX */
	unsigned char src = 0x30;
	char key = 0xab;

	pkt = RMAP_BuildReadCommandPacket(&dst, 1, &src, 1, 1, key, 0, addr, 0, 5, &len, NULL, 1);
	if (!pkt) {
		printf("Failure in  RMAP_BuildReadCommandPacket()\n");
		return;
	}

	STAR_transmitPacket(spw_chan_id, pkt, len, STAR_EOP_TYPE_EOP, 5);

	RMAP_FreeBuffer(pkt);



}

static void rmap_write_cmd(unsigned char dst, uint32_t addr, unsigned char *data,
			   uint32_t size)
{
	unsigned long len;

	void *pkt;

	/** XXX */
	unsigned char src = 0x30;
	char key = 0xab;

	pkt = RMAP_BuildWriteCommandPacket(&dst, 1, &src, 1, 0, 0, 1, key, 0,
					  addr, 0, data, size, &len, NULL, 1);
	if (!pkt) {
		printf("Failure in  RMAP_BuildWriteCommandPacket()\n");
		return;
	}

	STAR_transmitPacket(spw_chan_id, pkt, len, STAR_EOP_TYPE_EOP, 5);

	RMAP_FreeBuffer(pkt);
}



static void rmap_net_to_spw(int sockfd)
{
	unsigned char dst;
	unsigned char op;
	uint32_t addr;
	uint32_t size;

	ssize_t n;
	unsigned char tmp[10];
	unsigned char *rec = NULL;

	bzero(tmp, 10);
	
	n = recv(sockfd, tmp, 10, MSG_PEEK);
	if (n <= 0) {
		FD_CLR(sockfd, &rmap_conn_set);
		perror("poll_client_socket");
		goto cleanup;
	}

	/* logical address */
	dst = tmp[0];
	printf("dst: %x\n", dst);

	/* operation */
	op  = tmp[1];
	printf("op: %x\n", op);

	addr = ((uint32_t) tmp[2]) << 24;
	addr|= ((uint32_t) tmp[3]) << 16;
	addr|= ((uint32_t) tmp[4]) <<  8;
	addr|=  (uint32_t) tmp[5];


	size = ((uint32_t) tmp[6]) << 24;
	size|= ((uint32_t) tmp[7]) << 16;
	size|= ((uint32_t) tmp[8]) <<  8;
	size|=  (uint32_t) tmp[9];

	rec = (unsigned char *) malloc(size + 10);
	if (!rec)
		exit(EXIT_FAILURE);

	n  = recv(sockfd, rec, size + 10, 0);

	if (n <= 0) {
		FD_CLR(sockfd, &rmap_conn_set);
		perror("poll_client_socket");
		goto cleanup;
	}

	/* TODO: payload */


	printf("Here I generate a %s packet at address %x of size %d\n", op? "WRITE":"READ", addr, size);


	if (!op)
		rmap_read_cmd(dst, addr);
	else
		rmap_write_cmd(dst, addr, &rec[10], size);




cleanup:
	free(rec);
}


/**
 * @brief thread function polling an RMAP network socket
 */

static void *poll_rmap_socket(__attribute__((unused)) void *arg)
{
	int32_t sockfd;

	struct timeval timeout;

	fd_set readset;


	/* wait 10 ms in select() */
	timeout.tv_sec  = 0;
	timeout.tv_usec = 10000;

	while (1) {

		if (!spw_chan_id) {
			usleep(1000);
			continue;
		}

		readset = rmap_conn_set;

		/* select ready sockets */
		if (select(rmap_nfds, &readset, NULL, NULL, &timeout) <= 0)  {
			usleep(1000);
			continue;
		}


		for (sockfd = 0; sockfd < rmap_nfds; sockfd++) {

			if (!FD_ISSET(sockfd, &readset))
				continue;
		
			rmap_net_to_spw(sockfd);
		}
	}

	return NULL;
}


/**
 *  lacks size checks!
 */

static void rmap_net_send(unsigned char *buf, unsigned int len)
{
	int fd;
	unsigned int i;

	struct rmap_pkt *pkt;

	unsigned char *tmp;
	unsigned int n;


	pkt = rmap_pkt_from_buffer(&buf[skip_header_bytes], len - skip_header_bytes);
	if (!pkt)
		return;


	rmap_parse_pkt(&buf[skip_header_bytes]);

	/* logical spw address, op, starting address, data size */
	n = 1 + 1 + 4 + 4;

	/* data only on write */
	if (pkt->ri.cmd & RMAP_CMD_BIT_WRITE)
		n += pkt->data_len;

	tmp = (unsigned char *) calloc(n, 1);
	if (!tmp) {
		rmap_erase_packet(pkt);
		return;
	}

	/* logical address */
	tmp[0] = pkt->dst;

	/* operation */
	tmp[1] = pkt->ri.cmd & RMAP_CMD_BIT_WRITE;

	/* address */
	tmp[2] = (unsigned char) (pkt->addr >> 24);
	tmp[3] = (unsigned char) (pkt->addr >> 16);
	tmp[4] = (unsigned char) (pkt->addr >>  8);
	tmp[5] = (unsigned char)  pkt->addr;

	/* size */
	tmp[6] = (unsigned char) (pkt->data_len >> 24);
	tmp[7] = (unsigned char) (pkt->data_len >> 16);
	tmp[8] = (unsigned char) (pkt->data_len >>  8);
	tmp[9] = (unsigned char)  pkt->data_len;

	/* data */
	for (i = 0; i < pkt->data_len; i++)
		tmp[10 + i] = pkt->data[i];


	for (fd = 0; fd < rmap_nfds; fd++) {
		if (!FD_ISSET(fd, &rmap_conn_set))
			continue;
		if (send_all(fd, tmp, n) == -1) {
			perror("send");
			FD_CLR(fd, &rmap_conn_set);
		}
	}

	free(tmp);
	rmap_erase_packet(pkt);
}


/**
 * @brief thread function polling a SpW link 
 */

static void *poll_spw(__attribute__((unused)) void *arg)
{
	int fd;
	unsigned int i;

	unsigned int  spw_recv_bytes;
	unsigned char *spw_recv_buffer = NULL;
	uint8_t *gresb_pkt = NULL;
	unsigned short sctr;
	static unsigned short expseq = 1;

	STAR_SPACEWIRE_PACKET *p_spw_packet       = NULL;
	STAR_TRANSFER_OPERATION *p_rx_transfer_op = NULL;
	STAR_TRANSFER_STATUS rx_status;


	while(1) {

		if (spw_recv_buffer)
			STAR_destroyPacketData(spw_recv_buffer);

		if (p_rx_transfer_op)
			STAR_disposeTransferOperation(p_rx_transfer_op);


		p_rx_transfer_op = STAR_createRxOperation(1, STAR_RECEIVE_PACKETS);
		if (!p_rx_transfer_op) {
			printf("Error creating transfer operation\n");
			exit(EXIT_FAILURE);
		}

		pus_rx_transfer_op = p_rx_transfer_op; /* XXX */
		if (!STAR_submitTransferOperation(spw_chan_id, p_rx_transfer_op)) {
			printf("Error during transfer submission\n");
			exit(EXIT_FAILURE);
		}


		/* don't block for more than 1 second */
		rx_status = STAR_waitOnTransferOperationCompletion(p_rx_transfer_op, -1);
		if (rx_status == STAR_TRANSFER_STATUS_CANCELLED) {
			printf("TRANSFER CANCELLED!\n");
			//STAR_destroyPacketData(spw_recv_buffer);
			//STAR_disposeTransferOperation(p_rx_transfer_op);
			break;
		}
		
		if (rx_status != STAR_TRANSFER_STATUS_COMPLETE) {
			printf("TRANSFER INCOMPLETE!\n");
			continue;
		}

		p_spw_packet = (STAR_SPACEWIRE_PACKET *) STAR_getTransferItem(p_rx_transfer_op, 0)->item;

		spw_recv_buffer = STAR_getPacketData(p_spw_packet, &spw_recv_bytes);
#if 0
		/* skip header/address byte(s) */
		printf("SpW link received %d bytes\n", spw_recv_bytes);
#endif
		printf("SPW->PC: ");
		for (i = 0; i < spw_recv_bytes; i++)
			printf("%02x", spw_recv_buffer[i]);
		printf("\n");

		rmap_parse_pkt(spw_recv_buffer);

		if (spw_recv_bytes <= skip_header_bytes) {
			printf("skip_header_bytes is %u, dropping packet\n", skip_header_bytes);
			continue;
		}


		if (enable_rmap) {
			if (spw_recv_buffer[skip_header_bytes + 1] == 0x1) {
				rmap_net_send(spw_recv_buffer, spw_recv_bytes);
				continue;
			}
		}

		if (interpret_pus) {
			sctr = (((unsigned short) spw_recv_buffer[2 + 4] << 8)
					| spw_recv_buffer[3 + 4 ]) & 0x3fff;

			if (sctr != expseq)
				fprintf(stderr, "Sequence expected: %d is: %d\n", expseq, sctr);

			expseq = (sctr + 1) & 0x3fff;
		}

		if (enable_gresb)
			gresb_pkt = gresb_create_host_data_pkt(spw_recv_buffer + skip_header_bytes,
							       spw_recv_bytes - skip_header_bytes);




		for (fd = 0; fd < nfds; fd++) {
			if (!FD_ISSET(fd, &conn_set))
				continue;

			if (!enable_gresb) {
				if (send_all(fd, spw_recv_buffer + skip_header_bytes,
					     spw_recv_bytes  - skip_header_bytes) == -1) {
					perror("send");
					FD_CLR(fd, &conn_set);
				}

			} else {
				if (send_all(fd, gresb_pkt, gresb_get_host_data_pkt_size(gresb_pkt)) == -1) {
					perror("send");
					FD_CLR(fd, &conn_set);

				}
			}
		}

		if (enable_gresb)
			gresb_destroy_host_data_pkt((struct host_to_gresb_pkt *) gresb_pkt);
	}

	/* never reached... unless transfer cancelled on ctrl-c*/
	/*if (spw_recv_buffer)
		STAR_destroyPacketData(spw_recv_buffer);

	if (p_rx_transfer_op)
		STAR_disposeTransferOperation(p_rx_transfer_op);
	 */
	return NULL;
}




/**
 * @brief select a SpaceWire device
 * @param device to select [0, 1 ... n]
 */

static STAR_DEVICE_ID select_device(unsigned int dev_num)
{

	unsigned int i;
	unsigned int num_devs;

	char *p_str;

	STAR_DEVICE_ID* dev_list;
	STAR_DEVICE_ID  dev_id;


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

	if ((num_devs - 1) < dev_num) {
		printf("Cannot select device with number %d, only %d available\n", dev_num, num_devs);
		STAR_destroyDeviceList(dev_list);
		exit(EXIT_FAILURE);
	} else {
		dev_id = dev_list[dev_num];
		STAR_destroyDeviceList(dev_list);
		return dev_id;
	}
}


/**
 * @brief select/check channel on a device
 * @param a Star SpW device id
 */

static unsigned int select_channel(STAR_DEVICE_ID dev_id, unsigned int channel)
{
	unsigned int i;

	STAR_CHANNEL_MASK channel_mask;

	channel_mask = STAR_getDeviceChannels(dev_id);

	if (!channel_mask || (channel_mask == 0x1)) {
		printf("Error: no valid channels on device\n");
		exit(EXIT_FAILURE);
	}

	if (channel_mask & (0x1 << channel)) {

		return channel;

	} else {

		printf("Non-existent channel, available channels are: ");

		for (i = 1; i < 32; i++) {
			if (channel_mask & (0x1 << i))
				printf("%u ", i);
		}

		printf("\n");
		exit(EXIT_FAILURE);
	}
}




int main(int argc, char **argv)
{
	char    *node_addr;

	char url[256];
	char host[128];
	unsigned char path[256];

	double sig_rate;
	unsigned short path_len = 0;

	int opt;
	unsigned int ret;
	unsigned int port;
	unsigned int rmap_port;
	unsigned int channel;
	unsigned int link_id;
	unsigned int dev_num = 0;
	int reset_dev = 0;

	struct addrinfo *res;

	struct sigaction SIGINT_handler;

	PORT_STATUS_CONTROL port_status;
	STAR_CFG_SPW_LINK_STATUS link_status;

	enum {SERVER, CLIENT} mode;


	/**
	 * defaults
	 */
	mode    = SERVER;
	port    = DEFAULT_PORT;
	channel = DEFAULT_CHAN;
	rmap_port = DEFAULT_RMAP_PORT;
	interpret_pus = 0;
	enable_rmap  = 0;
	link_id = channel;
	sprintf(host, "%s", DEFAULT_ADDR);
	skip_header_bytes = 0;
	pkt_throttle_usec = 0;
	sig_rate = DEFAULT_LINK_SPEED;


	while ((opt = getopt(argc, argv, "i:c:n:p:s:r:d:t:L:S:PFGXR::h")) != -1) {
		switch (opt) {
		case 'i':
			dev_num = strtol(optarg, NULL, 0);
			break;
		case 'c':
			channel= strtol(optarg, NULL, 0);
			link_id = channel;
			break;

		case 'n':
			node_addr = strtok(optarg, ":");
			while (node_addr) {
				path[path_len] = strtol(node_addr, NULL, 16);
				path_len++;
				node_addr = strtok(NULL, ":");
			}
			break;

		case 'p':
			port = strtol(optarg, NULL, 0);
			break;

		case 's':
			ret = getaddrinfo(optarg, NULL, NULL, &res);
			if (ret) {
				printf("error in getaddrinfo: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			sprintf(host, "%s", strtok(optarg, ":"));
			while (res) {
				if (res->ai_family == AF_INET) {
					inet_ntop(res->ai_family, &((struct sockaddr_in *) res->ai_addr)->sin_addr, host, sizeof(host));
					break;	/* just take the first one and hope for the best */
				}
				res = res->ai_next;
			}
			freeaddrinfo(res);
			break;

		case 'r':
			mode = CLIENT;
			/* yes, it's totally redundant... */
			sprintf(host, "%s", strtok(optarg, ":"));
			port = strtol(strtok(NULL, ":"), NULL, 0);

			ret = getaddrinfo(optarg, NULL, NULL, &res);
			if (ret) {
				printf("error in getaddrinfo: %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			while (res) {
				if (res->ai_family == AF_INET) {
					inet_ntop(res->ai_family, &((struct sockaddr_in *) res->ai_addr)->sin_addr, host, sizeof(host));
					break;
				}
				res = res->ai_next;
			}
			freeaddrinfo(res);

			break;

		case 'd':
			skip_header_bytes = strtol(optarg, NULL, 0);
			break;

		case 't':
			pkt_throttle_usec = strtol(optarg, NULL, 0);
			break;
		case 'S':
		        sig_rate = (double)strtoul(optarg, NULL, 0);
			if (sig_rate < 2.0 || sig_rate > 400.) {
				printf("\nInvalid link rate %g Mbps, must be between 2 and 400\n\n", sig_rate);
				exit(-1);
			}
			break;
		case 'L':
			link_id = strtol(optarg, NULL, 0);
			break;
		case 'P':
			interpret_pus = 1;
			break;
		case 'F':
			interpret_fee = 1;
			break;
		case 'G':
			enable_gresb = 1;
			break;
		case 'R':

			if (argv[optind])
				rmap_port = strtol(argv[optind], NULL, 0);

			enable_rmap = 1;
			break;
		case 'X':
			reset_dev = 1;
			break;

		case 'h':
		default:
			printf("\nUsage: %s [OPTIONS]\n", argv[0]);
			printf("  -i DEVNUM                 SpW device id to use (default %d)\n", dev_num);
			printf("  -c CHANNEL                SpW channel to use (default %d)\n", channel);
			printf("  -n 01:1a:cc:..            SpW address nodes route/path in hex bytes (1 byte per node)\n");
			printf("  -p PORT                   local port number (default %d)\n", port);
			printf("  -s ADDRESS                local source address (default: %s\n", url);
			printf("  -r ADDRESS:PORT           client mode: address and port of remote target\n");
			printf("  -d NUM                    number of header bytes to drop from incoming SpW packet (default %d)\n", skip_header_bytes);
			printf("  -t timeout (µs)           throttle transmission of SpW packets by inserting a delay between packets (default %d)\n", pkt_throttle_usec);
			printf("  -S LINKSPEED              link speed in Mbit/s (default %g)\n", DEFAULT_LINK_SPEED);
			printf("  -L LINKID                 id of link to set speed/divider for; needed with Brick Mk2 port 2; (default link_id = channel). \n");
			printf("  -P                        parse network byte stream for PUS packets\n");
			printf("  -F                        parse network byte stream for FEE data packets\n");
			printf("  -R RMAP_PORT              exchange RMAP via RMAP_PORT\n");
			printf("  -G                        use GRESB protocol for network exchange\n");
			printf("  -X                        execute a device reset\n");
			printf("  -h, --help                print this help and exit\n");
			printf("\n");
			exit(0);
		}

	}



    	/**
	 * set up network
	 */

	sprintf(url, "%s:%d", host, port);

	if (mode == SERVER) {

		nfds = 0;
		server_socket = bind_server_socket(url);

		server_accept_thread.sock_fd = &server_socket;
		server_accept_thread.conn_fd = &server_socket_connection;

		if ((ret = pthread_create(&th_server, NULL, accept_connections, &server_accept_thread))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		if ((ret = pthread_create(&th_server_poll, NULL, poll_socket, NULL)))
		{
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}


		printf("Started in SERVER mode\n");

	} else {

		server_socket_connection=connect_client_socket(url);

		if (server_socket_connection < 0) {
			printf("Failed to connect to %s\n", url),
				exit(EXIT_FAILURE);
		}

		if ((ret = pthread_create(&th_server, NULL, poll_socket, NULL))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		printf("Started in CLIENT mode\n");
	}

	if (enable_rmap) {

		sprintf(url, "%s:%d", host, rmap_port);
		nfds = 0;
		server_socket = rmap_bind_server_socket(url);

		rmap_server_accept_thread.sock_fd = &server_socket;
		rmap_server_accept_thread.conn_fd = &server_socket_connection;

		if ((ret = pthread_create(&th_rmap_server, NULL, rmap_accept_connections, &rmap_server_accept_thread))) {
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}

		if ((ret = pthread_create(&th_rmap_server_poll, NULL, poll_rmap_socket, NULL)))
		{
			printf("Epic fail in pthread_create: %s\n", strerror(ret));
			exit(EXIT_FAILURE);
		}


		printf("Started RMAP SERVER\n");
	}




	/**
	 * catch ctrl+c
	 */
	SIGINT_handler.sa_handler = sigint_handler;
	sigemptyset(&SIGINT_handler.sa_mask);
	SIGINT_handler.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_handler, NULL);


	/**
	 * set up SpW device
	 */

	dev_id = select_device(dev_num);

	if (reset_dev) {
		printf("Attempting to reset the device, this will have an effect on other ports!\n");
		STAR_resetDevice(dev_id);
	}

	channel = select_channel(dev_id, channel);

	printf("Setting link speed to %g Mbps\n", sig_rate);
	CFG_setTxSignallingRate(dev_id, link_id, sig_rate, &sig_rate);
	printf("Actual link speed is %g Mbps\n", sig_rate);

	spw_chan_id = STAR_openChannelToLocalDevice(dev_id, STAR_CHANNEL_DIRECTION_INOUT, channel, TRUE);
	if (!spw_chan_id) {
		printf("Error opening channel\n");
		exit(EXIT_FAILURE);
	} else {
		printf("Selected channel %d\n", channel);
	}

	/**
	 *  address path header, set via CLA
	 *  for the brick, this contain an extra port slector, i.e. <port> <remote address>
	 */
	printf("Using path of %d nodes: ", path_len);
	for(ret =0; ret < path_len; ret ++) {
		printf(":%x", path[ret]);
	}
	printf(":\n");

	p_address = STAR_createAddress(path, path_len);


	ret = pthread_create(&th_spw_poll, NULL, poll_spw, NULL);
	if (ret) {
		printf("Epic fail in pthread_create: %s\n", strerror(ret));
		exit(EXIT_FAILURE);
	}

	/* make sure the link is running */
	CFG_getPortStatusControl(dev_id, channel, &port_status);
	CFG_getSpaceWireLinkStatus(port_status, &link_status);
	link_status.start = 1;
	link_status.running = 1;
	CFG_setSpaceWireLinkStatus(dev_id, channel, &link_status);

	CFG_getRxSignallingRate(dev_id, link_id, &sig_rate);
	printf("Measured RX link speed %g Mbps\n", sig_rate);

	/**
	 *  wait for signal, then clean up
	 */

	printf("Ready...\n");

	pause();

	pthread_cancel(th_server);

	if (th_server_poll)
		pthread_cancel(th_server_poll);

	close(server_socket);

	if (server_socket_connection)
		close(server_socket_connection);


	/* XXX */
	/* Cancelled transfers are already freed and disposing them again causes a double free */
	if (STAR_getTransferOperationStatus(pus_rx_transfer_op) != STAR_TRANSFER_STATUS_CANCELLED)
		STAR_disposeTransferOperation(pus_rx_transfer_op);

	if (STAR_getTransferOperationStatus(pus_rx_transfer_op) != STAR_TRANSFER_STATUS_CANCELLED)
		STAR_disposeTransferOperation(pus_tx_transfer_op);

	if (p_address)
		STAR_destroyAddress(p_address);

	if (spw_chan_id)
		STAR_closeChannel(spw_chan_id);
}
