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

#include <getopt.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_memzone.h>
#include <rte_log.h>

#include <ovs-vport.h>

/* Number of packets to attempt to read from queue */
#define PKT_READ_SIZE           32u
#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1
#define ALLOC_INTERVAL          128

/* Our client port name - tells us which rx queue to read, and tx
 * queue to write to. */
static char *port_name = NULL;

/*
 * Print a usage message
 */
static void
usage(const char *progname)
{
	RTE_LOG(INFO, APP, "Usage: %s [EAL args] -- -p <port_name>\n",
	        progname);
}

/*
 * Parse the application arguments to the client app.
 */
static int
parse_app_args(int argc, char *argv[])
{
	int option_index = 0, opt = 0;
	char **argvopt = argv;
	const char *progname = NULL;

	progname = argv[0];

	while ((opt = getopt_long(argc, argvopt, "p:", NULL,
		&option_index)) != EOF){
		switch (opt) {
		case 'p':
			if (ovs_vport_is_vport_name_valid(optarg) < 0)
				return -2;
			port_name = optarg;
			break;
		default:
			usage(progname);
			return -1;
		}
	}

	return 0;
}

/*
 * Application main function - loops through
 * receiving and processing packets. Never returns
 */
int main(int argc, char *argv[])
{
	struct rte_ring *rx_ring = NULL;
	struct rte_ring *tx_ring = NULL;
	struct rte_ring *free_q = NULL;
	struct rte_ring *alloc_q = NULL;
	int retval = 0;
	void *pkts[PKT_READ_SIZE];
	void *af_pkts[PKT_READ_SIZE];
	int ret = 0;
	unsigned rx_count, free_count;
	unsigned pkts_count;
	uint32_t alloc_count = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv += retval;

	if ((retval = parse_app_args(argc, argv)) < 0)
		rte_exit(EXIT_FAILURE, "Invalid command-line arguments type %d\n", retval);

	if (ovs_vport_lookup_vport_info() == NULL)
		return -1;

	if ((tx_ring = ovs_vport_client_lookup_rx_q(port_name)) == NULL)
		return -1;

	if ((rx_ring = ovs_vport_client_lookup_tx_q(port_name)) == NULL)
		return -1;

	if ((free_q = ovs_vport_client_lookup_free_q(port_name)) == NULL)
		return -1;

	if ((alloc_q = ovs_vport_client_lookup_alloc_q(port_name)) == NULL)
		return -1;

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

	RTE_LOG(INFO, APP, "Client handling packets from port '%s'\n",
	        port_name);
	RTE_LOG(INFO, APP, "[Press Ctrl-C to quit ...]\n");

	for (;;) {
		/*Do the actual work of forwarding packets*/
		rx_count = rte_ring_count(rx_ring);
		free_count = rte_ring_free_count(free_q);

		pkts_count = RTE_MIN(rx_count, free_count);
		pkts_count = RTE_MIN(pkts_count, PKT_READ_SIZE);

		if (unlikely(pkts_count == 0))
			continue;

		ret = rte_ring_dequeue_bulk(rx_ring, pkts, pkts_count);
		if (unlikely(ret < 0))
			continue;

		ret = rte_ring_enqueue_bulk(tx_ring, pkts, pkts_count);

		/*
		 * There's a known issue with DPDK mempools per core caches being
		 * accessed from host and guest applications. Due to this we can't call
		 * rte_pktmbuf_free or rte_pktmbuf_alloc from the guest since this will
		 * trigger an access to the underlying mempool cache possibly causing
		 * memory corruption.
		 *
		 * In case we needed to free mbufs because a ring is full we basically
		 * enqueue them into a special ring that will hold "mbufs to be freed".
		 * The host app will be in charge to periodically check this ring and
		 * free all the buffers in there. Check example below.
		 *
		 * A similar issue may be found in case we needed to allocate buffers
		 * in the guest. This can be avoided if we added an "alloc" queue
		 * where the guest could retrieve packets from. The host app will
		 * be periodically allocating mbufs and adding them to this queue.
		 * Add this new queue the same way the free_q queue is added.
		 */
		if (unlikely(ret == -ENOBUFS)) {
			RTE_LOG (DEBUG, APP, "ovs_client: Couldn't transmit %u "
			        "packets to Tx ring %s;enqueuing to free ring "
				"%s\n", pkts_count, tx_ring->name, free_q->name);

			ret = rte_ring_enqueue_bulk(free_q, pkts, pkts_count);
			if (unlikely(ret == -ENOBUFS))
				RTE_LOG(DEBUG, APP, "ovs_client: couldn't "
				       "enqueue %u Tx mbufs to free queue %s\n",
				       pkts_count, free_q->name);
		}

		if (alloc_count == ALLOC_INTERVAL) {
			/*Simulate using the alloc queue*/
			free_count = rte_ring_free_count(free_q);
			pkts_count = RTE_MIN(free_count, PKT_READ_SIZE);

			if (unlikely(pkts_count == 0))
				continue;

			ret = rte_ring_dequeue_bulk(alloc_q, af_pkts,
			                            pkts_count);
			if (unlikely(ret < 0))
				continue;

			ret = rte_ring_enqueue_bulk(free_q, af_pkts,
			                            pkts_count);
			if (unlikely(ret == -ENOBUFS))
				RTE_LOG(DEBUG, APP, "ovs_client: couldn't "
				       "enqueue %u alloc mbufs to free queue "
				       "%s\n", pkts_count, free_q->name);
			alloc_count = 0;
		} else {
			alloc_count++;
		}
	}

	return 0;
}
