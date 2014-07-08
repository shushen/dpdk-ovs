/*-
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

/*
 * ovdk_stats maintains statistics associated with the dataplane.
 *
 * Currently ovdk_stats maintains only port statistics (flow statistics are
 * maintained separately).
 *
 * All stats are maintained in huge pages to improve performance and to
 * make it easy to share with other processes if required
 */

#include <string.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_ethdev.h>

#include "ovdk_stats.h"
#include "ovdk_mempools.h"
#include "ovdk_vport_types.h"
#include "ovdk_vport_info.h"
#include "ovdk_vport.h"
#include "ovdk_datapath_messages.h"

#define NO_FLAGS                0
#define MZ_STATS_INFO           "OVDK_stats_info"
#define VPORT_STATS_SIZE        (sizeof(struct vport_statistics) * \
                                 OVDK_MAX_VPORTS + \
                                 sizeof(struct vswitch_statistics))
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NUM_BYTES_MAC_ADDR  6
#define MAC_ADDR_STR_INT_L  3
#define NEWLINE_CHAR_OFFSET 1

/*
 * Port statistics structure
 *
 * This structure stores statistics for any data or control interface
 * entering or exiting a core.
 */
struct ovdk_stats_port_lcore_statistics {
	uint64_t rx;      /* number of pkts received on this port */
	uint64_t tx;      /* number of pkts transmitted by this port */
	uint64_t rx_drop; /* number of pkts dropped on reception */
	uint64_t tx_drop; /* number of pkts dropped on transmission */
	uint64_t overrun; /* number of overruns on transmission */
} __rte_cache_aligned;

/*
 * vPort statistics structure
 *
 * This structure stores the statistics of a single data interface (a vport)
 * for all available lcores.
 *
 * An array of these structs will be used to store statistics for all vports.
 */
struct vport_statistics {
	struct ovdk_stats_port_lcore_statistics stats[RTE_MAX_LCORE];
};

/*
 * vswitch statistics structure
 *
 * This structure stores the statistics of the vswitch's slow path and control
 * interfaces for all available lcores.
 */
struct vswitch_statistics {
	struct ovdk_stats_port_lcore_statistics data_stats[RTE_MAX_LCORE];
	struct ovdk_stats_port_lcore_statistics control_stats[RTE_MAX_LCORE];
};

static struct vport_statistics *vport_stats[OVDK_MAX_VPORTS] = {NULL};
static struct vswitch_statistics *vswitch_stats = NULL;

static const char *get_printable_mac_addr(uint8_t port);

/*
 * Set all statistic values to zero.
 */
inline void
ovdk_stats_clear(void)
{
	ovdk_stats_vswitch_clear();
	ovdk_stats_vport_clear_all();
}

/*
 * Set vswitch statistic values to zero.
 */
inline void
ovdk_stats_vswitch_clear(void)
{
	memset(vswitch_stats, 0, sizeof(*vswitch_stats));
}

/*
 * Set a vport's statistic values to zero.
 */
inline void
ovdk_stats_vport_clear(unsigned vportid)
{
	memset(vport_stats[vportid], 0, sizeof(*vport_stats[vportid]));
}

/*
 * Set all vport statistics to zero.
 */
void
ovdk_stats_vport_clear_all(void)
{
	unsigned vportid = 0;

	for (vportid = 0; vportid < OVDK_MAX_VPORTS; vportid++) {
		ovdk_stats_vport_clear(vportid);
	}
}

/*
 * vPort statistics getters/setters
 */

/*
 * Increment 'rx' statistics for a given vport
 */
inline void
ovdk_stats_vport_rx_increment(unsigned vportid, unsigned inc)
{
	vport_stats[vportid]->stats[rte_lcore_id()].rx += inc;
}

/*
 * Increment 'rx drop' statistics for a given vport
 */
inline void
ovdk_stats_vport_rx_drop_increment(unsigned vportid, unsigned inc)
{
	vport_stats[vportid]->stats[rte_lcore_id()].rx_drop += inc;
}

/*
 * Increment 'tx' statistics for a given vport
 */
inline void
ovdk_stats_vport_tx_increment(unsigned vportid, unsigned inc)
{
	vport_stats[vportid]->stats[rte_lcore_id()].tx += inc;
}

/*
 * Increment 'tx drop' statistics for a given vport
 */
inline void
ovdk_stats_vport_tx_drop_increment(unsigned vportid, unsigned inc)
{
	vport_stats[vportid]->stats[rte_lcore_id()].tx_drop += inc;
}

/*
 * Increment 'overrun' statistics for a given vport
 */
inline void
ovdk_stats_vport_overrun_increment(unsigned vportid, unsigned inc)
{
	vport_stats[vportid]->stats[rte_lcore_id()].overrun += inc;
}

