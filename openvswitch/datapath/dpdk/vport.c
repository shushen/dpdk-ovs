/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
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
 *
 */

#include <rte_ethdev.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>

#include "init.h"
#include "vport.h"
#include "stats.h"
#include "args.h"
#include "kni.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS        0
#define SOCKET0         0

#define MZ_PORT_INFO "MProc_port_info"
/* define common names for structures shared between server and client */
#define MP_CLIENT_RXQ_NAME "MProc_Client_%u_RX"
#define MP_CLIENT_TXQ_NAME "MProc_Client_%u_TX"
#define MP_PORT_TXQ_NAME "MProc_PORT_%u_TX"

/* Ethernet port TX/RX ring sizes */
#define RTE_MP_RX_DESC_DEFAULT    512
#define RTE_MP_TX_DESC_DEFAULT    512
/* Ring size for communication with clients */
#define CLIENT_QUEUE_RINGSIZE     4096

/*
 * This is the maximum number of digits that are required to represent
 * the largest possible unsigned int on a 64-bit machine. It will be used
 * to calculate the length of the strings above when %u is substituted.
 */
#define MAX_DIGITS_UNSIGNED_INT 20

/*
 * RX and TX Prefetch, Host, and Write-back threshold values should be
 * carefully set for optimal performance. Consult the network
 * controller's datasheet and supporting DPDK documentation for guidance
 * on how these parameters should be set.
 */
/* Default configuration for rx and tx thresholds etc. */
/*
 * These default values are optimized for use with the Intel(R) 82599 10 GbE
 * Controller and the DPDK ixgbe PMD. Consider using other values for other
 * network controllers and/or network drivers.
 */
#define MP_DEFAULT_PTHRESH 36
#define MP_DEFAULT_RX_HTHRESH 8
#define MP_DEFAULT_TX_HTHRESH 0
#define MP_DEFAULT_WTHRESH 0

static const struct rte_eth_rxconf rx_conf_default = {
		.rx_thresh = {
				.pthresh = MP_DEFAULT_PTHRESH,
				.hthresh = MP_DEFAULT_RX_HTHRESH,
				.wthresh = MP_DEFAULT_WTHRESH,
		},
};

static const struct rte_eth_txconf tx_conf_default = {
		.tx_thresh = {
				.pthresh = MP_DEFAULT_PTHRESH,
				.hthresh = MP_DEFAULT_TX_HTHRESH,
				.wthresh = MP_DEFAULT_WTHRESH,
		},
		.tx_free_thresh = 0, /* Use PMD default values */
		.tx_rs_thresh = 0, /* Use PMD default values */
};

/*
 * Define a client structure with all needed info.
 */
struct client {
	struct rte_ring *rx_q;
	struct rte_ring *tx_q;
	unsigned client_id;
};

static struct client *clients = NULL;

/* the port details */
struct port_info *ports = NULL;
struct port_queue *port_queues = NULL;

/*
 * Given the rx queue name template above, get the queue name
 */
static inline const char *
get_rx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_CLIENT_RXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_CLIENT_RXQ_NAME, id);
	return buffer;
}

static inline const char *
get_tx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_CLIENT_TXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_CLIENT_TXQ_NAME, id);
	return buffer;
}

static inline const char *
get_port_tx_queue_name(unsigned id)
{
	static char buffer[sizeof(MP_PORT_TXQ_NAME) + MAX_DIGITS_UNSIGNED_INT];

	rte_snprintf(buffer, sizeof(buffer), MP_PORT_TXQ_NAME, id);
	return buffer;
}

/**
 * Initialise an individual port:
 * - configure number of rx and tx rings
 * - set up each rx ring, to pull from the main mbuf pool
 * - set up each tx ring
 * - start the port and report its status to stdout
 */
