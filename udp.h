#ifndef UDP_H
#define UDP_H

#include <netinet/in.h>

void dgram_add_client(struct sockaddr_in client);
int dgram_bind_socket(int port);
void *dgram_poll_socket(void *arg);

void dgram_send_all(unsigned char *buffer, unsigned int buf_len);

#endif
