#ifndef __RDTP_CLIENT__

#define __RDTP_CLIENT__

void rdtp_connect(int socket_fd, struct sockaddr_in *server_addr);

int rdtp_write(int socket_fd, unsigned char *buf, int buf_len);

void rdtp_close(int socket_fd);

#endif // __RDTP_CLIENT__