static int
init_port(uint8_t port_num)
{
	/* for port configuration all features are off by default */
	const struct rte_eth_conf port_conf = {
		.rxmode = {
			.mq_mode = ETH_RSS
		}
	};
	const uint16_t rx_rings = 1, tx_rings = num_clients;
	const uint16_t rx_ring_size = RTE_MP_RX_DESC_DEFAULT;
	const uint16_t tx_ring_size = RTE_MP_TX_DESC_DEFAULT;
	struct rte_eth_link link = {0};
	uint16_t q = 0;
	int retval = 0;

	printf("Port %u init ... ", (unsigned)port_num);
	fflush(stdout);

	/* Standard DPDK port initialisation - config port, then set up
	 * rx and tx rings */
	if ((retval = rte_eth_dev_configure(port_num, rx_rings, tx_rings,
		&port_conf)) != 0)
		return retval;

	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port_num, q, rx_ring_size,
				SOCKET0, &rx_conf_default, pktmbuf_pool);
		if (retval < 0) return retval;
	}

	for ( q = 0; q < tx_rings; q ++ ) {
		retval = rte_eth_tx_queue_setup(port_num, q, tx_ring_size,
				SOCKET0, &tx_conf_default);
		if (retval < 0) return retval;
	}

	rte_eth_promiscuous_enable(port_num);

	retval  = rte_eth_dev_start(port_num);
	if (retval < 0) return retval;

	printf( "done: ");

	/* get link status */
	rte_eth_link_get(port_num, &link);
	if (link.link_status) {
		printf(" Link Up - speed %u Mbps - %s\n",
		       (uint32_t) link.link_speed,
		       (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
		       ("full-duplex") : ("half-duplex\n"));
	} else {
		printf(" Link Down\n");
	}

	return 0;
}

/**
 * Set up the DPDK rings which will be used to pass packets, via
 * pointers, between the multi-process server and client processes.
 * Each client needs one RX queue.
 */
static int
init_shm_rings(void)
{
	unsigned i;
	const unsigned ringsize = CLIENT_QUEUE_RINGSIZE;

	clients = rte_malloc("client details",
		sizeof(*clients) * num_clients, 0);
	if (clients == NULL)
		rte_exit(EXIT_FAILURE, "Cannot allocate memory for clients \n");

	port_queues = rte_malloc("port_txq details",
		sizeof(*port_queues) * ports->num_ports, 0);
	if (port_queues == NULL)
		rte_exit(EXIT_FAILURE, "Cannot allocate memory for port tx_q details\n");

	for (i = 0; i < num_clients; i++) {
		/* Create an RX queue for each client */
		clients[i].rx_q = rte_ring_create(get_rx_queue_name(i),
				ringsize, SOCKET0,
				NO_FLAGS); /* multi producer multi consumer*/
		if (clients[i].rx_q == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create rx ring for client %u\n", i);

		clients[i].tx_q = rte_ring_create(get_tx_queue_name(i),
				ringsize, SOCKET0,
				NO_FLAGS); /* multi producer multi consumer*/
		if (clients[i].tx_q == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create tx ring for client %u\n", i);
	}

	for (i = 0; i < ports->num_ports; i++) {
		/* Create an RX queue for each ports */
		port_queues[i].tx_q = rte_ring_create(get_port_tx_queue_name(i),
				ringsize, SOCKET0,
				RING_F_SC_DEQ); /* multi producer single consumer*/
		if (port_queues[i].tx_q == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create tx ring for port %u\n", i);
	}

	return 0;
}


void vport_init(void)
{
	const struct rte_memzone *mz = NULL;
	uint8_t i = 0;
	int retval = 0;

	/* set up array for port data */
	mz = rte_memzone_reserve(MZ_PORT_INFO, sizeof(*ports),
				rte_socket_id(), NO_FLAGS);
	if (mz == NULL)
		rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for port information\n");
	memset(mz->addr, 0, sizeof(*ports));
	ports = mz->addr;
	RTE_LOG(INFO, APP, "memzone address is %lx\n", mz->phys_addr);

	ports->num_ports = port_cfg.num_ports;

	/* now initialise the ports we will use */
	for (i = 0; i < ports->num_ports; i++) {
		ports->id[i] = port_cfg.id[i];
		retval = init_port(ports->id[i]);
		if (retval != 0)
			rte_exit(EXIT_FAILURE, "Cannot initialise port %u\n",
					(unsigned)i);
	}

	/* initialise the client queues/rings for inter process comms */
	init_shm_rings();

	/* initalise kni queues */
	init_kni();
}

/*
 * Enqueue a single packet to a client rx ring
 */
int
send_to_client(uint8_t client, struct rte_mbuf *buf)
{
	struct client *cl = NULL;
	int rslt = 0;

	cl = &clients[client];

	rslt = rte_ring_mp_enqueue(cl->rx_q, (void *)buf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(buf);
			stats_vswitch_tx_drop_increment(INC_BY_1);
			stats_vport_rx_drop_increment(client, INC_BY_1);
		} else {
			stats_vport_rx_increment(client, INC_BY_1);
			stats_vport_overrun_increment(client, INC_BY_1);
		}
	} else {
		stats_vport_rx_increment(client, INC_BY_1);
	}

	return 0;
}

/*
 * Enqueue single packet to a port
 */
int
send_to_port(uint8_t vportid, struct rte_mbuf *buf)
{
	struct port_queue *pq = &port_queues[vportid & PORT_MASK];
	int rslt = rte_ring_mp_enqueue(pq->tx_q, (void *)buf);

	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(buf);
		}
	}

	return 0;
}

