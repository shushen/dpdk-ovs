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
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_fbk_hash.h>
#include <rte_string_fns.h>
#include <rte_hash.h>
#include <rte_cpuflags.h>
#include <rte_memcpy.h>
#include <rte_kni.h>
#include <rte_cycles.h>

#include "kni.h"
#include "common.h"
#include "args.h"
#include "init.h"
#include "main.h"

/*
 * When reading/writing to/from rings/ports use this batch size
 */
#define PKT_BURST_SIZE      32
#define RX_RING_SIZE        128
#define TX_RING_SIZE        512
#define VSWITCHD            0
#define CLIENT1             1
#define KNI0                0x20
#define SOCKET0             0
#define PREFETCH_OFFSET     3
#define RING_SIZE           (PKT_BURST_SIZE * 8)
#define VLAN_ID_MASK        0xFFF
#define VLAN_PRIO_SHIFT     13
#define NUM_BYTES_MAC_ADDR  6
#define MAC_ADDR_STR_INT_L  3
#define NEWLINE_CHAR_OFFSET 1
#define HASH_NAME           "hash1"
#define HASH_BUCKETS        4
#define BYTES_TO_PRINT      256
#define PORT_MASK           0x0F
#define KNI_MASK            0x1F
#define IS_PHY_PORT(action) ((action) > PORT_MASK && (action) <= KNI_MASK)
#define IS_KNI_PORT(action) ((action) > KNI_MASK  && (action) < (KNI_MASK + MAX_KNI_PORTS))
#define TCP_FLAG_MASK       0x3F
#define BURST_TX_DRAIN_US   (100) /* TX drain every ~100us */

/* Hash function used if none is specified */
#ifdef RTE_MACHINE_CPUFLAG_SSE4_2
#include <rte_hash_crc.h>
#define DEFAULT_HASH_FUNC       rte_hash_crc
#else
#include <rte_jhash.h>
#define DEFAULT_HASH_FUNC       rte_jhash
#endif

/* Parameters used for hash table in unit test functions. Name set later. */
static struct rte_hash_parameters ut_params = {
	.name               = HASH_NAME,
	.entries            = MAX_FLOWS,
	.bucket_entries     = HASH_BUCKETS,
	.key_len            = sizeof(struct flow_key), /* 13 */
	.hash_func          = DEFAULT_HASH_FUNC, /* rte_hash_crc is only declared when SSE4.2 is enabled */
	.hash_func_init_val = 0,
	.socket_id          = SOCKET0,
};
extern struct cfg_params *cfg_params;
/* One buffer per client rx queue - dynamically allocate array */
static struct rte_hash *handle = NULL;
static int switch_rx_drop = 0;
static int switch_tx_drop = 0;
/* When > 0, indicates that a ring's high water mark has been
 * reached or exceeded */
static int overruns = 0;
extern uint16_t nb_cfg_params;
struct statistics *vport_stats;

/* Global timestamp counter that can be updated
 * only by vswitchd core. It's used as flow's last
 * used time, when next packet arrives.
 */
volatile uint64_t curr_tsc = 0;
/* Measured CPU frequency. Needed to translate tsk to ms. */
uint64_t cpu_freq = 0;

static void send_to_client(uint8_t client, struct rte_mbuf *buf);
static void send_to_port(uint8_t vportid, struct rte_mbuf *buf);
static void send_to_kni(uint8_t vportid, struct rte_mbuf *buf);
static void send_packet_to_vswitchd(struct rte_mbuf *mbuf, struct dpdk_upcall *info);
static void send_reply_to_vswitchd(struct dpdk_message *reply);
static void receive_from_client(uint16_t client);
static void receive_from_port(unsigned vportid);
static void receive_from_kni(uint8_t vportid);
static void receive_request_from_vswitchd(void);
static void flush_pkts(unsigned);

static void action_execute(int pos, struct rte_mbuf *pkt);
static void action_forward(uint32_t vport, struct rte_mbuf *pkt);
static void switch_packet(struct rte_mbuf *pkt, uint8_t in_port);

static void flow_table_init(void);
static void flow_table_add_flow(int32_t pos, struct flow_key *key, uint32_t action, bool clear);
static void flow_table_get_flow(int32_t pos, struct flow_key *key, uint32_t *action, struct flow_stats *stats);
static void flow_table_del_flow(int32_t pos);
static void flow_table_update_stats(int32_t pos, struct rte_mbuf *pkt);
static void flow_table_clear_stats(int32_t pos);
static void flow_table_print_flow(int32_t pos);
static void flow_key_extract(struct rte_mbuf *pkt, uint8_t in_port, struct flow_key *key);
static void flow_key_print(volatile struct flow_key *key);
static void flow_cmd_get(struct dpdk_flow_message *request);
static void flow_cmd_new(struct dpdk_flow_message *request);
static void flow_cmd_del(struct dpdk_flow_message *request);
static void flow_cmd_dump(struct dpdk_flow_message *request);

