/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
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

#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>
#include <string.h>

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
#include "virtio-net.h"
#include "vport-memnic.h"

#include "flow.h"

#define rpl_strnlen strnlen

#define RTE_LOGTYPE_APP        RTE_LOGTYPE_USER1
#define NO_FLAGS               0
#define SOCKET0                0

#define MZ_PORT_INFO           "OVS_port_info"
#define OVS_CLIENT_RXQ_NAME    "OVS_Client_%u_RX"
#define OVS_CLIENT_TXQ_NAME    "OVS_Client_%u_TX"
#define OVS_CLIENT_FREE_Q_NAME "OVS_Client_%u_FREE_Q"
#define OVS_PORT_TXQ_NAME      "OVS_PORT_%u_TX"

/* Ethernet port TX/RX ring sizes */
#define RTE_MP_RX_DESC_DEFAULT 512
#define RTE_MP_TX_DESC_DEFAULT 512
/* Ring size for communication with clients */
#define CLIENT_QUEUE_RINGSIZE  4096

#define PORT_FLUSH_PERIOD_US  (100) /* TX drain every ~100us */
#define LOCAL_MBUF_CACHE_SIZE  32
#define CACHE_NAME_LEN         32
#define MAX_QUEUE_NAME_SIZE    32

/* Userspace vHost */
/* Number of descriptors per cacheline. */
#define DESC_PER_CACHELINE (CACHE_LINE_SIZE / sizeof(struct vring_desc))
#define MAX_PRINT_BUFF         6072  /* Size of buffers used for rte_snprintfs for printing packets */
#define MAX_MRG_PKT_BURST      16    /* Max burst for merge buffers. This is used for legacy virtio. */
#define BURST_TX_WAIT_US       15    /* Defines how long we wait between retries on TX */
#define BURST_TX_RETRIES       4     /* Number of retries on TX. */

/* Specify timeout (in useconds) between retries on TX. */
uint32_t burst_tx_delay_time = BURST_TX_WAIT_US;
/* Specify the number of retries on TX. */
uint32_t burst_tx_retry_num = BURST_TX_RETRIES;

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
#define MP_DEFAULT_PTHRESH     36
#define MP_DEFAULT_RX_HTHRESH  8
#define MP_DEFAULT_TX_HTHRESH  0
#define MP_DEFAULT_WTHRESH     0
#define RX_FREE_THRESH         64
#define TX_FREE_THRESH         32
#define TX_RS_THRESH           32

/*
 * Core mapping to access per-core caches using rte_lcore_id() function.
 */
static unsigned lcore_map[RTE_MAX_LCORE] = {0};

static const struct rte_eth_rxconf rx_conf_default = {
		.rx_thresh = {
				.pthresh = MP_DEFAULT_PTHRESH,
				.hthresh = MP_DEFAULT_RX_HTHRESH,
				.wthresh = MP_DEFAULT_WTHRESH,
		},
		.rx_free_thresh = RX_FREE_THRESH,
};

static const struct rte_eth_txconf tx_conf_default = {
		.tx_thresh = {
				.pthresh = MP_DEFAULT_PTHRESH,
				.hthresh = MP_DEFAULT_TX_HTHRESH,
				.wthresh = MP_DEFAULT_WTHRESH,
		},
		.tx_free_thresh = TX_FREE_THRESH,
		.tx_rs_thresh = TX_RS_THRESH,
};

/*
 * Local cache used to buffer the mbufs before enqueueing them to client's
 * or port's TX queues.
 */
struct local_mbuf_cache {
	struct rte_mbuf *cache[LOCAL_MBUF_CACHE_SIZE];
	                   /* per-port and per-core local mbuf cache */
	unsigned count;    /* number of mbufs in the local cache */
};

/*
 * Per-core local buffers to cache mbufs before sending them in bursts.
 * They use a two dimensions array. One list of all vports per each used lcore.
 * Since it's based on the idea that all working threads use different cores
 * no concurrency issues should occur.
 */
static struct local_mbuf_cache **client_mbuf_cache = NULL;
static struct local_mbuf_cache **port_mbuf_cache = NULL;
static struct local_mbuf_cache **vhost_mbuf_cache = NULL;

static int send_to_client(uint32_t client, struct rte_mbuf *buf);
static int send_to_port(uint32_t vportid, struct rte_mbuf *buf);
static int send_to_kni(uint32_t vportid, struct rte_mbuf *buf);
static int send_to_veth(uint32_t vportid, struct rte_mbuf *buf);
static int send_to_vhost(uint32_t vportid, struct rte_mbuf *buf);
static uint16_t receive_from_client(uint32_t client, struct rte_mbuf **bufs);
static uint16_t receive_from_port(uint32_t vportid, struct rte_mbuf **bufs);
static uint16_t receive_from_kni(uint32_t vportid, struct rte_mbuf **bufs);
static uint16_t receive_from_veth(uint32_t vportid, struct rte_mbuf **bufs);
static uint16_t receive_from_vhost(uint32_t vportid, struct rte_mbuf **bufs);
static void flush_phy_port_cache(uint32_t vportid);
static void flush_client_port_cache(uint32_t clientid);
static void flush_vhost_dev_port_cache(uint32_t vportid);

/* vports details */
static struct vport_info *vports;

/* Drain period to flush packets out of the physical ports and caches */
static uint64_t port_flush_period;

/*
 * Given the queue name template, get the queue name
 */
static inline const char *
get_queue_name(unsigned id, const char *queue_name_template)
{
	static char buffer[MAX_QUEUE_NAME_SIZE];
	rte_snprintf(buffer, sizeof(buffer), queue_name_template, id);
	return buffer;
}

static inline const char *
get_rx_queue_name(unsigned id)
{
	return get_queue_name(id, OVS_CLIENT_RXQ_NAME);
}

static inline const char *
get_tx_queue_name(unsigned id)
{
	return get_queue_name(id, OVS_CLIENT_TXQ_NAME);
}

