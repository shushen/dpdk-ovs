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
#include <rte_cycles.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>

#include "init.h"
#include "vport.h"
#include "stats.h"
#include "args.h"
#include "kni.h"
#include "veth.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NO_FLAGS        0
#define SOCKET0         0

#define MZ_PORT_INFO "MProc_port_info"
#define MZ_VPORT_INFO "MProc_vport_info"
/* define common names for structures shared between server and client */
#define MP_CLIENT_RXQ_NAME "MProc_Client_%u_RX"
#define MP_CLIENT_TXQ_NAME "MProc_Client_%u_TX"
#define MP_PORT_TXQ_NAME "MProc_PORT_%u_TX"

/* Ethernet port TX/RX ring sizes */
#define RTE_MP_RX_DESC_DEFAULT    512
#define RTE_MP_TX_DESC_DEFAULT    512
/* Ring size for communication with clients */
#define CLIENT_QUEUE_RINGSIZE     4096

#define BURST_TX_DRAIN_US   (100) /* TX drain every ~100us */

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

enum vport_type {
	VPORT_TYPE_DISABLED = 0,
	VPORT_TYPE_VSWITCHD,
	VPORT_TYPE_PHY,
	VPORT_TYPE_CLIENT,
	VPORT_TYPE_KNI,
	VPORT_TYPE_VETH,
};

struct vport_phy {
	struct rte_ring *tx_q;
	uint8_t index;
};

struct vport_client {
	struct rte_ring *rx_q;
	struct rte_ring *tx_q;
};

struct vport_kni {
	uint8_t index;
};

struct vport_veth {
	uint8_t index;
};

#define VPORT_INFO_NAMESZ	(32)

struct vport_info {
	enum vport_type __rte_cache_aligned type;
	char __rte_cache_aligned name[VPORT_INFO_NAMESZ];
	union {
		struct vport_phy phy;
		struct vport_client client;
		struct vport_kni kni;
		struct vport_veth veth;
	};
};

static int send_to_client(uint8_t client, struct rte_mbuf *buf);
static int send_to_port(uint8_t vportid, struct rte_mbuf *buf);
static int send_to_kni(uint8_t vportid, struct rte_mbuf *buf);
static int send_to_veth(uint8_t vportid, struct rte_mbuf *buf);
static uint16_t receive_from_client(uint8_t client, struct rte_mbuf **bufs);
static uint16_t receive_from_port(uint8_t vportid, struct rte_mbuf **bufs);
static uint16_t receive_from_kni(uint8_t vportid, struct rte_mbuf **bufs);
static uint16_t receive_from_veth(uint8_t vportid, struct rte_mbuf **bufs);


/* vports details */
static struct vport_info *vports;

