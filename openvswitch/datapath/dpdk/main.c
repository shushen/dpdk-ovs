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
#include <rte_kni.h>
#include <rte_cycles.h>

#include "kni.h"
#include "args.h"
#include "init.h"
#include "main.h"
#include "vport.h"
#include "stats.h"
#include "flow.h"
#include "datapath.h"
#include "action.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/*
 * When reading/writing to/from rings/ports use this batch size
 */
#define PKT_BURST_SIZE      32
#define PREFETCH_OFFSET     3
#define RX_RING_SIZE        128
#define TX_RING_SIZE        512
#define RING_SIZE           (PKT_BURST_SIZE * 8)
#define BYTES_TO_PRINT      256
#define BURST_TX_DRAIN_US   (100) /* TX drain every ~100us */
#define RUN_ON_THIS_THREAD  1

extern struct cfg_params *cfg_params;
extern uint16_t nb_cfg_params;

static void flush_pkts(unsigned);
void switch_packet(struct rte_mbuf *pkt, uint8_t in_port);

/*
 * This function takes a packet and routes it as per the flow table.
 */
void
switch_packet(struct rte_mbuf *pkt, uint8_t in_port)
{
	int rc = 0;
	struct dpdk_upcall info = {0};
	enum action_type type = ACTION_NULL;
	struct action_output output = {0};

	flow_key_extract(pkt, in_port, &info.key);

	rc = flow_table_get_flow(&info.key, &type, &output, NULL);
	if (rc >= 0) {
		flow_table_update_stats(&info.key, pkt);
		action_execute(type, &output, pkt);
	} else {
		/* flow table miss, send unmatched packet to the daemon */
		info.cmd = PACKET_CMD_MISS;
		send_packet_to_vswitchd(pkt, &info);
	}
}

static inline void
do_vswitchd(void)
{
	static uint64_t last_stats_display_tsc = 0;

	/* handle any packets from vswitchd */
	handle_request_from_vswitchd();

	/* display stats every 'stats' sec */
	curr_tsc = rte_rdtsc();
	if ((curr_tsc - last_stats_display_tsc) / cpu_freq >= stats_display_interval
	              && stats_display_interval != 0)
	{
		last_stats_display_tsc = curr_tsc;

		do_stats_display();
	}
}

static inline void
do_client_switching(void)
{
	static unsigned client = CLIENT1;
	static unsigned kni_vportid = KNI0;
	int rx_count = 0;
	int j = 0;
	struct rte_mbuf *bufs[PKT_BURST_SIZE] = {0};

	rx_count = receive_from_client(client, &bufs[0]);

	/* Prefetch first packets */
	for (j = 0; j < PREFETCH_OFFSET && j < rx_count; j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[j], void *));
	}

	/* Prefetch and forward already prefetched packets */
	for (j = 0; j < (rx_count - PREFETCH_OFFSET); j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[
					j + PREFETCH_OFFSET], void *));
		switch_packet(bufs[j], client);
	}

	/* Forward remaining prefetched packets */
	for (; j < rx_count; j++) {
		switch_packet(bufs[j], client);
	}

	/* move to next client and dont handle client 0*/
	if (++client == num_clients) {
		client = 1;
	}

	rx_count = receive_from_kni(kni_vportid, &bufs[0]);

	/* Prefetch first packets */
	for (j = 0; j < PREFETCH_OFFSET && j < rx_count; j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[j], void *));
	}

	/* Prefetch and forward already prefetched packets */
	for (j = 0; j < (rx_count - PREFETCH_OFFSET); j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[
					j + PREFETCH_OFFSET], void *));
		switch_packet(bufs[j], kni_vportid);
	}

	/* Forward remaining prefetched packets */
	for (; j < rx_count; j++) {
		switch_packet(bufs[j], kni_vportid);
	}

	/* move to next kni port */
	if (++kni_vportid == KNI0 + num_kni) {
		kni_vportid = KNI0;
	}

}

static inline void
do_port_switching(unsigned vportid)
{
	int rx_count = 0;
	int j = 0;
	struct rte_mbuf *bufs[PKT_BURST_SIZE] = {0};

	rx_count = receive_from_port(vportid, &bufs[0]);

	/* Prefetch first packets */
	for (j = 0; j < PREFETCH_OFFSET && j < rx_count; j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[j], void *));
	}

	/* Prefetch and forward already prefetched packets */
	for (j = 0; j < (rx_count - PREFETCH_OFFSET); j++) {
		rte_prefetch0(rte_pktmbuf_mtod(bufs[
					j + PREFETCH_OFFSET], void *));
		switch_packet(bufs[j], vportid);
	}

	/* Forward remaining prefetched packets */
	for (; j < rx_count; j++) {
		switch_packet(bufs[j], vportid);
	}

	flush_pkts(vportid);
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
	s->tx += sent;
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
	const unsigned id = rte_lcore_id();
	unsigned nr_vswitchd = 0;
	unsigned nr_client_switching = 0;
	unsigned nr_port_switching = 0;
	unsigned portid_map[MAX_PHYPORTS] = {0};

	/* vswitchd core is used for print_stat and receive_from_vswitchd */
	if (id == vswitchd_core) {
		RTE_LOG(INFO, APP, "Print stat core is %d.\n", id);

		/* Measuring CPU frequency */
		measure_cpu_frequency();
		RTE_LOG(INFO, APP, "CPU frequency is %"PRIu64" MHz\n", cpu_freq / 1000000);
		nr_vswitchd = RUN_ON_THIS_THREAD;
	}
	/* client_switching_core is used process packets from client rings
	 * or fifos
	 */
	if (id == client_switching_core) {
		RTE_LOG(INFO, APP, "Client switching core is %d.\n", id);
		nr_client_switching = RUN_ON_THIS_THREAD;
	}

	for (i = 0; i < nb_cfg_params; i++) {
		if (id == cfg_params[i].lcore_id) {
			RTE_LOG(INFO, APP, "Port core is %d.\n", id);
			portid_map[nr_port_switching++] = cfg_params[i].port_id;
		}
	}

	for (;;) {
		if (nr_vswitchd)
			do_vswitchd();
		if (nr_client_switching)
			do_client_switching();
		for (i = 0; i < nr_port_switching; i++)
			do_port_switching(portid_map[i]);
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
