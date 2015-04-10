#include	"unpifiplus.h"
#include	"math.h"
#include    "unprtt.h"

#define IF_NUM 10 						// The maximum client interface number.
#define CONN_TIME_OUT 200				//
#define MAX_NAME_LEN 255
#define WAIT_TIME 2
#define INPUT_FILE_NAME "client.in"
#define DGRAM_LEN 512
#define RESIVE_BUFF_SIZE 512

//
struct client_in{
    char            server_address[INET_ADDRSTRLEN];
    int             port_num;
    char            file_name[MAX_NAME_LEN];
    int             window_size;
    int 			seed_value;
    float           p_loss;
    int             receive_rate;
} input_file;


// Define the client interface struct.
struct cliIF{
	char			cif_bound[INET_ADDRSTRLEN]; // IP address/Interface to be bound
	char			cif_ntm[INET_ADDRSTRLEN];	// Client network mask
};

//
struct hdr {
  	uint32_t  	seq;    /* sequence # */
  	uint32_t  	ts;     /* timestamp when sent */
	uint32_t	fin;	/* mark when file eof */
};

FILE				*fn;
char 				recivebuff[RESIVE_BUFF_SIZE][DGRAM_LEN];
char 				buffflag[RESIVE_BUFF_SIZE];
uint32_t 			wroteposition;
pthread_t 			tid, main_tid;
pthread_mutex_t 	accessbuff_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t 		accessbuff_cond  = PTHREAD_COND_INITIALIZER;

// Print out the interface infos of client
void prifsif(struct cliIF *, int);
//calculate the subnet mask
char* subaddr(char *, char *);
//
void hs_pnum_c(int, struct sockaddr_in, struct sockaddr_in);
// Request the data by file name
void data_req(int);
//
struct client_in get_input_file();
// Get sleep time based on given seed
struct timespec sleeptime();
//
uint32_t get_last_ack_packet_bias(char *);
//
struct hdr cli_recv(int , void *, size_t);
//
ssize_t cli_send(int, uint32_t, uint32_t, void *, size_t);
//
void * consume_buff(void *);

