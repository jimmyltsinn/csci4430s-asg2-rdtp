CC=gcc
CFLAGS=-Wall -I. -g
LDFLAGS=-lpthread

all: client server

client: client.o rdtp_client.o
	${CC} ${CFLAGS} client.o rdtp_client.o -o client ${LDFLAGS}
	
client.o: client.c rdtp_client.h
	${CC} ${CFLAGS} client.c -c ${LDFLAGS}

rdtp_client.o: rdtp_client.c rdtp_client.h rdtp_common.h rdtp.h
	${CC} ${CFLAGS} rdtp_client.c -c ${LDFLAGS}

server: server.o rdtp_server.o
	${CC} ${CFLAGS} server.o rdtp_server.o -o server ${LDFLAGS}
	
server.o: server.c rdtp_server.h
	${CC} ${CFLAGS} server.c -c ${LDFLAGS}

rdtp_server.o: rdtp_server.c rdtp_server.h rdtp_common.h rdtp.h
	${CC} ${CFLAGS} rdtp_server.c -c ${LDFLAGS}

clean:
	rm -f *.o client server
