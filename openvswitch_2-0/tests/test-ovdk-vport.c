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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <config.h>
#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_memzone.h>

#include "lib/timeval.h"

#include "vlog.h"
#include "command-line.h"

#include "datapath/dpdk/ovdk_mempools.h"
#include "datapath/dpdk/ovdk_vport.h"
#include "datapath/dpdk/ovs-vport.h"
#include "datapath/dpdk/ovdk_vport_types.h"
#include "datapath/dpdk/ovdk_vport_info.h"
#include "datapath/dpdk/ovdk_virtio-net.h"

static struct vport_info *local_vport_info = NULL;

void initialise_vport_array(void);

void test_vport_get_in_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_in_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_in_portid__null_portid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_in_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_in_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_portid__null_portid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_out_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_out_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_vportid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_vportid__invalid_portinid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_vportid__null_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_in_params__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_in_params__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_in_params__null_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_params__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_params__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_out_params__null_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_port_name__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_port_name__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_get_port_name__null_portname(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_port_name__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_port_name__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_set_port_name__null_portname(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_vhost_up__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_vhost_up__no_such_device(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_vhost_down__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_vport_vhost_down__no_such_device(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

/* Helper functions */
void
initialise_vport_array(void)
{
	static const struct rte_memzone *vports_mz = NULL;

	ovdk_vport_init();

	/* Get the memzone of the vport_info array so we can read it's conents */
	vports_mz = ovs_vport_lookup_vport_info();
	assert(vports_mz != NULL);
	local_vport_info = vports_mz->addr;
}

void
test_vport_get_in_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t set_portid = 0;
	uint32_t get_portid = 0;
	int result = 0;

	vportid = OVDK_VPORT_TYPE_PHY;	/* Element of the array to test */
	set_portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	/* First set the in portid to a known value */
	result = ovdk_vport_set_in_portid(vportid, set_portid);
	assert(result == 0);

	/* Now check that the get function returns this value */
	result = ovdk_vport_get_in_portid(vportid, &get_portid);
	assert(result == 0);
	assert(get_portid == set_portid);
	assert(local_vport_info[vportid].port_in_id == get_portid); /* Same thing */
}

void
test_vport_get_in_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t get_portid = 0;
	int result = 0;

	vportid = OVDK_MAX_VPORTS;	/* Element of the array to test */

	/* Now check that the get function returns this value */
	result = ovdk_vport_get_in_portid(vportid, &get_portid);
	assert(result == -1);
}

void
test_vport_get_in_portid__null_portid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	int result = 0;

	vportid = OVDK_VPORT_TYPE_PHY;	/* Element of the array to test */

	/* Now check that the get function returns this value */
	result = ovdk_vport_get_in_portid(vportid, NULL);
	assert(result == -1);
}

void
test_vport_set_in_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t portid = 0;
	int result = 0;

	vportid = OVDK_VPORT_TYPE_PHY + 1;	/* Element of the array to test */
	portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	/* Make sure the port_in_id in question is not originally the value we
	 * wish to set it to.
	 */
	assert(local_vport_info[vportid].port_in_id != portid);

	result = ovdk_vport_set_in_portid(vportid, portid);
	assert(result == 0);

	/* Check that the vport_info array element has been updated accordingly */
	assert(local_vport_info[vportid].port_in_id == portid);
}

void
test_vport_set_in_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t portid = 0;
	int result = 0;

	vportid = OVDK_MAX_VPORTS;
	portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	result = ovdk_vport_set_in_portid(vportid, portid);
	assert(result == -1);
}

void
test_vport_get_out_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	unsigned lcore_id = 0;
	uint32_t vportid = 0;
	uint32_t set_portid = 0;
	uint32_t get_portid = 0;
	int result = 0;

	lcore_id = rte_lcore_id();

	vportid = OVDK_VPORT_TYPE_PHY + 2;	/* Element of the array to test */
	set_portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	/* First set the out portid to a known value */
	result = ovdk_vport_set_out_portid(vportid, set_portid);
	assert(result == 0);

	/* Now check that the get function returns this value */
	result = ovdk_vport_get_out_portid(vportid, &get_portid);
	assert(result == 0);
	assert(get_portid == set_portid);
	assert(local_vport_info[vportid].port_out_id[lcore_id] == get_portid); /* Same thing */
}

void
test_vport_get_out_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t get_portid = 0;
	int result = 0;

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_out_portid(vportid, &get_portid);
	assert(result == -1);
}

