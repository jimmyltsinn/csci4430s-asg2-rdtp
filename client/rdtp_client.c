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
        timer.tv_sec = time(NULL) + sec; \
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
static pthread_mutex_t mutex_ack = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_work = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_done = PTHREAD_MUTEX_INITIALIZER;

struct rdtp_argv {
    int sockfd;
    struct sockaddr_in *addr;
};

static pthread_mutex_t mutex_seq = PTHREAD_MUTEX_INITIALIZER;
static int seq;
static int len;

/**
 * State variable
 * 0 - idle
 * 1 - 3WHS SYN-ACK Received
 * 2 - Connection establish
 * 3 - 4WHS FIN-ACK Received
 * 4 - Waiting for connection close
 */
static int state = 0;

static void sender(struct rdtp_argv *argv) {
    int header;
    int sockfd;
    struct sockaddr_in *addr;
    struct timespec timer; 

    sockfd = argv -> sockfd;
    addr = argv -> addr;

    /* 3WHS */
    printe("Start 3WHS\n");
    do {
        set_timer(timer, RTO);
        header = set_header(SYN, 0);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        if (pthread_cond_timedwait(&cond_ack, &mutex_ack, &timer)) {
            printe("[Connect] Timeout\n");
            continue;
        }
        if (seq != 1) {
            printe("[Connect] Wrong SEQ\n");
            continue;
        }
        break;
    } while (1);
    
    printe("First packet received. \n");

    do {
        set_timer(timer, RTO);
        header = set_header(SYN, seq);
        sendto( sockfd, &header, sizeof(int), 0,
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* TODO Retransmit ? */
    } while (0);

    pthread_cond_signal(&cond_done);

    while (1) {
        /* Main content */
        pthread_cond_wait(&cond_work, &mutex_work);
        printe("Receive work ... \n");
    }

    /* 4WHS */
    do {
        /* FIN */
        pthread_mutex_lock(&mutex_seq);
        int tmp = seq;
        set_timer(timer, RTO);
        header = set_header(SYN, tmp);
        pthread_mutex_unlock(&mutex_seq);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* FIN-ACK */
        if (pthread_cond_timedwait(&cond_ack, &mutex_ack, &timer)) {
            printe("[Close] Timeout\n");
            continue;
        }
        if (seq - tmp != 1) {
            printe("[Close] Wrong SEQ \n");
            continue;
        }
        break;
    } while (1);

    do {
        /* ACK */
        set_timer(timer, RTO);
        header = set_header(ACK, seq);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* TODO Retransmit ? */
    } while (0);

    sleep(TIME_WAIT);

    /* Wrap up ... */
    pthread_join(&thread[1], NULL);
    free(argv);
    pthread_exit(0);
}

static void receiver(struct rdtp_argv *argv) {
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
    pthread_cond_signal(&cond_work);
    pthread_cond_wait(&cond_done, &mutex_done);
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

