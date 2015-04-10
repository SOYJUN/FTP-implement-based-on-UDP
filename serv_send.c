#include	"unprtt.h"

static struct rtt_info   rttinfo;
static struct msghdr	msgsend;	/* assumed init to 0 */

static struct hdr {
  	uint32_t	seq;	/* sequence # */
  	uint32_t	ts;		/* timestamp when sent */
  	uint32_t	fin;	/* mark when file eof */
};

ssize_t serv_send(int fd, int seq_num, int fin, struct hdr *sendhdr, void *outbuff, size_t outbytes){
	ssize_t			n;
	struct iovec	iovsend[2];  //the argument set to 2 because 0 for header, 1 for data

	sendhdr->seq = seq_num;
	sendhdr->ts = rtt_ts(&rttinfo);    //store the time at begin sending data
	sendhdr->fin = fin;
	printf("FIN:%d\n", fin);
	msgsend.msg_name = NULL;
	msgsend.msg_namelen = 0;
	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = 2;
	iovsend[0].iov_base = sendhdr;
	iovsend[0].iov_len = sizeof(struct hdr);
	iovsend[1].iov_base = outbuff;
	iovsend[1].iov_len = outbytes;

	if((n = sendmsg(fd, &msgsend, 0)) < 0){
		return(-1);
	}else{
		printf("[INFO]: datagram [%d] sent...\n", (int)n);
	}

	return(n);	/* return send datagram size */
}