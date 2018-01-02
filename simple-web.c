/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* simple-web.c: Simple WEB Server using DPDK. */

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>

#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static const struct rte_eth_conf port_conf_default = {
	.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};

typedef unsigned long int uint32;
typedef unsigned short int uint16;

struct __attribute__((packed)) arp_header
{
	unsigned short arp_hd;
	unsigned short arp_pr;
	unsigned char arp_hdl;
	unsigned char arp_prl;
	unsigned short arp_op;
	unsigned char arp_sha[6];
	unsigned char arp_spa[4];
	unsigned char arp_dha[6];
	unsigned char arp_dpa[4];
};

struct ether_addr my_eth_addr;	// My ethernet address
uint32 my_ip;  			// My IP Address in network order
uint16 tcp_port; 		// listen tcp port in network order


char * INET_NTOA(uint32 ip);
void swap_bytes(unsigned char *a, unsigned char *b, int len);
void dump_packet(unsigned char *buf, int len);
void dump_arp_packet(struct ethhdr *eh);
void process_arp(struct rte_mbuf *mbuf);

char * INET_NTOA(uint32 ip)	// ip is network order
{
	static char buf[100];
	sprintf(buf,"%d.%d.%d.%d",
		(int)(ip&0xff), (int)((ip>>8)&0xff), (int)((ip>>16)&0xff), (int)((ip>>24)&0xff));
	return buf;
}

void swap_bytes(unsigned char *a, unsigned char *b, int len)
{
	unsigned char t;
	int i;
	if (len <= 0)
		return;
	for (i = 0; i < len; i++) {
		t = *(a + i);
		*(a + i) = *(b + i);
		*(b + i) = t;
	}
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;

	if (port >= rte_eth_dev_count())
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct ether_addr addr;
	rte_eth_macaddr_get(port, &addr);
	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	my_eth_addr = addr;
	/* Enable RX in promiscuous mode for the Ethernet device. */
	// rte_eth_promiscuous_enable(port);

	return 0;
}

void dump_packet(unsigned char *buf, int len)
{
	printf("packet buf=%p len=%d\n",buf,len);
	int i,j;
	unsigned char c;
	for(i=0;i<len;i++) {
		printf("%02X",buf[i]);
		if(i%16 == 7) 
			printf("  ");
		if((i%16)==15 || (i==len-1)) {
			if(i%16 < 7) printf("  ");
			for(j=0;j<15-(i%16);j++) printf("  ");
			printf(" | ");
			for(j=(i-(i%16));j<=i;j++) {
				c=buf[j];
				if((c>31) &&(c<127)) 
					printf("%c",c);
				else
					printf(".");
			}
			printf("\n");
		}
	}
}

void dump_arp_packet(struct ethhdr *eh)
{
	struct arp_header *ah;
	ah = (struct arp_header*) ((unsigned char *)eh + 14);
	printf("+++++++++++++++++++++++++++++++++++++++\n" );
	printf("ARP PACKET: %p \n",eh);
	printf("ETHER DST MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
		eh->h_dest[0], eh->h_dest[1], eh->h_dest[2], eh->h_dest[3],
		eh->h_dest[4], eh->h_dest[5]);
	printf("ETHER SRC MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
		eh->h_source[0], eh->h_source[1], eh->h_source[2], eh->h_source[3], eh->h_source[4],
		eh->h_source[5]);
	printf("H/D TYPE : %x PROTO TYPE : %x \n",ah->arp_hd,ah->arp_pr);
	printf("H/D leng : %x PROTO leng : %x \n",ah->arp_hdl,ah->arp_prl);
	printf("OPERATION : %x \n", ah->arp_op);
	printf("SENDER MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
		ah->arp_sha[0], ah->arp_sha[1], ah->arp_sha[2], ah->arp_sha[3],
		ah->arp_sha[4], ah->arp_sha[5]);
	printf("SENDER IP address: %02d:%02d:%02d:%02d\n",
		ah->arp_spa[0], ah->arp_spa[1], ah->arp_spa[2], ah->arp_spa[3]);
	printf("TARGET MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
		ah->arp_dha[0], ah->arp_dha[1], ah->arp_dha[2], ah->arp_dha[3],
		ah->arp_dha[4], ah->arp_dha[5]);
	printf("TARGET IP address: %02d:%02d:%02d:%02d\n",
		ah->arp_dpa[0], ah->arp_dpa[1], ah->arp_dpa[2], ah->arp_dpa[3]);
}

