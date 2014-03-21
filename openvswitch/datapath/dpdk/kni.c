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

#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#include "init_drivers.h"
#include "args.h"
#include "init.h"
#include "main.h"
#include "kni.h"
#include "vport.h"

#define OBJNAMSIZ 32

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define OVS_KNI_QUEUE_TX	"kni_port_%u_tx"
#define OVS_KNI_QUEUE_RX 	"kni_port_%u_rx"
#define OVS_KNI_QUEUE_ALLOC	"kni_port_%u_alloc"
#define OVS_KNI_QUEUE_FREE	"kni_port_%u_free"
#define OVS_KNI_QUEUE_REQ	"kni_port_%u_req"
#define OVS_KNI_QUEUE_RESP	"kni_port_%u_resp"
#define OVS_KNI_QUEUE_SYNC	"kni_port_%u_sync"

#define FAIL_ON_MEMZONE_NULL(mz) \
	do { \
		if ((mz) == NULL) \
		{ rte_exit(EXIT_FAILURE, "FIFO initialisation failed.\n"); } \
	}while(0)

static void kni_fifo_init(struct rte_kni_fifo *fifo, unsigned size);
static int create_kni_fifos(struct rte_kni *kni_dev,
		struct vport_kni_fifo_names *fifo_names, uint8_t kni_port_id);

/**
 * Create memzones and fifos for a KNI port.
 */
static int
create_kni_fifos(struct rte_kni *kni_dev,
		struct vport_kni_fifo_names *fifo_names, uint8_t kni_port_id)
{
	const struct rte_memzone *mz = NULL;
	char obj_name[OBJNAMSIZ];
	kni_dev->pktmbuf_pool = pktmbuf_pool;

	/* TX RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_TX, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->tx_q = mz->addr;
	kni_fifo_init(kni_dev->tx_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->tx, sizeof(fifo_names->tx), "%s", mz->name);

	/* RX RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_RX, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->rx_q = mz->addr;
	kni_fifo_init(kni_dev->rx_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->rx, sizeof(fifo_names->rx), "%s", mz->name);

	/* ALLOC RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_ALLOC, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->alloc_q = mz->addr;
	kni_fifo_init(kni_dev->alloc_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->alloc, sizeof(fifo_names->alloc), "%s", mz->name);

	/* FREE RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_FREE, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->free_q = mz->addr;
	kni_fifo_init(kni_dev->free_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->free, sizeof(fifo_names->free), "%s", mz->name);

	/* Request RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_REQ, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->req_q = mz->addr;
	kni_fifo_init(kni_dev->req_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->req, sizeof(fifo_names->req), "%s", mz->name);

	/* Response RING */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_RESP, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->resp_q = mz->addr;
	kni_fifo_init(kni_dev->resp_q, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->resp, sizeof(fifo_names->resp), "%s", mz->name);

	/* Req/Resp sync mem area */
	rte_snprintf(obj_name, OBJNAMSIZ, OVS_KNI_QUEUE_SYNC, kni_port_id);
	mz = rte_memzone_reserve(obj_name, KNI_FIFO_SIZE, SOCKET_ID_ANY, 0);
	FAIL_ON_MEMZONE_NULL(mz);
	kni_dev->sync_addr= mz->addr;
	kni_fifo_init(kni_dev->sync_addr, KNI_FIFO_COUNT_MAX);
	rte_snprintf(fifo_names->sync, sizeof(fifo_names->sync), "%s", mz->name);

	return 0;
}

/**
 * Initializes the kni fifo structure
 */
static void
kni_fifo_init(struct rte_kni_fifo *fifo, unsigned size)
{
	/* Ensure size is power of 2 */
	if (size & (size - 1))
		rte_panic("KNI fifo size must be power of 2\n");

	fifo->write = 0;
	fifo->read = 0;
	fifo->len = size;
	fifo->elem_size = sizeof(void *);
}

rte_spinlock_t rte_kni_locks[MAX_KNI_PORTS];

void
init_kni(void)
{
	uint8_t port_id = 0;
	struct vport_kni_fifo_names kni_names;

	memset(rte_kni_list, 0, sizeof(rte_kni_list));

	/* Create the rte_kni fifos for each KNI port */
	for (port_id = 0; port_id < num_kni; port_id++) {
		RTE_LOG(INFO, APP, "Initialising KNI port '%s'\n",
				vport_get_name(KNI0 + port_id));
		create_kni_fifos(&rte_kni_list[port_id], &kni_names, port_id);
		vport_set_kni_fifo_names(KNI0 + port_id, &kni_names);
		rte_spinlock_init(&rte_kni_locks[port_id]);
	}
}
