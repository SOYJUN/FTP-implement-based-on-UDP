#include "unpifiplus.h"
#include "unprtt.h"
#include <setjmp.h>

#define IF_NUM 10					//
#define TIME_OUT 30					//
#define CONN_TIME_OUT 200			//
#define WAIT_TIME 2
#define INPUT_FILE_NAME "server.in"	//
#define	DGRAM_LEN 512
#define MAX_CWIN 32

//
struct server_in{
    int             port_num;
    int             window_size;
};

struct servIF{
	int					sif_sockfd;						//
	char				sif_bound[INET_ADDRSTRLEN];		//
	char				sif_ntm[INET_ADDRSTRLEN];		//
	char				sif_snm[INET_ADDRSTRLEN];						//
};

struct hdr {
 	uint32_t	seq;	/* sequence # */
  	uint32_t	ts;		/* timestamp when sent */
  	uint32_t	fin;	/* mark when file eof */
};

sigjmp_buf		jmpbuf;

//
void sig_chld(int);
//
void sig_alrm(int);
//
ssize_t serv_send(int, int, int, struct hdr*, void*, size_t);
//
ssize_t serv_recv(int, struct hdr*, void*, size_t);
//print out the structure of data
void prifsif(struct servIF *, int);
//calculate the subnet mask
char* subaddr(char *, char *);
//realize the three handshake
void hs_pnum_s(int, int, struct sockaddr_in, struct sockaddr_in);
//
struct server_in get_input_file(struct server_in);

