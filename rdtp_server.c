#include "rdtp.h"
#include "rdtp_server.h"

extern unsigned char global_send_buf[MAX_BUF_SIZE]; 
extern unsigned char global_recv_buf[MAX_BUF_SIZE];

static pthread_t thread[2];
static pthread_cond_t cond_work = PTHREAD_COND_INITIALIZER; 
static pthread_cond_t cond_done = PTHREAD_COND_INITIALIZER; 
static pthread_mutex_t mutex_work = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t mutex_done = PTHREAD_MUTEX_INITIALIZER; 

static int state;
static int seq; 

static void sender(struct rdtp_argv *argv) {
    int sockfd = argv -> sockfd;
    struct sockaddr_in *addr; 

    while (1) {
        int header; 
        char buf[1004]; 
        int reclen; 
        socklen_t addrlen = sizeof(struct sockaddr_in);
        reclen = recvfrom(  sockfd, buf, 1004, 0, 
                            (struct sockaddr*) addr, &addrlen);
        if (reclen < 4) {
            printe("Reclen (%d) less than header ... \n", reclen);
            continue;
        }
        header = ntohl((int) buf[0]);
        

    } 
    pthread_cond_wait(&cond_work, &mutex_work);

    pthread_exit(0);
}

static void receiver(struct rdtp_argv *argv) {

    

    pthread_exit(0);
}

void rdtp_accept(int socket_fd, struct sockaddr_in *server_addr) {
    struct rdtp_argv *argv; 

    argv = malloc(sizeof(struct rdtp_argv));
    argv -> sockfd = socket_fd; 
    argv -> addr = server_addr; 

    pthread_create(&thread[0], NULL, (void* (*) (void *)) sender, argv);
    pthread_create(&thread[1], NULL, (void* (*) (void *)) receiver, argv);
    
    pthread_cond_wait(&cond_done, &mutex_done);

	return;
}

int rdtp_read(int socket_fd, unsigned char *buf, int buf_len) {
    // please extend this function

	return 1;		//return bytes of data read
}
