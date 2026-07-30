#include "udp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <star/star-api.h>

#define BUFFER_SIZE 65536
/* That should be the maximum for a single UDP packet */
#define MAX_PKT_LEN 65507

static uint8_t buf[BUFFER_SIZE];

struct dgram_clients {
	size_t capacity;
	size_t length;
	struct sockaddr_in *clients;
};

static struct dgram_clients clients;

/* defined in spw_bridge.c */
extern STAR_SPACEWIRE_ADDRESS *p_address;
extern STAR_CHANNEL_ID spw_chan_id;
extern int pkt_throttle_usec;

static void dgram_to_spw(unsigned char *buffer, unsigned int pkt_len)
{
	STAR_STREAM_ITEM 	*p_tx_stream_item = NULL;
	STAR_TRANSFER_OPERATION *p_tx_transfer_op = NULL;
	STAR_TRANSFER_STATUS tx_status;
	
	p_tx_stream_item = STAR_createPacket(p_address, buffer, pkt_len, STAR_EOP_TYPE_EOP);
	if (!p_tx_stream_item){
		printf("Error creating stream item\n");
		exit(EXIT_FAILURE);
	}

	if (!p_tx_stream_item){
		printf("Error creating stream item\n");
		exit(EXIT_FAILURE);
	}

	p_tx_transfer_op = STAR_createTxOperation(&p_tx_stream_item, 1);

	if (!p_tx_transfer_op){
		printf("Error creating transfer operation\n");
		exit(EXIT_FAILURE);
	}

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

	if (p_tx_transfer_op)
		STAR_disposeTransferOperation(p_tx_transfer_op);

	if (p_tx_stream_item)
		STAR_destroyStreamItem(p_tx_stream_item);
}

void dgram_add_client(struct sockaddr_in client)
{
	/* On first invocation this will initialize the structure  */
	if (clients.capacity >= clients.length) {
		clients.capacity = clients.capacity == 0 ? 4 : clients.capacity*2;
		clients.length = 0;
		clients.clients = realloc(clients.clients, sizeof(struct sockaddr_in) * clients.capacity);
	}

	/* Check if client already in the list */
	for (size_t i = 0; i < clients.length; i++) {
		if (clients.clients[i].sin_addr.s_addr == client.sin_addr.s_addr &&
			clients.clients[i].sin_port == client.sin_port) {
			/* client already found we can return from here */
			return;
		}
	}
	
	/* Otherwise add it */
	memcpy(&clients.clients[clients.length], &client, sizeof(struct sockaddr_in));
	client.length++;
}

/**
 * @brief binds a dgram socket
 */
int dgram_bind_socket(int port)
{
	int endpoint;
	struct sockaddr_in server;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sockfd < 0) {
		printf("Dgram socket creation failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);

	endpoint = bind(sockfd, (struct sockaddr*)&server, sizeof(server));
	if (endpoint < 0) {
		close(sockfd);
		printf("Could not bind dgram endpoint: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return sockfd;
}



/**
 * @brief polls data from the socket
 */
void *dgram_poll_socket(void *arg)
{
	struct sockaddr_in client;
	ssize_t recv_len;
	uint32_t client_len = sizeof(struct sockaddr_in);
	int nfds = sockfd + 1;
	fd_set read_set, conn_set;
	struct timeval timeout;

	FD_ZERO(&conn_set);
	FD_SET(sockfd, &conn_set);

	/* wait 10 ms in select() */
	timeout.tv_sec  = 0;
	timeout.tv_usec = 10000;

	printf("Starting UDP packet polling\n");

	while (1) {
		read_set = conn_set;
		
		if (select(nfds, &read_set, NULL, NULL, &timeout) <= 0) {
			usleep(1000);
			continue;
		}
		
		recv_len = recvfrom(sockfd, buf, BUFFER_SIZE, 0,
							(struct sockaddr*)&client, &client_len);

		printf("Received UPD packet with size %ld from client %s:%d\n",
			   recv_len,
			   inet_ntoa(client.sin_addr),
			   ntohs(client.sin_port));

		/* if client is new add it to the list of recepients */
		dgram_add_client(client);

		dgram_to_spw(buf, recv_len);
	}
	
	return NULL;
}



void dgram_send_all(unsigned char *buffer, unsigned int buf_len)
{
	ssize_t res;

	if (buf_len > MAX_PKT_LEN) {
		printf("Packet size %u is larger than can be sent in a single UDP packet.\n", buf_len);
		return;
	}
	
	for(size_t i = 0; i < clients.length; i++) {
		res = sendto(sockfd, buffer, buf_len, 0,
					 (struct sockaddr*)&clients.clients[i], sizeof(struct sockaddr_in));

		if (res < 0) {
			printf("Udp message could not be sent to %s:%d. Error %s\n",
				   inet_ntoa(clients.clients[i].sin_addr),
				   ntohs(clients.clients[i].sin_port),
				   strerror(errno));
		}
	}
}