int main(int argc, char **argv){

	int						n, sockfd, snum, i, pnum, issamehost;
	char					paddr[INET_ADDRSTRLEN], baddr[INET_ADDRSTRLEN], mesg[DGRAM_LEN], **pptr, str[INET_ADDRSTRLEN], *tempcharp, *ack;
	char					buf[20] = "INIT_CON";
	const int				on = 1;
	socklen_t				len, peerlen;
	struct sockaddr_in		*sa, cliaddr, servaddr, peeraddr;
	struct in_addr 			inaddr;
	struct cliIF			cif[IF_NUM];
	struct ifi_info			*ifi;
	struct  hostent 		*hptr;
	struct timeval			tm;
	fd_set					rset;
	char *					fpname;

	main_tid = pthread_self();

	// Get parameters in input file;
	input_file = get_input_file();
	srand(input_file.seed_value);

	fpname = input_file.file_name;
	//check if the file exits or not
	if((fn = fopen(fpname, "wb+")) == NULL){
		err_sys("[Error]: File build or open fail");
	}


	// Use inet_pton to judge whether this is a valid IPv4 address.
	if (inet_pton(AF_INET, input_file.server_address, &(inaddr.s_addr)) <= 0){
		err_quit("[ERROR]: Please use a valid IPv4 address is <client.in> file.");
	} else {
		// If the input is an ipv4 address
		in_addr_t addr = inet_addr(input_file.server_address);
		// If the IP address has no Domain record, give warning and process hostent struct.
		if((hptr = gethostbyaddr((const char *)&addr, 4, AF_INET)) == NULL){
			err_msg("[WARNING]: Cannot find hostname for %s:%s", input_file.server_address, hstrerror(h_errno));
		}
	}

	// Print Server's information (Hostname and IP addresses).
	printf("\n---------- Server Info. ---------\n");
	printf("* Hostname:\n\t%s;\n", hptr->h_name);
	printf("* Address:\n" );
    for (pptr = hptr->h_addr_list; *pptr != NULL; pptr++){
		printf("\t%s;\n", inet_ntop(AF_INET, *pptr, paddr, sizeof(paddr)));
	}
	printf("---------------------------------\n\n");

	// Build up the socket
	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		err_sys("[ERROR]: Build socket error, program terminated.");
	}

	// Set socket option SO_REUSEADDR.
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
		err_sys("[ERROR]: Setting SO_REUSEADDR socket option error.");
	}

	// Number of interface
	snum = 0;

	// Get all local interfaces infomation
	for(ifi = get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi -> ifi_next){

		sa = (struct sockaddr_in *) ifi -> ifi_addr;
		sa -> sin_family = AF_INET;
		sa -> sin_port = htons(0);

		// Store Interface address and mask info. to cif array.
		if(inet_ntop(AF_INET, &sa -> sin_addr, cif[snum].cif_bound, sizeof(cif[snum].cif_bound)) == NULL){
			err_sys("[ERROR]: Interface address not supported.");
		}
		if ((sa = (struct sockaddr_in *)ifi -> ifi_ntmaddr) != NULL){
			if(inet_ntop(AF_INET, &sa -> sin_addr, cif[snum].cif_ntm, sizeof(cif[snum].cif_ntm)) == NULL){
				err_sys("[ERROR]: Network mask not supported.");
			}
		}
		snum++;
	}

	// Print out the ip struct
	prifsif(cif, snum);

	//Bind the socket to the loopback address
	bzero(&cliaddr, sizeof(cliaddr));
	cliaddr.sin_family = AF_INET;
	cliaddr.sin_port = htons(0);
	if(snum == 1){
		if (inet_pton(AF_INET, "127.0.0.1", &cliaddr.sin_addr) <= 0){
			err_quit("[ERROR]: inet_pton function error for 127.0.0.1");
		}
	} else {
		if (inet_pton(AF_INET, cif[1].cif_bound, &cliaddr.sin_addr) <= 0){
			err_quit("[ERROR]: inet_pton function error for %s", cif[1].cif_bound);
		}
	}

	if(bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr)) < 0){
		err_sys("[ERROR]: Socket bind error");//error occur
	} else {
		len = sizeof(cliaddr);
		if(getsockname(sockfd, (SA *) &cliaddr, &len) < 0){
			err_sys("[ERROR]: getsockname function error");
		}
		printf("[INFO]: Successfully bind to %s\n", sock_ntop((SA *) &cliaddr, len));
	}
	//obtain the port number of client
	pnum = ntohs(cliaddr.sin_port);

	/* judge the typing IP whether the same as the client IP, if the same client sends to server SAME_HOST, then client socket
		bind to the new socket to 127.0.0.1, the same as server socket
	*/

	issamehost = 0;
	// Judege the typing IP
	for(i = 0; i < snum; i++){
		if(!strcmp(input_file.server_address, cif[i].cif_bound)){
			printf("[INFO]: The destination host is same host.\n");
			issamehost = 1;
			break;
		}
	}

	if(issamehost){
		// If
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(input_file.port_num);

		if(inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) <= 0){
			err_sys("[ERROR]: inet_pton function error for 127.0.0.1");
		}

		i = 0;

