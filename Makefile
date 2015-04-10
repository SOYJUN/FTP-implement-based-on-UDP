CC := gcc

LIBS := -lresolv -lnsl -lpthread -lm\
	../stevens_code/libunp.a\

FLAGS := -g -O2

CFLAGS := ${FLAGS} -I../stevens_code/lib

all: client server test get_ifi_info_plus.o rtt.o serv_send.o serv_recv.o cli_send.o cli_recv.o 

test: 	test.o 
	${CC} ${FLAGS} -o test test.o ${LIBS}
server.o: server.c
	${CC} ${CFLAGS} -c server.c

server: server.o get_ifi_info_plus.o rtt.o serv_send.o serv_recv.o
	${CC} ${FLAGS} -o server server.o get_ifi_info_plus.o rtt.o serv_send.o serv_recv.o ${LIBS}
server.o: server.c
	${CC} ${CFLAGS} -c server.c


client:	client.o get_ifi_info_plus.o rtt.o cli_send.o cli_recv.o
	${CC} ${FLAGS} -o client client.o get_ifi_info_plus.o rtt.o cli_send.o cli_recv.o ${LIBS}
client.o: client.c
	${CC} ${CFLAGS} -c client.c


get_ifi_info_plus.o: get_ifi_info_plus.c
	${CC} ${CFLAGS} -c get_ifi_info_plus.c

rtt.o:  rtt.c
	${CC} ${CFLAGS} -c rtt.c

data_send.o: serv_send.c
	${CC} ${CFLAGS} -c serv_send.c

data_recv.o: serv_recv.c
	${CC} ${CFLAGS} -c serv_recv.c

cli_send.o: cli_send.c
	${CC} ${CFLAGS} -c cli_send.c

cli_recv.o: cli_recv.c
	${CC} ${CFLAGS} -c cli_recv.c


clean:
	rm test test.o client client.o server server.o get_ifi_info_plus.o rtt.o serv_send.o serv_recv.o cli_send.o cli_recv.o