static inline const char *
get_free_queue_name(unsigned id)
{
	return get_queue_name(id, OVS_CLIENT_FREE_Q_NAME);
}

static inline const char *
get_port_tx_queue_name(unsigned id)
{
	return get_queue_name(id, OVS_PORT_TXQ_NAME);
}

/*
 * Attempts to create a ring or exit
 */
static inline struct rte_ring *
queue_create(const char *ring_name, int flags)
{
	struct rte_ring *ring;

	ring = rte_ring_create(ring_name, CLIENT_QUEUE_RINGSIZE, SOCKET0, flags);
	if (ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create '%s' ring \n", ring_name);
	return ring;
}

/*
 * Macro to print out packet contents. Wrapped in debug define so that the
 * data path is not effected when debug is disabled.
 */
#ifdef VHOST_DEBUG
#define PRINT_PACKET(device, addr, size, header) do {					\
	char *pkt_addr = (char*)(addr);										\
	unsigned int index;													\
	char packet[MAX_PRINT_BUFF];										\
																		\
	if ((header))														\
	rte_snprintf(packet, MAX_PRINT_BUFF,								\
		"(%"PRIu64") Header size %d: ", (device->device_fh), (size));	\
	else																\
	rte_snprintf(packet, MAX_PRINT_BUFF,								\
		"(%"PRIu64") Packet size %d: ", (device->device_fh), (size));	\
	for (index = 0; index < (size); index++) {							\
		rte_snprintf(packet + strnlen(packet, MAX_PRINT_BUFF),			\
			MAX_PRINT_BUFF - strnlen(packet, MAX_PRINT_BUFF),			\
			"%02hhx ", pkt_addr[index]);								\
	}																	\
	rte_snprintf(packet + strnlen(packet, MAX_PRINT_BUFF),				\
			MAX_PRINT_BUFF - strnlen(packet, MAX_PRINT_BUFF), "\n");	\
																		\
	LOG_DEBUG(APP, "%s", packet);						             	\
} while(0)
#else
#define PRINT_PACKET(device, addr, size, header) do{} while(0)
#endif

/*
 * Function to convert guest physical addresses to vhost virtual addresses. This
 * is used to convert virtio buffer addresses.
 */
static inline uint64_t __attribute__((always_inline))
gpa_to_vva(struct virtio_net *dev, uint64_t guest_pa)
{
	struct virtio_memory_regions *region;
	uint32_t regionidx;
	uint64_t vhost_va = 0;

	for (regionidx = 0; regionidx < dev->mem->nregions; regionidx++) {
		region = &dev->mem->regions[regionidx];
		if ((guest_pa >= region->guest_phys_address) &&
			(guest_pa <= region->guest_phys_address_end)) {
			vhost_va = region->address_offset + guest_pa;
			break;
		}
	}
	LOG_DEBUG(APP, "(%"PRIu64") GPA %p| VVA %p\n",
		dev->device_fh, (void*)(uintptr_t)guest_pa, (void*)(uintptr_t)vhost_va);

	return vhost_va;
}

/*
 * Enqueues packets to the guest virtio RX virtqueue for vhost devices.
 */
