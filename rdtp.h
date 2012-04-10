#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>
#include "rdtp_common.h"

#define set_header(type, seq) (htonl(((type << 28) | (seq & (0xF << 28)))))
#define get_type(header) ((ntohl(header) >> 28))
#define get_seq(header) (ntohl(header) & (0xF << 28))

#define SYN 0
#define SYN_ACK 1
#define FIN 2
#define FIN_ACK 3
#define ACK 4
#define DATA 5

#define RTO 1
#define TIME_WAIT 10

#ifndef BUILD
#define printe(fmt, arg ...) \
           fprintf(stderr, "[%s():%3d] " fmt, __FUNCTION__, __LINE__, ##arg)
#else
#define printe(fmt, ...) (0)
#endif

struct rdtp_argv {
    int sockfd;
    struct sockaddr_in *addr;
};
