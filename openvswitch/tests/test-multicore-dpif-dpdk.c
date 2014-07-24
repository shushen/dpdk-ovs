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

#include <config.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_byteorder.h>

#include "config.h"
#include "command-line.h"
#include "timeval.h"
#include "util.h"
#include "vlog.h"
#include "datapath/dpdk/ovdk_datapath_messages.h"
#include "datapath/dpdk/ovdk_action_types.h"
#include "dpif-dpdk-flow-table.h"
#include "dpif-dpdk-vport-table.h"
#include "datapath/dpdk/ovdk_vport_types.h"
#include "dpif.h"
#include "dpif-provider.h"
#include "dpif-dpdk.h"
#include "dpdk-link.h"
#include "netdev-provider.h"
#include "netdev-dpdk.h"
#include "netlink.h"
#include "ovs-thread.h"
#include "dpdk-ring-stub.h"
#include "dpdk-flow-stub.h"
#include "odp-util.h"

#define MAX_OPS        10
#define MAX_ACTIONS    32

static struct dpif dpif;
static struct dpif *dpif_p = &dpif;

void test_multicore_dpif_dpdk_add(int argc OVS_UNUSED,
										char *argv[] OVS_UNUSED);
void test_multicore_dpif_dpdk_del(int argc OVS_UNUSED,
										char *argv[] OVS_UNUSED);
void test_multicore_dpif_dpdk_getstats(int argc OVS_UNUSED,
										char *argv[] OVS_UNUSED);
void test_multicore_dpif_dpdk_recv(int argc OVS_UNUSED,
										char *argv[] OVS_UNUSED);

static int find_next_add_pipeline(void);
static int peek_next_add_pipeline(void);
static int num_pipelines(void);

static unsigned last_add_found = RTE_MAX_LCORE - 1;

/* Bitmask of enabled cores determined by call to dpdk_link_init */
uint64_t pipeline_mask = 0xF0A00007C00A0001;

/*
 * Multi-Queue Tests e.g. multiple lcores/pipeline_id's
 */

static void
multiqueue_success_replys(void) {

	unsigned pipeline_id = 0;
	int result = -1;
	struct ovdk_message reply = {0};

	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {
			create_dpdk_port_reply(&reply, 0);
			result = enqueue_reply_on_reply_ring(reply, pipeline_id);
			assert(result == 0);
		}
	}
}

static void
multiqueue_add_check_port_requests (unsigned pipeline_id,
                                    uint32_t vport_id,
                                    enum ovdk_vport_type type) {

	unsigned i = 0;
	int result = -1;
	struct ovdk_message *request = NULL;

	for (i = 0; i < NUM_CORES; i++) {
		if (1 & (pipeline_mask >> i)) {
			if (i == pipeline_id) {
				/* Check the in_port/out_port request */
				result = dequeue_request_from_request_ring(&request, pipeline_id);
				assert(result == 0);
				assert(request->type == OVDK_VPORT_CMD_FAMILY);
				assert(request->vport_msg.cmd == VPORT_CMD_NEW);
				assert(request->vport_msg.flags == VPORT_FLAG_INOUT_PORT );
				assert(request->vport_msg.vportid == vport_id);
				assert(strncmp(request->vport_msg.port_name, "example_name", 12) == 0);
				assert(request->vport_msg.type == type);
			} else {
				result = dequeue_request_from_request_ring(&request, i);
				assert(result == 0);
				assert(request->type == OVDK_VPORT_CMD_FAMILY);
				assert(request->vport_msg.cmd == VPORT_CMD_NEW);
				assert(request->vport_msg.flags == VPORT_FLAG_OUT_PORT );
				assert(request->vport_msg.vportid == vport_id);
				assert(strncmp(request->vport_msg.port_name, "example_name", 12) == 0);
				assert(request->vport_msg.type == type);
			}
		}
	}
}