static void handle_vswitchd_cmd(struct rte_mbuf *mbuf);
static void handle_flow_cmd(struct dpdk_flow_message *request);
static void handle_packet_cmd(struct dpdk_packet_message *request, struct rte_mbuf *pkt);
static void handle_unknown_cmd(void);


/*
 * Returns MAC address for port in a string
 */
static const char *
get_printable_mac_addr(uint8_t port)
{
	static const char err_address[] = "00:00:00:00:00:00";
	static char addresses[RTE_MAX_ETHPORTS][sizeof(err_address)] = {0};
	struct ether_addr mac = {0};
	int ether_addr_len = sizeof(err_address) - NEWLINE_CHAR_OFFSET;
	int i = 0;
	int j = 0;

	if (unlikely(port >= RTE_MAX_ETHPORTS))
		return err_address;

	/* first time run for this port so we populate addresses */
	if (unlikely(addresses[port][0] == '\0')) {
		rte_eth_macaddr_get(port, &mac);
		while(j < NUM_BYTES_MAC_ADDR) {
			rte_snprintf(&addresses[port][0] + i,
			             MAC_ADDR_STR_INT_L + NEWLINE_CHAR_OFFSET,
			             "%02x:",
			             mac.addr_bytes[j]);
			i += MAC_ADDR_STR_INT_L;
			j++;
		}
		/* Overwrite last ":" and null terminate the string */
		addresses[port][ether_addr_len] = '\0';
	}
	return addresses[port];
}

/*
 * This function displays the recorded statistics for each port
 * and for each client. It uses ANSI terminal codes to clear
 * screen when called. It is called from a single non-master
 * thread in the server process, when the process is run with more
 * than one lcore enabled.
 */
static void
do_stats_display(void)
{
	unsigned i = 0;
	unsigned j = 0;
	/* ANSI escape sequences for terminal display.
	 * 27 = ESC, 2J = Clear screen */
	const char clr[] = {27, '[', '2', 'J', '\0'};
	/* H = Home position for cursor*/
	const char topLeft[] = {27, '[', '1', ';', '1', 'H','\0'};

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("Physical Ports\n");
	printf("-----\n");
	for (i = 0; i < ports->num_ports; i++)
		printf("Port %u: '%s'\t", ports->id[i],
				get_printable_mac_addr(ports->id[i]));
	printf("\n\n");

	printf("\nVport Statistics\n"
		     "============   ============  ============  ============  ============\n"
		     "Interface      rx_packets    rx_dropped    tx_packets    tx_dropped  \n"
		     "------------   ------------  ------------  ------------  ------------\n");
	for (i = 0; i < MAX_VPORTS; i++) {
		const volatile struct statistics vstats = vport_stats[i];
		if (i == 0) {
			printf("vswitchd   ");
		} else if (i <= PORT_MASK) {
			printf("Client   %2u", i);
		} else if (i <= KNI_MASK) {
			printf("Port     %2u", i & PORT_MASK);
		} else {
			printf("KNI Port %2u", i & KNI_MASK);
		}
		printf("%13"PRIu64" %13"PRIu64" %13"PRIu64" %13"PRIu64"\n",
					vstats.rx,
					vstats.rx_drop,
					vstats.tx,
					vstats.tx_drop);
	}
	printf("============   ============  ============  ============  ============\n");

	printf("\n Switch rx dropped %d\n", switch_rx_drop);
	printf("\n Switch tx dropped %d\n", switch_tx_drop);
	printf("\n Queue overruns   %d\n",  overruns);
	printf("\n Mempool count    %9u\n", rte_mempool_count(pktmbuf_pool));
	printf("\n");
}

/*
 * Function translates TSC cycles to monotonic linux time.
 */
uint64_t ovs_flow_used_time(uint64_t flow_tsc)
{
	uint64_t curr_ms = 0;
	uint64_t idle_ms = 0;
	struct timespec tp = {0};

	/*
	 * Count idle time of flow. As TSC overflows infrequently
	 * (i.e. of the order of many years) and will only result
	 * in a spurious reading for flow used time, we dont check
	 * for overflow condition
	 */
	idle_ms = (curr_tsc - flow_tsc) * 1000UL / cpu_freq;

	/* Return monotonic linux time */
	clock_gettime(CLOCK_MONOTONIC, &tp);
	curr_ms = tp.tv_sec * 1000UL + tp.tv_nsec / 1000000UL;

	return curr_ms - idle_ms;
}

