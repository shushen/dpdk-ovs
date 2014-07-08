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
 * ovdk_vport creates an array of struct vport_info. Each entry in the array
 * corresponds to a bidirectional port of the dataplane.
 *
 * The index of the array is the vportid and corresponds to the odp number in
 * the vswitchd.
 *
 * Each struct vport_info contains all information required by the port in the
 * dataplane.
 *
 * ovdk_vport manages this structure in an opaque manner providing interfaces
 * to manipulate the data.
 */

#include <string.h>

#include <rte_config.h>
#include <rte_pipeline.h>

#include "ovdk_vport.h"
#include "ovdk_vport_types.h"
#include "ovdk_vport_info.h"
#include "ovdk_vport_phy.h"
#include "ovdk_vport_client.h"
#include "ovdk_vport_bridge.h"
#include "ovdk_flow.h"
#include "ovdk_vport_vhost.h"
#include "ovdk_vport_types.h"
#include "ovdk_virtio-net.h"
#include "ovdk_args.h"

#define rpl_strnlen strnlen

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define RTE_LOGTYPE_APP                 RTE_LOGTYPE_USER1

#define OVDK_MZ_VPORT_INFO_SIZE    sizeof(struct vport_info) * OVDK_MAX_VPORTS
#define INVALID_VPORTID            (-1)
#define VALID_PORTMASK             0

/*
 * 'vport_info' has an entry for every dataplane port.
 */
static struct vport_info *vport_info;

/*
 * This array is needed to allow us to convert between packet framework in port
 * ids back to vportids. It is required to translate the in_port of upcalls
 */
static int vportid_map[RTE_MAX_LCORE][RTE_PIPELINE_PORT_OUT_MAX];

/*
 * Portmask for physical devices.
 */
static uint64_t phy_portmask = 0x0;

/*
 * Initialize 'vport_info'. This should do any one-time initialization of
 * 'vport_info' as this will be executed only once
 */
void
ovdk_vport_init(void) {
	int i = 0;
	int j = 0;
	int offset = 0;
	const struct rte_memzone *vport_mz = NULL;
	int max_phyports = 0;
	phy_portmask = ovdk_args_get_portmask();

	vport_mz = rte_memzone_reserve(OVDK_MZ_VPORT_INFO,
	                               OVDK_MZ_VPORT_INFO_SIZE,
	                               rte_socket_id(),
	                               0);

	vport_info = (struct vport_info*)vport_mz->addr;

	for (i = 0; i < OVDK_MAX_VPORTS; i++) {
		memset(&vport_info[i], 0, sizeof(struct vport_info));
		vport_info[i].vportid = i;
	}

	max_phyports = ovdk_vport_phy_init();
	if (max_phyports < 0)
		rte_panic("Problem initialising NIC");

	/* Init phy ports */
	RTE_LOG(INFO, APP, "Initializing %d physical ports\n", max_phyports);
	for (i = 0; i < max_phyports; i++)
		ovdk_vport_phy_port_init(&vport_info[i],i);

	offset += OVDK_MAX_PHYPORTS;

	/* Init client ports */
	for(i = OVDK_VPORT_TYPE_CLIENT; i < (OVDK_VPORT_TYPE_CLIENT + OVDK_MAX_CLIENTS); i++) {
		ovdk_vport_client_port_init(&vport_info[i]);
	}

	for (i = OVDK_VPORT_TYPE_VHOST; i < (OVDK_VPORT_TYPE_VHOST + OVDK_MAX_VHOSTS); i++) {
		ovdk_vport_vhost_port_init(&vport_info[i]);
	}

	for (i = OVDK_VPORT_TYPE_BRIDGE; i < OVDK_VPORT_TYPE_BRIDGE + OVDK_MAX_BRIDGES; i++)
		ovdk_vport_bridge_port_init(&vport_info[i]);


	for (i = 0; i < OVDK_MAX_VPORTS; i++) {
		/*
		 * Every mbuf that arrives at an in_port needs to be
		 * classified. This is done by the in_port f_action() callback.
		 * The callback function will extract tuples from the packet
		 * and generate a signature which will be used to index the
		 * hash table.
		 *
		 * Every time the callback is called the 'arg_ah' parameter is
		 * passed as an argument to flow_keys_extract(). Using this,
		 * the 'port_in_id' can be added as a tuple used for
		 * classification.
		 */
		vport_info[i].port_in_params.f_action = flow_keys_extract;
		vport_info[i].port_in_params.arg_ah = &vport_info[i].port_in_id;
	}

	for (i = 0; i < RTE_MAX_LCORE; i++)
		for (j = 0; j < RTE_PIPELINE_PORT_IN_MAX; j++)
			vportid_map[i][j] = INVALID_VPORTID;

	return;
}

/*
 * Get the 'port_in_id' for a given 'vportid'. This is the port_in_id
 * assigned by the rte_pipeline when adding the vport as an in port
 */
inline int
ovdk_vport_get_in_portid(uint32_t vportid, uint32_t *portid) {
	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	if (portid == NULL )
		return -1;

	*portid = vport_info[vportid].port_in_id;

	return 0;
}

/*
 * Get the port_out_id for a given 'vportid'. This is the port_out_id
 * assigned by the rte_pipeline when adding the vport as an out port
 */
inline int
ovdk_vport_get_out_portid(uint32_t vportid, uint32_t *portid) {
	unsigned lcore_id = 0;

	lcore_id = rte_lcore_id();

	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	if (portid == NULL )
		return -1;

	*portid = vport_info[vportid].port_out_id[lcore_id];

	return 0;
}

/*
 * Get the 'vportid' for a given port_in_id. This is the port_in_id
 * assigned by the rte_pipeline when adding the vport as an in port
 */