void
test_multicore_dpif_dpdk_add(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct netdev netdev;
	struct netdev_class netdev_class;
	odp_port_t port_no = -1;
	uint16_t pipeline_id = 0;
	uint16_t initial_pipeline = 0;
	int i = 0;
	enum ovdk_vport_type vport_type = OVDK_VPORT_TYPE_DISABLED;

	netdev.netdev_class = &netdev_class;
	netdev_class.type = "dpdkclient";
	netdev.name = "example_name";

	vport_type = OVDK_VPORT_TYPE_CLIENT;
	/*
	 * Check that load balancing is done and that all pipelines in turn receive
	 * a in_port message. Also check that all cores receive an out_port message.
	 */
	initial_pipeline = NUM_CORES;

	do {
		pipeline_id = find_next_add_pipeline();
		/* Set the initial_pipeline on the first loop*/
		if (initial_pipeline == NUM_CORES) {
			initial_pipeline = pipeline_id;
		}

		/* Add successful replies for port msgs */
		multiqueue_success_replys();

		/* Add the ports */
		port_no = OVDK_VPORT_TYPE_CLIENT + i;
		result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
		assert(result == 0);
		assert(port_no == OVDK_VPORT_TYPE_CLIENT + i);

		/* Check the generated requests for the in_port/out_port and out_ports*/
		multiqueue_add_check_port_requests(pipeline_id, port_no, vport_type);

		/*
		 * Increment variable used for name and port_no and move to different
		 * type if required (we only support x many Clients)
		 */
		if (++i >= OVDK_MAX_CLIENTS) {
			netdev_class.type = "dpdkvhost";
			vport_type = OVDK_VPORT_TYPE_VHOST;
		}

	} while(peek_next_add_pipeline() != initial_pipeline);


	/*
	 * We have now looped through every available pipeline - do one more test
	 * to ensure that rollover is occurring correctly
	 */

	/*
	 * We may have gone over may available Clients and VHOST,
	 * so switch type to KNI
	 */
	if (i >= OVDK_MAX_CLIENTS+OVDK_MAX_VHOSTS) {
		netdev_class.type = "dpdkkni";
		vport_type = OVDK_VPORT_TYPE_KNI;
	}

	pipeline_id = find_next_add_pipeline();

	/* Add successful replies for port msgs */
	multiqueue_success_replys();

	/* Add the ports */
	port_no = OVDK_VPORT_TYPE_CLIENT + i;
	result = dpif_p->dpif_class->port_add(dpif_p, &netdev, &port_no);
	if (result != 0)
		i++;
	assert(result == 0);
	assert(port_no == OVDK_VPORT_TYPE_CLIENT + i);

	/* Check the generated requests for the in_port/out_port and out_ports*/
	multiqueue_add_check_port_requests(pipeline_id, port_no, vport_type);

}

static void
multiqueue_getstats_replys(unsigned pipeline_id)
{
	struct ovdk_message reply = {0};
	int result = -1;

	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {
			create_dpdk_port_reply(&reply, 0);
			reply.vport_msg.stats.rx = 0xDEADBEEF;
			reply.vport_msg.stats.tx = 3;
			reply.vport_msg.stats.rx_bytes = 0xDEADBEEF;
			reply.vport_msg.stats.tx_bytes = 3;
			reply.vport_msg.stats.rx_drop = 0xDEADBEEF;
			reply.vport_msg.stats.tx_drop = 3;
			reply.vport_msg.stats.rx_error = 0xDEADBEEF;
			reply.vport_msg.stats.tx_error = 3;
			result = enqueue_reply_on_reply_ring(reply, pipeline_id);
			assert(result == 0);
		}
	}
}

static void
multiqueue_getstats_check_stats(struct ovdk_port_stats stats)
{
	int expected_tx_val = (num_pipelines() * 3);
	uint64_t expected_rx_val = (((uint64_t)num_pipelines()) * 0xDEADBEEF);

	assert(stats.rx == expected_rx_val);
	assert(stats.tx == expected_tx_val);
	assert(stats.rx_bytes == UINT64_MAX);
	assert(stats.tx_bytes == UINT64_MAX);
	assert(stats.rx_drop == expected_rx_val);
	assert(stats.tx_drop == expected_tx_val);
	assert(stats.rx_error == UINT64_MAX);
	assert(stats.tx_error == UINT64_MAX);
}