/*
 * Function to set all the client statistic values to zero.
 * Called at program startup.
 */
static void
clear_stats(void)
{
	unsigned i = 0;

	for (i = 0; i < MAX_VPORTS; i++) {
		vport_stats[i].rx = 0;
		vport_stats[i].rx_drop = 0;
		vport_stats[i].tx = 0;
		vport_stats[i].tx_drop = 0;
	}
}

/*
 * Enqueue a single packet to a client rx ring
 */
static void
send_to_client(uint8_t client, struct rte_mbuf *buf)
{
	struct client *cl = NULL;
	int rslt = 0;
	struct statistics *s = NULL;

	cl = &clients[client];
	s = &vport_stats[client];

	rslt = rte_ring_sp_enqueue(cl->rx_q, (void *)buf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(buf);
			switch_tx_drop++;
			s->rx_drop++;
		} else {
			overruns++;
			s->rx++;
		}
	} else {
		s->rx++;
	}
}

/*
 * Enqueue single packet to a port
 */
static void
send_to_port(uint8_t vportid, struct rte_mbuf *buf)
{
	struct port_queue *pq = &port_queues[vportid & PORT_MASK];

	if (rte_ring_mp_enqueue(pq->tx_q, (void *)buf) < 0) {
		rte_pktmbuf_free(buf);
	}
}

/*
 * Enqueue single packet to a KNI fifo
 */
static void
send_to_kni(uint8_t vportid, struct rte_mbuf *buf)
{
	int i = 0;
	int rslt = 0;
	struct kni_port *kp = NULL;
	struct statistics *s = NULL;

	s = &vport_stats[vportid];

	rslt = rte_kni_tx_burst(&rte_kni_list[vportid & KNI_MASK], &buf, 1);
	/* FIFO is full */
	if (rslt == 0) {
		rte_pktmbuf_free(buf);
		s->rx_drop++;
		switch_tx_drop++;
	} else {
		s->rx++;
	}
}

/*
 * Receive burst of packets from a KNI fifo
 */
static void
receive_from_kni(uint8_t vportid)
{
	int i = 0;
	int rslt = 0;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};
	struct statistics *s = NULL;

	s = &vport_stats[vportid];

	rslt = rte_kni_rx_burst(&rte_kni_list[vportid & KNI_MASK], buf, PKT_BURST_SIZE);

	if (rslt != 0) {
		s->tx += rslt;
		for (i = 0; i < rslt; i++) {
			switch_packet(buf[i], vportid);
		}
	}
}

/*
 * Receive burst of packets from client
 */
static void
receive_from_client(uint16_t client)
{
	int j = 0;
	uint16_t dq_pkt = PKT_BURST_SIZE;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};
	struct client *cl = NULL;
	struct statistics *s = NULL;

	cl = &clients[client];
	s = &vport_stats[client];

	/* Attempt to dequeue maximum available number of mbufs from ring */
	while (dq_pkt > 0 &&
			unlikely(rte_ring_sc_dequeue_bulk(
					cl->tx_q, (void **)buf, dq_pkt) != 0))
		dq_pkt = (uint16_t)RTE_MIN(
				rte_ring_count(cl->tx_q), PKT_BURST_SIZE);

	/* Update number of packets transmitted by client */
	s->tx += dq_pkt;

	for (j = 0; j < dq_pkt; j++) {
		switch_packet(buf[j], client);
	}
}


/*
 * Receive burst of packets from physical port.
 */
static void
receive_from_port(unsigned vportid)
{
	int j = 0;
	uint16_t rx_count = 0;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};
	/* read a port */
	rx_count = rte_eth_rx_burst(ports->id[vportid & PORT_MASK], 0, \
			buf, PKT_BURST_SIZE);
	/* Now process the NIC packets read */
	if (likely(rx_count > 0)) {
		vport_stats[vportid].rx += rx_count;
		/* Prefetch first packets */
		for (j = 0; j < PREFETCH_OFFSET && j < rx_count; j++) {
			rte_prefetch0(rte_pktmbuf_mtod(buf[j], void *));
		}

		/* Prefetch and forward already prefetched packets */
		for (j = 0; j < (rx_count - PREFETCH_OFFSET); j++) {
			rte_prefetch0(rte_pktmbuf_mtod(buf[
						j + PREFETCH_OFFSET], void *));
			switch_packet(buf[j], vportid);
		}

		/* Forward remaining prefetched packets */
		for (; j < rx_count; j++) {
			switch_packet(buf[j], vportid);
		}
	}
}

/*
 * Function sends unmatched packets to vswitchd.
 */