/*
 * Get total 'rx' statistics for a given vport.
 *
 * Sum the 'rx' statistics from each lcore for a given vport, and return
 * the total.
 */
inline uint64_t
ovdk_stats_vport_rx_get(unsigned vportid)
{
	uint64_t rx;
	int i;

	for (rx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx += vport_stats[vportid]->stats[i].rx;

	return rx;
}

/*
 * Get total 'rx drop' statistics for a given vport.
 *
 * Sum the 'rx drop' statistics from each lcore for a given vport, and return
 * the total.
 */
inline uint64_t
ovdk_stats_vport_rx_drop_get(unsigned vportid)
{
	uint64_t rx_drop;
	int i;

	for (rx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx_drop += vport_stats[vportid]->stats[i].rx_drop;

	return rx_drop;
}

/*
 * Get total 'tx' statistics for a given vport.
 *
 * Sum the 'tx' statistics from each lcore for a given vport, and return
 * the total.
 */
inline uint64_t
ovdk_stats_vport_tx_get(unsigned vportid)
{
	uint64_t tx;
	int i;

	for (tx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx += vport_stats[vportid]->stats[i].tx;

	return tx;
}

/*
 * Get total 'tx drop' statistics for a given vport.
 *
 * Sum the 'tx drop' statistics from each lcore for a given vport, and return
 * the total.
 */
inline uint64_t
ovdk_stats_vport_tx_drop_get(unsigned vportid)
{
	uint64_t tx_drop;
	int i;

	for (tx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx_drop += vport_stats[vportid]->stats[i].tx_drop;

	return tx_drop;
}

/*
 * Get total 'overrun' statistics for a given vport.
 *
 * Sum the 'overrun' statistics from each lcore for a given vport, and return
 * the total.
 */
inline uint64_t
ovdk_stats_vport_overrun_get(unsigned vportid)
{
	uint64_t overrun;
	int i;

	for (overrun = 0, i = 0; i < RTE_MAX_LCORE; i++)
		overrun += vport_stats[vportid]->stats[i].overrun;

	return overrun;
}

/*
 * Get total statistics for a given vport.
 *
 * Sum all statistics from each lcore for a given vport, and return
 * the as an outparam. Return 0 if successful, else -1.
 */
inline int
ovdk_stats_vport_get(unsigned vportid, struct ovdk_port_stats *stats)
{
	if (stats == NULL)
		return -1;

	stats->rx = ovdk_stats_vport_rx_get(vportid);
	stats->tx = ovdk_stats_vport_tx_get(vportid);
	stats->rx_drop = ovdk_stats_vport_rx_drop_get(vportid);
	stats->tx_drop = ovdk_stats_vport_tx_drop_get(vportid);

	return 0;
}

/*
 * vswitch slow path message statistics getters/setters
 */

/*
 * Increment 'rx' statistics for the vswitch slow path interface
 */
inline void
ovdk_stats_vswitch_data_rx_increment(unsigned inc)
{
	vswitch_stats->data_stats[rte_lcore_id()].rx += inc;
}

/*
 * Increment 'rx drop' statistics for the vswitch slow path interface
 */
inline void
ovdk_stats_vswitch_data_rx_drop_increment(unsigned inc)
{
	vswitch_stats->data_stats[rte_lcore_id()].rx_drop += inc;
}

/*
 * Increment 'tx' statistics for the vswitch slow path interface
 */
inline void
ovdk_stats_vswitch_data_tx_increment(unsigned inc)
{
	vswitch_stats->data_stats[rte_lcore_id()].tx += inc;
}

/*
 * Increment 'tx drop' statistics for the vswitch slow path interface
 */
inline void
ovdk_stats_vswitch_data_tx_drop_increment(unsigned inc)
{
	vswitch_stats->data_stats[rte_lcore_id()].tx_drop += inc;
}

/*
 * Increment 'overrun' statistics for the vswitch slow path interface
 */
inline void
ovdk_stats_vswitch_data_overrun_increment(unsigned inc)
{
	vswitch_stats->data_stats[rte_lcore_id()].overrun += inc;
}

/*
 * Get total 'rx' statistics for vswitch slow path interface.
 *
 * Sum the 'rx' statistics from each lcore for the vswitch data
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_data_rx_get(void)
{
	uint64_t rx;
	int i;

	for (rx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx += vswitch_stats->data_stats[i].rx;

	return rx;
}

/*
 * Get total 'rx drop' statistics for vswitch slow path interface.
 *
 * Sum the 'rx drop' statistics from each lcore for the vswitch data
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_data_rx_drop_get(void)
{
	uint64_t rx_drop;
	int i;

	for (rx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx_drop += vswitch_stats->data_stats[i].rx_drop;

	return rx_drop;
}

/*
 * Get total 'tx' statistics for vswitch slow path interface.
 *
 * Sum the 'tx' statistics from each lcore for the vswitch data
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_data_tx_get(void)
{
	uint64_t tx;
	int i;

	for (tx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx += vswitch_stats->data_stats[i].tx;

	return tx;
}

/*
 * Get total 'tx drop' statistics for vswitch slow path interface.
 *
 * Sum the 'tx drop' statistics from each lcore for the vswitch data
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_data_tx_drop_get(void)
{
	uint64_t tx_drop;
	int i;

	for (tx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx_drop += vswitch_stats->data_stats[i].tx_drop;

	return tx_drop;
}

/*
 * Get total 'overrun' statistics for vswitch slow path interface.
 *
 * Sum the 'overrun' statistics from each lcore for the vswitch data
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_data_overrun_get(void)
{
	uint64_t overrun;
	int i;

	for (overrun = 0, i = 0; i < RTE_MAX_LCORE; i++)
		overrun += vswitch_stats->data_stats[i].overrun;

	return overrun;
}

/*
 * vswitch control message statistics getters/setters
 */

/*
 * Increment 'rx' statistics for the vswitchd control interface
 */
inline void
ovdk_stats_vswitch_control_rx_increment(unsigned inc)
{
	vswitch_stats->control_stats[rte_lcore_id()].rx += inc;
}

/*
 * Increment 'rx drop' statistics for the vswitchd control interface
 */
inline void
ovdk_stats_vswitch_control_rx_drop_increment(unsigned inc)
{
	vswitch_stats->control_stats[rte_lcore_id()].rx_drop += inc;
}

/*
 * Increment 'tx' statistics for the vswitchd control interface
 */
inline void
ovdk_stats_vswitch_control_tx_increment(unsigned inc)
{
	vswitch_stats->control_stats[rte_lcore_id()].tx += inc;
}

/*
 * Increment 'tx drop' statistics for the vswitchd control interface
 */
inline void
ovdk_stats_vswitch_control_tx_drop_increment(unsigned inc)
{
	vswitch_stats->control_stats[rte_lcore_id()].tx_drop += inc;
}

/*
 * Increment 'overrun' statistics for the vswitchd control interface
 */
inline void
ovdk_stats_vswitch_control_overrun_increment(unsigned inc)
{
	vswitch_stats->control_stats[rte_lcore_id()].overrun += inc;
}

/*
 * Get total 'rx' statistics for vswitch control interface.
 *
 * Sum the 'rx' statistics from each lcore for the vswitch control
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_control_rx_get(void)
{
	uint64_t rx;
	int i;

	for (rx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx += vswitch_stats->control_stats[i].rx;

	return rx;
}

/*
 * Get total 'rx drop' statistics for vswitch control interface.
 *
 * Sum the 'rx drop' statistics from each lcore for the vswitch control
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_control_rx_drop_get(void)
{
	uint64_t rx_drop;
	int i;

	for (rx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		rx_drop += vswitch_stats->control_stats[i].rx_drop;

	return rx_drop;
}

/*
 * Get total 'tx' statistics for vswitch control interface.
 *
 * Sum the 'tx' statistics from each lcore for the vswitch control
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_control_tx_get(void)
{
	uint64_t tx;
	int i;

	for (tx = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx += vswitch_stats->control_stats[i].tx;

	return tx;
}

/*
 * Get total 'tx drop' statistics for vswitch control interface.
 *
 * Sum the 'tx drop' statistics from each lcore for the vswitch control
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_control_tx_drop_get(void)
{
	uint64_t tx_drop;
	int i;

	for (tx_drop = 0, i = 0; i < RTE_MAX_LCORE; i++)
		tx_drop += vswitch_stats->control_stats[i].tx_drop;

	return tx_drop;
}

/*
 * Get total 'overrun' statistics for vswitch control interface.
 *
 * Sum the 'overrun' statistics from each lcore for the vswitch control
 * interface, and return the total.
 */
inline uint64_t
ovdk_stats_vswitch_control_overrun_get(void)
{
	uint64_t overrun;
	int i;

	for (overrun = 0, i = 0; i < RTE_MAX_LCORE; i++)
		overrun += vswitch_stats->control_stats[i].overrun;

	return overrun;
}

/*
 * Wrapper for 'tx increment' function.
 *
 * Wrap the 'tx increment' function so that it can be used by the 'rte_port'
 * action handlers.
 */
int
ovdk_stats_port_out_update(struct rte_mbuf **pkts __rte_unused,
                           uint64_t *pkts_mask,
                           void *arg)
{
	unsigned n = __builtin_popcount(*pkts_mask);  /* count bits set */
	uint32_t vportid = *(uint32_t *)arg;

	ovdk_stats_vport_tx_increment(vportid, n);

	return 0;
}

/*
 * Returns MAC address for port in a string
 */
static const char *
get_printable_mac_addr(uint8_t port)
{
	static const char err_address[] = "00:00:00:00:00:00";
	static char addresses[RTE_MAX_ETHPORTS][sizeof(err_address)] = {{0}};
	struct ether_addr mac = {{0}};
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
void
ovdk_stats_display(void)
{
	unsigned i = 0;
	int error = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE];
	/* ANSI escape sequences for terminal display.
	 * 27 = ESC, 2J = Clear screen */
	const char clr[] = {27, '[', '2', 'J', '\0'};
	/* H = Home position for cursor*/
	const char topLeft[] = {27, '[', '1', ';', '1', 'H','\0'};
	uint64_t overruns = 0;
	struct rte_mempool *pktmbuf_pool = NULL;
	struct rte_mempool *ctrlmbuf_pool = NULL;

	pktmbuf_pool = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	ctrlmbuf_pool = rte_mempool_lookup(CTRLMBUF_POOL_NAME);

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("Physical Ports\n");
	printf("-----\n");
	for (i = 0; i < OVDK_MAX_PHYPORTS; i++) {
		error = ovdk_vport_get_port_name(i, name);
		if (error || name[0] == '\0')
			continue;
		printf("Port %u: '%s'\n", i,
				get_printable_mac_addr(i));
	}
	printf("\n\n");

	printf("\nVport Statistics\n"
	       "=============   ============  ============  ============  ============\n"
	       "Interface       rx_packets    rx_dropped    tx_packets    tx_dropped  \n"
	       "-------------   ------------  ------------  ------------  ------------\n");
	printf("%-*.*s ", 13, 13, "vSwitchD data");
	printf("%13"PRIu64" %13"PRIu64" %13"PRIu64" %13"PRIu64"\n",
	       ovdk_stats_vswitch_data_rx_get(),
	       ovdk_stats_vswitch_data_rx_drop_get(),
	       ovdk_stats_vswitch_data_tx_get(),
	       ovdk_stats_vswitch_data_tx_drop_get());

	printf("%-*.*s ", 13, 13, "vSwitchD control");
	printf("%13"PRIu64" %13"PRIu64" %13"PRIu64" %13"PRIu64"\n",
	       ovdk_stats_vswitch_control_rx_get(),
	       ovdk_stats_vswitch_control_rx_drop_get(),
	       ovdk_stats_vswitch_control_tx_get(),
	       ovdk_stats_vswitch_control_tx_drop_get());

	for (i = 0; i < OVDK_MAX_VPORTS; i++) {
		error = ovdk_vport_get_port_name(i, name);
		if (error || name[0] == '\0')
			continue;
		printf("%-*.*s ", 13, 13, name);
		printf("%13"PRIu64" %13"PRIu64" %13"PRIu64" %13"PRIu64"\n",
		       ovdk_stats_vport_rx_get(i),
		       ovdk_stats_vport_rx_drop_get(i),
		       ovdk_stats_vport_tx_get(i),
		       ovdk_stats_vport_tx_drop_get(i));

		overruns += ovdk_stats_vport_overrun_get(i);
	}
	printf("=============   ============  ============  ============  ============\n");

	printf("\n Switch control rx dropped %lu\n", ovdk_stats_vswitch_control_rx_drop_get());
	printf("\n Switch control tx dropped %lu\n", ovdk_stats_vswitch_control_tx_drop_get());
	printf("\n Switch data rx dropped %lu\n", ovdk_stats_vswitch_data_rx_drop_get());
	printf("\n Switch data tx dropped %lu\n", ovdk_stats_vswitch_data_tx_drop_get());
	printf("\n Queue overruns    %lu\n",  overruns);
	printf("\n Pkt Mempool count     %9u\n", rte_mempool_count(pktmbuf_pool));
	printf("\n Ctrl Mempool count     %9u\n", rte_mempool_count(ctrlmbuf_pool));
	printf("\n");
}

/*
 * Initialise memzones and structs required by statistics.
 */
void
ovdk_stats_init(void)
{
	const struct rte_memzone *mz = NULL;
	unsigned vportid = 0;

	/* set up array for statistics */
	mz = rte_memzone_reserve(MZ_STATS_INFO, VPORT_STATS_SIZE,
	                         rte_socket_id(), NO_FLAGS);
	if (mz == NULL)
		rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for"
		         " statistics\n");

	memset(mz->addr, 0, VPORT_STATS_SIZE);

	for (vportid = 0; vportid < OVDK_MAX_VPORTS; vportid++) {
		vport_stats[vportid] = (struct vport_statistics *)(void *)(
		                        (char *)mz->addr + vportid *
		                        sizeof(struct vport_statistics));
	}

	vswitch_stats = (void *)((char *)mz->addr +
	                 OVDK_MAX_VPORTS * sizeof(struct vport_statistics));
}
