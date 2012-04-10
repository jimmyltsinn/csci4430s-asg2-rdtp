#ifndef __RDTP_SERVER__

#define __RDTP_SERVER__

void rdtp_accept(int socket_fd, struct sockaddr_in *server_addr);

int rdtp_read(int socket_fd, unsigned char *buf, int buf_len);

#endif // __RDTP_SERVER__
