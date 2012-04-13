#include "rdtp.h"
#include "rdtp_client.h"

extern unsigned char global_send_buf[];

static char *sendbuf;

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
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr = argv -> addr;
    struct timespec timer; 

    seq = 0;

    /* 3WHS */
    printe("Start 3WHS\n");
        pthread_mutex_lock(&mutex_ack);
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
    
    printe("state = %d\n", state);
    
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
        pthread_cond_wait(&cond_work, &mutex_work);
        printe("Receive work ... \n");
        if (state == 3) {
            /* TODO Check empty buffer */
            break;
        }
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

    sendbuf = malloc(sizeof(char) * ((2 << 28) -  1));

    while (1) {
        int header; 
        char buf[1004];
        int reclen;
        socklen_t addrlen = sizeof(struct sockaddr_in);

        printe("recvfrom() ... \n");
        if (state < 4) {
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
                    seq += len;
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

    pthread_create(&thread[0], NULL, (void* (*) (void *)) sender, argv);
    pthread_create(&thread[1], NULL, (void* (*) (void *)) receiver, argv);

    pthread_cond_signal(&cond_work);
    pthread_cond_wait(&cond_done, &mutex_done);
	return;
}

int rdtp_write(int socket_fd, unsigned char *buf, int buf_len) {
    // please extend this function

    printe("Write is not yet implemented\n");
	return 0; 	//return bytes of data sended out
}

void rdtp_close(int socket_fd) {
    state = 3;
    pthread_cond_signal(&cond_work);
    pthread_join(thread[0], NULL);
    free(sendbuf);
	return;
}

