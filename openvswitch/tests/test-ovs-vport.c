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

#include <stdlib.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_eal.h>
#include "ovs-vport.h"
#include "vport-types.h"

#include "ut.h"

#define CLIENT_PORT_NAME		"Client1"
#define KNI_PORT_NAME			"KNI1"
#define NON_EXISTENT_PORT_NAME	"DOESNOTEXIST"

static struct vport_info *stub_vports = NULL;
static struct rte_mempool *pktmbuf_pool;

static void
set_up_all(void)
{
	static const struct rte_memzone *stub_vports_mz = NULL;

	stub_vports_mz = rte_memzone_reserve(MZ_VPORT_INFO,
			sizeof(struct vport_info) * MAX_VPORTS, rte_socket_id(), 0);
	assert(stub_vports_mz != NULL);

	stub_vports = stub_vports_mz->addr;
	create_vport_client(&stub_vports[0], CLIENT_PORT_NAME);
	create_vport_kni(&stub_vports[1], KNI_PORT_NAME);
	assert(stub_vports_mz == ovs_vport_lookup_vport_info());

	pktmbuf_pool = rte_mempool_create(PKTMBUF_POOL_NAME, 32, 16, 32, 32, NULL,
			NULL, NULL, NULL, rte_socket_id(), 0);

	assert(pktmbuf_pool != NULL);
}

static void
test_valid_vport(int argc __rte_unused, char *argv[] __rte_unused)
{
	assert(ovs_vport_is_vport_client(CLIENT_PORT_NAME) == 0);
	assert(ovs_vport_is_vport_client(NON_EXISTENT_PORT_NAME) < 0);
	assert(ovs_vport_is_vport_kni(KNI_PORT_NAME) == 0);
	assert(ovs_vport_is_vport_kni(NON_EXISTENT_PORT_NAME) < 0);
}

static void
test_vport_client_lookup(int argc __rte_unused, char *argv[] __rte_unused)
{
	struct vport_client *client = &stub_vports[0].client;

	assert(ovs_vport_client_lookup_rx_q(CLIENT_PORT_NAME) == client->rx_q);
	assert(ovs_vport_client_lookup_tx_q(CLIENT_PORT_NAME) == client->tx_q);
	assert(ovs_vport_client_lookup_free_q(CLIENT_PORT_NAME) == client->free_q);
	assert(ovs_vport_client_lookup_rx_q(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_client_lookup_tx_q(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_client_lookup_free_q(NON_EXISTENT_PORT_NAME) == NULL);
}

static void
test_vport_kni_lookup(int argc __rte_unused, char *argv[] __rte_unused)
{
	struct vport_kni *kni = &stub_vports[1].kni;
	const struct rte_memzone *mz = NULL;

	mz = ovs_vport_kni_lookup_tx_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.tx) == 0);
	mz = ovs_vport_kni_lookup_rx_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.rx) == 0);
	mz = ovs_vport_kni_lookup_alloc_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.alloc) == 0);
	mz = ovs_vport_kni_lookup_free_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.free) == 0);
	mz = ovs_vport_kni_lookup_resp_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.resp) == 0);
	mz = ovs_vport_kni_lookup_req_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.req) == 0);
	mz = ovs_vport_kni_lookup_sync_fifo(KNI_PORT_NAME);
	assert(strcmp(mz->name, kni->fifo_names.sync) == 0);

	assert(ovs_vport_kni_lookup_tx_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_rx_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_alloc_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_free_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_resp_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_req_fifo(NON_EXISTENT_PORT_NAME) == NULL);
	assert(ovs_vport_kni_lookup_sync_fifo(NON_EXISTENT_PORT_NAME) == NULL);
}

static void
test_lookup_packet_mempool(int argc __rte_unused, char *argv[] __rte_unused)
{
	const struct rte_memzone *mz = NULL;

	mz = rte_memzone_lookup("MP_"PKTMBUF_POOL_NAME);
	assert(mz != NULL);

	assert(ovs_vport_host_lookup_packet_mempool() == pktmbuf_pool);
	assert(ovs_vport_guest_lookup_packet_mempools_memzone() == mz);
}

static void
test_is_vport_name_valid(int argc __rte_unused, char *argv[] __rte_unused)
{
	assert(ovs_vport_is_vport_name_valid(CLIENT_PORT_NAME) == 0);
	assert(ovs_vport_is_vport_name_valid(KNI_PORT_NAME) == 0);
	assert(ovs_vport_is_vport_name_valid(NON_EXISTENT_PORT_NAME) == 0);
	/* Port name longer than 32 chars*/
	assert(ovs_vport_is_vport_name_valid("PORTNAMEISREALLYLOOOOOOOOOOOOOOOONG") < 0);
	/* Invalid port name with non-alphanumeric chars */
	assert(ovs_vport_is_vport_name_valid("INVALID_PORT_NAME") < 0);
}

static const struct command commands[] = {
	{"valid_vport", 0, 0, test_valid_vport},
	{"vport_client_lookup", 0, 0, test_vport_client_lookup},
	{"vport_kni_lookup", 0, 0, test_vport_kni_lookup},
	{"lookup_packet_mempool", 0, 0, test_lookup_packet_mempool},
	{"is_vport_name_valid", 0, 0, test_is_vport_name_valid},

	{NULL, 0, 0, NULL},
};

int main(int argc, char *argv[])
{
	/* init EAL, parsing EAL args */
	int retval = 0;
	retval = rte_eal_init(argc, argv);
	assert(retval >= 0);

	/* Discard EAL args */
	argc -= retval;
	argv += retval;

	set_up_all();

	run_command(argc, argv, commands);

	exit(EXIT_SUCCESS);
}