static inline uint32_t __attribute__((always_inline))
vhost_enqueue_burst(struct virtio_net *dev, struct rte_mbuf **pkts, unsigned count)
{
	struct vhost_virtqueue *vq;
	struct vring_desc *desc;
	struct rte_mbuf *buff;
	/* The virtio_hdr is initialised to 0. */
	struct virtio_net_hdr_mrg_rxbuf virtio_hdr = {{0,0,0,0,0,0},0};
	uint64_t buff_addr = 0;
	uint64_t buff_hdr_addr = 0;
	uint32_t head[PKT_BURST_SIZE], packet_len = 0;
	uint32_t head_idx, packet_success = 0;
	uint32_t mergeable, mrg_count = 0;
	uint32_t retry = 0;
	uint16_t avail_idx, res_cur_idx;
	uint16_t res_base_idx, res_end_idx;
	uint16_t free_entries;
	uint8_t success = 0;

	LOG_DEBUG(APP, "(%"PRIu64") virtio_dev_rx()\n", dev->device_fh);
	vq = dev->virtqueue[VIRTIO_RXQ];
	count = (count > PKT_BURST_SIZE) ? PKT_BURST_SIZE : count;

	/* As many data cores may want access to available buffers, they need to be reserved. */
	do {
		res_base_idx = vq->last_used_idx_res;
		avail_idx = *((volatile uint16_t *)&vq->avail->idx);

		free_entries = (avail_idx - res_base_idx);
		/* If retry is enabled and the queue is full then we wait and retry to avoid packet loss. */
		if (unlikely(count > free_entries)) {
			for (retry = 0; retry < burst_tx_retry_num; retry++) {
				rte_delay_us(burst_tx_delay_time);
				avail_idx =
					*((volatile uint16_t *)&vq->avail->idx);
				free_entries = (avail_idx - res_base_idx);
				if (count <= free_entries)
					break;
			}
		}

		/*check that we have enough buffers*/
		if (unlikely(count > free_entries))
			count = free_entries;

		if (count == 0)
			return 0;

		res_end_idx = res_base_idx + count;
		/* vq->last_used_idx_res is atomically updated. */
		success = rte_atomic16_cmpset(&vq->last_used_idx_res, res_base_idx,
									res_end_idx);
	} while (unlikely(success == 0));
	res_cur_idx = res_base_idx;
	LOG_DEBUG(APP, "(%"PRIu64") Current Index %d| End Index %d\n", dev->device_fh, res_cur_idx, res_end_idx);

	/* Prefetch available ring to retrieve indexes. */
	rte_prefetch0(&vq->avail->ring[res_cur_idx & (vq->size - 1)]);

	/* Check if the VIRTIO_NET_F_MRG_RXBUF feature is enabled. */
	mergeable = dev->features & (1 << VIRTIO_NET_F_MRG_RXBUF);

	/* Retrieve all of the head indexes first to avoid caching issues. */
	for (head_idx = 0; head_idx < count; head_idx++)
		head[head_idx] = vq->avail->ring[(res_cur_idx + head_idx) & (vq->size - 1)];

	/*Prefetch descriptor index. */
	rte_prefetch0(&vq->desc[head[packet_success]]);

	while (res_cur_idx != res_end_idx) {
		/* Get descriptor from available ring */
		desc = &vq->desc[head[packet_success]];

		buff = pkts[packet_success];

		/* Convert from gpa to vva (guest physical addr -> vhost virtual addr) */
		buff_addr = gpa_to_vva(dev, desc->addr);
		/* Prefetch buffer address. */
		rte_prefetch0((void*)(uintptr_t)buff_addr);

		if (mergeable && (mrg_count != 0)) {
			desc->len = packet_len = rte_pktmbuf_data_len(buff);
		} else {
			/* Copy virtio_hdr to packet and increment buffer address */
			buff_hdr_addr = buff_addr;
			packet_len = rte_pktmbuf_data_len(buff) + vq->vhost_hlen;

			/*
			 * If the descriptors are chained the header and data are placed in
			 * separate buffers.
			 */
			if (desc->flags & VRING_DESC_F_NEXT) {
				desc->len = vq->vhost_hlen;
				desc = &vq->desc[desc->next];
				/* Buffer address translation. */
				buff_addr = gpa_to_vva(dev, desc->addr);
				desc->len = rte_pktmbuf_data_len(buff);
			} else {
				buff_addr += vq->vhost_hlen;
				desc->len = packet_len;
			}
		}

		/* Update used ring with desc information */
		vq->used->ring[res_cur_idx & (vq->size - 1)].id = head[packet_success];
		vq->used->ring[res_cur_idx & (vq->size - 1)].len = packet_len;

		/* Copy mbuf data to buffer */
		rte_memcpy((void *)(uintptr_t)buff_addr, (const void*)buff->pkt.data, rte_pktmbuf_data_len(buff));

		PRINT_PACKET(dev, (uintptr_t)buff_addr, rte_pktmbuf_data_len(buff), 0);

		res_cur_idx++;
		packet_success++;

		/* If mergeable is disabled then a header is required per buffer. */
		if (!mergeable) {
			rte_memcpy((void *)(uintptr_t)buff_hdr_addr, (const void*)&virtio_hdr, vq->vhost_hlen);
			PRINT_PACKET(dev, (uintptr_t)buff_hdr_addr, vq->vhost_hlen, 1);
		} else {
			mrg_count++;
			/* Merge buffer can only handle so many buffers at a time. Tell the guest if this limit is reached. */
			if ((mrg_count == MAX_MRG_PKT_BURST) || (res_cur_idx == res_end_idx)) {
				virtio_hdr.num_buffers = mrg_count;
				LOG_DEBUG(APP, "(%"PRIu64") RX: Num merge buffers %d\n", dev->device_fh, virtio_hdr.num_buffers);
				rte_memcpy((void *)(uintptr_t)buff_hdr_addr, (const void*)&virtio_hdr, vq->vhost_hlen);
				PRINT_PACKET(dev, (uintptr_t)buff_hdr_addr, vq->vhost_hlen, 1);
				mrg_count = 0;
			}
		}
		if (res_cur_idx < res_end_idx) {
			/* Prefetch descriptor index. */
			rte_prefetch0(&vq->desc[head[packet_success]]);
		}
	}

	rte_compiler_barrier();

	/* Wait until it's our turn to add our buffer to the used ring. */
	while (unlikely(vq->last_used_idx != res_base_idx))
		rte_pause();

	*(volatile uint16_t *) &vq->used->idx += count;
	vq->last_used_idx = res_end_idx;

	/* Kick the guest if necessary. */
	if (!(vq->avail->flags & VRING_AVAIL_F_NO_INTERRUPT))
		eventfd_write(vq->kickfd,1);

	return count;
}

/*
 * Dequeues packets from the guest virtio TX virtqueue for vhost devices.
 */