void
test_multicore_dpif_dpdk_getstats(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{

	int result = -1;
	odp_port_t port_no = -1;
	uint16_t pipeline_id = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "port_example_%02u";
	int i = 0;
	struct ovdk_port_stats stats = {0};
	enum ovdk_vport_type type = 0;

	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {

			snprintf(name, sizeof(name), name, i++);

			/* If there are more than 32 pipelines active, we add additional
			 * ports as Vhost ports, as the maximum KNI ports we can add is 32.
			 */
			if (i <= 32) {
				type = OVDK_VPORT_TYPE_KNI;
				port_no = OVDK_VPORT_TYPE_KNI;
			}
			else {
				type = OVDK_VPORT_TYPE_VHOST;
				port_no = OVDK_VPORT_TYPE_VHOST;
			}
			result = dpif_dpdk_vport_table_entry_add(type,
													pipeline_id, name,
													&port_no);
			assert(result == 0);

			multiqueue_getstats_replys(pipeline_id);

			result = dpif_dpdk_port_get_stats(&name[0], &stats);
			assert(result == 0);

			multiqueue_getstats_check_stats(stats);
		}
	}
}

static void
multiqueue_del_check_requests(void) {

	unsigned i = 0;
	int result = -1;
	struct ovdk_message *request = NULL;

	for (i = 0; i < NUM_CORES; i++) {
		if (1 & (pipeline_mask >> i)) {
			result = dequeue_request_from_request_ring(&request, i);
			assert(result == 0);
			assert(request->vport_msg.cmd == VPORT_CMD_DEL);
			assert(request->vport_msg.flags == 0 );
			assert(request->vport_msg.vportid == OVDK_VPORT_TYPE_KNI);
		}
	}
}

void
test_multicore_dpif_dpdk_del(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{

	uint16_t pipeline_id = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "kni_example_%02u";
	int i = 0;
	uint32_t port_no = 0;
	int result = -1;

	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {

			snprintf(name, sizeof(name), "kni_example_%02u", i++);
			/* build fake successful replys */
			multiqueue_success_replys();

			port_no = OVDK_VPORT_TYPE_KNI;
			result = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_KNI, pipeline_id, name, &port_no);
			assert(result == 0);
			result = dpif_p->dpif_class->port_del(dpif_p, port_no);
			assert(result == 0);

			multiqueue_del_check_requests();

			/*
			 * check entry was correctly removed from the table
			 * (table should be empty)
			 */
			result = dpif_dpdk_vport_table_entry_get_first_inuse(&port_no);
			assert(result == -ENOENT);
		}
	}
}

static int
num_pipelines(void)
{
	unsigned i = 0;
	int num_pipelines = 0;

	for (i = 0; i < NUM_CORES; i++) {
		if (1 & (pipeline_mask >> i)) {
			num_pipelines++;
		}
	}
	return num_pipelines;
}

static int
peek_next_add_pipeline(void)
{
	int i = 0;
	unsigned start_search_index = 0;

	if (last_add_found == (RTE_MAX_LCORE - 1)) {
		start_search_index = 0;
	} else {
		start_search_index = last_add_found + 1;
	}

	for (i = start_search_index; i < RTE_MAX_LCORE; i++)
	{
		if (1 & (pipeline_mask >> i)) {
			return i;
		}
	}
	for (i = 0; i < start_search_index; i++)
	{
		if (1 & (pipeline_mask >> i)) {
			return i;
		}
	}
	return -1;
}