/*
 * Enqueue single packet to a KNI fifo
 */
int
send_to_kni(uint8_t vportid, struct rte_mbuf *buf)
{
	int i = 0;
	int rslt = 0;
	struct kni_port *kp = NULL;

	i = vportid & KNI_MASK;
	rte_spinlock_lock(&rte_kni_locks[i]);
	rslt = rte_kni_tx_burst(&rte_kni_list[i], &buf, 1);
	rte_spinlock_unlock(&rte_kni_locks[i]);
	/* FIFO is full */
	if (rslt == 0) {
		rte_pktmbuf_free(buf);
		stats_vport_rx_drop_increment(vportid, INC_BY_1);
		stats_vswitch_tx_drop_increment(INC_BY_1);
	} else {
		stats_vport_rx_increment(vportid, INC_BY_1);
	}

	return 0;
}

/*
 * Receive burst of packets from a KNI fifo
 */
uint16_t
receive_from_kni(uint8_t vportid, struct rte_mbuf **bufs)
{
	int i = 0;
	int rslt = 0;
	struct statistics *s = NULL;

	rslt = rte_kni_rx_burst(&rte_kni_list[vportid & KNI_MASK], bufs, PKT_BURST_SIZE);
	if (rslt != 0)
		stats_vport_tx_increment(vportid, rslt);

	return rslt;
}

/*
 * Receive burst of packets from client
 */
uint16_t
receive_from_client(uint8_t client, struct rte_mbuf **bufs)
{
	int j = 0;
	uint16_t dq_pkt = PKT_BURST_SIZE;
	struct client *cl = NULL;

	cl = &clients[client];

	/* Attempt to dequeue maximum available number of mbufs from ring */
	while (dq_pkt > 0 &&
			unlikely(rte_ring_sc_dequeue_bulk(
					cl->tx_q, (void **)bufs, dq_pkt) != 0))
		dq_pkt = (uint16_t)RTE_MIN(
				rte_ring_count(cl->tx_q), PKT_BURST_SIZE);

	/* Update number of packets transmitted by client */
	stats_vport_tx_increment(client, dq_pkt);

	return dq_pkt;
}


/*
 * Receive burst of packets from physical port.
 */
uint16_t
receive_from_port(uint8_t vportid, struct rte_mbuf **bufs)
{
	int j = 0;
	uint16_t rx_count = 0;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};
	/* read a port */
	rx_count = rte_eth_rx_burst(ports->id[vportid & PORT_MASK], 0, \
			bufs, PKT_BURST_SIZE);
	/* Now process the NIC packets read */
	if (likely(rx_count > 0))
		stats_vport_rx_increment(vportid, rx_count);


	return rx_count;
}