static inline uint16_t __attribute__((always_inline))
vhost_dequeue_burst(struct virtio_net *dev, struct rte_mbuf **pkts, unsigned count)
{
	struct rte_mbuf *mbuf;
	struct vhost_virtqueue *vq;
	struct vring_desc *desc;
	uint64_t buff_addr = 0;
	uint32_t head[PKT_BURST_SIZE];
	uint32_t used_idx, i;
	uint16_t free_entries, packet_success = 0;
	uint16_t avail_idx;

	vq = dev->virtqueue[VIRTIO_TXQ];
	avail_idx = *((volatile uint16_t *)&vq->avail->idx);

	/* If there are no available buffers then return. */
	if (vq->last_used_idx == avail_idx)
		return 0;

	LOG_DEBUG(APP, "(%"PRIu64") virtio_dev_tx()\n", dev->device_fh);

	/* Prefetch available ring to retrieve head indexes. */
	rte_prefetch0(&vq->avail->ring[vq->last_used_idx & (vq->size - 1)]);

	/*get the number of free entries in the ring*/
	free_entries = (avail_idx - vq->last_used_idx);

	/* Limit to PKT_BURST_SIZE. */
	if (free_entries > count)
		free_entries = count;

	/*
	 * Performance is better if cachelines containing descriptors are not accessed by multiple
	 * cores. We try finish with a cacheline before passing it on.
	 */
	if (likely(free_entries > DESC_PER_CACHELINE))
		free_entries = free_entries - ((vq->last_used_idx + free_entries) % DESC_PER_CACHELINE);

	LOG_DEBUG(APP, "(%"PRIu64") Buffers available %d\n", dev->device_fh, free_entries);
	/* Retrieve all of the head indexes first to avoid caching issues. */
	for (i = 0; i < free_entries; i++)
		head[i] = vq->avail->ring[(vq->last_used_idx + i) & (vq->size - 1)];

	/* Prefetch descriptor index. */
	rte_prefetch0(&vq->desc[head[packet_success]]);
	rte_prefetch0(&vq->used->ring[vq->last_used_idx & (vq->size - 1)]);

	while (packet_success < free_entries) {
		desc = &vq->desc[head[packet_success]];

		/* Discard first buffer as it is the virtio header */
		desc = &vq->desc[desc->next];

		/* Buffer address translation. */
		buff_addr = gpa_to_vva(dev, desc->addr);
		/* Prefetch buffer address. */
		rte_prefetch0((void*)(uintptr_t)buff_addr);

		used_idx = vq->last_used_idx & (vq->size - 1);

		if (packet_success < (free_entries - 1)) {
			/* Prefetch descriptor index. */
			rte_prefetch0(&vq->desc[head[packet_success+1]]);
			rte_prefetch0(&vq->used->ring[(used_idx + 1) & (vq->size - 1)]);
		}

		/* Update used index buffer information. */
		vq->used->ring[used_idx].id = head[packet_success];
		vq->used->ring[used_idx].len = 0;

		/* Allocate an mbuf and populate the structure. */
		mbuf = rte_pktmbuf_alloc(pktmbuf_pool);
		if (unlikely(mbuf == NULL)) {
			RTE_LOG(ERR, APP, "Failed to allocate memory for mbuf.\n");
			return packet_success;
		}

		/* Setup dummy mbuf. */
		mbuf->pkt.data_len = desc->len;
		mbuf->pkt.pkt_len = mbuf->pkt.data_len;

		rte_memcpy((void*) mbuf->pkt.data,
		        (const void*) buff_addr, mbuf->pkt.data_len);

		pkts[packet_success]=mbuf;

		PRINT_PACKET(dev, (uintptr_t)buff_addr, desc->len, 0);

		vq->last_used_idx++;
		packet_success++;
	}

	rte_compiler_barrier();
	vq->used->idx += packet_success;
	/* Kick guest if required. */
	if (!(vq->avail->flags & VRING_AVAIL_F_NO_INTERRUPT))
		eventfd_write(vq->kickfd,1);
	return packet_success;
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
		retval = rte_eth_rx_queue_setup(port_num, q, RTE_MP_RX_DESC_DEFAULT,
				SOCKET0, &rx_conf_default, pktmbuf_pool);
		if (retval < 0) return retval;
	}

	for (q = 0; q < tx_rings; q ++) {
		retval = rte_eth_tx_queue_setup(port_num, q, RTE_MP_TX_DESC_DEFAULT,
				SOCKET0, &tx_conf_default);
		if (retval < 0)
			return retval;
	}

	rte_eth_promiscuous_enable(port_num);

	retval = rte_eth_dev_start(port_num);
	if (retval < 0)
		return retval;

	printf( "done: ");

	/* get link status */
	rte_eth_link_get(port_num, &link);
	if (link.link_status) {
		printf(" Link Up - speed %u Mbps - %s\n",
			   (uint32_t) link.link_speed,
			   (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
		       ("full-duplex") : ("half-duplex"));
	} else {
		printf(" Link Down\n");
	}

	return 0;
}

static void *
secure_rte_zmalloc(const char *type, size_t size, unsigned align)
{
	void *addr;

	addr = rte_zmalloc(type, size, align);
	if (addr == NULL)
		rte_exit(EXIT_FAILURE, "Cannot allocate memory for %s \n", type);

	return addr;
}

/**
 * Set up the DPDK rings which will be used to pass packets, via
 * pointers, between the multi-process server and client processes.
 * Each client needs one RX queue.
 */
static int
init_shm_rings(void)
{
	unsigned i, clientid;
	char cache_name[CACHE_NAME_LEN];

	client_mbuf_cache = secure_rte_zmalloc("per-core-client cache",
			sizeof(*client_mbuf_cache) * rte_lcore_count(), 0);

	for (i = 0; i < rte_lcore_count(); i++) {
		rte_snprintf(cache_name, sizeof(cache_name), "core%u client cache", i);
		client_mbuf_cache[i] = secure_rte_zmalloc(cache_name,
				sizeof(**client_mbuf_cache) * num_clients, 0);
	}

	port_mbuf_cache = secure_rte_zmalloc("per-core-core cache",
			sizeof(*port_mbuf_cache) * rte_lcore_count(), 0);

	for (i = 0; i < rte_lcore_count(); i++) {
		rte_snprintf(cache_name, sizeof(cache_name), "core%u port cache", i);
		port_mbuf_cache[i] = secure_rte_zmalloc(cache_name,
				sizeof(**port_mbuf_cache) * ports->num_phy_ports, 0);
	}

	if (num_vhost) {
		vhost_mbuf_cache = secure_rte_zmalloc("per-core-vhost cache",
				sizeof(*vhost_mbuf_cache) * rte_lcore_count(), 0);

		for (i = 0; i < rte_lcore_count(); i++) {
			rte_snprintf(cache_name, sizeof(cache_name), "core%u vhost cache", i);
			vhost_mbuf_cache[i] = secure_rte_zmalloc(cache_name,
					sizeof(**vhost_mbuf_cache) * num_vhost, 0);
		}
	}

	for (i = 0; i < num_clients; i++) {
		clientid = CLIENT1 + i;

		struct vport_client *cl = &vports[clientid].client;
		RTE_LOG(INFO, APP, "Initialising Client %d\n", clientid);
		/* Create a "multi producer multi consumer" queue for each client */
		cl->rx_q = queue_create(get_rx_queue_name(clientid), NO_FLAGS);
		rte_snprintf(cl->ring_names.rx, sizeof(cl->ring_names.rx), "%s",
				cl->rx_q->name);

		cl->tx_q = queue_create(get_tx_queue_name(clientid), NO_FLAGS);
		rte_snprintf(cl->ring_names.tx, sizeof(cl->ring_names.tx), "%s",
				cl->tx_q->name);

		cl->free_q = queue_create(get_free_queue_name(clientid), NO_FLAGS);
		rte_snprintf(cl->ring_names.free, sizeof(cl->ring_names.free), "%s",
				cl->free_q->name);
	}

	for (i = 0; i < ports->num_phy_ports; i++) {
		struct vport_phy *phy = &vports[PHYPORT0 + i].phy;
		RTE_LOG(INFO, APP, "Initialising Port %d\n", i);
		/* Create an RX queue for each ports */
		phy->tx_q = queue_create(get_port_tx_queue_name(i), RING_F_SC_DEQ);
	}

	return 0;
}