void
test_vport_get_out_portid__null_portid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	int result = 0;

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_out_portid(vportid, NULL);
	assert(result == -1);
}

void
test_vport_set_out_portid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	unsigned lcore_id = 0;
	uint32_t vportid = 0;
	uint32_t portid = 0;
	int result = 0;

	lcore_id = rte_lcore_id();

	vportid = OVDK_VPORT_TYPE_PHY + 3;	/* Element of the array to test */
	portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	/* Make sure the port_out_id in question is not originally the value we
	 * wish to set it to.
	 */
	assert(local_vport_info[vportid].port_out_id[lcore_id] != portid);

	result = ovdk_vport_set_out_portid(vportid, portid);
	assert(result == 0);

	/* Check that the vport_info array element has been updated accordingly */
	assert(local_vport_info[vportid].port_out_id[lcore_id] == portid);
}

void
test_vport_set_out_portid__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t portid = 0;
	int result = 0;

	vportid = OVDK_MAX_VPORTS;
	portid = RTE_PIPELINE_PORT_IN_MAX - 1;

	result = ovdk_vport_set_out_portid(vportid, portid);
	assert(result == -1);
}

void
test_vport_get_vportid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t set_vportid = 0;
	uint32_t port_in_id = 0;
	int result = 0;

	set_vportid = OVDK_VPORT_TYPE_BRIDGE;
	port_in_id = RTE_PIPELINE_PORT_IN_MAX - 1;

	/* First set the port_in_id for a given vportid */
	result = ovdk_vport_set_in_portid(set_vportid, port_in_id);
	assert(result == 0);

	result = ovdk_vport_get_vportid(port_in_id, &vportid);
	assert(result == 0);
	assert(vportid == set_vportid);
}

void
test_vport_get_vportid__invalid_portinid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t vportid = 0;
	uint32_t port_in_id = 0;
	int result = 0;

	port_in_id = RTE_PIPELINE_PORT_IN_MAX;

	result = ovdk_vport_get_vportid(port_in_id, &vportid);
	assert(result == -1);
}

void
test_vport_get_vportid__null_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	uint32_t port_in_id = 0;
	int result = 0;

	port_in_id = RTE_PIPELINE_PORT_IN_MAX - 1;

	result = ovdk_vport_get_vportid(port_in_id, NULL);
	assert(result == -1);
}

void
test_vport_get_in_params__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	struct rte_pipeline_port_in_params *params = {0};

	vportid = OVDK_VPORT_TYPE_CLIENT;

	result = ovdk_vport_get_in_params(vportid, &params);
	assert(result == 0);
	assert(&local_vport_info[vportid].port_in_params == params);
}

void
test_vport_get_in_params__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	struct rte_pipeline_port_in_params *params = {0};

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_in_params(vportid, &params);
	assert(result == -1);
}

void
test_vport_get_in_params__null_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;

	vportid = OVDK_MAX_VPORTS - 1;

	result = ovdk_vport_get_in_params(vportid, NULL);
	assert(result == -1);
}

void
test_vport_get_out_params__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	unsigned lcore_id = 0;
	int result = 0;
	uint32_t vportid = 0;
	struct rte_pipeline_port_out_params *params = {0};

	lcore_id = rte_lcore_id();

	vportid = OVDK_VPORT_TYPE_CLIENT + 1;

	result = ovdk_vport_get_out_params(vportid, &params);
	assert(result == 0);
	assert(&local_vport_info[vportid].port_out_params[lcore_id] == params);
}

void
test_vport_get_out_params__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	struct rte_pipeline_port_out_params *params = {0};

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_out_params(vportid, &params);
	assert(result == -1);
}

void
test_vport_get_out_params__null_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;

	vportid = OVDK_VPORT_TYPE_CLIENT + 1;

	result = ovdk_vport_get_out_params(vportid, NULL);
	assert(result == -1);
}

void
test_vport_get_port_name__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	char *set_port_name = "fakename2";
	char get_port_name[OVDK_MAX_VPORT_NAMESIZE];

	vportid = OVDK_VPORT_TYPE_KNI;

	/* First set the port name */
	result = ovdk_vport_set_port_name(vportid, set_port_name);
	assert(result == 0);

	result = ovdk_vport_get_port_name(vportid, get_port_name);
	assert(result == 0);
	assert(strncmp(get_port_name, set_port_name, sizeof(*set_port_name)) == 0);
	assert(strncmp(local_vport_info[vportid].name, get_port_name,
						sizeof(local_vport_info[vportid].name)) == 0);
}