init1:	snprintf(mesg, sizeof(mesg), "INIT_127");
		if((n = sendto(sockfd, mesg, strlen(mesg), 0, (SA *) &servaddr, sizeof(servaddr))) < 0){
					err_sys("[ERROR]: Cannot send INIT_127 to server");
		} else {
			tm.tv_sec = WAIT_TIME;
			tm.tv_usec = 0;
			FD_ZERO(&rset);
			FD_SET(sockfd, &rset);
			if((n = select(sockfd+1, &rset, NULL, NULL, &tm)) < 0){
				if(errno == EINTR){
					printf("[WARNING]: Ignored EINTR signal");
				} else {
					err_sys("[ERROR]: Select function error");
				}
			}

			if(FD_ISSET(sockfd, &rset)){
				len = sizeof(servaddr);
				if((n = recvfrom(sockfd, mesg, DGRAM_LEN, 0, (SA *) &servaddr, &len)) < 0){
					err_sys("[ERROR]: recvfrom function error");
				}
				mesg[n] = '\0';
				ack = mesg;

				if(!strcmp(ack, "SAME_HOST")){
					printf("[INFO]: Received the ACK from server: %s\n", mesg);
					close(sockfd);

					// rebuild up the socket
					if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
						err_sys("[ERROR]: Build socket error, program terminated.");
					} else {
						printf("[INFO]: Socket successfully rebuilt\n");
					}

					// reset socket option SO_REUSEADDR.
					if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
						err_sys("[Error]: Setting SO_REUSEADDR socket option error.");
					}

					//rebind the socket to the loopback address
					bzero(&cliaddr, sizeof(cliaddr));
					cliaddr.sin_family = AF_INET;
					cliaddr.sin_port = htons(pnum);
					if (inet_pton(AF_INET, "127.0.0.1", &cliaddr.sin_addr) <= 0)
						err_quit("[ERROR]: main: inet_pton error for 127.0.0.1");
					if(bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr)) < 0)
						err_sys("[ERROR]: bind error...");
					else{
						len = sizeof(cliaddr);
						if(getsockname(sockfd, (SA *) &cliaddr, &len) < 0)
							err_sys("[ERROR]: getsockname error...");
						printf("[INFO]: Rebind to %s: success\n", sock_ntop((SA *) &cliaddr, sizeof(cliaddr)));
					}

					//reconnect the socket to the loopback addressm
					if(connect_timeo(sockfd, (SA *) &servaddr, sizeof(servaddr), CONN_TIME_OUT) < 0)
						err_sys("[ERROR]: connect error...");
					else{
						peerlen = sizeof(peeraddr);
						getpeername(sockfd, (SA *) &peeraddr, &peerlen);
						printf("[INFO]: Connects to %s: success\n", sock_ntop((SA *) &peeraddr, peerlen));
						printf("---------------------------------\n");
					}
				}

				else{
					printf("[ERROR]: UNKOWN ACK: %s\n", mesg);
					exit(0);
				}
			}
			else if(n == 0){
				if(i > 12){
					errno = ETIMEDOUT;
					err_quit("[ERROR]: No response from the server, giving up.");
				}else{
					printf("[INIT_127] time out!\nRetry the requset...\n");
					i++;
					goto init1;
				}
			}
		}
	}else{
		/*send INIT_255 to subnet broadcast address, if server receive and reply LOCAL_HOST,
		then client socket set SO_DONTROUTE socket option，so does the server
		*/

		close(sockfd);

		// rebuild up the socket
		if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
			err_sys("[ERROR]: Build socket error, program terminated.");
		else
			printf("[INFO]: Rebuild the socket...\n");

		// reset socket option SO_REUSEADDR.
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
			err_sys("[Error]: Setting SO_REUSEADDR socket option error.");

		//rebind the socket to the loopback address
		bzero(&cliaddr, sizeof(cliaddr));
		cliaddr.sin_family = AF_INET;
		cliaddr.sin_port = htons(pnum);
		if (inet_pton(AF_INET, cif[1].cif_bound, &cliaddr.sin_addr) <= 0)
			err_quit("[ERROR]: main: inet_pton error");
		if(bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr)) < 0)
			err_sys("[ERROR]: bind error...");

		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(input_file.port_num);
		if(inet_pton(AF_INET, input_file.server_address, &servaddr.sin_addr) <= 0)
			err_sys("[ERROR]: inet_pton error...");

		strcpy(mesg, strcat(buf, subaddr(cif[1].cif_bound, cif[1].cif_ntm)));
		//snprintf(mesg, sizeof(mesg), strcat(buf, subaddr(cif[1].cif_bound, cif[1].cif_ntm)));

		i = 0;

