#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <string.h>
#include <openssl/md5.h>

#include <libnetfilter_queue/libnetfilter_queue.h>

#define IPTYPE 8
#define TCPTYPE 6

struct packet_info {
    u_int32_t id;
    u_int8_t flag;
};

u_int8_t host_dict[1000005][33];
u_int8_t *method_dict[6] = {"GET", "POST", "HEAD", "PUT", "DELETE", "OPTIONS"};
struct packet_info *pi;

void read_host() {
	FILE *f;
	int i = 0;
	int len;

	f = fopen("top-1m-hash-sort.csv", "r");

	while(fgets(host_dict[i], 34, f) != NULL) {
		len = strlen(host_dict[i]);
		host_dict[i][len-1] = 0;
		i++;
	}

	fclose(f);
}

u_int8_t *md5_hash(const char *str, int length) {
	MD5_CTX c;
	u_int8_t digest[16];
	u_int8_t *str_hash = (char *)malloc(33);
	int i;

	MD5_Init(&c);
	MD5_Update(&c, str, length);
	MD5_Final(digest, &c);
	
	for(i=0;i<16;i++)
		snprintf(&(str_hash[i*2]), 32, "%02x", (unsigned int)digest[i]);

	return str_hash;
}

int _strcmp(const void *a, const void *b) {
	return strcmp((char *)a, (char *)b);
}

u_int32_t nl_offset(u_int8_t *buf, u_int32_t size) {
	int i;
	for(i=0;i<size;i++) {
		if(buf[i] == '\x0a')
			break;
	}
	return (i == size) ? -1 : i;
}

struct packet_info *print_pkt (struct nfq_data *tb)
{
	struct nfqnl_msg_packet_hdr *ph;
	struct ip *iptr;
	struct tcphdr *tptr;
	char *dptr;
	u_int8_t *data;
	u_int8_t *method;
	u_int8_t *host;
	u_int8_t *host_hash;
	u_int32_t ret;
	u_int32_t dptr_len;
	u_int32_t method_len;
	u_int32_t host_len;
	u_int32_t i;

	ph = nfq_get_msg_packet_hdr(tb);
	if (ph) 
		pi->id = ntohl(ph->packet_id);
	pi->flag = 0;

	ret = nfq_get_payload(tb, &data);
	
	iptr = (struct ip *) data;
	if(iptr->ip_p == TCPTYPE) {
		tptr = (struct tcphdr *) (data + (iptr->ip_hl * 4));
		dptr = data + (iptr->ip_hl * 4) + (tptr->th_off * 4);
		dptr_len = ret - (iptr->ip_hl * 4) + (tptr->th_off * 4);

		if(nl_offset(dptr, dptr_len) != -1) {
			method_len = nl_offset(dptr, dptr_len);
			method = (u_int8_t *)malloc(method_len);
			memcpy(method, dptr, method_len);

			for(i=0;i<6;i++) {
				if(strstr(method, method_dict[i]) != NULL) {
					host_len = nl_offset(dptr + method_len + 1, dptr_len - method_len) - 7;
					host = (u_int8_t *)malloc(host_len);
					memcpy(host, dptr + method_len + 7, host_len);
					host[host_len] = 0;
					host_hash = md5_hash(host, host_len);
					if(bsearch(host_hash, host_dict, 1000000, 33, _strcmp) != NULL)
						pi->flag = 1;
					printf("[+] Method    : %s\n", method_dict[i]);
					printf("[+] Host      : %s\n", host);
					printf("[+] Host_hash : %s\n", host_hash);
					free(host);
					free(host_hash);
					break;
				}
			}
			free(method);
		}
	}
	
	return pi;
}

static int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
	      struct nfq_data *nfa, void *data)
{
	struct packet_info *pi = print_pkt(nfa);
	
	if(pi->flag == 0)
		return nfq_set_verdict(qh, pi->id, NF_ACCEPT, 0, NULL);
	else {
		printf("[+] DROP packet id %d\n", pi->id);
		return nfq_set_verdict(qh, pi->id, NF_DROP, 0, NULL);
	}
}

int main(int argc, char **argv) {
    struct nfq_handle *h;
    struct nfq_q_handle *qh;
    struct nfnl_handle *nh;
    int fd;
    int rv;
    char buf[4096] __attribute__ ((aligned));

	read_host();
    printf("[+] opening library handle\n");
    h = nfq_open();
    if (!h) {
        fprintf(stderr, "[-] error during nfq_open()\n");
        exit(1);
    }

    printf("[+] unbinding existing nf_queue handler for AF_INET (if any)\n");
    if (nfq_unbind_pf(h, AF_INET) < 0) {
        fprintf(stderr, "[-] error during nfq_unbind_pf()\n");
        exit(1);
    }

    printf("[+] binding nfnetlink_queue as nf_queue handler for AF_INET\n");
    if (nfq_bind_pf(h, AF_INET) < 0) {
        fprintf(stderr, "[-] error during nfq_bind_pf()\n");
        exit(1);
    }

    printf("[+] binding this socket to queue '0'\n");
    qh = nfq_create_queue(h,  0, &cb, NULL);
    if (!qh) {
        fprintf(stderr, "[-] error during nfq_create_queue()\n");
        exit(1);
    }

    printf("[+] setting copy_packet mode\n");
    if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
        fprintf(stderr, "[-] can't set packet_copy mode\n");
        exit(1);
    }

    fd = nfq_fd(h);
    pi = (struct packet_info *)malloc(sizeof(struct packet_info));

    for (;;) {
        if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0) {
            nfq_handle_packet(h, buf, rv);
            continue;
        }
        if (rv < 0 && errno == ENOBUFS) {
            printf("[-] losing packets!\n");
            continue;
        }
        perror("[-] recv failed");
        break;
    }

    free(pi);

    printf("[+] unbinding from queue 0\n");
    nfq_destroy_queue(qh);

#ifdef INSANE
    printf("[+] unbinding from AF_INET\n");
    nfq_unbind_pf(h, AF_INET);
#endif

    printf("[+] closing library handle\n");
    nfq_close(h);

    exit(0);
}