void
vport_init(void)
{
	const struct rte_memzone *mz = NULL;
	uint8_t i = 0;
	int retval = 0;
	unsigned core_count = 0;

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

	ports->num_phy_ports = port_cfg.num_phy_ports;

	/* vports setup */

	/* vport 0 is for vswitchd */
	vports[0].type = VPORT_TYPE_VSWITCHD;
	vport_disable(0);
	vport_set_name(0, "vswitchd");

	/* vport for client */
	for (i = CLIENT1; i < num_clients; i++) {
		vports[i].type = VPORT_TYPE_CLIENT;
		vport_disable(i);
		vport_set_name(i, "Client%u", i);
	}
	/* vport for kni */
	for (i = 0; i < num_kni; i++) {
		vports[KNI0 + i].type = VPORT_TYPE_KNI;
		vports[KNI0 + i].kni.index = i;
		vport_disable(KNI0 + i);
		vport_set_name(KNI0 + i, "KNI%u", i);
	}
	/* vport for veth */
	for (i = 0; i < num_veth; i++) {
		vports[VETH0 + i].type = VPORT_TYPE_VETH;
		vports[VETH0 + i].veth.index = i;
		vport_disable(VETH0 + i);
		vport_set_name(VETH0 + i, "vEth Port  %2u", i);
	}
	/* vport for vhost */
	for (i = 0; i < num_vhost; i++) {
		vports[VHOST0 + i].type = VPORT_TYPE_VHOST;
		vports[VHOST0 + i].vhost.index = i;
		vport_disable(VHOST0 + i);
		vport_set_name(VHOST0 + i, "vHost Port %2u", i);
	}
	/* vport for MEMNIC */
	for (i = 0; i < num_memnics; i++) {
		vports[MEMNIC0 + i].type = VPORT_TYPE_MEMNIC;
		vport_disable(MEMNIC0 + i);
		vport_set_name(MEMNIC0 + i, "MEMNIC    %2u", i);
		if (init_memnic_port(&vports[MEMNIC0 + i].memnic, MEMNIC0 + i) != 0) {
			rte_exit(EXIT_FAILURE,
				"Cannot initialize MEMNIC %u\n", i);
		}
	}

	/* now initialise the physical ports we will use */
	for (i = 0; i < ports->num_phy_ports; i++) {
		unsigned int vportid = cfg_params[i].port_id;

		vports[vportid].type = VPORT_TYPE_PHY;
		vports[vportid].phy.index = port_cfg.id[i];
		vport_disable(vportid);
		vport_set_name(vportid, "Port       %2u", port_cfg.id[i]);

		retval = init_port(port_cfg.id[i]);
		if (retval != 0)
			rte_exit(EXIT_FAILURE, "Cannot initialise port %u\n", i);
	}

	/* initialise lcore mapping by querying DPDK is a core was enabled from the
	 * command line */
	for (i = 0; i < RTE_MAX_LCORE; i++)
		if (rte_lcore_is_enabled(i))
			lcore_map[i] = core_count++;

	/* initialise the client queues/rings for inter process comms */
	init_shm_rings();

	/* initalise kni queues */
	init_kni();

	/* initalise veth queues */
	init_veth();

	/* initialize flush periods using CPU frequency */
	port_flush_period = (rte_get_tsc_hz() + US_PER_S - 1) /
	        US_PER_S * PORT_FLUSH_PERIOD_US;
}

/*
 * Enqueue a single packet to a client rx ring
 */
static inline int
send_to_client(uint32_t client, struct rte_mbuf *buf)
{
	int ret, i;
	struct rte_mbuf *freebufs[PKT_BURST_SIZE];
	struct vport_client *cl = NULL;
	struct local_mbuf_cache *per_cl_cache = NULL;
	unsigned lcore_id = lcore_map[rte_lcore_id()];

	per_cl_cache = &client_mbuf_cache[lcore_id][client - CLIENT1];

	per_cl_cache->cache[per_cl_cache->count++] = buf;

	if (unlikely(per_cl_cache->count == LOCAL_MBUF_CACHE_SIZE))
		flush_client_port_cache(client);

	cl = &vports[client].client;
	ret = rte_ring_mc_dequeue_burst(cl->free_q, (void *)freebufs, PKT_BURST_SIZE);
	for (i = 0; i < ret; i++)
		rte_pktmbuf_free(freebufs[i]);

	return 0;
}

/*
 * Enqueue single packet to a port
 */
static inline int
send_to_port(uint32_t vportid, struct rte_mbuf *buf)
{
	unsigned lcore_id = lcore_map[rte_lcore_id()];
	struct local_mbuf_cache *per_port_cache =
			&port_mbuf_cache[lcore_id][vportid - PHYPORT0];

	per_port_cache->cache[per_port_cache->count++] = buf;

	if (unlikely(per_port_cache->count == LOCAL_MBUF_CACHE_SIZE))
		flush_phy_port_cache(vportid);

	return 0;
}

/*
 * Enqueue single packet to a KNI fifo
 */
static inline int
send_to_kni(uint32_t vportid, struct rte_mbuf *buf)
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
send_to_veth(uint32_t vportid, struct rte_mbuf *buf)
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

/*
 * Enqueue a single packet to a vhost port
 */