static int
find_next_add_pipeline(void)
{
	int i = 0;
	unsigned start_search_index = 0;

	if (last_add_found == (RTE_MAX_LCORE - 1)) {
		start_search_index = 0;
	} else {
		start_search_index = last_add_found + 1;
	}

	for (i = start_search_index; i < RTE_MAX_LCORE; i++)
	{
		if (1 & (pipeline_mask >> i)) {
			last_add_found = i;
			return last_add_found;
		}
	}
	for (i = 0; i < start_search_index; i++)
	{
		if (1 & (pipeline_mask >> i)) {
			last_add_found = i;
			return last_add_found;
		}
	}
	return -1;
}

static void
multiqueue_generate_recv_upcalls(void)
{
	unsigned pipeline_id = 0;
	int result = -1;
	uint8_t upcall_cmd = 0;
	int toggle = 0;

	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {
			if (toggle) {
				upcall_cmd = OVS_PACKET_CMD_ACTION;
			}
			else {
				upcall_cmd = OVS_PACKET_CMD_MISS;
			}
			result = enqueue_upcall_on_exception_ring(upcall_cmd,
				                                  pipeline_id);
			assert(result == 0);
			toggle = ~toggle;
		}
	}
}

void
test_multicore_dpif_dpdk_recv(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct dpif_upcall upcall = {0};
	struct ofpbuf ofpbuf = {0};
	int result = 0;
	uint8_t upcall_cmd = 0;
	int toggle = 0;
	unsigned pipeline_id = 0;
	unsigned last_pipeline = 0;

	/* Enqueue upcall on all active pipeline's exception rings, with alternating
	 * upcall commands (action/miss).
	 */
	multiqueue_generate_recv_upcalls();

	/* Call the receive function a number of times equal to the number of
	 * pipelines we have, and check that the upcall has been handled
	 * appropriately.
	 */
	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {
			if (toggle) {
				upcall_cmd = DPIF_UC_ACTION;
			}
			else {
				upcall_cmd = DPIF_UC_MISS;
			}
			result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
			assert(result == 0);
			assert(upcall.type == upcall_cmd);
			toggle = ~toggle;
		}
	}

	/* Test that a single upcall can be found on each pipeline */
	for (pipeline_id = 0; pipeline_id < NUM_CORES; pipeline_id++) {
		if (1 & (pipeline_mask >> pipeline_id)) {
			result = enqueue_upcall_on_exception_ring(OVS_PACKET_CMD_MISS,
			                                          pipeline_id);
			assert(result == 0);

			result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
			assert(result == 0);
			assert(upcall.type == DPIF_UC_MISS);
			last_pipeline = pipeline_id;
		}
	}

	/*Test that the last_used will be found if it is called again*/
	/* Enqueue to the last use pipeline*/
	result = enqueue_upcall_on_exception_ring(OVS_PACKET_CMD_MISS,
			                                  last_pipeline);
	assert(result == 0);

	result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
	assert(result == 0);
	assert(upcall.type == DPIF_UC_MISS);

	/* Test that when no upcalls EAGAIN is returned*/
	result = dpif_p->dpif_class->recv(dpif_p, &upcall, &ofpbuf);
	assert(result == EAGAIN);
}

static const struct command commands[] = {
	{"multicore-port-add", 0, 0, test_multicore_dpif_dpdk_add},
	{"multicore-port-del", 0, 0, test_multicore_dpif_dpdk_del},
	{"multicore-port-getstats", 0, 0, test_multicore_dpif_dpdk_getstats},
	{"multicore-recv", 0, 0, test_multicore_dpif_dpdk_recv},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	int err = 0;

	set_program_name(argv[0]);
	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(30);  /* Allow a long time for DPDK to start */

	/* Initialise system */
	rte_eal_init(argc, argv);
	/* Init up to 64 rings and there's no freeing, so alloc lots of buffers */
	init_test_rings(1024*1024, &pipeline_mask);

	err = dpif_create_and_open("br0", "dpdk", &dpif_p);
	assert(err == 0);

	/* Assume only three EAL parameters */
	run_command(argc - 6, argv + 6, commands);

	/* Cleanup system */
	dpif_p->dpif_class->destroy(dpif_p);

	return 0;
}

