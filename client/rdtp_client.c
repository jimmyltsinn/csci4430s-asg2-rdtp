#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>

#define set_header(type, seq) ((type << 28) | (seq & (0xF << 28)))
#define get_type(header) (header >> 28)
#define get_seq(header) (header & (0xF << 28))

#define SYN 0
#define SYN_ACK 1
#define FIN 2
#define FIN_ACK 3
#define ACK 4
#define DATA 5

#define RTO 1
#define TIME_WAIT 10

#define set_timer(timer, sec) \
    do { \
        timer.tv_sec = sec; \
        timer.tv_nsec = 0; \
    } while (0)

#ifndef BUILD
#define printe(fmt, arg ...) \
           fprintf(stderr, "[%s():%3d] " fmt, __FUNCTION__, __LINE__, ##arg)
#else
#define printe(fmt, ...) (0)
#endif

extern unsigned char global_send_buf[];

static pthread_t thread[2];
static pthread_cond_t cond_ack = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_done = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex;

struct rdtp_argv {
    int sockfd;
    struct sockaddr_in *addr;
};

static int seq;
static int len;

static void sender(struct rdtp_argv *argv) {
    int header;
    int sockfd;
    struct sockaddr_in *addr;
    struct timespec timer; 

    sockfd = argv -> sockfd;
    addr = argv -> addr;

//    printe("Wait for cond_ack ...\n");
//    pthread_cond_wait(&cond_ack, &mutex);
//    printe("Fire cond_done\n");
//    pthread_cond_signal(&cond_done);

    /* 3WHS */
    printe("Wait for cond_work ... \n");
    pthread_cond_wait(&cond_work, &mutex);
    printe("Start 3WHS\n");
    do {
        set_timer(timer, RTO);
        header = set_header(SYN, 0);
        sendto(sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        if (pthread_cond_timedwait(&cond_ack, &mutex, &timer)) {
            printe("[Connect] Timeout\n");
            continue;
        }
        if (seq != 1) {
            printe("[Connect] Wrong SEQ\n");
            continue;
        }
    } while (0);
    

    while (1) {
    }

    free(argv);
    pthread_exit(0);
}

static void receiver(struct rdtp_argv *argv) {
//    pthread_cond_signal(&cond_ack);
//    printe("Fire cond_ack\n");
    while(1);
    return;
}

void rdtp_connect(int socket_fd, struct sockaddr_in *server_addr) {
    struct rdtp_argv *argv;

    argv = malloc(sizeof(struct rdtp_argv));
    argv -> sockfd = socket_fd;
    argv -> addr = server_addr;

    pthread_create(&thread[0], NULL, (void* (*) (void *)) sender, argv);
    pthread_create(&thread[1], NULL, (void* (*) (void *)) receiver, argv);
    sleep(0);
    pthread_cond_wait(&cond_done, &mutex);
    pthread_cond_signal(&cond_work);
    pthread_cond_wait(&cond_done, &mutex);
	return;
}

int rdtp_write(int socket_fd, unsigned char *buf, int buf_len) {
    // please extend this function

	return 1; 	//return bytes of data sended out
}

void rdtp_close(int socket_fd) {
    // please extend this function
    
	return;
}