#define DEBUGARP

void process_arp(struct rte_mbuf *mbuf)
{
	struct ethhdr *eh = rte_pktmbuf_mtod(mbuf, struct ethhdr*);
	struct arp_header *ah;
	ah = (struct arp_header*) ((unsigned char *)eh + 14);
#ifdef DEBUGARP
	dump_arp_packet(eh);
#endif
	if(htons(ah->arp_op) != 0x0001) { // ARP request
		rte_pktmbuf_free(mbuf);
		return;
	}
	if(memcmp((unsigned char*)&my_ip, (unsigned char*)ah->arp_dpa, 4)==0) {
#ifdef DEBUGARP
		printf("Asking me....\n");
#endif
		memcpy((unsigned char*)eh->h_dest, (unsigned char*)eh->h_source, 6);
		memcpy((unsigned char*)eh->h_source, (unsigned char*)&my_eth_addr, 6);
		ah->arp_op=htons(0x2);
		memcpy((unsigned char*)ah->arp_dha, (unsigned char*)ah->arp_sha, 6);
		memcpy((unsigned char*)ah->arp_dpa, (unsigned char*)ah->arp_spa, 4);
		memcpy((unsigned char*)ah->arp_sha, (unsigned char*)&my_eth_addr, 6);
		memcpy((unsigned char*)ah->arp_spa, (unsigned char*)&my_ip, 4);
#ifdef DEBUGARP
		printf("I will reply following \n");
		dump_arp_packet(eh);
#endif
		if(likely(1 == rte_eth_tx_burst(0, 0, &mbuf, 1)))
			return;
	}
	rte_pktmbuf_free(mbuf);
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static __attribute__((noreturn)) void
lcore_main(void)
{
	const uint16_t nb_ports = rte_eth_dev_count();
	uint16_t port;

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	for (port = 0; port < nb_ports; port++)
		if (rte_eth_dev_socket_id(port) > 0 &&
				rte_eth_dev_socket_id(port) !=
						(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);

	printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n",
			rte_lcore_id());

	/* Run until the application is quit or killed. */
	for (;;) {
		/* Get burst of RX packets, from first port of pair. */
		int port = 0;
		int i;
		struct rte_mbuf *bufs[BURST_SIZE];
		const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
			bufs, BURST_SIZE);
		if (unlikely(nb_rx == 0))
			continue;
		printf("got %d packets\n",nb_rx);
		for(i=0;i<nb_rx;i++) {
			dump_packet(rte_pktmbuf_mtod(bufs[i], unsigned char*), rte_pktmbuf_pkt_len(bufs[i]));
			struct ethhdr *eh = rte_pktmbuf_mtod(bufs[i], struct ethhdr*);
			if(htons(eh->h_proto)== 0x806){  // ARP protocol
				process_arp(bufs[i]);
			} else
				rte_pktmbuf_free(bufs[i]);
		}

#if 0
		/* Send burst of TX packets, to second port of pair. */
		const uint16_t nb_tx = rte_eth_tx_burst(port ^ 1, 0,
			bufs, nb_rx);

		/* Free any unsent packets. */
		if (unlikely(nb_tx < nb_rx)) {
			uint16_t buf;
			for (buf = nb_tx; buf < nb_rx; buf++)
				rte_pktmbuf_free(bufs[buf]);
		}
#endif
	}
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	if(argc!=3)
		rte_exit(EXIT_FAILURE, "You need tell me my IP and port\n");

	my_ip = inet_addr(argv[1]);

	tcp_port = htons(atoi(argv[2]));

	printf("My IP is: %s, port is %d\n", INET_NTOA(my_ip), ntohs(tcp_port));

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count();
	if (nb_ports !=  1)
		rte_exit(EXIT_FAILURE, "Error: number of ports must be ene\n");

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize port. */
	if (port_init(0, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
					0);
	printf("My ether addr is: %02X:%02X:%02X:%02X:%02X:%02X",
			my_eth_addr.addr_bytes[0], my_eth_addr.addr_bytes[1],
			my_eth_addr.addr_bytes[2], my_eth_addr.addr_bytes[3],
			my_eth_addr.addr_bytes[4], my_eth_addr.addr_bytes[5]);


	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call lcore_main on the master core only. */
	lcore_main();

	return 0;
}