void
test_vport_get_port_name__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	char get_port_name[OVDK_MAX_VPORT_NAMESIZE];

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_port_name(vportid, get_port_name);
	assert(result == -1);
}

void
test_vport_get_port_name__null_portname(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;

	vportid = OVDK_MAX_VPORTS;

	result = ovdk_vport_get_port_name(vportid, NULL);
	assert(result == -1);
}

void
test_vport_set_port_name__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	char *port_name = "fakename";

	vportid = OVDK_VPORT_TYPE_KNI + 1;

	/* Make sure that the original port name does not equal to the name we wish
	 * to set it to
	 */
	assert(strncmp(local_vport_info[vportid].name, port_name,
						sizeof(local_vport_info[vportid].name)) != 0);

	/* Set the port name */
	result = ovdk_vport_set_port_name(vportid, port_name);
	assert(result == 0);

	/* Check that the name was updated correctly in the vport_info array */
	assert(strncmp(local_vport_info[vportid].name, port_name,
					sizeof(local_vport_info[vportid].name)) == 0);
}

void
test_vport_set_port_name__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;
	char *port_name = "fakename";

	vportid = OVDK_MAX_VPORTS;

	/* Set the port name */
	result = ovdk_vport_set_port_name(vportid, port_name);
	assert(result == EINVAL);
}

void
test_vport_set_port_name__null_portname(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = 0;
	uint32_t vportid = 0;

	vportid = OVDK_MAX_VPORTS - 1;

	/* Set the port name */
	result = ovdk_vport_set_port_name(vportid, NULL);
	assert(result == EINVAL);
}

void
test_vport_vhost_up__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct virtio_net *dev = NULL;
	int result = 0;
	uint32_t vportid = 0;

	dev = malloc(sizeof(struct virtio_net));

	vportid = OVDK_VPORT_TYPE_VHOST;

	/* Give the virtio-net device a name. In practice, this is retrieved from
	 * the interface name of the vhost tap device you add to the VM. However,
	 * here we will set it directly.
	 */
	strncpy(dev->port_name, "test_vhost_up", sizeof(dev->port_name));

	/* Assign the same name to a vhost element of the vport_info array. In
	 * practice, this is added when you add a new vhost port to the switch, but
	 * here we will set it directly.
	 */
	result = ovdk_vport_set_port_name(vportid, "test_vhost_up");
	assert(result == 0);

	/* Ensure that the element of the vport_info array does not already
	 * contain the pointer to the device we want to bring up.
	 */
	assert(local_vport_info[vportid].vhost.dev != dev);

	/* ovdk_vport_vhost_up() searches through the vport_info array for a
	 * port name which matches that of our virtio-net device. If a match is
	 * found, we set the vhost.dev pointer of that element equal to the one we
	 * passed in.
	 */
	result = ovdk_vport_vhost_up(dev);
	assert(result == 0);

	/* Check that the device pointer is equal to the one we passed in */
	assert(local_vport_info[vportid].vhost.dev == dev);

	free(dev);
}

void
test_vport_vhost_up__no_such_device(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct virtio_net *dev = NULL;
	int result = 0;

	dev = malloc(sizeof(struct virtio_net));

	/* Give the virtio-net device a name. In practice, this is retrieved from
	 * the interface name of the vhost tap device you add to the VM. However,
	 * here we will set it directly.
	 */
	strncpy(dev->port_name, "test_vhost_up", sizeof(dev->port_name));

	/* ovdk_vport_vhost_up() searches through the vport_info array for a
	 * port name which matches that of our virtio-net device. If a match is
	 * found, we set the vhost.dev pointer of that element equal to the one we
	 * passed in.
	 */
	result = ovdk_vport_vhost_up(dev);
	assert(result == ENODEV);

	free(dev);
}