static int
send_to_vhost(uint32_t vportid, struct rte_mbuf *buf)
{
	struct local_mbuf_cache *per_vhost_cache = NULL;

	per_vhost_cache = &vhost_mbuf_cache[rte_lcore_id()][vportid - VHOST0];
	per_vhost_cache->cache[per_vhost_cache->count++] = buf;

	if (unlikely(per_vhost_cache->count == LOCAL_MBUF_CACHE_SIZE))
		flush_vhost_dev_port_cache(vportid);

	return 0;
}

static int
send_to_memnic(uint8_t vportid, struct rte_mbuf *buf)
{
	return memnic_tx(&vports[vportid].memnic, vportid, buf);
}

int
send_to_vport(uint32_t vportid, struct rte_mbuf *buf)
{
	if (unlikely(vportid >= MAX_VPORTS)) {
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
	case VPORT_TYPE_VHOST:
		return send_to_vhost(vportid, buf);
	case VPORT_TYPE_MEMNIC:
		return send_to_memnic(vportid, buf);
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
receive_from_veth(uint32_t vportid, struct rte_mbuf **bufs)
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
receive_from_kni(uint32_t vportid, struct rte_mbuf **bufs)
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
receive_from_client(uint32_t client, struct rte_mbuf **bufs)
{
	uint16_t rx_count = PKT_BURST_SIZE;
	struct vport_client *cl;

	cl = &vports[client].client;

	rx_count = rte_ring_sc_dequeue_burst(cl->tx_q, (void **)bufs, PKT_BURST_SIZE);

	/* Update number of packets transmitted by client */
	stats_vport_tx_increment(client, rx_count);

	return rx_count;
}

/*
 * Receive burst of packets from physical port.
 */
static inline uint16_t
receive_from_port(uint32_t vportid, struct rte_mbuf **bufs)
{
	uint16_t rx_count = 0;

	/* Read a port */
	rx_count = rte_eth_rx_burst(vports[vportid].phy.index, 0,
			bufs, PKT_BURST_SIZE);

	/* Now process the NIC packets read */
	if (likely(rx_count > 0))
		stats_vport_rx_increment(vportid, rx_count);

	return rx_count;
}


/*
 * Receive burst of packets from vhost port.
 */
static inline uint16_t
receive_from_vhost(uint32_t vportid, struct rte_mbuf **bufs)
{

	uint16_t rx_count = 0;
	struct virtio_net *dev = vports[vportid].vhost.dev;

	if(dev == NULL)
		return 0;

	/* Read a port */
	rx_count = vhost_dequeue_burst(dev, bufs, PKT_BURST_SIZE);

	/* Update number of packets transmitted by vHost device */
	stats_vport_tx_increment(vportid, rx_count);

	return rx_count;
}

static inline uint16_t
receive_from_memnic(uint32_t vportid, struct rte_mbuf **bufs)
{
	return memnic_rx(&vports[vportid].memnic, vportid, bufs);
}

inline uint16_t
receive_from_vport(uint32_t vportid, struct rte_mbuf **bufs)
{
	if (unlikely(vportid >= MAX_VPORTS)) {
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
	case VPORT_TYPE_VHOST:
		return receive_from_vhost(vportid, bufs);
	case VPORT_TYPE_MEMNIC:
		return receive_from_memnic(vportid, bufs);
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
inline void
flush_nic_tx_ring(unsigned vportid)
{
	struct rte_mbuf *pkts[PKT_BURST_SIZE];
	struct vport_phy *phy = &vports[vportid].phy;
	static uint64_t prev_tsc[MAX_PHYPORTS] = { 0 };
	uint8_t portid = phy->index;
	uint64_t diff_tsc, cur_tsc = rte_rdtsc();
	unsigned tx_count, pkts_sent, i;

	tx_count = rte_ring_count(phy->tx_q);

	/* If queue idles with less than PKT_BURST packets, drain it*/
	if (tx_count < PKT_BURST_SIZE) {
		diff_tsc = cur_tsc - prev_tsc[portid];
		if (unlikely(diff_tsc < port_flush_period))
			return;
	}

	/* maximum number of packets that can be handles is PKT_BURST_SIZE */
	if (tx_count > PKT_BURST_SIZE)
		tx_count = PKT_BURST_SIZE;

	if (unlikely(rte_ring_dequeue_bulk(phy->tx_q, (void **)pkts, tx_count) != 0))
		return;

	pkts_sent = rte_eth_tx_burst(portid, 0, pkts, tx_count);

	prev_tsc[portid] = cur_tsc;

	if (unlikely(pkts_sent < tx_count)) {
		for (i = pkts_sent; i < tx_count; i++)
			rte_pktmbuf_free(pkts[i]);

		stats_vport_tx_drop_increment(vportid, tx_count - pkts_sent);
	}
	stats_vport_tx_increment(vportid, pkts_sent);
}

/*
 * This function must be called periodically to ensure that no mbufs get
 * stuck in the port mbuf cache.
 *
 * This must be called by each core that calls send_to_port()
 *
 */
inline void
flush_ports(void)
{
	uint32_t portid = 0;
	unsigned lcore_id = lcore_map[rte_lcore_id()];
	struct local_mbuf_cache *per_port_cache = NULL;

	/* iterate over all port caches for this core */
	for (portid = 0; portid < ports->num_phy_ports; portid++) {
		per_port_cache = &port_mbuf_cache[lcore_id][portid];
		if (per_port_cache->count)
			flush_phy_port_cache(portid + PHYPORT0);
	}

	return;
}

/*
 * Flush any mbufs in port's cache to NIC TX pre-queue ring.
 *
 * Update 'next_tsc' to indicate when next flush is required
 */
static inline void
flush_phy_port_cache(uint32_t vportid)
{
	unsigned i = 0, tx_count = 0;
	struct local_mbuf_cache *per_port_cache = NULL;
	struct vport_phy *phy;
	uint8_t portid = vportid - PHYPORT0;
	unsigned lcore_id = lcore_map[rte_lcore_id()];

	per_port_cache = &port_mbuf_cache[lcore_id][portid];

	phy = &vports[vportid].phy;

	tx_count = rte_ring_mp_enqueue_burst(phy->tx_q,
			(void **) per_port_cache->cache, per_port_cache->count);

	if (unlikely(tx_count < per_port_cache->count)) {
		uint8_t dropped = per_port_cache->count - tx_count;
		for (i = tx_count; i < per_port_cache->count; i++)
			rte_pktmbuf_free(per_port_cache->cache[i]);

		stats_vswitch_tx_drop_increment(dropped);
		stats_vport_tx_drop_increment(vportid, dropped);
		/* TODO: stats_vport_overrun_increment */
	}

	per_port_cache->count = 0;
}

/*
 * This function must be called periodically to ensure that no mbufs get
 * stuck in the client mbuf cache.
 *
 * This must be called by each core that calls send_to_client()
 *
 */
inline void
flush_clients(void)
{
	uint32_t clientid = 0;
	unsigned lcore_id = lcore_map[rte_lcore_id()];
	struct local_mbuf_cache *per_client_cache = NULL;

	/* iterate over all client caches for this core */
	for (clientid = 0; clientid < num_clients; clientid++) {
		per_client_cache = &client_mbuf_cache[lcore_id][clientid];
		if (per_client_cache->count)
			flush_client_port_cache(clientid + CLIENT1);
	}
}

/*
 * Flush any mbufs in 'clientid' client's cache to client ring.
 *
 * Update 'next_tsc' to indicate when next flush is required
 */
static inline void
flush_client_port_cache(uint32_t clientid)
{
	struct vport_client *cl = NULL;
	struct local_mbuf_cache *per_cl_cache = NULL;
	unsigned tx_count = 0, i = 0;
	unsigned lcore_id = lcore_map[rte_lcore_id()];

	per_cl_cache = &client_mbuf_cache[lcore_id][clientid - CLIENT1];

	cl = &vports[clientid].client;

	tx_count = rte_ring_mp_enqueue_burst(cl->rx_q,
				(void **)per_cl_cache->cache, per_cl_cache->count);

	if (unlikely(tx_count < per_cl_cache->count)) {
		uint8_t dropped = per_cl_cache->count - tx_count;
		for (i = tx_count; i < per_cl_cache->count; i++)
			rte_pktmbuf_free(per_cl_cache->cache[i]);

		stats_vswitch_tx_drop_increment(dropped);
		stats_vport_rx_drop_increment(clientid, dropped);
		/* TODO: stats_vport_overrun_increment */
	}

	stats_vport_rx_increment(clientid, tx_count);

	per_cl_cache->count = 0;
}

/*
 * This function must be called periodically to ensure that no mbufs get
 * stuck in the vhost mbuf cache.
 *
 * This must be called by each core that calls send_to_vhost()
 *
 */
void
flush_vhost_devs(void)
{
	uint32_t vhostid = 0;
	uint8_t lcore_id = rte_lcore_id();
	struct local_mbuf_cache *per_vhost_cache = NULL;

	/* iterate over all vhost caches for this core */
	for (vhostid = 0; vhostid < num_vhost; vhostid++) {
		per_vhost_cache = &vhost_mbuf_cache[lcore_id][vhostid];
		/* only flush when we have exceeded our deadline */
		if (per_vhost_cache->count) {
			flush_vhost_dev_port_cache(vhostid + VHOST0);
		}
	}

	return;
}

/*
 * Flushes a vhost device mbuf cache for the current lcore.
 */
static inline void
flush_vhost_dev_port_cache(uint32_t vportid)
{
	struct virtio_net *dev = NULL;
	struct local_mbuf_cache *per_vhost_cache = NULL;
	int tx_count;
	unsigned  i;

	per_vhost_cache = &vhost_mbuf_cache[rte_lcore_id()][vportid - VHOST0];

	dev = vports[vportid].vhost.dev;

	if(unlikely(dev == NULL)){
		stats_vswitch_tx_drop_increment(per_vhost_cache->count);
		stats_vport_rx_drop_increment(vportid, per_vhost_cache->count);
	} else {
		tx_count = vhost_enqueue_burst(dev,
				(struct rte_mbuf**)per_vhost_cache->cache, per_vhost_cache->count);

		if (unlikely(tx_count < (int)per_vhost_cache->count)) {
			uint8_t dropped = per_vhost_cache->count - tx_count;
			stats_vswitch_tx_drop_increment(dropped);
			stats_vport_rx_drop_increment(vportid, dropped);
		}
		stats_vport_rx_increment(vportid, tx_count);
	}
	for (i = 0; i < per_vhost_cache->count; i++)
		rte_pktmbuf_free_seg(per_vhost_cache->cache[i]);

	per_vhost_cache->count = 0;
}

/* Helper functions for vport management */

/* Get 'vportid' for a vport with the given 'name'.
 *
 * Attempts to resolve 'name' to its associated vport number. Returns:
 * - 'vport_id' if 'name' is a valid vport name
 * - 'UINT32_MAX' if 'name' is not associated with a vport
 */
uint32_t
vport_name_to_portid(const char *name)
{
	int i = 0;

	for (i = 0; i < MAX_VPORTS; i++) {
		if (vport_exists(i) &&
		    !strncmp(vports[i].name, name, MAX_VPORT_NAME_SIZE))
			return i;
	}

	return UINT32_MAX;  /* Name not found */
}

/* Get an array index for next available vport of given 'type'.
 *
 * Returns an index if any free devices available, else 'MAX_VPORTS'.
 */
uint32_t
vport_next_available_index(enum vport_type type)
{
	uint32_t start_idx = 0, end_idx = 0, i = 0;

	switch (type) {
	/* TODO - remove this when bridges no longer need it */
	case VPORT_TYPE_VSWITCHD:
	case VPORT_TYPE_BRIDGE:
		/* TODO - we currently only support one bridge, which is
		 * hardcoded to port 0. */
		return CLIENT0;
	case VPORT_TYPE_CLIENT:
		start_idx = CLIENT1;
		/* Currently, num_clients includes client 0, thus client 0
		 * must be subtracted from the range */
		end_idx = CLIENT1 + (num_clients - 1);
		break;
	case VPORT_TYPE_PHY:
		start_idx = PHYPORT0;
		end_idx = KNI0;
		break;
	case VPORT_TYPE_KNI:
		start_idx = KNI0;
		end_idx = KNI0 + num_kni;
		break;
	case VPORT_TYPE_VETH:
		start_idx = VETH0;
		end_idx = VETH0 + num_veth;
		break;
	case VPORT_TYPE_VHOST:
		start_idx = VHOST0;
		end_idx = VHOST0 + num_vhost;
		break;
	case VPORT_TYPE_DISABLED:
	default:
		return MAX_VPORTS;
	}

	for (i = start_idx; i < end_idx; i++) {
		if (vport_exists(i) && !vport_is_enabled(i))
			return i;
	}

	return MAX_VPORTS;
}

/* Checks if 'vportid' is valid for a vport of given 'type'. */
inline bool
vport_id_is_valid(unsigned vportid, enum vport_type type)
{
	/* Special case for bridges, until a special bridge range of ports is
	 * assigned */
	if (vports[vportid].type == VPORT_TYPE_VSWITCHD &&
		type == VPORT_TYPE_BRIDGE)
		return true;

	/* We assume that type stored in the 'vports' array is constant, and
	 * therefore indicative of the type of vport that can be stored at that
	 * index. */
	if(vportid < MAX_VPORTS)
		return vports[vportid].type == type;

	return false;
}

/* Check if vport indicated by 'vportid' currently exists in the datapath. */
inline bool
vport_exists(unsigned vportid)
{
	/* We can assume that 'vports[vportid].type' will be 0 for unitialised
	 * ports, owing to a prior memset on the 'vports' struct. This will need
	 * to change when ports are dynamically added/removed in the datapath. */
	return (vportid < MAX_VPORTS && vports[vportid].type != VPORT_TYPE_DISABLED);
}

/* External setters/getters for vports */

/* Set name of a given vport */
void
vport_set_name(unsigned vportid, const char *fmt, ...)
{
	va_list ap;

	if(vport_exists(vportid)) {
		va_start(ap, fmt);
		vsnprintf(vports[vportid].name, VPORT_INFO_NAMESZ, fmt, ap);
		va_end(ap);
	}
}

/* Get name of a given vport. */
inline char *
vport_get_name(unsigned vportid)
{
	if(vport_exists(vportid))
		return vports[vportid].name;

	return NULL;
}

/* Get type of a given vport. */
inline enum vport_type
vport_get_type(unsigned vportid)
{
	if(vport_exists(vportid))
		return vports[vportid].type;

	return VPORT_TYPE_DISABLED;
}

inline void
vport_enable(unsigned vportid)
{
	if(vport_exists(vportid))
		vports[vportid].enabled = true;
}

inline void
vport_disable(unsigned vportid)
{
	if(vport_exists(vportid))
		vports[vportid].enabled = false;
}

/* Check if vport given by 'vportid' is enabled (i.e. has been "added"). */
inline bool
vport_is_enabled(unsigned vportid)
{
	if(vport_exists(vportid))
		return vports[vportid].enabled;

	return false;
}

/* vhost port control functions */

inline int
vport_vhost_up(struct virtio_net *dev)
{
	uint32_t vhostid;
	struct vport_info *info;

	/* Search for the portname and set the dev pointer. */
	for (vhostid = 0; vhostid < num_vhost; vhostid++) {
		info = &vports[VHOST0 + vhostid];
		if (strncmp(dev->port_name, info->name, strnlen(dev->port_name,
				sizeof(dev->port_name))) == 0 &&
			(strnlen(dev->port_name, sizeof(dev->port_name)) ==
				 strnlen(info->name, sizeof(info->name)))) {
				info->vhost.dev = dev;
				return 0;
		}
	}

	RTE_LOG(ERR, APP, "(%"PRIu64") Port name %s does not match any \
		ovs_dpdk port names for adding device\n",
		dev->device_fh, dev->port_name);

	return -1;
}

inline int
vport_vhost_down(struct virtio_net *dev)
{
	uint32_t vhostid;
	struct vport_info *info;

	/* Search for the portname and clear the dev pointer. */
	for (vhostid = 0; vhostid < num_vhost; vhostid++) {
		info = &vports[VHOST0 + vhostid];
		if (strncmp(dev->port_name, info->name, strnlen(dev->port_name,
				sizeof(dev->port_name))) == 0 &&
			(strnlen(dev->port_name, sizeof(dev->port_name)) ==
				 strnlen(info->name, sizeof(info->name)))) {
			info->vhost.dev = NULL;
			return 0;
		}
	}

	RTE_LOG(ERR, APP, "(%"PRIu64") Port name %s does not match any \
		ovs_dpdk port names for device removal\n",
		dev->device_fh, dev->port_name);

	return -1;
}

void
vport_set_kni_fifo_names(unsigned vportid,
		const struct vport_kni_fifo_names *names)
{
	struct vport_kni_fifo_names *internal;

	if(vport_exists(vportid)) {
		internal = &vports[vportid].kni.fifo_names;

		rte_snprintf(internal->tx, sizeof(internal->tx), "%s", names->tx);
		rte_snprintf(internal->rx, sizeof(internal->rx), "%s", names->rx);
		rte_snprintf(internal->alloc, sizeof(internal->alloc), "%s", names->alloc);
		rte_snprintf(internal->free, sizeof(internal->free), "%s", names->free);
		rte_snprintf(internal->req, sizeof(internal->req), "%s", names->req);
		rte_snprintf(internal->resp, sizeof(internal->resp), "%s", names->resp);
		rte_snprintf(internal->sync, sizeof(internal->sync), "%s", names->sync);
	}
}
