#include <netinet/in.h>

extern unsigned char global_send_buf[];

void rdtp_connect(int socket_fd, struct sockaddr_in *server_addr)
{
    // please extend this function

	return ;
}

int rdtp_write(int socket_fd, unsigned char *buf, int buf_len)
{
    // please extend this function

	return 1; 	//return bytes of data sended out
}

void rdtp_close(int socket_fd)
{
    // please extend this function

	return ;
}