int main(int argc, char **agrv){
	int 				i, n, sockfd[100], connfd, snum, maxfdp1, tmp, pos, last_pos, dg_num, fin, rs_sqn, is_timeout, is_noack, ssthresh, is_dup, is_eof;
	int  				issamehost, islocal, cwin, seq_num, rttinit;
	ssize_t				nread;
	size_t              leng;
	fd_set				rset;
	FILE 				*fn;
	socklen_t			len, peerlen;
	pid_t 				pid;
	char 				mesg[DGRAM_LEN], fpname[DGRAM_LEN], sendline[DGRAM_LEN], *req;
	const int 			on = 1;
	struct ifi_info 	*ifi;
	struct sockaddr_in	*sa, servaddr, cliaddr, peeraddr;
	struct servIF		sif[IF_NUM]; //assume 10 interface for unicast
	struct server_in 	input_file;
	struct hdr			sendhdr[MAX_CWIN], recvhdr[MAX_CWIN];
	char				outbuf[MAX_CWIN][DGRAM_LEN], intbuf[MAX_CWIN][DGRAM_LEN];
	struct rtt_info     rttinfo;


	//Initiation
	snum = 0;
	issamehost = 0;
	islocal = 0;

	// Get parameters in input file;
	input_file = get_input_file(input_file);

	//get the interface of the host
	for(ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi -> ifi_next){

		if((sockfd[snum] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
			err_sys("[ERROR]: build sock error...");
		else
			sif[snum].sif_sockfd = sockfd[snum];

		if(setsockopt(sockfd[snum], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
			err_sys("[ERROR]: set socket option(SO_REUSEADDR) error...");

		sa = (struct sockaddr_in *) ifi -> ifi_addr;
		sa -> sin_family = AF_INET;
		sa -> sin_port = htons(input_file.port_num);
		//bind the sockfd and the server ip
		if(bind(sockfd[snum], (SA *) sa, sizeof(*sa)) < 0)
			err_sys("[ERROR]: bind error...");

		if(inet_ntop(AF_INET, &sa -> sin_addr, sif[snum].sif_bound, sizeof(sif[snum].sif_bound)) == NULL)
			err_sys("[ERROR]: inet_ntop error");

		if ((sa = (struct sockaddr_in *)ifi->ifi_ntmaddr) != NULL){
			if(inet_ntop(AF_INET, &sa -> sin_addr, sif[snum].sif_ntm, sizeof(sif[snum].sif_ntm)) == NULL)
				err_sys("[ERROR]: inet_ntop error");
		}

		strcpy(sif[snum].sif_snm, subaddr(sif[snum].sif_bound, sif[snum].sif_ntm));

		snum++;
	}

	//print out the ip struct
	prifsif(sif, snum);

	//set up the block select function
	while(1){
		FD_ZERO(&rset);
		for(i = 0; i < snum; i++){
			tmp = 0;
			tmp = max(sockfd[i], tmp);
		}
		maxfdp1 = tmp + 1;
		for(i = 0; i < snum; i++)
			FD_SET(sockfd[i], &rset);
		if(select(maxfdp1, &rset, NULL, NULL, NULL) < 0){
			if(errno == EINTR)
				continue;
			else
				err_sys("[ERROR]: select error...");
		}

		signal(SIGCHLD, sig_chld);

		for(i = 0; i < snum; i++){
			if(FD_ISSET(sockfd[i], &rset)){
				if((pid = fork()) == 0){

					//first read the socket buffer and impulse the handshake
					len = sizeof(cliaddr);
					if((n = recvfrom(sockfd[i], mesg, DGRAM_LEN, 0, (SA *) &cliaddr, &len)) < 0)
						err_sys("[ERROR]: child: recvfrom error...");

					sleep(1);
					printf("[INFO]: Enter child process...\n");

					//judge the connection type
					printf("\n\n");
					mesg[n] = 0;
					req = mesg;
					if(!strcmp(req, "INIT_127")){
						issamehost = 1;
						printf("[INFO]: Receive the REQ from client: [%s]\n", mesg);

						len = sizeof(cliaddr);
						snprintf(mesg, sizeof(mesg), "SAME_HOST");
						if(sendto(sockfd[i], mesg, sizeof(mesg), 0, (SA *) &cliaddr, len) < 0)
							err_sys("[ERROR]: hs_pnum_s: sendto error...");
					}

					else if(!memcmp(req, "INIT_CON", 8)){
						printf("[INFO]: Receive the REQ from client: [%s]\n", mesg);
						if(!strcmp(req+8, sif[i].sif_snm)){
							islocal = 1;
							len = sizeof(cliaddr);
							snprintf(mesg, sizeof(mesg), "LOCAL_HOST");
							if(sendto(sockfd[i], mesg, sizeof(mesg), 0, (SA *) &cliaddr, len) < 0)
								err_sys("[ERROR]: hs_pnum_s: sendto error...");
						}
						else{
							len = sizeof(cliaddr);
							snprintf(mesg, sizeof(mesg), "COMM_HOST");
							if(sendto(sockfd[i], mesg, sizeof(mesg), 0, (SA *) &cliaddr, len) < 0)
								err_sys("[ERROR]: hs_pnum_s: sendto error...");
						}
					}

					else{
						printf("[ERROR]: UNKNOWN REQ: %s\n", req);
						exit(0);
					}

					//build up the connection socket and bind it to the ephemeral port
					if((connfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
						err_sys("[ERROR]: child: build sock error...");

					if(setsockopt(connfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
						err_sys("[Error]: Setting SO_REUSEADDR socket option error.");

					//set up the socket option SO_DONTROUTE
					if(islocal){
						if(setsockopt(connfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on)) < 0)
							err_sys("[Error]: Setting SO_DONTROUTE socket option error.");
						else
							printf("[INFO]: The socket option SO_DONTROUTE is set up...\n");
					}

					bzero(&servaddr, sizeof(servaddr));
					if(issamehost){
						if (inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) <= 0)
							err_sys("[ERROR]: main: inet_pton error for 127.0.0.1");
					}
					else{
						if(getsockname(sockfd[i], (SA *) &servaddr, &len) < 0)
							err_sys("[ERROR]: getsockname error...");
					}
					servaddr.sin_family = AF_INET;
					servaddr.sin_port = htons(0);

					if(bind(connfd, (SA *) &servaddr, sizeof(servaddr)) < 0)
						err_sys("[ERROR]: child: bind error...");

					len = sizeof(servaddr);
					if(getsockname(connfd, (SA *) &servaddr, &len) < 0)
						err_sys("[ERROR]: getsockname error...");
					else
						printf("[INFO]: The connection socket is bound to %s\n", sock_ntop((SA *) &servaddr, len));

					//get the IP client and its port number
					if(issamehost){
						if (inet_pton(AF_INET, "127.0.0.1", &cliaddr.sin_addr) <= 0)
							err_sys("[ERROR]: main: inet_pton error for 127.0.0.1");
					}

					len = sizeof(cliaddr);
					if(connect_timeo(connfd, (SA *) &cliaddr, sizeof(cliaddr), CONN_TIME_OUT) < 0)
						err_sys("[ERROR]: connect error...");
					peerlen = sizeof(peeraddr);
					getpeername(connfd, (SA *) &peeraddr, &peerlen);
					printf("[INFO]: And it connects to %s: success\n", sock_ntop((SA *) &peeraddr, peerlen));
					printf("---------------------------------\n");

					sleep(1);

					//handshake: send the new port num to client
					hs_pnum_s(sockfd[i], connfd, cliaddr, servaddr);

					while(1){
						//wait for file request
						if((n = recv(connfd, mesg, DGRAM_LEN, 0)) < 0)
							err_sys("[ERROR]: child: recv error...");
						if(strcpy(fpname, mesg) == NULL)
							err_sys("[ERROR]: child: strcpy error...");
						printf("[INFO]: Client requests the document: %s\n", fpname);

						fn = fopen(fpname, "rb+");
						if(fn == NULL){
							printf("[WARNING]: file not exist...\n\n");
							snprintf(mesg, sizeof(mesg), "NO_FILE");
							if(send(connfd, mesg, sizeof(mesg), 0) < 0){
								err_sys("[ERROR]child: send error...");
							}
							err_quit("[ERROR]: %s\n",mesg);
						}
						else{
							printf("[INFO]: File exists...\n\n");
							snprintf(mesg, sizeof(mesg), "YES_FILE");
							if(send(connfd, mesg, sizeof(mesg), 0) < 0)
								err_sys("[ERROR]: child: send error...");
							if(fseek(fn, 0, SEEK_SET) != 0)
								err_sys("[ERROR]: child: fseek error...");

							//initiation
							cwin = 1;
							ssthresh = MAX_CWIN;
							seq_num = 0;
							fin = 0;
							rttinit = 0;
							rs_sqn = 0;
							is_timeout = 0;
							is_noack = 0;
							is_dup = 0;
							is_eof = 0;

							//first time we're called
							if (rttinit == 0){
								rtt_init(&rttinfo);
								rttinit = 1;
								rtt_d_flag = 1;
							}

							signal(SIGALRM, sig_alrm);

							pos = 0;//pos point to the next datagram need to  send
							last_pos = 0;//record the last RTT datagram positions

							//transmit file
							while(1){

								if(cwin > ssthresh){
									if(cwin < MAX_CWIN){
										cwin++;
										printf("[INFO]: Conjection window reaches ssthresh, turn into linear increasing\n");
									}else{
										printf("[INFO]: Conjection window_size reaches max: %d\n", ssthresh);
									}
								}

								//initialize for this term of packet
								rtt_newpack(&rttinfo);
								last_pos = pos;
								//reading the file
								for(i = 0; i < cwin; i++){
									if(!feof(fn)){
										if((nread = fread(outbuf[i], DGRAM_LEN, 1, fn)) < 0)
											err_sys("[ERROR]: child: fread error...");
									}else{
										is_eof = 1;
										break;
									}
								}


								//judge the send buffer whether is full
								if(i == MAX_CWIN){
									printf("[INFO]: Send buffer is full.\n");
								}

								//record the datagram num
								dg_num = i;

								//sending the data
								for(i = 0; i < dg_num; i++){
									if(is_eof){
										if(i == dg_num-1){
											fin = 1;
										}
									}
									leng = sizeof(outbuf[i]);
									if((n = serv_send(connfd, seq_num, fin, &sendhdr[i], outbuf[i], leng)) < 0){
										err_sys("[ERROR]: child: serv_send error...");
									}else{
										seq_num++;
										printf("[INFO]: Send SQN[%d]\n", sendhdr[i].seq);
									}
								}

								if (sigsetjmp(jmpbuf, 1) != 0) {
									is_timeout = 1;

									if(i == 0){
										is_noack = 1;
									}

									//get the ack of last receive datagram
									leng = sizeof(outbuf[pos-last_pos]);
									if((n = serv_send(connfd, pos, fin, &sendhdr[pos-last_pos], outbuf[pos-last_pos], leng)) < 0){
										err_sys("[ERROR]: child: serv_send error...");
									}else{
										printf("[INFO]: Resend SQN[%d]\n", sendhdr[pos-last_pos].seq);
									}

									//judge the resend packet whether the same as the last one
									if(pos > rs_sqn){
										rtt_newpack(&rttinfo);
									}
									rs_sqn = pos;

									if (rtt_timeout(&rttinfo) < 0) {
										rttinit = 0;	/* reinit in case we're called again */
										errno = ETIMEDOUT;
										err_quit("[ERROR]: No response from the client, giving up.");
									}
								}

								//calc timeout value & start timer
								alarm(rtt_start(&rttinfo)/1000);
								rtt_debug(&rttinfo);

								i = 0;
								//receiving the ack
								do{
									if((n = serv_recv(connfd, &recvhdr[i], NULL, 0)) < 0){
										err_sys("[ERROR]: child: serv_send error...");
									}else{
										printf("[INFO]: received ack[%d]\n", recvhdr[i].seq);
									}
									pos = recvhdr[i].seq;
									i++;
									//block in recv until the ack equals the send seq plus 1
								}while(n < sizeof(struct hdr) || recvhdr[i-1].seq != (sendhdr[dg_num-1].seq + 1));

								alarm(0);
								rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr[i].ts);

								//change the cwin size
								if(is_timeout){
									ssthresh = cwin >> 1;
									printf("[WARNING]: Time out! ssthresh set to the half of conjection window: %d\n", ssthresh);
									is_dup = 1;
									if(is_noack){
										is_timeout = 0;
										is_noack = 0;
										cwin = 1;
										printf("[WARNING]: Receive none ack, conjection window set to 1.\n");
									}else{
										is_timeout = 0;
										if(cwin != 1){
											cwin = cwin >> 1;
										}
									}
								}else if(cwin < MAX_CWIN){
									cwin = cwin << 1;
									if(is_dup){
										cwin = ssthresh;
										is_dup = 0;
									}
								}

								//exit the file transmit
								if(feof(fn)){
									break;
								}
							}

							printf("\n[INFO]: Finish %s transmitting!\n\n", fpname);
							fclose(fn);
							exit(0);
							//inform client the file finish sending
							//snprintf(mesg, sizeof(mesg), "FIN");
							//if(send(connfd, mesg, sizeof(mesg), 0) < 0)
							//	err_sys("[ERROR]: child: send error...");
						}
					}
					exit(0); //exit the child process
				}
			}
		}
	}
	exit(0);
}


void sig_chld(int signo){
	pid_t				pid;
	int 				stat;

	while((pid = waitpid(-1, &stat, WNOHANG)) > 0)
		printf("***child %d of server terminated!***\n\n\n", pid);
	return;
}

void sig_alrm(int signo){
	siglongjmp(jmpbuf, 1);
}

void prifsif(struct servIF *sif, int snum){
	int 				i;
	printf("\n----- Local interface Info. -----\n");
	for(i = 0; i < snum; i++){
		printf("* Interface %d:\n", i+1);
		printf("\tThe IP address:\t\t%s;\n", sif[i].sif_bound);
		printf("\tThe network mask:\t%s;\n", sif[i].sif_ntm);
		printf("\tThe subnet address:\t%s;\n", sif[i].sif_snm);
	}
	printf("---------------------------------\n\n");
	return;
}

char* subaddr(char *ipaddr, char *ntmaddr){
	char 				*snaddr;
	struct 				in_addr inaddr;

	inaddr.s_addr= inet_addr(ipaddr) & inet_addr(ntmaddr);
	snaddr = inet_ntoa(inaddr);
	return snaddr;
}

void hs_pnum_s(int sockfd, int connfd, struct sockaddr_in cliaddr, struct sockaddr_in servaddr){
	int					n, i;
	int					pnum;
	char				mesg[DGRAM_LEN], *ack;
	socklen_t			len;
	fd_set 			rset;
	struct timeval	tm;

	//send the port num
	len = sizeof(cliaddr);
	pnum = ntohs(servaddr.sin_port);
	snprintf(mesg, sizeof(mesg), "%d", pnum);

	i = 0;

resend:	if(sendto(sockfd, mesg, sizeof(mesg), 0, (SA *) &cliaddr, len) < 0){
		err_sys("[ERROR]: hs_pnum_s: sendto error...");
	}else{
		printf("[INFO]: Port number %s is sent to client...\n", mesg);

		tm.tv_sec = WAIT_TIME;
		tm.tv_usec = 0;
		FD_ZERO(&rset);
		FD_SET(connfd, &rset);
		if((n = select(connfd+1, &rset, NULL, NULL, &tm)) < 0){
			if(errno == EINTR){
				printf("[WARNING]: Ignored EINTR signal");
			} else {
				err_sys("[ERROR]: Select function error");
			}
		}
		if(FD_ISSET(connfd, &rset)){
			//receive the ack
			if((n = recvfrom(connfd, mesg, DGRAM_LEN, 0, (SA *) &cliaddr, &len)) < 0)
				err_sys("[ERROR]: hs_pnum_s: recvfrom error...");
			mesg[n] = 0; //null terminate
			ack = mesg;
			if(!strcmp(ack, "ACK_PNUM")){
				printf("[INFO]: Receive the ACK from client: [%s]\n", mesg);
				//close the inherited socket
				close(sockfd);
			}
		}else if (n == 0){
			if(i > 12){
				errno = ETIMEDOUT;
				err_quit("[ERROR]: No response from the client, giving up.");
			}else{
				printf("[WARNING]: Time out!\nResend the port number...\n");
				i++;
				goto resend;
			}
		}

		printf("---------------------------------\n");
	}
}

struct server_in get_input_file(struct server_in input_file){
	FILE *	file = fopen(INPUT_FILE_NAME, "r");
	char 	temp[DGRAM_LEN];

	if (file == NULL){
		err_sys("[ERROR]: Read input file <server.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.port_num = atoi(temp);
	} else{
		err_sys("[ERROR]: Read port number in file <server.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.window_size = atoi(temp);
	} else{
		err_sys("[ERROR]: Read sliding-window size in file <server.in> error, program terminated.");
	}

	return input_file;
}