init2:	if(sendto(sockfd, mesg, strlen(mesg), 0, (SA *) &servaddr, sizeof(servaddr)) < 0)
			err_sys("[INIT_CON]: sendto error...");
		else{
			printf("%s is send to server.\n", mesg);

			tm.tv_sec = WAIT_TIME;
			tm.tv_usec = 0;
			FD_ZERO(&rset);
			FD_SET(sockfd, &rset);
			if((n = select(sockfd+1, &rset, NULL, NULL, &tm)) < 0){
				if(errno == EINTR)
					printf("[ERROR]: EINTR...");
				else
					err_sys("[ERROR]: select error...");
			}

			if(FD_ISSET(sockfd, &rset)){
				len = sizeof(servaddr);
				if((n = recvfrom(sockfd, mesg, DGRAM_LEN, 0, (SA *) &servaddr, &len)) < 0)
					err_sys("[ERROR]:hs_pnum_s: recvfrom error...");
				mesg[n] = 0;
				ack = mesg;
				if(!strcmp(ack, "LOCAL_HOST")){
					printf("[INFO]: Receive the ACK from server: [%s]\n", mesg);

					close(sockfd);

					// rebuild up the socket
					if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
						err_sys("[ERROR]: Build socket error, program terminated.");
					else
						printf("[INFO]: Rebuild the socket...\n");

					// reset socket option SO_REUSEADDR.
					if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
						err_sys("[Error]: Setting SO_REUSEADDR socket option error.");

					//set up the socket option SO_DONTROUTE
					if(setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on)) < 0)
						err_sys("[Error]: Setting SO_DONTROUTE socket option error.");
					else
						printf("[INFO]: The socket option SO_DONTROUTE is set up...\n");

					bzero(&cliaddr, sizeof(cliaddr));
					cliaddr.sin_family = AF_INET;
					cliaddr.sin_port = htons(pnum);
					if (inet_pton(AF_INET, cif[1].cif_bound, &cliaddr.sin_addr) <= 0)//default the first interface address
						err_sys("[ERROR]: main: inet_pton error for %s", cif[1].cif_bound);

					//bind the socket to the designated client IP address
					if(bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr)) < 0)
						err_sys("[ERROR]: bind error...");

					len = sizeof(cliaddr);
					if(getsockname(sockfd, (SA *) &cliaddr, &len) < 0)
						err_sys("[ERROR]: getsockname error...");
					printf("[INFO]: Rebind to %s: success\n", sock_ntop((SA *) &cliaddr, sizeof(cliaddr)));

					bzero(&servaddr, sizeof(servaddr));
					servaddr.sin_family = AF_INET;
					servaddr.sin_port = htons(input_file.port_num);
					if(inet_pton(AF_INET, input_file.server_address, &servaddr.sin_addr) <= 0)
						err_sys("[ERROR]: inet_pton error...");

					if(connect_timeo(sockfd, (SA *) &servaddr, sizeof(servaddr), CONN_TIME_OUT) < 0)
						err_sys("[ERROR]: connect error...");

					peerlen = sizeof(peeraddr);
					getpeername(sockfd, (SA *) &peeraddr, &peerlen);
					printf("Connets to %s: success\n", sock_ntop((SA *) &peeraddr, peerlen));
					printf("---------------------------------\n");
				}

				else if(!strcmp(ack, "COMM_HOST")){
					printf("Receive the ACK from server: [%s]\n", mesg);

					close(sockfd);

					// rebuild up the socket
					if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
						err_sys("[ERROR]: Build socket error, program terminated.");
					else
						printf("[INFO]: Rebuild the socket...\n");

					// reset socket option SO_REUSEADDR.
					if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
						err_sys("[ERROR]: Setting SO_REUSEADDR socket option error.");

					//common connection application
					bzero(&cliaddr, sizeof(cliaddr));
					cliaddr.sin_family = AF_INET;
					cliaddr.sin_port = htons(pnum);
					if (inet_pton(AF_INET, cif[1].cif_bound, &cliaddr.sin_addr) <= 0)//default the first interface address
					err_sys("[ERROR]: main: inet_pton error for %s", cif[1].cif_bound);

					//bind the socket to the designated client IP address
					if(bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr)) < 0)
						err_sys("[ERROR]: bind error...");

					len = sizeof(cliaddr);
					if(getsockname(sockfd, (SA *) &cliaddr, &len) < 0)
						err_sys("[ERROR]: getsockname error...");
					printf("[INFO]: Rebind to %s: success\n", sock_ntop((SA *) &cliaddr, sizeof(cliaddr)));

					if(connect_timeo(sockfd, (SA *) &servaddr, sizeof(servaddr), CONN_TIME_OUT) < 0)
						err_sys("[ERROR]: connect error...");

					peerlen = sizeof(peeraddr);
					getpeername(sockfd, (SA *) &peeraddr, &peerlen);
					printf("[INFO]: Connets to %s: success\n", sock_ntop((SA *) &peeraddr, peerlen));
					printf("---------------------------------\n");
				}

				else{
					printf("[ERROR]: UNKOWN ACK: %s\n", mesg);
					exit(0);
				}
			}
			else if(n == 0){
				if(i > 12){
					errno = ETIMEDOUT;
					err_quit("[ERROR]: No response from the server, giving up.");
				}else{
					printf("[INIT_CON] time out!\nRetry the requset...\n");
					i++;
					goto init2;
				}
			}
		}
	}

	//handshake: receive the new port number and reconnect to the child server
	hs_pnum_c(sockfd, cliaddr, servaddr);

	//ftp process
	data_req(sockfd);
	close(sockfd);
	pthread_join(tid, NULL);
	fclose(fn);
	exit(0);
}


