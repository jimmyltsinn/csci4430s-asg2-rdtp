#include "rdtp.h"
#include "rdtp_client.h"

#ifndef BUILD
#define printe(fmt, arg ...) \
    fprintf(stderr, "[1;32m[%ld][0m [1;42m[%s: %10s(): %3d][0m " fmt, \
            time(NULL), __FILE__, __FUNCTION__, __LINE__, ##arg)
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

static pthread_mutex_t mutex_seq = PTHREAD_MUTEX_INITIALIZER;

static int seq;
static int len;
static char *sendbuf;
static char *buf_front, *buf_end; 

/**
 * State variable
 * 0 - idle
 * 1 - 3WHS SYN-ACK Received
 * 2 - Connection establish
 * 3 - Closing connection
 * 4 - 4WHS FIN-ACK Received
 * 5 - TIME WAIT
 */
static int state = 0;

static void sender(struct rdtp_argv *argv) {
    int header;
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr = argv -> addr;
    struct timespec timer; 

    seq = 0;

    /* 3WHS */
    printe("Start 3WHS\n");
    pthread_mutex_lock(&mutex_ack);
    pthread_mutex_lock(&mutex_work);
    do {
        int e; 
        header = set_header(SYN, 0);
        len = 1;
        printe("[> SYN] (%d: %d-%d)\n", 
                header, get_type(header), get_seq(header));
        sendto(sockfd, &header, sizeof(int), 0, 
               (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        perror("[> SYN] sendto()");
        set_abstimer(timer, RTO);
        printe("[> SYN] Start wait ... \n");
        if ((e = pthread_cond_timedwait(&cond_ack, &mutex_ack, &timer)) != 0) {
            printe("[X<SYN-ACK] (%ld) %s\n", time(NULL), strerror(e));
            continue;
        } 
        printe("[< SYN-ACK] Correct\n");
        state = 1;
        len = 0;
        break;
    } while (1);
    
    do {
        set_abstimer(timer, RTO);
        header = set_header(ACK, seq);
        len = 0;
        printe("[> ACK] Send %d: %d-%d \n", 
                header, get_type(header), get_seq(header));
        sendto( sockfd, &header, sizeof(int), 0,
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        /* TODO Retransmit ? */
    } while (0);

    state = 2;
    printe("3WHS Done\n");
    pthread_cond_signal(&cond_done);

    while (1) {
        /* Main content */
        char content[1004]; 
        int len; 
        if (buf_front == buf_end) {
            printe("Wait cond_work\n");
            pthread_cond_wait(&cond_work, &mutex_work);
        } else {
            printe("Wait cond_ack\n");
            set_abstimer(timer, RTO);
            if (pthread_cond_timedwait(&cond_ack, &mutex_ack, &timer)) {
                printe("No ACK return ... Retransmit\n");
            }
        }
        printe("Trigger send ...\n");
        if (state == 3) {
            printe("In close state ...\n");
            if (buf_front == buf_end) {
                printe("No more thing in buf ... Really close connection\n");
                break;
            }
        }
        
        len = ((intptr_t) buf_end) - ((intptr_t) buf_front); 
        if (len > 1000)
            len = 1000; 

        *((int *) content) = set_header(DATA, seq);
        memcpy(content + 4, sendbuf, len);
        
        printe("Sending [%d-%d] with len = %d\n", 
               get_type(*((int *) content)), get_seq(*((int *) content)), len);
        
        sendto(sockfd, content, 4 + len, 0, 
               (struct sockaddr*) addr, sizeof(struct sockaddr_in));
    }

    /* 4WHS */
    printe("Start 4WHS\n");
    do {
        /* FIN */
        int tmp;
        pthread_mutex_lock(&mutex_seq);
        header = set_header(FIN, seq);
        pthread_mutex_unlock(&mutex_seq);
        len = 1;
        printe("[> FIN] Sent %d: %d-%d\n", 
                header, get_type(header), get_seq(header));
        sendto(sockfd, &header, sizeof(int), 0, 
               (struct sockaddr*) addr, sizeof(struct sockaddr_in));

        /* FIN-ACK */
        set_abstimer(timer, RTO);
        if (pthread_cond_timedwait(&cond_ack, &mutex_ack, &timer)) {
            printe("[x<FIN-ACK] Timeout\n");
            continue;
        }
        len = 0;
        break;
    } while (1);

    state = 4;

    set_abstimer(timer, TIME_WAIT);
    do {
        /* ACK */
        struct timespec tmp = timer; 
        header = set_header(ACK, seq);
        sendto( sockfd, &header, sizeof(int), 0, 
                (struct sockaddr*) addr, sizeof(struct sockaddr_in));
        printe("[> ACK] %d: %d-%d\n", 
                header, get_type(header), get_seq(header));
        state = 5;
        if (pthread_cond_timedwait(&cond_ack, &mutex_ack, &tmp)) {
            printe("TIME_WAIT exceed\n");
            break;
        }
        continue;
    } while (0);

    /* Wrap up ... */
    pthread_join(thread[1], NULL);
    state = 0;
    free(argv);
    pthread_exit(0);
}

static void receiver(struct rdtp_argv *argv) {
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr = argv -> addr;

    while (1) {
        int header; 
        char buf[1004];
        int reclen;
        socklen_t addrlen = sizeof(struct sockaddr_in);

        if (state < 4) {
            printe("recvfrom() ... \n");
            reclen = recvfrom(sockfd, buf, sizeof(int), 0, 
                              (struct sockaddr*) addr, &addrlen);
        } else {
            fd_set rfds; 
            static struct timeval tv; 
            int ret; 
            FD_ZERO(&rfds);
            FD_SET(sockfd, &rfds);
            if (tv.tv_sec == 0) {
                tv.tv_sec = TIME_WAIT;
                tv.tv_usec = 0;
            }
            printe("TIME WAIT ...\n");
            ret = select(sockfd + 1, &rfds, NULL, NULL, &tv); 
            if (ret == 0) {
                pthread_exit(0);
            } else if (ret < 0) {
                printe("Error ... Reset the timer\n");
                tv.tv_sec = TIME_WAIT;
                tv.tv_usec = 0;
            } else {
                reclen = recvfrom(sockfd, buf, sizeof(int), 0, 
                                  (struct sockaddr*) addr, &addrlen);
            }
        }
        printe("recnfrom() return (%d)\n", reclen);
        if (reclen < 4) {
            printe("Reclen (%d) less than header ... \n", reclen);
            continue;
        }
        pthread_mutex_lock(&mutex_ack);
        header = ((int*) buf)[0];
        printe("Header = %d\n", header);
        pthread_mutex_lock(&mutex_seq);

        if (get_seq(header) != (seq + len)) {
            printe( "Wrong SEQ ... Expected: %d | Received: %d\n", 
                    seq + len, get_seq(header));
            continue;
        }

        switch (state) {
            case 0: 
            case 1: 
                if (get_type(header) == SYN_ACK) {
                    printe("[< SYN-ACK] Correct: %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    state = 1;
                    seq = 1;
                } else {
                    printe("[< SYN-ACK] WRONG SEQ ... [%d: %d-%d]\n", 
                            header, get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_ack);
                    continue;
                }
                break;
            case 2: 
                printe("Receiving in normal state ...");
                if (get_type(header) == ACK) {
                    printe("[< ACK-DATA] %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    printe("seq change\n");
                    seq = get_seq(header);
                } else {
                    printe("[< ???] %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_ack);
                    continue;
                }
                break;
            case 3: 
            case 5: 
                if (get_type(header) == FIN_ACK) {
                    printe("[< FIN-ACK] Correct\n");
                    state = 5;
                    seq += len;
                } else {
                    printe("[X<FIN-ACK] %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    pthread_mutex_unlock(&mutex_ack);
                    continue;
                }
                break;
            default: 
                printe("Unknown state (%d) receive data ... [%d-%d]\n",
                        state, get_type(header), get_seq(header));
                pthread_mutex_unlock(&mutex_ack);
                continue;
        }
        
        printe("cond_ack signal sent\n");
        pthread_mutex_unlock(&mutex_ack);
        pthread_cond_signal(&cond_ack);
        pthread_mutex_unlock(&mutex_seq);
    }
}

void rdtp_connect(int socket_fd, struct sockaddr_in *server_addr) {
    struct rdtp_argv *argv;

    argv = malloc(sizeof(struct rdtp_argv));
    argv -> sockfd = socket_fd;
    argv -> addr = server_addr;
    
    sendbuf = malloc(sizeof(char) * SEND_BUF_SIZE);
    buf_front = sendbuf;
    buf_end = sendbuf; 

    pthread_create(&thread[0], NULL, (void* (*) (void *)) sender, argv);
    pthread_create(&thread[1], NULL, (void* (*) (void *)) receiver, argv);

    pthread_cond_signal(&cond_work);
    pthread_cond_wait(&cond_done, &mutex_done);
	return;
}

int rdtp_write(int socket_fd, const unsigned char *buf, int buf_len) {
    switch (state) {
        case 0: 
            printe("Connection is closed or never be connected. \n");
            return -1; 
        case 1: 
            printe("3WHS working ...\n");
            return 0;
        case 3: 
        case 4: 
        case 5: 
            printe("4WHS working ... \n");
            return 0;
    }

    memcpy(buf_end, buf, buf_len);
    buf_end += buf_len;

    pthread_cond_signal(&cond_work);

	return buf_len;
}

void rdtp_close(int socket_fd) {
    state = 3;
    printe("Wait for mutex_work\n");
    pthread_mutex_lock(&mutex_work);
    printe("Fire cond_work\n");
    pthread_cond_signal(&cond_work);
    pthread_mutex_unlock(&mutex_work);
    printe("Join ..\n");
    pthread_join(thread[0], NULL);
    free(sendbuf);
    sendbuf = NULL;
    buf_front = NULL;
    buf_end = NULL;
	return;
}