static void
send_packet_to_vswitchd(struct rte_mbuf *mbuf, struct dpdk_upcall *info)
{
	int rslt = 0;
	struct statistics *vswd_stat = NULL;
	void *mbuf_ptr = NULL;

	vswd_stat = &vport_stats[VSWITCHD];

	/* send one packet, delete information about segments */
	rte_pktmbuf_pkt_len(mbuf) = rte_pktmbuf_data_len(mbuf);

	/* allocate space before the packet for the upcall info */
	mbuf_ptr = rte_pktmbuf_prepend(mbuf, sizeof(*info));

	if (mbuf_ptr == NULL) {
		printf("Cannot prepend upcall info\n");
		rte_pktmbuf_free(mbuf);
		switch_tx_drop++;
		vswd_stat->tx_drop++;
		return;
	}

	rte_memcpy(mbuf_ptr, info, sizeof(*info));

	/* send the packet and the upcall info to the daemon */
	rslt = rte_ring_sp_enqueue(vswitch_packet_ring, mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_pktmbuf_free(mbuf);
			switch_tx_drop++;
			vswd_stat->tx_drop++;
			return;
		} else {
			overruns++;
		}
	}

	vswd_stat->tx++;
}

/*
 * Function receives messages from the daemon.
 */
static void
receive_request_from_vswitchd(void)
{
	int j = 0;
	uint16_t dq_pkt = PKT_BURST_SIZE;
	struct client *vswd = NULL;
	struct statistics *vswd_stat = NULL;
	struct rte_mbuf *buf[PKT_BURST_SIZE] = {0};

	vswd = &clients[VSWITCHD];
	vswd_stat = &vport_stats[VSWITCHD];

	/* Attempt to dequeue maximum available number of mbufs from ring */
	while (dq_pkt > 0 &&
			unlikely(rte_ring_sc_dequeue_bulk(
					vswd->tx_q, (void **)buf, dq_pkt) != 0))
		dq_pkt = (uint16_t)RTE_MIN(
				rte_ring_count(vswd->tx_q), PKT_BURST_SIZE);

	/* Update number of packets transmitted by daemon */
	vswd_stat->rx += dq_pkt;

	for (j = 0; j < dq_pkt; j++) {
		handle_vswitchd_cmd(buf[j]);
	}
}

/*
 * Flush packets scheduled for transmit on ports
 */
static void
flush_pkts(unsigned action)
{
	unsigned i = 0;
	uint16_t deq_count = PKT_BURST_SIZE;
	struct rte_mbuf *pkts[PKT_BURST_SIZE] =  {0};
	struct port_queue *pq =  &port_queues[action & PORT_MASK];
	struct statistics *s = &vport_stats[action];
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
	uint64_t diff_tsc = 0;
	static uint64_t prev_tsc[MAX_PHYPORTS] = {0};
	uint64_t cur_tsc = rte_rdtsc();
	unsigned num_pkts;

	diff_tsc = cur_tsc - prev_tsc[action & PORT_MASK];

	if (unlikely(rte_ring_count(pq->tx_q) >= PKT_BURST_SIZE))
	{
		num_pkts = PKT_BURST_SIZE;
	}
	else
	{
		/* If queue idles with less than PKT_BURST packets, drain it*/
		if(unlikely(diff_tsc > drain_tsc)) {
			num_pkts = rte_ring_count(pq->tx_q);
		}
		else {
			return;
		}
	}

	if (unlikely(rte_ring_dequeue_bulk(
			      pq->tx_q, (void **)pkts, num_pkts) != 0))
		return;

	const uint16_t sent = rte_eth_tx_burst(
				 ports->id[action & PORT_MASK], 0, pkts, num_pkts);

	prev_tsc[action & PORT_MASK] = cur_tsc;

	if (unlikely(sent < num_pkts))
	{
		for (i = sent; i < num_pkts; i++)
			rte_pktmbuf_free(pkts[i]);
		s->tx_drop += (num_pkts - sent);
	}
	else
	{
		s->tx += sent;
	}
}

/*
 * This function takes a packet and routes it as per the flow table.
 */
static void
switch_packet(struct rte_mbuf *pkt, uint8_t in_port)
{
	int pos = 0;
	struct dpdk_upcall info = {0};

	flow_key_extract(pkt, in_port, &info.key);

	pos = rte_hash_lookup(handle, &info.key);

	if (pos < 0) {
		/* flow table miss, send unmatched packet to the daemon */
		info.cmd = PACKET_CMD_MISS;
		send_packet_to_vswitchd(pkt, &info);
	} else {
		flow_table_update_stats(pos, pkt);
		action_execute(pos, pkt);
	}
}

