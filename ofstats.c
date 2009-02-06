#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>	// for handy macro


#include "oftrace.h"
#include "utils.h"

typedef struct buffer_id 
{
	uint32_t sip;	// of the packet_in
	uint32_t dip;
	uint16_t sport;
	uint16_t dport;
	uint32_t b_id;
	struct timeval ts;
	struct buffer_id * next;
} buffer_id;

/************************
 * main()
 *
 */
int calc_stats(oftrace * oft, uint32_t ip, int port);

int main(int argc, char * argv[])
{
	char * filename = "openflow.trace";
	char * controller = "0.0.0.0";
	int port = OFP_TCP_PORT;
	uint32_t controller_ip;
	oftrace *oft;
	// FIXME: parse options from cmdline
	if(argc>1)
		filename=argv[1];
	if(argc>2)
		controller=argv[2];
	if(argc>3)
		port=atoi(argv[3]);
	if(!strcmp(controller,"0.0.0.0"))
		fprintf(stderr,"Reading from pcap file %s for any controller ip on port %d\n",
				filename,port);
	else
		fprintf(stderr,"Reading from pcap file %s for controller %s on port %d\n",
				filename,controller,port);
	inet_pton(AF_INET,controller,&controller_ip);	// FIXME: use getaddrinfo
	oft= oftrace_open(filename);
	if(!oft)
	{
		fprintf(stderr,"Problem openning %s; aborting....\n",filename);
		return 0;
	}
	return calc_stats(oft,controller_ip, port);
}
/************************************************************************
 * calc_stats:
 * 	match packet_in to packet_out or flow_mod statements that release the buffer
 */

int calc_stats(oftrace * oft, uint32_t ip, int port)
{
	const openflow_msg *m;
	char dst_ip[BUFLEN];
	char src_ip[BUFLEN];
	struct timeval diff;
	uint32_t id;
	buffer_id * b, *b_list, *b_prev;
	b_list = NULL;
	memset(&m,sizeof(m),0);	// zero msg contents
	// for each openflow msg
	while( (m = oftrace_next_msg(oft, ip, port)) != NULL)
	{
		switch(m->type)
		{
			case OFPT_PACKET_IN:
				// create a new buffer_id struct and track this buffer_id
				b = malloc_and_check(sizeof(buffer_id));
				b->sip = m->ip->saddr;
				b->dip = m->ip->daddr;
				b->sport = m->tcp->source;
				b->dport = m->tcp->dest;
				b->b_id = m->ptr.packet_in->buffer_id;
				b->ts.tv_sec = m->phdr.ts_sec;
				b->ts.tv_usec = m->phdr.ts_usec;
				b->next=b_list;
				b_list = b;
				break;
			case OFPT_PACKET_OUT:
			case OFPT_FLOW_MOD:
				inet_ntop(AF_INET,&m->ip->saddr,src_ip,BUFLEN);
				inet_ntop(AF_INET,&m->ip->daddr,dst_ip,BUFLEN);
				if(m->type == OFPT_PACKET_OUT)
					id = m->ptr.packet_out->buffer_id;
				else
					id = m->ptr.flow_mod->buffer_id;
				// now find this buffer_id in the list
				b_prev=NULL;
				b= b_list;
				while(b)
				{
					if(b->b_id == id)
						break;
					else
					{
						b_prev=b;
						b=b->next;
					}
				}
				if(!b)
				{
					if(id != -1)
						fprintf(stderr,"WEIRD: unmatched buffer_id %u in flow %s:%u -> %s:%u\n",
								id,
								src_ip, ntohs(m->tcp->source),
								dst_ip, ntohs(m->tcp->dest));
				}
				else	// found it
				{
					diff.tv_sec = m->phdr.ts_sec;
					diff.tv_usec = m->phdr.ts_usec;
					timersub(&diff,&b->ts,&diff);	// handy macro
					printf("%ld.%.6ld 	secs_to_resp buf_id=%d in flow %s:%u -> %s:%u - %s\n",
							diff.tv_sec, diff.tv_usec,
							id,
							src_ip, ntohs(m->tcp->source),
							dst_ip, ntohs(m->tcp->dest),
							m->type==OFPT_PACKET_OUT? "packet_out":"flow_mod");
					if(b_prev)
						b_prev=b->next;
					else
						b_list=b->next;
					free(b);
				}
				break;
		};
	}
	return 0;
}

