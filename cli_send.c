#include	"unprtt.h"
#include	<setjmp.h>

#define	RTT_DEBUG

static struct msghdr	msgsend;	/* assumed init to 0 */
static struct hdr {
  	uint32_t	seq;	/* sequence # */
  	uint32_t	ts;		/* timestamp when sent */
  	uint32_t	fin;	/* mark when file eof */
} sendhdr;

ssize_t cli_send(int fd, uint32_t seq_num, uint32_t timestamp, void *outbuff, size_t outbytes)
{
	ssize_t			n;
	struct iovec	iovsend[2];

	sendhdr.seq = seq_num;
	sendhdr.ts = timestamp;
	sendhdr.fin = 0;

	msgsend.msg_iov = iovsend;
	msgsend.msg_iovlen = 2;

	iovsend[0].iov_base = &sendhdr;
	iovsend[0].iov_len = sizeof(struct hdr);
	iovsend[1].iov_base = outbuff;
	iovsend[1].iov_len = outbytes;

	if (sendmsg(fd, &msgsend, 0)<0){
		err_sys("[ERROR:] Send packet error");
		return(-1);
	}

	return(0);
}