/* Initialize the flow table  */
static void
flow_table_init(void)
{
	/* Check if hardware-accelerated hashing supported */
#ifdef RTE_MACHINE_CPUFLAG_SSE4_2
	if (ut_params.hash_func == rte_hash_crc &&
			!rte_cpu_get_flag_enabled(RTE_CPUFLAG_SSE4_2)) {
		RTE_LOG(WARNING, HASH, "CRC32 instruction requires SSE4.2, "
				               "which is not supported on this system. "
				               "Falling back to software hash.\n");
		ut_params.hash_func = rte_jhash;
	}
#endif /* This check does not compile if SSE4_2 is not enabled for build */

	handle = rte_hash_create(&ut_params);
	if (handle == NULL) {
		RTE_LOG(WARNING, APP, "Failed to create hash table\n");
		exit(EXIT_FAILURE);
	}
}

/*
 * Clear flow table statistics at entry pos
 */
static void
flow_table_clear_stats(int32_t pos)
{
	rte_spinlock_init((rte_spinlock_t *)&flow_table->stats[pos].lock);

	flow_table->stats[pos].used = 0;
	flow_table->stats[pos].tcp_flags = 0;
	flow_table->stats[pos].packet_count = 0;
	flow_table->stats[pos].byte_count = 0;
}

/*
 * Add entry to flow table. For the modify case, clear signals that statistics
 * need to be reset
 */
static void
flow_table_add_flow(int32_t pos, struct flow_key *key, uint32_t action, bool clear)
{

	flow_table->key[pos] = *key;
	flow_table->dst_port[pos] = action;
	flow_table->used[pos] = true;

	if (clear)
		flow_table_clear_stats(pos);
}

/*
 * Return flow table entry at pos. By setting key, action or stats to NULL, this
 * information will not be returned.
 */
static void
flow_table_get_flow(int32_t pos, struct flow_key *key, uint32_t *action, struct flow_stats *stats)
{
	if (key)
		*key = flow_table->key[pos];

	if (action)
		*action = flow_table->dst_port[pos];

	if (stats) {
		*stats = flow_table->stats[pos];
		/* vswitchd needs linux monotonic time (not TSC cycles) */
		stats->used = flow_table->stats[pos].used ? ovs_flow_used_time(flow_table->stats[pos].used) : 0;
	}
}

/*
 * Delete flow table entry at pos
 */
static void
flow_table_del_flow(int32_t pos)
{
	flow_table->dst_port[pos] = 0;
	flow_table->used[pos] = false;
	memset((void *)&flow_table->key[pos], 0, sizeof(struct flow_key));

	flow_table_clear_stats(pos);
}
/*
 * Send a reply message to the vswitchd
 */
static void
send_reply_to_vswitchd(struct dpdk_message *reply)
{
	int rslt = 0;
	struct rte_mbuf *mbuf = NULL;
	void *ctrlmbuf_data = NULL;
	struct client *vswd = NULL;
	struct statistics *vswd_stat = NULL;

	vswd = &clients[VSWITCHD];
	vswd_stat = &vport_stats[VSWITCHD];

	/* Preparing the buffer to send */
	mbuf = rte_ctrlmbuf_alloc(pktmbuf_pool);

	if (!mbuf) {
		RTE_LOG(WARNING, APP, "Error : Unable to allocate an mbuf : %s : %d", __FUNCTION__, __LINE__);
		switch_tx_drop++;
		vswd_stat->rx_drop++;

		return;
	}

	ctrlmbuf_data = rte_ctrlmbuf_data(mbuf);
	rte_memcpy(ctrlmbuf_data, reply, sizeof(*reply));
	rte_ctrlmbuf_len(mbuf) = sizeof(*reply);

	/* Sending the buffer to vswitchd */

	rslt = rte_ring_sp_enqueue(vswd->rx_q, mbuf);
	if (rslt < 0) {
		if (rslt == -ENOBUFS) {
			rte_ctrlmbuf_free(mbuf);
			switch_tx_drop++;
			vswd_stat->rx_drop++;
		} else {
			overruns++;
		}
	}
	vswd_stat->tx++;
}

/*
 * Send message to vswitchd indicating message type is not known
 */
static void
handle_unknown_cmd(void)
{
	struct dpdk_message reply = {0};

	reply.type = EINVAL;

	send_reply_to_vswitchd(&reply);
}

/*
 * Add or modify flow table entry.
 *
 * When modifying, the stats can be optionally cleared
 */