inline int
ovdk_vport_get_vportid(uint32_t port_in_id, uint32_t *vportid)
{
	unsigned lcore_id = 0;
	uint32_t temp_vportid = 0;

	lcore_id = rte_lcore_id();

	if (port_in_id >= RTE_PIPELINE_PORT_IN_MAX)
		return -1;

	if (vportid == NULL)
		return -1;

	temp_vportid = vportid_map[lcore_id][port_in_id];

	if (temp_vportid == INVALID_VPORTID)
		return -1;

	*vportid = temp_vportid;

	return 0;
}

/*
 * Save the port_in_id assigned by an rte_pipeline when adding this vport
 * as an in port
 */
inline int
ovdk_vport_set_in_portid(uint32_t vportid, uint32_t portid)
{
	unsigned lcore_id = 0;

	lcore_id = rte_lcore_id();

	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	vport_info[vportid].port_in_id = portid;
	vportid_map[lcore_id][portid] = vportid;

	return 0;
}

/*
 * Save the port_out_id assigned by an rte_pipeline when adding this vport
 * as an out port
 */
inline int
ovdk_vport_set_out_portid(uint32_t vportid, uint32_t portid) {
	unsigned lcore_id = 0;

	lcore_id = rte_lcore_id();

	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	vport_info[vportid].port_out_id[lcore_id] = portid;

	return 0;
}

/*
 * Get the rte_pipeline_port_out_params used to configure a vport
 */
inline int
ovdk_vport_get_out_params(uint32_t vportid,
		struct rte_pipeline_port_out_params **params) {
	unsigned lcore_id = 0;

	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	if (params == NULL )
		return -1;

	lcore_id = rte_lcore_id();

	*params = &vport_info[vportid].port_out_params[lcore_id];

	return 0;

}

/*
 * Get the rte_pipeline_port_in_params used to configure a vport
 */
inline int
ovdk_vport_get_in_params(uint32_t vportid,
		struct rte_pipeline_port_in_params **params) {
	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	if (params == NULL )
		return -1;

	*params = &vport_info[vportid].port_in_params;

	return 0;
}

/*
 * Vhost port control functions
 */
inline int
ovdk_vport_vhost_up(struct virtio_net *dev) {
	uint32_t vhostid;
	struct vport_info *info;

	/* Search for the portname and set the dev pointer. */
	for (vhostid = OVDK_VPORT_TYPE_VHOST;
			vhostid < (OVDK_VPORT_TYPE_VHOST + OVDK_MAX_VHOSTS); vhostid++) {
		info = &vport_info[vhostid];
		if (strncmp(dev->port_name, info->name,
				strnlen(dev->port_name, sizeof(dev->port_name))) == 0
				&& (strnlen(dev->port_name, sizeof(dev->port_name))
						== strnlen(info->name, sizeof(info->name)))) {
			info->vhost.dev = dev;
			return 0;
		}
	}

	RTE_LOG(ERR, APP, "(%"PRIu64") Port name %s does not match any"
			" ovdk_pf port names for adding device\n",
			dev->device_fh, dev->port_name);

	return ENODEV;
}

inline int
ovdk_vport_vhost_down(struct virtio_net *dev) {
	uint32_t vhostid;
	struct vport_info *info;

	/* Search for the portname and clear the dev pointer. */
	for (vhostid = OVDK_VPORT_TYPE_VHOST;
			vhostid < (OVDK_VPORT_TYPE_VHOST + OVDK_MAX_VHOSTS); vhostid++) {
		info = &vport_info[vhostid];
		if (strncmp(dev->port_name, info->name,
				strnlen(dev->port_name, sizeof(dev->port_name))) == 0
				&& (strnlen(dev->port_name, sizeof(dev->port_name))
						== strnlen(info->name, sizeof(info->name)))) {
			info->vhost.dev = NULL;
			return 0;
		}
	}

	RTE_LOG(ERR, APP, "(%"PRIu64") Port name %s does not match any"
			" ovdk_pf port names for device removal\n",
			dev->device_fh, dev->port_name);

	return ENODEV;
}

/*
 * Save the name passed down to ovdk_pipeline_add_port when adding this vport
 * as the vport name.
 */
inline int
ovdk_vport_set_port_name(uint32_t vportid, char *port_name)
{
	if (vportid >= OVDK_MAX_VPORTS)
		return EINVAL;

	if (&vport_info[vportid] == NULL)
		return EINVAL;

	if (port_name == NULL)
		return EINVAL;

	/* When copying port_name, allow one byte for the null terminator */
	strncpy(vport_info[vportid].name,
	        port_name,
	        OVDK_MAX_VPORT_NAMESIZE - 1);

	return 0;
}

/*
 * Return the name that was added to the port with 'vportid'
 */
inline int
ovdk_vport_get_port_name(uint32_t vportid, char *port_name)
{
	if (vportid >= OVDK_MAX_VPORTS)
		return -1;

	if (&vport_info[vportid] == NULL)
		return -2;

	if (port_name == NULL)
		return -3;

	strncpy(port_name,
	        vport_info[vportid].name,
	        OVDK_MAX_VPORT_NAMESIZE);

	return 0;
}

/*
 * Verify the vportid of a physical device against the
 * portmask of allowed physical devices. If not then
 * verify that port is winthin range of allowed port
 * types.
 */
int
ovdk_vport_port_verify(uint32_t vportid)
{
        int pmask_val = -1;
        int ret = VALID_PORTMASK;

        if (vportid < OVDK_MAX_PHYPORTS)
                pmask_val = (phy_portmask & (1 << vportid));
                if (pmask_val == 0)
                        ret = ENODEV;
        else
                if (vportid > OVDK_MAX_VPORTS)
                        ret = ENODEV;

        return ret;
}

