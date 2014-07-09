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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>

#include "ovdk_mempools.h"
#include "ovdk_vport_info.h"
#include "ovs-vport.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define client_vport_name_equal_to(client, client_name) \
	(client.type == OVDK_VPORT_TYPE_CLIENT					\
	&& !strncmp(client.name, client_name, sizeof(client.name)))

#define kni_vport_name_equal_to(kni, kni_name) 			\
	(kni.type == OVDK_VPORT_TYPE_KNI							\
	&& !strncmp(kni.name, kni_name, sizeof(kni.name)))

#define ASSERT_VPORTS_NOT_NULL() assert(vports != NULL)

/* Global references to vports structure and its memzone */
static struct vport_info *vports = NULL;
static const struct rte_memzone* vports_mz = NULL;

static inline struct vport_client *
get_client_vport_by_name(const char *port_name)
{
	int i = 0;
	struct vport_client *client = NULL;

	if ((ovs_vport_is_vport_name_valid(port_name)) < 0) {
		RTE_LOG(ERR, APP,
		        "Client vport name is invalid [%s]\n",
		        port_name);
		return NULL;
	}

	for (i = 0; i < OVDK_MAX_VPORTS; i++) {
		if (client_vport_name_equal_to(vports[i], port_name)) {
			client = &vports[i].client;
			break;
		}
	}
	if (client == NULL)
		RTE_LOG(ERR, APP, "Cannot find client vport '%s'\n", port_name);
	return client;
}

static inline struct vport_kni *
get_kni_vport_by_name(const char *port_name)
{
	int i = 0;
	struct vport_kni *kni = NULL;

	if ((ovs_vport_is_vport_name_valid(port_name)) < 0)
		return NULL;

	for (i = 0; i < OVDK_MAX_VPORTS; i++) {
		if (kni_vport_name_equal_to(vports[i], port_name)) {
			kni = &vports[i].kni;
			break;
		}
	}
	if (kni == NULL)
		RTE_LOG(ERR, APP, "Cannot find KNI vport '%s'\n", port_name);
	return kni;
}

static inline struct rte_ring *
ring_lookup(const char *ring_name)
{
	struct rte_ring *ring = NULL;

	ring = rte_ring_lookup(ring_name);
	if (ring == NULL)
		RTE_LOG(ERR, APP, "Cannot find ring '%s'\n", ring_name);
	return ring;
}

static inline const struct rte_memzone *
memzone_lookup(const char *mz_name)
{
	const struct rte_memzone *mz = NULL;

	mz = rte_memzone_lookup(mz_name);
	if (mz == NULL)
		RTE_LOG(ERR, APP, "Cannot find memzone '%s'\n", mz_name);
	return mz;
}

const struct rte_memzone *
ovs_vport_lookup_vport_info(void)
{
	if (vports != NULL) {
		RTE_LOG(WARNING, APP, "vports is already setup\n");
		return vports_mz;
	}

	vports_mz = rte_memzone_lookup(OVDK_MZ_VPORT_INFO);
	if (vports_mz == NULL) {
		RTE_LOG(ERR, APP, "Cannot find vport memzone\n");
		return NULL;
	}

	vports = vports_mz->addr;

	return vports_mz;
}

int
ovs_vport_is_vport_client(const char *port_name)
{
	int i = 0;

	ASSERT_VPORTS_NOT_NULL();


	if ((ovs_vport_is_vport_name_valid(port_name)) < 0)
		return -1;

	for (i = 0; i < OVDK_MAX_VPORTS; i++)
		if (client_vport_name_equal_to(vports[i], port_name))
			return 0;
	return -1;
}

int
ovs_vport_is_vport_kni(const char *port_name)
{
	int i = 0;

	ASSERT_VPORTS_NOT_NULL();

	if ((ovs_vport_is_vport_name_valid(port_name)) < 0)
		return -1;

	for (i = 0; i < OVDK_MAX_VPORTS; i++)
		if (kni_vport_name_equal_to(vports[i], port_name))
			return 0;
	return -1;
}

struct rte_ring *
ovs_vport_client_lookup_rx_q(const char *port_name)
{
	struct vport_client *client = NULL;
	struct rte_ring *ring = NULL;

	ASSERT_VPORTS_NOT_NULL();

	client = get_client_vport_by_name(port_name);

	if (client != NULL)
		ring = ring_lookup(client->ring_names.rx);

	return ring;
}

struct rte_ring *
ovs_vport_client_lookup_tx_q(const char *port_name)
{
	struct vport_client *client = NULL;
	struct rte_ring *ring = NULL;

	ASSERT_VPORTS_NOT_NULL();

	client = get_client_vport_by_name(port_name);
	if (client != NULL)
		ring = ring_lookup(client->ring_names.tx);

	return ring;
}

struct rte_ring *
ovs_vport_client_lookup_free_q(const char *port_name)
{
	struct vport_client *client = NULL;
	struct rte_ring *ring = NULL;

	ASSERT_VPORTS_NOT_NULL();

	client = get_client_vport_by_name(port_name);
	if (client != NULL)
		ring = ring_lookup(client->ring_names.free);

	return ring;
}

struct rte_ring *
ovs_vport_client_lookup_alloc_q(const char *port_name)
{
	struct vport_client *client = NULL;
	struct rte_ring *ring = NULL;

	ASSERT_VPORTS_NOT_NULL();

	client = get_client_vport_by_name(port_name);
	if (client != NULL)
		ring = ring_lookup(client->ring_names.alloc);

	return ring;
}

const struct rte_memzone *
ovs_vport_kni_lookup_tx_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.tx);
	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_rx_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.rx);

	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_alloc_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.alloc);

	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_free_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.free);

	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_resp_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.resp);

	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_req_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.req);

	return mz;
}

const struct rte_memzone *
ovs_vport_kni_lookup_sync_fifo(const char *port_name)
{
	struct vport_kni *kni = NULL;
	const struct rte_memzone *mz = NULL;

	ASSERT_VPORTS_NOT_NULL();

	kni = get_kni_vport_by_name(port_name);
	if (kni != NULL)
		mz = memzone_lookup(kni->fifo_names.sync);

	return mz;
}

inline int
ovs_vport_is_vport_name_valid(const char *port_name)
{
	return vport_is_valid_name(port_name);
}

struct rte_mempool *
ovs_vport_host_lookup_packet_mempool(void)
{
	struct rte_mempool *mp = NULL;
	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		RTE_LOG(ERR, APP, "Could not find mempool '%s'\n", PKTMBUF_POOL_NAME);
	return mp;
}