static void set_vport_name(unsigned i, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(vports[i].name, VPORT_INFO_NAMESZ, fmt, ap);
	va_end(ap);
}

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

	for (i = CLIENT1; i < num_clients; i++) {
		struct vport_client *cl = &vports[i].client;

		/* Create an RX queue for each client */
		cl->rx_q = rte_ring_create(get_rx_queue_name(i),
				ringsize, SOCKET0,
				NO_FLAGS); /* multi producer multi consumer*/
		if (cl->rx_q == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create rx ring for client %u\n", i);

		cl->tx_q = rte_ring_create(get_tx_queue_name(i),
				ringsize, SOCKET0,
				NO_FLAGS); /* multi producer multi consumer*/
		if (cl->tx_q == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create tx ring for client %u\n", i);
	}

	for (i = 0; i < ports->num_ports; i++) {
		struct vport_phy *phy = &vports[PHYPORT0 + i].phy;

		/* Create an RX queue for each ports */
		phy->tx_q = rte_ring_create(get_port_tx_queue_name(i),
				ringsize, SOCKET0,
				RING_F_SC_DEQ); /* multi producer single consumer*/
		if (phy->tx_q == NULL)
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

	/* set up array for vport info */
	mz = rte_memzone_reserve(MZ_VPORT_INFO,
				sizeof(struct vport_info) * MAX_VPORTS,
				rte_socket_id(), NO_FLAGS);

	if (!mz)
		rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for vport information\n");
	memset(mz->addr, 0, sizeof(struct vport_info) * MAX_VPORTS);
	vports = mz->addr;
	RTE_LOG(INFO, APP, "memzone for vport info address is %lx\n", mz->phys_addr);

	ports->num_ports = port_cfg.num_ports;

	/* vports setup */

	/* vport 0 is for vswitchd */
	vports[0].type = VPORT_TYPE_VSWITCHD;
	set_vport_name(0, "vswitchd");

	/* vport for client */
	for (i = CLIENT1; i < num_clients; i++) {
		vports[i].type = VPORT_TYPE_CLIENT;
		set_vport_name(i, "Client    %2u", i);
	}
	/* vport for kni */
	for (i = 0; i < num_kni; i++) {
		vports[KNI0 + i].type = VPORT_TYPE_KNI;
		vports[KNI0 + i].kni.index = i;
		set_vport_name(KNI0 + i, "KNI Port  %2u", i);
	}
	/* vport for veth */
	for (i = 0; i < num_veth; i++) {
		vports[VETH0 + i].type = VPORT_TYPE_VETH;
		vports[VETH0 + i].veth.index = i;
		set_vport_name(VETH0 + i, "vEth Port %2u", i);
	}

	/* now initialise the ports we will use */
	for (i = 0; i < ports->num_ports; i++) {
		unsigned int vportid = cfg_params[i].port_id;

		vports[vportid].type = VPORT_TYPE_PHY;
		vports[vportid].phy.index = port_cfg.id[i];
		set_vport_name(vportid, "Port      %2u", port_cfg.id[i]);

		retval = init_port(port_cfg.id[i]);
		if (retval != 0)
			rte_exit(EXIT_FAILURE, "Cannot initialise port %u\n",
					(unsigned)i);
	}

	/* initialise the client queues/rings for inter process comms */
	init_shm_rings();

	/* initalise kni queues */
	init_kni();

	/* initalise veth queues */
	init_veth();
}

/*
 * Enqueue a single packet to a client rx ring
 */
static inline int
send_to_client(uint8_t client, struct rte_mbuf *buf)
{
	struct vport_client *cl;
	int tx_count = 0;

	cl = &vports[client].client;

	tx_count = rte_ring_mp_enqueue(cl->rx_q, (void *)buf);

	if (tx_count < 0) {
		if (tx_count == -ENOBUFS) {
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
static inline int
send_to_port(uint8_t vportid, struct rte_mbuf *buf)
{
	struct vport_phy *phy = &vports[vportid].phy;
	int tx_count = 0;

	tx_count = rte_ring_mp_enqueue(phy->tx_q, (void *)buf);

	if (tx_count == -ENOBUFS) {
		rte_pktmbuf_free(buf);
	}

	return 0;
}

/*
 * Enqueue single packet to a KNI fifo
 */
static inline int
send_to_kni(uint8_t vportid, struct rte_mbuf *buf)
{
	int i = 0;
	int tx_count = 0;

	i = vports[vportid].kni.index;
	rte_spinlock_lock(&rte_kni_locks[i]);
	tx_count = rte_kni_tx_burst(&rte_kni_list[i], &buf, 1);
	rte_spinlock_unlock(&rte_kni_locks[i]);

	/* FIFO is full */
	if (tx_count == 0) {
		rte_pktmbuf_free(buf);
		stats_vport_rx_drop_increment(vportid, INC_BY_1);
		stats_vswitch_tx_drop_increment(INC_BY_1);
	} else {
		stats_vport_rx_increment(vportid, INC_BY_1);
	}

	return 0;
}

/*
 * Enqueue single packet to a vETH fifo
 */
static int
send_to_veth(uint8_t vportid, struct rte_mbuf *buf)
{
	int i = 0;
	int tx_count = 0;

	i = vports[vportid].veth.index;
	/* Spinlocks not needed here as veth only used for OFTest currently. This
	 * may change in the future */
	tx_count = rte_kni_tx_burst(rte_veth_list[i], &buf, 1);

	/* FIFO is full */
	if (tx_count == 0) {
		rte_pktmbuf_free(buf);
		stats_vport_rx_drop_increment(vportid, INC_BY_1);
		stats_vswitch_tx_drop_increment(INC_BY_1);
	} else {
		stats_vport_rx_increment(vportid, INC_BY_1);
	}

	return 0;
}

int
send_to_vport(uint8_t vportid, struct rte_mbuf *buf)
{
	if (vportid >= MAX_VPORTS) {
		RTE_LOG(WARNING, APP,
			"sending to invalid vport: %u\n", vportid);
		goto drop;
	}

	switch (vports[vportid].type) {
	case VPORT_TYPE_PHY:
		return send_to_port(vportid, buf);
	case VPORT_TYPE_CLIENT:
		return send_to_client(vportid, buf);
	case VPORT_TYPE_KNI:
		return send_to_kni(vportid, buf);
	case VPORT_TYPE_VETH:
		return send_to_veth(vportid, buf);
	case VPORT_TYPE_VSWITCHD:
		/* DPDK vSwitch cannot handle it now, ignore */
		break;
	default:
		RTE_LOG(WARNING, APP, "unknown vport %u type %u\n",
			vportid, vports[vportid].type);
		break;
	}
drop:
	rte_pktmbuf_free(buf);
	return -1;
}

/*
 * Receive burst of packets from a vETH fifo
 */
static uint16_t
receive_from_veth(uint8_t vportid, struct rte_mbuf **bufs)
{
	int i = 0;
	uint16_t rx_count = 0;

	i = vports[vportid].veth.index;
	rx_count = rte_kni_rx_burst(rte_veth_list[i], bufs, PKT_BURST_SIZE);

	if (likely(rx_count != 0))
		stats_vport_tx_increment(vportid, rx_count);

	/* handle callbacks, i.e. ifconfig */
	rte_kni_handle_request(rte_veth_list[i]);

	return rx_count;
}

/*
 * Receive burst of packets from a KNI fifo
 */
static inline uint16_t
receive_from_kni(uint8_t vportid, struct rte_mbuf **bufs)
{
	int i = 0;
	uint16_t rx_count = 0;

	i = vports[vportid].kni.index;
	rx_count = rte_kni_rx_burst(&rte_kni_list[i], bufs, PKT_BURST_SIZE);

	if (likely(rx_count > 0))
		stats_vport_tx_increment(vportid, rx_count);

	return rx_count;
}

/*
 * Receive burst of packets from client
 */
static inline uint16_t
receive_from_client(uint8_t client, struct rte_mbuf **bufs)
{
	uint16_t rx_count = PKT_BURST_SIZE;
	struct vport_client *cl;

	cl = &vports[client].client;

	/* Attempt to dequeue maximum available number of mbufs from ring */
	while (rx_count > 0 &&
			unlikely(rte_ring_sc_dequeue_bulk(
					cl->tx_q, (void **)bufs, rx_count) != 0))
		rx_count = (uint16_t)RTE_MIN(
				rte_ring_count(cl->tx_q), PKT_BURST_SIZE);

	/* Update number of packets transmitted by client */
	stats_vport_tx_increment(client, rx_count);

	return rx_count;
}


/*
 * Receive burst of packets from physical port.
 */
static inline uint16_t
receive_from_port(uint8_t vportid, struct rte_mbuf **bufs)
{
	uint16_t rx_count = 0;

	/* Read a port */
	rx_count = rte_eth_rx_burst( vports[vportid].phy.index, 0,
			bufs, PKT_BURST_SIZE);

	/* Now process the NIC packets read */
	if (likely(rx_count > 0))
		stats_vport_rx_increment(vportid, rx_count);

	return rx_count;
}

uint16_t
receive_from_vport(uint8_t vportid, struct rte_mbuf **bufs)
{
	if (vportid >= MAX_VPORTS) {
		RTE_LOG(WARNING, APP,
			"receiving from invalid vport %u\n", vportid);
		return 0;
	}

	switch (vports[vportid].type) {
	case VPORT_TYPE_PHY:
		return receive_from_port(vportid, bufs);
	case VPORT_TYPE_CLIENT:
		return receive_from_client(vportid, bufs);
	case VPORT_TYPE_KNI:
		return receive_from_kni(vportid, bufs);
	case VPORT_TYPE_VETH:
		return receive_from_veth(vportid, bufs);
	default:
		RTE_LOG(WARNING, APP,
			"receiving from unknown vport %u type %u\n",
			vportid, vports[vportid].type);
		break;
	}
	return 0;
}

/*
 * Flush packets scheduled for transmit on ports
 */
void flush_pkts(unsigned action)
{
	unsigned i = 0;
	struct rte_mbuf *pkts[PKT_BURST_SIZE] =  {0};
	struct vport_phy *phy = &vports[action].phy;
	uint8_t portid = phy->index;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
	uint64_t diff_tsc = 0;
	static uint64_t prev_tsc[MAX_PHYPORTS] = {0};
	uint64_t cur_tsc = rte_rdtsc();
	unsigned num_pkts;

	diff_tsc = cur_tsc - prev_tsc[portid];

	num_pkts = rte_ring_count(phy->tx_q);

	/* If queue idles with less than PKT_BURST packets, drain it*/
	if ((num_pkts < PKT_BURST_SIZE))
		if(unlikely(diff_tsc < drain_tsc))
			return;

	/* maximum number of packets that can be handles is PKT_BURST_SIZE */
	if (unlikely(num_pkts >= PKT_BURST_SIZE))
		num_pkts = PKT_BURST_SIZE;

	if (unlikely(rte_ring_dequeue_bulk(phy->tx_q, (void **)pkts, num_pkts) != 0))
		return;

	const uint16_t sent = rte_eth_tx_burst(portid, 0, pkts, num_pkts);

	prev_tsc[action & PORT_MASK] = cur_tsc;

	if (unlikely(sent < num_pkts))
	{
		for (i = sent; i < num_pkts; i++)
			rte_pktmbuf_free(pkts[i]);

		stats_vport_tx_drop_increment(action, num_pkts - sent);
	}
	stats_vport_tx_increment(action, sent);
}

const char *vport_name(unsigned vportid)
{
	return vports[vportid].name;
}
