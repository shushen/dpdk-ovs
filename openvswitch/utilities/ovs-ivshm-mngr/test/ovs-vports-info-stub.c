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
#include <unistd.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_string_fns.h>

#include <ovs-vport.h>
#include <vport-types.h>

static struct vport_info *stub_vports = NULL;
static struct rte_mempool *pktmbuf_pool;

static struct rte_ring *
create_ring(const char *name)
{
	struct rte_ring *r;

	r = rte_ring_create(name, 32, SOCKET_ID_ANY, 0);
	assert(r != NULL);
	return r;
}

static const struct rte_memzone *
create_memzone(const char *name)
{
	const struct rte_memzone *mz;

	mz = rte_memzone_reserve(name, 32, SOCKET_ID_ANY, 0);
	assert(mz != NULL);
	return mz;
}

static void
create_vport_client(struct vport_info *vport, const char *port_name)
{
	struct vport_client *client;
	char ring_name[RTE_RING_NAMESIZE];

	vport->type = VPORT_TYPE_CLIENT;
	rte_snprintf(vport->name, sizeof(vport->name), port_name);

	client = &vport->client;

	/* Create rings and store their names */
	rte_snprintf(ring_name, sizeof(ring_name), "%sRX", port_name);
	client->rx_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.rx, sizeof(client->ring_names.rx),
			ring_name);

	rte_snprintf(ring_name, sizeof(ring_name), "%sTX", port_name);
	client->tx_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.tx, sizeof(client->ring_names.tx),
			ring_name);

	rte_snprintf(ring_name, sizeof(ring_name), "%sFREE", port_name);
	client->free_q = create_ring(ring_name);
	rte_snprintf(client->ring_names.free, sizeof(client->ring_names.free),
			ring_name);
}

static void
create_vport_kni(struct vport_info *vport, const char *port_name)
{
	struct vport_kni *kni;
	char mz_name[RTE_MEMZONE_NAMESIZE];

	vport->type = VPORT_TYPE_KNI;
	rte_snprintf(vport->name, sizeof(vport->name), port_name);

	kni = &vport->kni;

	/* Create memzones and store their names */
	rte_snprintf(mz_name, sizeof(mz_name), "%sTX", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.tx, sizeof(kni->fifo_names.tx), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sRX", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.rx, sizeof(kni->fifo_names.rx), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sALLOC", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.alloc, sizeof(kni->fifo_names.alloc), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sFREE", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.free, sizeof(kni->fifo_names.free), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sRESP", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.resp, sizeof(kni->fifo_names.resp), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sREQ", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.req, sizeof(kni->fifo_names.req), mz_name);

	rte_snprintf(mz_name, sizeof(mz_name), "%sSYNC", port_name);
	create_memzone(mz_name);
	rte_snprintf(kni->fifo_names.sync, sizeof(kni->fifo_names.sync), mz_name);
}


int
main(int argc, char *argv[])
{
	if (rte_eal_init(argc, argv) < 0)
		return -1;

	static const struct rte_memzone *stub_vports_mz = NULL;

	stub_vports_mz = rte_memzone_reserve(MZ_VPORT_INFO,
			sizeof(struct vport_info) * MAX_VPORTS, rte_socket_id(), 0);
	assert(stub_vports_mz != NULL);

	stub_vports = stub_vports_mz->addr;
	create_vport_client(&stub_vports[0], "Client1");
	create_vport_client(&stub_vports[1], "Client2");
	create_vport_kni(&stub_vports[2], "KNI0");
	create_vport_kni(&stub_vports[3], "KNI1");

	assert(stub_vports_mz == ovs_vport_lookup_vport_info());

	pktmbuf_pool = rte_mempool_create(PKTMBUF_POOL_NAME, 32, 16, 32, 32, NULL,
			NULL, NULL, NULL, rte_socket_id(), 0);

	assert(pktmbuf_pool != NULL);

	for (;;)
		sleep(1);

	return 0;
}





