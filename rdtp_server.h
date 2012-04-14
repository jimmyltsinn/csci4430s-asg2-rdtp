#ifndef __RDTP_SERVER__

#define __RDTP_SERVER__

#define RECV_BUF_SIZE ((1 << 28) - 3)

void rdtp_accept(int socket_fd, struct sockaddr_in *server_addr);

int rdtp_read(int socket_fd, unsigned char *buf, int buf_len);

void rdtp_close();

#endif // __RDTP_SERVER__
