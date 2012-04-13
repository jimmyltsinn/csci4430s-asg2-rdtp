#include "rdtp.h"
#include "rdtp_server.h"

extern unsigned char global_send_buf[MAX_BUF_SIZE]; 
extern unsigned char global_recv_buf[MAX_BUF_SIZE];

static pthread_t thread[2];
static pthread_cond_t cond_work = PTHREAD_COND_INITIALIZER; 
static pthread_cond_t cond_done = PTHREAD_COND_INITIALIZER; 
static pthread_mutex_t mutex_work = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t mutex_done = PTHREAD_MUTEX_INITIALIZER; 

/**
 * State variable
 *  -1  No connection
 *  0   Accept block - waiting SYN
 *  1   Waiting for ACK / DATA
 *  2   ACK / DATA received ... Main phase
 *  3   4WHS
 *  4   Waiting for ACK of FIN
 *  5   TIME WAIT
 */
static int state;
static int seq; 

static void sender(struct rdtp_argv *argv) {
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr = argv -> addr; 
    
    pthread_mutex_lock(&mutex_work);

    while (1) {
        int header; 
        pthread_cond_wait(&cond_work, &mutex_work);
        printe("Start work .. \n");
        switch (state) {
            case 0: 
                do {
                    struct timespec timer; 
                    header = set_header(SYN_ACK, 1);
                    printe("[> SYN-ACK] (%d: %d-%d)\n", 
                            header, get_type(header), get_seq(header));
                    sendto(sockfd, &header, sizeof(int), 0, 
                           (struct sockaddr*) addr, sizeof(struct sockaddr_in));
                    printe("Send ...\n");
                    perror("[> SYN-ACK] sendto()");
                    state = 1;
                    set_abstimer(timer, 1);
                    printe("[> SYN-ACK] Cond wait ...\n");
                    seq = 1;
                    if (pthread_cond_timedwait(&cond_work, 
                                               &mutex_work, &timer)) {
                        printe("[> SYN-ACK] Timeout ... \n");
                        continue;
                    }
                    state = 2; 
                    pthread_cond_signal(&cond_done);
                    break;
                } while (1);
                break;
            case 2: 
                /* TODO Main phase 
                        Sending ACK     */
                break;
            case 3: 
                /* Send FIN-ACK */
                do {
                    struct timespec timer; 
                    header = set_header(FIN_ACK, seq);
                    printe("[> FIN-ACK] (%d: %d-%d)\n", 
                            header, get_type(header), get_seq(header));
                    sendto(sockfd, &header, sizeof(int), 0, 
                           (struct sockaddr*) addr, sizeof(struct sockaddr_in));
                    perror("[> FIN-ACK] sendto()");
                    state = 4;
                    set_abstimer(timer, 1);
                    printe("[> FIN-ACK] Cond wait ...\n");
                    if (pthread_cond_timedwait(&cond_work, 
                                               &mutex_work, &timer)) {
                        printe("[< ACK] Timeout ... \n");
                        continue;
                    }
                    state = -1; 
                    pthread_cond_signal(&cond_done);
                    printe("pthread_exit()\n");
                    pthread_exit(0);
                    break;
                } while (1);
            default: 
                ;
        }
    }
}

static void receiver(struct rdtp_argv *argv) {
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr = argv -> addr; 
    
    while (1) {
        int header;
        char buf[1004]; 
        int reclen;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        printe("recvfrom() ... \n");
        reclen = recvfrom(sockfd, buf, 1004, 0, 
                          (struct sockaddr*) addr, &addrlen);
        printe("recvfrom() return (%d) !\n", reclen);
        if (reclen < 4) {
            printe("Reclen (%d) less than header ... \n", reclen);
            continue;
        }
        header = ((int*) buf)[0];

        pthread_mutex_lock(&mutex_work);

        if (seq != get_seq(header)) {
            printe("Wrong SEQ ... Expected: %d | Received: %d\n", 
                   seq, get_seq(header));
            continue;
        }

        switch (state) {
            case 0: 
                if (get_type(header) == SYN) {
                    printe("[< SYN] Correct %d: %d-%d\n", 
                            header, get_type(header), get_seq(header)); 
                } else {
                    printe("[X<SYN] WRONG type: %d: %d-%d", 
                            header, get_type(header), get_seq(header)); 
                    continue;
                }
                break;
            case 1: 
                if (get_type(header) == ACK) {
                    printe("[< ACK] Correct %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    state = 2;
                    break;
                } else if (get_type(header) == DATA) {
                    printe("[< ACK] Receive DATA instead. \n");
                    state = 2;
                    //TODO DATA instead of ACK ... Any special thing to do ??
                } else {
                    printe("[X<ACK] WRONG type %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    continue;
                }
            case 2: 
                if (get_type(header) == DATA) {
                    /* Receive DATA */
                    printe("[< DAT] Data received %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    // TODO
                    break;
                } else if (get_type(header) == FIN) {
                    state = 3;
                    seq = get_seq(header) + 1; 
                } else {
                    printe("[< ???] Unknown data received %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    continue;
                }
                break;
            case 4: 
                if (get_type(header) == ACK) {
                    printe("[< ACK] %d: %d-%d\n", 
                            header, get_type(header), get_seq(header)); 
                    state = 5;
                    break;
                } else { 
                    printe("[< ???] Unknown data received %d: %d-%d\n", 
                            header, get_type(header), get_seq(header));
                    continue;
                }
            default: 
                printe("Unknown state ... State = %d | %d: %d-%d\n", 
                        state, header, get_type(header), get_seq(header));
                continue;
        }
        pthread_cond_signal(&cond_work);
        pthread_mutex_unlock(&mutex_work);
        if (state >= 5) {
            printe("pthread_exit()\n");
            pthread_exit(0);
        }
    }
}

void rdtp_accept(int socket_fd, struct sockaddr_in *server_addr) {
    struct rdtp_argv *argv; 

    argv = malloc(sizeof(struct rdtp_argv));
    argv -> sockfd = socket_fd; 
    argv -> addr = server_addr; 

    seq = 0;
    state = 0;
    
    pthread_create(&thread[0], NULL, (void* (*) (void *)) sender, argv);
    pthread_create(&thread[1], NULL, (void* (*) (void *)) receiver, argv);
   
    pthread_cond_wait(&cond_done, &mutex_done);

	return;
}

int rdtp_read(int socket_fd, unsigned char *buf, int buf_len) {
    // please extend this function
	return 1;		//return bytes of data read
}

void rdtp_close() {
    return;
}