void prifsif(struct cliIF *cif, int snum){
	int 			i;
	printf("\n----- Local interface Info. -----\n");
	for(i = 0; i < snum; i++){
		printf("* Interface %d:\n", i+1);
		printf("\tThe IP address:\t\t%s;\n", cif[i].cif_bound);
		printf("\tThe network mask:\t%s;\n", cif[i].cif_ntm);
	}
	printf("---------------------------------\n\n");
	return;
}

char* subaddr(char *ipaddr, char *ntmaddr){
	char 				*snaddr;
	struct in_addr 		inaddr;

	inaddr.s_addr= inet_addr(ipaddr) & inet_addr(ntmaddr);
	snaddr = inet_ntoa(inaddr);
	return snaddr;
}

void hs_pnum_c(int sockfd, struct sockaddr_in cliaddr, struct sockaddr_in servaddr){
	int				n, pnum;
	char 			mesg[DGRAM_LEN];
	socklen_t		len;

	//receive the port number
	len = sizeof(cliaddr);
	printf("[INFO]: waiting port number...\n");
	if((n = recv(sockfd, mesg, DGRAM_LEN, 0)) < 0)
		perror("hs_pnum_c: recv error...");
	else{
		mesg[n] = 0; //null terminate
		printf("[INFO]: Receive the new port number %s from server\n", mesg);
	}

	//reconnect
	pnum = atoi(mesg);
	servaddr.sin_port = htons(pnum);
	if(connect_timeo(sockfd, (SA *) &servaddr, sizeof(servaddr), CONN_TIME_OUT) < 0)
		err_sys("[ERROR]: connect error...");
	else
		printf("[INFO]: Reconnect to the new port: success\n");

	//reply the ack
	snprintf(mesg, sizeof(mesg), "ACK_PNUM");
	if(send(sockfd, mesg, sizeof(mesg), 0) < 0)
		err_sys("[ERROR]: hs_pnum_c: send error...");
	else
		printf("[INFO]: Reply the ACK to the server...\n");

	printf("---------------------------------\n");
}


void data_req(int sockfd){
	int				i, eofflag, n = 0;
	char			*choice, sendline[DGRAM_LEN], recvline[DGRAM_LEN], *ack;
	uint32_t 		lastackpacket;
	size_t 			dg_size = DGRAM_LEN;
	struct hdr 		recvhdr;
	fd_set 			rset;
	struct timeval	tm;
	char *			fpname = input_file.file_name;

	printf("[INFO]: The file request: %s\n", fpname);

	printf("---------------------------------\n");

	i = 0;

filereq:	if((n = send(sockfd, fpname, strlen(fpname)+1, 0)) < 0){
		err_sys("[ERROR]: data_req: send error...");
	} else {
		tm.tv_sec = WAIT_TIME;
		tm.tv_usec = 0;
		FD_ZERO(&rset);
		FD_SET(sockfd, &rset);
		if((n = select(sockfd+1, &rset, NULL, NULL, &tm)) < 0){
			if(errno == EINTR){
				printf("[WARNING]: Ignored EINTR signal");
			} else {
				err_sys("[ERROR]: Select function error");
			}
		}
		if(FD_ISSET(sockfd, &rset)){
			if((n = recv(sockfd, recvline, DGRAM_LEN, 0)) < 0){
				err_sys("[ERROR]: data_req: recv error...");
			} else {
				ack = recvline;
				if(!strcmp(ack, "NO_FILE")){
					printf("[Error]: cannot find the designated file...\n");
					exit(0);
				} else if (!strcmp(ack, "YES_FILE")) {
					printf("*The file exists in the server*\n");
				} else {
					err_quit("UNKOWN ACK");
				}
			}
		}else if (n == 0){
			if(i > 12){
				errno = ETIMEDOUT;
				err_quit("[ERROR]: No response from the server, giving up.");
			}else{
				printf("File requset time out!\nResend the requset...\n");
				i++;
				goto filereq;
			}
		}

		//receive the data
		printf("**Enter transmission mode**:\n");
		bzero(buffflag, RESIVE_BUFF_SIZE);
		lastackpacket = 0;
		wroteposition = 0;
		eofflag = 0;
		pthread_create(&tid, NULL, consume_buff, NULL);

		for (;;){
			recvhdr = cli_recv( sockfd, recivebuff[lastackpacket-wroteposition], dg_size);
			buffflag[(recvhdr.seq)-wroteposition] = 1;
			printf("[INFO]: Received data sequence:%d\n", recvhdr.seq);

			lastackpacket = get_last_ack_packet_bias(buffflag)+wroteposition;

			if (cli_send(sockfd, lastackpacket, recvhdr.ts, NULL, 0)<0){
				err_sys("[ERROR:] Send packet error");
			}

			if (recvhdr.fin==1){
				buffflag[(recvhdr.seq)-wroteposition] = 2;
				eofflag = 1;
				break;
			}
			pthread_cond_signal(&accessbuff_cond);
		}
		printf("**End transmission mode**:\n");
	}
}

