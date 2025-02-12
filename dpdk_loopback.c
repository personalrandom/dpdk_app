/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <time.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define SV_ETHERTYPE 0x88BA
#define GOOSE_ETHERTYPE 0x88B8
#define APPID_OFFSET 14

#define CYCLE_TIME_NS (300 * 1000)
#define NSEC_PER_SEC (1000 * 1000 * 1000)

static void norm_ts(struct timespec *tv)
{
	while (tv->tv_nsec >= NSEC_PER_SEC) {
		tv->tv_sec++;
		tv->tv_nsec -= NSEC_PER_SEC;
	}
}

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

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

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	if (retval != 0)
		return retval;

	return 0;
}
/* >8 End of main functional part of port initialization. */

/*
 * The loopback thread. It polls the ethernet port at a defined frequency, 
 	and sends back Goose and SV packets.
 */
static __rte_noreturn void loopback_main(void)
{
	uint16_t port;
	struct rte_ether_hdr *eth_hdr;
	uint16_t ethertype;
	int j;
	struct rte_ether_addr src_mac;
	struct rte_ether_addr dst_mac;
	struct timespec tv;
	uint8_t * appid;

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
		if (rte_eth_dev_socket_id(port) >= 0 &&
				rte_eth_dev_socket_id(port) !=
						(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);

	printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n",
			rte_lcore_id());

	/* Main work of application loop. 8< */
	clock_gettime(CLOCK_MONOTONIC, &tv);
	for (;;) {

		/* Get burst of RX packets */
		struct rte_mbuf *bufs[BURST_SIZE];
		const uint16_t nb_rx = rte_eth_rx_burst(0, 0,
				bufs, BURST_SIZE);

		if (nb_rx) {
			for (j = 0; j < nb_rx; j++) {
				struct rte_mbuf *m = bufs[j];

				eth_hdr = rte_pktmbuf_mtod(m,
						struct rte_ether_hdr *);
				ethertype = eth_hdr->ether_type;
				/* Loopback Goose and SV packets */
				if(rte_cpu_to_be_16(SV_ETHERTYPE) == ethertype || rte_cpu_to_be_16(GOOSE_ETHERTYPE) == ethertype)
				{
					/* Flip SRC and DST MAC */
					rte_ether_addr_copy(&eth_hdr->src_addr, &dst_mac);
					rte_ether_addr_copy(&eth_hdr->dst_addr, &src_mac);
					rte_ether_addr_copy(&dst_mac, &eth_hdr->dst_addr);
					rte_ether_addr_copy(&src_mac, &eth_hdr->src_addr);
					/* Set second APPID byte second as the first byte and set first byte as 0 */
					/* APPID first byte is a counter reserved for sender, APPID second byte is a counter reserved for receiver */
					appid = rte_pktmbuf_mtod_offset(m, uint8_t *, APPID_OFFSET);
					appid[1] = appid[0];
					appid[0] = 0; 
					/* Send back packet */
					const uint16_t nb_tx = rte_eth_tx_burst(0, 0,
							&m, 1);
					if (unlikely(!nb_tx)) {
						rte_pktmbuf_free(m);
					}
				}
				else
				{
					rte_pktmbuf_free(m);
				}
			}
		}
		/* wait for next cycle */
		tv.tv_nsec += CYCLE_TIME_NS;
		norm_ts(&tv);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tv, NULL);
	}
	/* >8 End of loop. */
}

/*
 * The main function, which does dpdk rte initializations.
 */
int main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;

	/* Initialization the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	/* Check that there is atleast a port to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports < 1)
		rte_exit(EXIT_FAILURE, "Error: no usable port found \n");

	/* Creates a new mempool in memory to hold the mbufs. */

	/* Allocates mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing first port. 8< */
	if (port_init(0, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
				0);
	/* >8 End of initializing first port. */

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call loopback main thread on the main core only. Called on single lcore. 8< */
	loopback_main();
	/* >8 End of called on single lcore. */

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
