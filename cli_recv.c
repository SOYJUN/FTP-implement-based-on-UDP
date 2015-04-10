/* include dgsendrecv1 */
#include    "unprtt.h"
#include    <setjmp.h>

#define CLI_TIMEOUT 8000
#define RTT_DEBUG

static int  rttinit = 0;
static struct msghdr    msgrecv;   /* assumed init to 0 */
static struct hdr {
    uint32_t  seq;    /* sequence # */
    uint32_t  ts;     /* timestamp when sent */
    uint32_t  fin;    /* mark when file eof */
} recvhdr;

static void sig_alrm(int signo);
static sigjmp_buf   jmpbuf;

struct hdr cli_recv(int fd, void *inbuff, size_t inbytes)
{
    ssize_t         n;
    struct iovec    iovrecv[2];
    struct rtt_info   rttinfo;


    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = &recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = inbuff;
    iovrecv[1].iov_len = inbytes;

    rttinfo.rtt_rto = CLI_TIMEOUT; //8 sec time out

    if (rttinit == 0) {
        rtt_init(&rttinfo);     /* first time we're called */
        rttinit = 1;
        rtt_d_flag = 1;
    }

    Signal(SIGALRM, sig_alrm);
    rtt_newpack(&rttinfo);      /* initialize for this packet */

    alarm(rtt_start(&rttinfo)/1000); /* calc timeout value & start timer */

//#ifdef  RTT_DEBUG
    rtt_debug(&rttinfo);
//#endif

    if (sigsetjmp(jmpbuf, 1) != 0) {
            err_msg("cli_recv: no response from server, giving up");
            rttinit = 0;    /* reinit in case we're called again */
            errno = ETIMEDOUT;
            err_sys("[ERROR]: Timeout");
            return recvhdr;
    }

    n = Recvmsg(fd, &msgrecv, 0);
    if (n < sizeof(struct hdr)){
        err_sys("[ERROR]: Received packet incomplete");
    }

    printf("Length: %d\n", (int)n);
    //printf("BUFFER: %s\n", (char *)iovrecv[1].iov_base);
    printf("SEQ NUM: %u\n", recvhdr.seq);
    printf("IS FIN: %u\n", recvhdr.fin);
    alarm(0);           /* stop SIGALRM timer */
    return(recvhdr); /* return size of received datagram */
}

static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}

