#include	"unprtt.h"

static struct msghdr	msgrecv;	/* assumed init to 0 */

static struct hdr {
    uint32_t	seq;	/* sequence # */
    uint32_t	ts;		/* timestamp when sent */
    uint32_t    fin;    /* mark when file eof */
};

ssize_t serv_recv(int fd, struct hdr *recvhdr, void *inbuff, size_t inbytes){
	ssize_t			n;
	struct iovec	iovrecv[2];  //the argument set to 2 because 0 for header, 1 for data

    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = inbuff;
    iovrecv[1].iov_len = inbytes;

    printf("SEQ%d\n", recvhdr->seq);
	if((n = recvmsg(fd, &msgrecv, 0)) < 0){
		return(-1);
	}else{
		printf("[INFO]: datagram [%d] received...\n", (int)n);
	}

	return(n);	/* return send datagram size */
}

