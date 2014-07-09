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

#include <unistd.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <rte_config.h>
#include <rte_string_fns.h>
#include <rte_port_ring.h>
#include <rte_table_hash.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_cycles.h>

#include <compiler.h>

#include "dpdk-flow-stub.h"
#include "datapath/dpdk/ovdk_pipeline.h"
#include "datapath/dpdk/ovdk_hash.h"
#include "datapath/dpdk/ovdk_flow.h"
#include "datapath/dpdk/ovdk_vport.h"
#include "datapath/dpdk/ovdk_mempools.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"

#define NO_FLAGS                        0
#define PKT_BURST_SIZE                  32u
#define EXCEPTION_PKT_BURST_SIZE        32u
#define VSWITCHD_RINGSIZE               2048
#define VSWITCHD_ALLOC_THRESHOLD        (VSWITCHD_RINGSIZE/4)
#define PIPELINE_NAME_PREFIX            "ovdk_pipeline_"

#define RTE_LOGTYPE_APP                 RTE_LOGTYPE_USER1

/* offsets to metadata in mbufs as required by rte_pipeline */
#define OVDK_PIPELINE_SIG_OFFSET        0
#define OVDK_PIPELINE_KEY_OFFSET        32
#define OVDK_PIPELINE_PORT_OFFSET       64

struct ovdk_pipeline_entry {
	struct rte_pipeline_table_entry pf_entry;    /* each flow table entry
	                                              * must have an
	                                              * rte_pipeline_table_entry
	                                              * as it is required by the
	                                              * packet framework */
	uint8_t num_actions;                         /* number of actions
	                                              * in entry */
	struct ovdk_action actions[OVDK_MAX_ACTIONS];/* actions to carry out on
	                                              * pkts matching flow */
	struct ovdk_flow_stats stats;                /* stats for flow table
	                                              * entry */
};

struct ovdk_pipeline {
	struct rte_pipeline_params params;     /* 'params' used to configure
	                                        * rte_pipeline running on this
	                                        * core. */
	struct rte_pipeline *pf_pipeline;      /* handle of rte_pipeline
	                                        * running on this core */
	uint32_t table_id;                     /* id of main hash table running
	                                        * on this pipeline */
	int dpif_socket;
};

struct ovdk_pipeline ovdk_pipeline[RTE_MAX_LCORE];


int
ovdk_pipeline_port_in_add(uint32_t vportid OVS_UNUSED, char vport_name[] OVS_UNUSED)
{
	return 0;
}

int
ovdk_pipeline_port_out_add(uint32_t vportid OVS_UNUSED)
{
	return 0;
}

int
ovdk_pipeline_port_in_del(uint32_t vportid OVS_UNUSED)
{
	return 0;
}

int
ovdk_pipeline_port_out_del(uint32_t vportid OVS_UNUSED)
{
	return 0;
}

int
ovdk_pipeline_flow_add(struct ovdk_flow_key *key, struct ovdk_action *actions,
                       uint8_t num_actions, uint64_t *flow_handle)
{
	uint64_t handle = 10; /* Arbitrary handle value */

	if (key == NULL)
		return -1;

	/* Simulate error case for coverage in ovdk_datapath_flow_new -
	 * in this case, ovdk_vport_get_out_portid failure */
	if (actions[0].data.output.port >= OVDK_MAX_VPORTS)
		return -1;

	assert(key->in_port == 1);
	assert(key->ip_src == 0x01010101);
	assert(key->ip_dst == 0x02020202);

	assert(actions != NULL);
	assert(actions[0].type == OVDK_ACTION_OUTPUT);
	assert(actions[0].data.output.port == 1);

	assert(num_actions == 1);

	*flow_handle = handle;

	return 0;
}

int
ovdk_pipeline_flow_del(struct ovdk_flow_key *key OVS_UNUSED,
                       int *key_found OVS_UNUSED,
                       struct ovdk_flow_stats *stats)
{
	/*
	 * Arbitrary values used to verify successful call to flow_del
	 * function */
	stats->packet_count = DPDK_FLOW_STUB_STATS_PACKET_COUNT ;
	stats->byte_count   = DPDK_FLOW_STUB_STATS_BYTE_COUNT;
	stats->used         = DPDK_FLOW_STUB_STATS_USED;
	stats->tcp_flags    = DPDK_FLOW_STUB_STATS_TCP_FLAGS;
	return 0;
}

int ovdk_pipeline_flow_get_actions(struct ovdk_action *actions,
                                uint8_t *num_actions,
                                uint64_t flow_handle OVS_UNUSED)
{
	actions[0].type = 0;
	actions[1].type = 0;
	*num_actions = 2;
	return 0;
}

int
ovdk_pipeline_flow_get_stats(struct ovdk_flow_stats *stats,
	                     uint64_t flow_handle OVS_UNUSED)
{
	/*
	 * Arbitrary values used to verify successful call to flow_del
	 * function */
	stats->packet_count = DPDK_FLOW_STUB_STATS_PACKET_COUNT ;
	stats->byte_count   = DPDK_FLOW_STUB_STATS_BYTE_COUNT;
	stats->used         = DPDK_FLOW_STUB_STATS_USED;
	stats->tcp_flags    = DPDK_FLOW_STUB_STATS_TCP_FLAGS;
	return 0;
}

int
ovdk_pipeline_flow_set_stats(struct ovdk_flow_stats *stats OVS_UNUSED,
	                     uint64_t flow_handle OVS_UNUSED)
{
	return 0;
}

void
measure_cpu_frequency() {
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
