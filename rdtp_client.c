#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>

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

#define set_abstimer(timer, sec) \
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
 * 3 - Closing connection
 * 4 - 4WHS FIN-ACK Received
 * 5 - Waiting for connection close
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
        header = set_header(SYN, 0);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        len = 1; 
        set_abstimer(timer, RTO);
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
    
    state = 1;
    printe("First packet received. \n");

    do {
        set_abstimer(timer, RTO);
        header = set_header(SYN, seq);
        sendto( sockfd, &header, sizeof(int), 0,
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* TODO Retransmit ? */
    } while (0);

    state = 2;
    printe("3WHS Done\n");
    pthread_cond_signal(&cond_done);

    while (1) {
        /* Main content */
        pthread_cond_wait(&cond_work, &mutex_work);
        printe("Receive work ... \n");
        if (state == 3)
            break;
    }

    /* 4WHS */
    printe("Start 4WHS\n");
    do {
        /* FIN */
        int tmp;
        pthread_mutex_lock(&mutex_seq);
        tmp = seq;
        header = set_header(SYN, tmp);
        pthread_mutex_unlock(&mutex_seq);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* FIN-ACK */
        set_abstimer(timer, RTO);
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
    state = 4;

    do {
        /* ACK */
        set_abstimer(timer, RTO);
        header = set_header(ACK, seq);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* TODO Retransmit ? */
    } while (0);

    state = 5;
    printe("4WHS Done ... Waiting for TIME_WAIT\n");

    sleep(TIME_WAIT);
    
    printe("TIME_WAIT exceed ... Go die now\n");
    pthread_cond_signal(&cond_ack);
    
    /* Wrap up ... */
    pthread_join(thread[1], NULL);
    state = 0;
    free(argv);
    pthread_exit(0);
}

static void receiver(struct rdtp_argv *argv) {
    int sockfd;
    struct sockaddr_in *addr;

    sockfd = argv -> sockfd;
    addr = argv -> addr;
    
    while (1) {
        int header; 
        char buf[1004];
        int reclen;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        reclen = recvfrom(   sockfd, buf, 1004, 0, 
                    (struct sockaddr*) addr, &addrlen);
        if (reclen < 4) {
            printe("Reclen less than header ... reclen = %d\n", reclen);
            continue;
        }
        header = ntohl((int) buf[0]);
        pthread_mutex_lock(&mutex_seq);
        if (get_seq(header) != (seq + len)) {
            printe( "Wrong SEQ ... Expected: %d | Received: %d\n", 
                    seq, get_seq(header));
            continue;
        }
        switch (get_type(header)) {
            case SYN_ACK: 
                if (state == 0) {
                    printe("SYN-ACK correct\n");
                } else {
                    printe( "SYN-ACK at non-starting phase [T = %d, S = %d]\n", 
                            get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_seq);
                    continue;
                }
                break;
            case FIN_ACK: 
                if (state == 4) {
                    printe("FIN-ACK correct\n");
                    goto out;
                } else {
                    printe( "FIN-ACK at non-ending phase [T = %d, S = %d] \n", 
                            get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_seq);
                    continue;
                }
                break;
            case ACK: 
                if (state == 3) {
                    printe("ACK correct\n");
                } else {
                    printe( "ACK at non-middle phase [T = %d, S = %d] \n", 
                            get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_seq);
                    continue;
                }
                break;
            default: 
                printe( "Unknown data received. [T = %d, S = %d]\n", 
                        get_type(header), get_seq(header));
        }
        seq += len;
        pthread_mutex_unlock(&mutex_seq);
        pthread_cond_signal(&cond_ack);
    }

out: 
    printe("Entering final state ... \n");
    pthread_cond_wait(&cond_ack, &mutex_ack);
    printe("Die now .. ");
    pthread_exit(0);
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
    state = 3;
    pthread_cond_signal(&cond_work);
    pthread_join(thread[0], NULL);
	return;
}