static void
flow_cmd_new(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int32_t pos = 0;

	pos = rte_hash_lookup(handle, &request->key);

	if (pos < 0) {
        if (request->flags & FLAG_CREATE) {
			pos = rte_hash_add_key(handle, &request->key);
			flow_table_add_flow(pos, &request->key, request->action, true);
            reply.type = 0;
		} else {
			reply.type = ENOENT;
		}
	} else {
        if (request->flags & FLAG_REPLACE) {
		/* Retrieve flow stats*/
		flow_table_get_flow(pos, NULL, NULL, &request->stats);
		/* Depending on the value of request->clear we will either update
		 * or keep the same stats
		 */
		flow_table_add_flow(pos, &request->key, request->action, request->clear);
		reply.type = 0;
	} else {
			reply.type = EEXIST;
		}
	}

	reply.flow_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Delete single flow or all flows.
 *
 * When request->key is empty delete all flows
 */
static void
flow_cmd_del(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	struct flow_key empty = {0};
	int32_t pos = 0;

	if (!memcmp(&request->key, &empty, sizeof(request->key))) {
		/* if flow is empty, delete all flows */
		for (pos = 0; pos < MAX_FLOWS; pos++) {
			if (flow_table->used[pos]) {
				rte_hash_del_key(handle, &request->key);
				flow_table_del_flow(pos);
			}
		}
		reply.type = 0;
	} else {
		/* delete specified flow */
		pos = rte_hash_del_key(handle, &request->key);

		if (pos < 0) {
			reply.type = ENOENT;
			}
		else {
			flow_table_get_flow(pos, NULL, NULL, &request->stats);
			flow_table_del_flow(pos);
			reply.type = 0;
		}
	}

	reply.flow_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Return flow entry to vswitchd if it exists
 */
static void
flow_cmd_get(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	int32_t pos = 0;

	pos = rte_hash_lookup(handle, &request->key);

	if (pos < 0) {
		reply.type = ENOENT;
	} else {
		flow_table_get_flow(pos, NULL, &request->action, &request->stats);
		reply.type = 0;
	}

	reply.flow_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Dump all flows.
 *
 * The message that is received contains the key for the previously dumped
 * flow entry. If the key is zero then we are dumping the first entry. We
 * reply with EOF when we have dumped all flows
 *
 */
static void
flow_cmd_dump(struct dpdk_flow_message *request)
{
	struct dpdk_message reply = {0};
	struct flow_key empty = {0};
	int32_t pos = 0;

	if (!memcmp(&request->key, &empty, sizeof(request->key))) {
		/*
		 * if flow is empty, it is first call of dump(),
		 * and start searching from the first rule
		 */
		pos = 0;
	} else {
		/* last dumped flow */
		pos = rte_hash_lookup(handle, &request->key);

		if (pos < 0) {
			/* send error reply - the flow must be in the flow table */
			reply.type = ENOENT;
			goto out;
		}
		/* search starting from the next flow */
		pos++;
	}

	/* find next using flow */
	for(;pos < MAX_FLOWS && !flow_table->used[pos]; pos++)
		;

	if (pos < MAX_FLOWS) {
		flow_table_get_flow(pos, &request->key, &request->action, &request->stats);
		reply.type = 0;
	} else {
		/* it was last flow, send message that no more flows here */
		reply.type = EOF;
	}

out:
	reply.flow_msg = *request;

	send_reply_to_vswitchd(&reply);
}

/*
 * Handle flow commands
 */
static void
handle_flow_cmd(struct dpdk_flow_message *request)
{
	switch (request->cmd) {
	case FLOW_CMD_NEW:
		flow_cmd_new(request);
		break;
	case FLOW_CMD_DEL:
		flow_cmd_del(request);
		break;
	case FLOW_CMD_GET:
		if (request->flags & FLAG_DUMP)
			flow_cmd_dump(request);
		else
			flow_cmd_get(request);
		break;
	default:
		handle_unknown_cmd();
	}
}

/*
 * Handle packet commands
 */
static void
handle_packet_cmd(struct dpdk_packet_message *request, struct rte_mbuf *pkt)
{
	uint32_t action = 0;

	action = request->action;

	action_forward(action, pkt);
}

/*
 * Parse message from vswitchd and send to appropriate handler
 */
static void
handle_vswitchd_cmd(struct rte_mbuf *mbuf)
{
	struct dpdk_message *request = NULL;

	request = rte_pktmbuf_mtod(mbuf, struct dpdk_message *);

	switch (request->type) {
	case FLOW_CMD_FAMILY:
		handle_flow_cmd(&request->flow_msg);
		rte_pktmbuf_free(mbuf);
		break;
	case PACKET_CMD_FAMILY:
		rte_pktmbuf_adj(mbuf, sizeof(*request));
		handle_packet_cmd(&request->packet_msg, mbuf);
		break;
	default:
		handle_unknown_cmd();
		rte_pktmbuf_free(mbuf);
	}
}

/*
 * Use pkt to update stats at entry pos in flow_table
 */
static void flow_table_update_stats(int pos, struct rte_mbuf *pkt)
{
	uint8_t tcp_flags = 0;

	if (flow_table->key[pos].ether_type == ETHER_TYPE_IPv4 &&
	    flow_table->key[pos].ip_proto == IPPROTO_TCP) {
		uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);

		pkt_data += sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);

		struct tcp_hdr *tcp = (struct tcp_hdr *) pkt_data;

		tcp_flags = tcp->tcp_flags & TCP_FLAG_MASK;
	}

	rte_spinlock_lock((rte_spinlock_t *)&flow_table->stats[pos].lock);
	flow_table->stats[pos].used = curr_tsc;
	flow_table->stats[pos].packet_count++;
	flow_table->stats[pos].byte_count += rte_pktmbuf_data_len(pkt);
	flow_table->stats[pos].tcp_flags |= tcp_flags;
	rte_spinlock_unlock((rte_spinlock_t *)&flow_table->stats[pos].lock);
}

/*
 * Do forward action
 */
static void
action_forward(uint32_t vport, struct rte_mbuf *pkt)
{
	if (IS_PHY_PORT(vport))         /* Physical port */
		send_to_port(vport, pkt);
	else if (IS_KNI_PORT(vport))    /* KNI FIFO */
		send_to_kni(vport, pkt);
	else                            /* Client ring */
		send_to_client(vport, pkt);
}

/*
 * Execute action in flow table at position pos
 */
static void
action_execute(int pos, struct rte_mbuf *pkt)
{
	uint32_t action = 0;

	action = flow_table->dst_port[pos];

	action_forward(action, pkt);
}

/*
 * Extract 13 tuple from pkt as key
 */
static void
flow_key_extract(struct rte_mbuf *pkt, uint8_t in_port, struct flow_key *key)
{
	struct ether_hdr *ether_hdr = NULL;
	struct vlan_hdr *vlan_hdr = NULL;
	struct ipv4_hdr *ipv4_hdr = NULL;
	struct tcp_hdr *tcp = NULL;
	struct udp_hdr *udp = NULL;
	unsigned char *pkt_data = NULL;
	uint16_t next_proto = 0;
	uint16_t vlan_tci = 0;

	key->in_port = in_port;

	/* Assume ethernet packet and get packet data */
	pkt_data = rte_pktmbuf_mtod(pkt, unsigned char *);
	ether_hdr = (struct ether_hdr *)pkt_data;
	pkt_data += sizeof(struct ether_hdr);

	key->ether_dst = ether_hdr->d_addr;
	key->ether_src = ether_hdr->s_addr;
	key->ether_type = rte_be_to_cpu_16(ether_hdr->ether_type);

	next_proto = key->ether_type;
	if (next_proto == ETHER_TYPE_VLAN) {
		vlan_hdr = (struct vlan_hdr *)pkt_data;
		pkt_data += sizeof(struct vlan_hdr);

		vlan_tci = rte_be_to_cpu_16(vlan_hdr->vlan_tci);
		key->vlan_id = vlan_tci & VLAN_ID_MASK;
		key->vlan_prio = vlan_tci >> VLAN_PRIO_SHIFT;

		next_proto = rte_be_to_cpu_16(vlan_hdr->eth_proto);
		next_proto = key->ether_type;
	}

	if (next_proto == ETHER_TYPE_IPv4) {
		ipv4_hdr = (struct ipv4_hdr *)pkt_data;
		pkt_data += sizeof(struct ipv4_hdr);

		key->ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
		key->ip_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
		key->ip_proto = ipv4_hdr->next_proto_id;
		key->ip_tos = ipv4_hdr->type_of_service;
		key->ip_ttl = ipv4_hdr->time_to_live;
	}

	switch (key->ip_proto) {
		case IPPROTO_TCP:
			tcp = (struct tcp_hdr *)pkt_data;
			pkt_data += sizeof(struct tcp_hdr);

			key->tran_dst_port = rte_be_to_cpu_16(tcp->dst_port);
			key->tran_src_port = rte_be_to_cpu_16(tcp->src_port);
			break;
		case IPPROTO_UDP:
			udp = (struct udp_hdr *)pkt_data;
			pkt_data += sizeof(struct udp_hdr);

			key->tran_dst_port = rte_be_to_cpu_16(udp->dst_port);
			key->tran_src_port = rte_be_to_cpu_16(udp->src_port);
			break;
		default:
			key->tran_dst_port = 0;
			key->tran_src_port = 0;
	}
}

/*
 * Print flow key to screen
 */
static void
flow_key_print(volatile struct flow_key *key)
{
	printf("key.in_port = %"PRIu32"\n", key->in_port);
	printf("key.ether_dst = "ETH_FMT"\n", ETH_ARGS(key->ether_dst.addr_bytes));
	printf("key.ether_src = "ETH_FMT"\n", ETH_ARGS(key->ether_src.addr_bytes));
	printf("key.ether_type = %"PRIx16"\n", key->ether_type);
	printf("key.vlan_id = %"PRIu16"\n", key->vlan_id);
	printf("key.vlan_prio = %"PRIu8"\n", key->vlan_prio);
	printf("key.ip_src = "IP_FMT"\n", IP_ARGS(key->ip_src));
	printf("key.ip_dst = "IP_FMT"\n", IP_ARGS(key->ip_dst));
	printf("key.ip_proto = %"PRIu8"\n", key->ip_proto);
	printf("key.ip_tos  = %"PRIx8"\n", key->ip_tos);
	printf("key.ip_ttl = %"PRIu8"\n", key->ip_ttl);
	printf("key.tran_src_port  = %"PRIu16"\n", key->tran_src_port);
	printf("key.tran_dst_port  = %"PRIu16"\n", key->tran_dst_port);
}

/* print flow table key at position pos*/
static void
flow_table_print_flow(int pos)
{
	printf("APP: flow_table->key[%d]\n", pos);
	flow_key_print(&flow_table->key[pos]);
}

/* Get CPU frequency */
static void
measure_cpu_frequency(void)
{
	uint64_t before = 0;
	uint64_t after = 0;

	/* How TSC changed in 1 second - it is the CPU frequency */
	before = rte_rdtsc();
	sleep(1);
	after = rte_rdtsc();
	cpu_freq = after - before;

	/* Round to millions */
	cpu_freq /= 1000000;
	cpu_freq *= 1000000;
}

/* Main function used by the processing threads.
 * Prints out some configuration details for the thread and then begins
 * performing packet RX and TX.
 */
static int
lcore_main(void *arg __rte_unused)
{
	unsigned i = 0;
	unsigned j = 0;
	unsigned vportid = 0;
	unsigned client = CLIENT1;
	unsigned kni_vportid = KNI0;
	uint64_t last_stats_display_tsc = 0;
	const unsigned id = rte_lcore_id();

	/* vswitchd core is used for print_stat and receive_from_vswitchd */
	if (id == vswitchd_core) {
		RTE_LOG(INFO, APP, "Print stat core is %d.\n", id);

		/* Measuring CPU frequency */
		measure_cpu_frequency();
		RTE_LOG(INFO, APP, "CPU frequency is %"PRIu64" MHz\n", cpu_freq / 1000000);

		for(;;) {
			/* handle any packets from vswitchd */
			receive_request_from_vswitchd();

			/* display stats every 'stats' sec */
			curr_tsc = rte_rdtsc();
			if ((curr_tsc - last_stats_display_tsc) / cpu_freq >= stats_display_interval
					&& stats_display_interval != 0)
			{
				last_stats_display_tsc = curr_tsc;

				do_stats_display();
			}
		}
	}
	/* client_switching_core is used process packets from client rings
	 * or fifos
	 */
	if (id == client_switching_core) {
		RTE_LOG(INFO, APP, "Client switching core is %d.\n", id);
		for (;;) {
			receive_from_client(client);
			/* move to next client and dont handle client 0*/
			if (++client == num_clients) {
				client = 1;
			}
			receive_from_kni(kni_vportid);
			/* move to next kni port */
			if (++kni_vportid == KNI0 + num_kni) {
				kni_vportid = KNI0;
			}
		}
	}

	for (i = 0; i < nb_cfg_params; i++) {
		if (id == cfg_params[i].lcore_id) {
			RTE_LOG(INFO, APP, "Port core is %d.\n", id);
			vportid = cfg_params[i].port_id;
			for (;;) {
				receive_from_port(vportid);
				flush_pkts(vportid);
			}
		}
	}

	return 0;
}

int
MAIN(int argc, char *argv[])
{
	unsigned i = 0;

	if (init(argc, argv) < 0 ) {
		RTE_LOG(INFO, APP, "Process init failed.\n");
		return -1;
	}
	RTE_LOG(INFO, APP, "Finished Process Init.\n");

	clear_stats();

	flow_table_init();

	for (i = 0; i < nb_cfg_params; i++) {
		RTE_LOG(INFO, APP, "config = %d,%d,%d\n",
		                cfg_params[i].port_id,
		                cfg_params[i].queue_id,
		                cfg_params[i].lcore_id);
	}
	RTE_LOG(INFO, APP, "nb_cfg_params = %d\n", nb_cfg_params);

	rte_eal_mp_remote_launch(lcore_main, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(i) {
		if (rte_eal_wait_lcore(i) < 0)
			return -1;
	}
	return 0;
}