struct client_in get_input_file(){
	FILE *	file = fopen(INPUT_FILE_NAME, "r");
	char 	temp[DGRAM_LEN];

	if (file == NULL){
		err_sys("[ERROR]: Read input file <client.in> error, program terminated.");
	}

	if (fgets ( temp, sizeof temp, file ) != NULL){
		temp[strlen(temp)-1] = '\0';
		strcpy(input_file.server_address, temp);
	} else{
		err_sys("[ERROR]: Read server address in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.port_num = atoi(temp);
	} else{
		err_sys("[ERROR]: Read port number in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		temp[strlen(temp)-1] = '\0';
		strcpy(input_file.file_name, temp);
	} else{
		err_sys("[ERROR]: Read file name in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.window_size = atoi(temp);
	} else{
		err_sys("[ERROR]: Read sliding-window size in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.seed_value = atoi(temp);
	} else{
		err_sys("[ERROR]: Read seed value in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.p_loss = atof(temp);
	} else{
		err_sys("[ERROR]: Read datagram loss probability in file <client.in> error, program terminated.");
	}

	if (fgets( temp, sizeof temp, file ) != NULL){
		input_file.receive_rate = atoi(temp);
	} else{
		err_sys("[ERROR]: Read receive rate parameter in file <client.in> error, program terminated.");
	}

	return input_file;
}

struct timespec sleeptime(){
	int mu = input_file.receive_rate;
	double random_value = (double)rand()/(double)RAND_MAX;
	struct timespec time_to_sleep;
	int stime;

	stime = (int) -1*mu*log(random_value);
	time_to_sleep.tv_sec = stime/1000;
	time_to_sleep.tv_nsec = stime%1000 * 1000000;

	return time_to_sleep;
}

uint32_t get_last_ack_packet_bias(char *flag){
	uint32_t 	lastackpacket;
	for (lastackpacket=0; lastackpacket<RESIVE_BUFF_SIZE; lastackpacket++){
		if (flag[lastackpacket]==0){
			break;
		}
	}
	return lastackpacket;
}

void * consume_buff(void *vptr){
	struct timespec 		sleep_time, temp_time;
	int 					i, consume_size;
	char 					c = 0;
	size_t 					pos = 0;
	for (;;){
		sleep_time = sleeptime();

		pthread_mutex_unlock(&accessbuff_mutex);
		nanosleep(&sleep_time, &temp_time);
		//printf("Sleep End\n");
		//pthread_cond_wait(&accessbuff_cond, &accessbuff_mutex);
		//printf("Wait End\n");
		pthread_mutex_lock(&accessbuff_mutex);
		consume_size = get_last_ack_packet_bias(buffflag);
		for (i=0; i<consume_size; i++){
			if (buffflag[i]==1){
				fwrite(recivebuff[i], 1, sizeof(recivebuff[i]), fn );
			}
			if (buffflag[i]==2){
				fwrite(recivebuff[i], 1, sizeof(recivebuff[i]), fn );
				return NULL;
			}

			if (i+consume_size<RESIVE_BUFF_SIZE){
				memcpy(recivebuff[i], recivebuff[i+consume_size], DGRAM_LEN);
				buffflag[i] = buffflag[i+consume_size];
			}
		}
		for (i=2*consume_size-1; i<RESIVE_BUFF_SIZE; i++){
			buffflag[i] = 0;
		}
		wroteposition += consume_size;
	}

}