void
test_vport_vhost_down__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct virtio_net *dev = NULL;
	int result = 0;
	uint32_t vportid = 0;

	dev = malloc(sizeof(struct virtio_net));

	vportid = OVDK_VPORT_TYPE_VHOST + 1;

	/* Give the virtio-net device a name. In practice, this is retrieved from
	 * the interface name of the vhost tap device you add to the VM. However,
	 * here we will set it directly.
	 */
	strncpy(dev->port_name, "test_vhost_down", sizeof(dev->port_name));

	/* Assign the same name to a vhost element of the vport_info array. In
	 * practice, this is added when you add a new vhost port to the switch, but
	 * here we will set it directly.
	 */
	result = ovdk_vport_set_port_name(vportid, "test_vhost_down");
	assert(result == 0);

	/* Add the virtio-net device to the vport_info array */
	result = ovdk_vport_vhost_up(dev);
	assert(result == 0);

	/* Ensure the device pointer of this element of the array is now non-NULL */
	assert(local_vport_info[vportid].vhost.dev != NULL);

	/* ovdk_vport_vhost_down() searches through the vport_info array for a
	 * port name which matches that of our virtio-net device. If a match is
	 * found, we set the vhost.dev pointer of that element equal to NULL.
	 */
	result = ovdk_vport_vhost_down(dev);
	assert(result == 0);
	assert(local_vport_info[vportid].vhost.dev == NULL);

	free(dev);
}

void
test_vport_vhost_down__no_such_device(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	struct virtio_net *dev = NULL;
	int result = 0;

	dev = malloc(sizeof(struct virtio_net));

	/* Give the virtio-net device a name. In practice, this is retrieved from
	 * the interface name of the vhost tap device you add to the VM. However,
	 * here we will set it directly.
	 */
	strncpy(dev->port_name, "test_vhost_down", sizeof(dev->port_name));

	/* ovdk_vport_vhost_down() searches through the vport_info array for a
	 * port name which matches that of our virtio-net device. If a match is
	 * found, we set the vhost.dev pointer of that element equal to NULL.
	 */
	result = ovdk_vport_vhost_down(dev);
	assert(result == ENODEV);

	free(dev);
}

static const struct command commands[] = {
	{"get-in-portid-default", 0, 0, test_vport_get_in_portid__default},
	{"get-in-portid-invalid-vportid", 0, 0, test_vport_get_in_portid__invalid_vportid},
	{"get-in-portid-null-portid", 0, 0, test_vport_get_in_portid__null_portid},
	{"set-in-portid-default", 0, 0, test_vport_set_in_portid__default},
	{"set-in-portid-invalid-vportid", 0, 0, test_vport_set_in_portid__invalid_vportid},
	{"get-out-portid-default", 0, 0, test_vport_get_out_portid__default},
	{"get-out-portid-invalid-vportid", 0, 0, test_vport_get_out_portid__invalid_vportid},
	{"get-out-portid-null-portid", 0, 0, test_vport_get_out_portid__null_portid},
	{"set-out-portid-default", 0, 0, test_vport_set_out_portid__default},
	{"set-out-portid-invalid-vportid", 0, 0, test_vport_set_out_portid__invalid_vportid},
	{"get-vportid-default", 0, 0, test_vport_get_vportid__default},
	{"get-vportid-invalid-port-in-id", 0, 0, test_vport_get_vportid__invalid_portinid},
	{"get-vportid-null-vportid", 0, 0, test_vport_get_vportid__null_vportid},
	{"get-in-params-default", 0, 0, test_vport_get_in_params__default},
	{"get-in-params-invalid-vportid", 0, 0, test_vport_get_in_params__invalid_vportid},
	{"get-in-params-null-params", 0, 0, test_vport_get_in_params__null_params},
	{"get-out-params-default", 0, 0, test_vport_get_out_params__default},
	{"get-out-params-invalid-vportid", 0, 0, test_vport_get_out_params__invalid_vportid},
	{"get-out-params-null-params", 0, 0, test_vport_get_out_params__null_params},
	{"get-port-name-default", 0, 0, test_vport_get_port_name__default},
	{"get-port-name-invalid-vportid", 0, 0, test_vport_get_port_name__invalid_vportid},
	{"get-port-name-null-portname", 0, 0, test_vport_get_port_name__null_portname},
	{"set-port-name-default", 0, 0, test_vport_set_port_name__default},
	{"set-port-name-invalid-vportid", 0, 0, test_vport_set_port_name__invalid_vportid},
	{"set-port-name-null-portname", 0, 0, test_vport_set_port_name__null_portname},
	{"vhost-up-default", 0, 0, test_vport_vhost_up__default},
	{"vhost-up-no-such-device", 0, 0, test_vport_vhost_up__no_such_device},
	{"vhost-down-default", 0, 0, test_vport_vhost_down__default},
	{"vhost-down-no-such-device", 0, 0, test_vport_vhost_down__no_such_device},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	set_program_name(argv[0]);

	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(30);

	rte_eal_init(argc, argv);

	ovdk_mempools_init();
	initialise_vport_array();

	run_command(argc - 6, argv + 6, commands);

	return 0;
}

