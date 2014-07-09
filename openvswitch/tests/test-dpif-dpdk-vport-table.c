/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 Intel Corporation. All rights reserved.
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

#include <assert.h>
#include <errno.h>

#include "config.h"
#include "command-line.h"
#include "timeval.h"
#include "util.h"
#include "vlog.h"

#include "dpif-dpdk-vport-table.h"

/* Helpers */

static inline void
init_table(void)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_construct();
	assert(ret == 0);
}

static inline void
remove_table(void)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_destroy();
	assert(ret == 0);
}

/*
 * Add 'num_entries' vport entries of type 'type' to table. This assumes the
 * table has already been configured
 */
static inline void
add_entries(unsigned num_entries, enum ovdk_vport_type type)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";
	int max_entries = 0;
	int first_entry = 0;

	switch (type) {
	case OVDK_VPORT_TYPE_PHY:
		max_entries = OVDK_MAX_PHYPORTS;
		first_entry = type;
		break;
	case OVDK_VPORT_TYPE_CLIENT:
		max_entries = OVDK_MAX_CLIENTS;
		first_entry = type;
		break;
	case OVDK_VPORT_TYPE_VHOST:
		max_entries = OVDK_MAX_VHOSTS;
		first_entry = type;
		break;
	case OVDK_VPORT_TYPE_KNI:
		max_entries = OVDK_MAX_KNIS;
		first_entry = type;
		break;
	case OVDK_VPORT_TYPE_BRIDGE:
		max_entries = OVDK_MAX_BRIDGES;
		first_entry = type;
		break;
	case OVDK_VPORT_TYPE_VETH:
	case OVDK_VPORT_TYPE_MEMNIC:
	case OVDK_VPORT_TYPE_VSWITCHD:
	case OVDK_VPORT_TYPE_DISABLED:
	case OVDK_VPORT_TYPE_MAX:
		assert(1);
	}

	/* If this fails, you've tried adding too many num_entries */
	assert(num_entries <= max_entries);

	for (vportid = first_entry; vportid < first_entry + num_entries; vportid++) {
		ret = dpif_dpdk_vport_table_entry_add(type,
		                                      0, &name[0], &vportid);
		assert(ret == 0);
	}
}

/* Tests */

static void
test_construct__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_construct();
	assert(ret == 0);

	/* TODO - need to mock system functions like `open` to test
	 * this properly */

	/*
	 *There's obviously some interdependency here. However, there's not
	 * much to be done for it - we need to delete the created table. Just
	 * assume this works and don't check *yet*.
	 */
	dpif_dpdk_vport_table_destroy();
}

static void
test_destroy__after_close(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	/*
	 * There's obviously some interdependency here. However, there's not
	 * much to be done for it - we need to create a table to work with.
	 * Obviously if 'test_constuct__default' fails, this will fail.
	 */
	ret = dpif_dpdk_vport_table_construct();
	assert(ret == 0);

	/* Close first, then try to destroy */
	ret = dpif_dpdk_vport_table_close();
	assert(ret == 0);

	ret = dpif_dpdk_vport_table_destroy();
	assert(ret == 0);
}

static void
test_destroy__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	/*
	 * There's obviously some interdependency here. However, there's not
	 * much to be done for it - we need to create a table to work with.
	 * Obviously if 'test_constuct__default' fails, this will fail.
	 */
	ret = dpif_dpdk_vport_table_construct();
	assert(ret == 0);

	ret = dpif_dpdk_vport_table_destroy();
	assert(ret == 0);

	/* TODO - need to mock system functions like `open` to test
	 * this properly */
}

static void
test_open__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	init_table();

	ret = dpif_dpdk_vport_table_open();
	assert(ret == 0);

	/* TODO - need to mock system functions like `open` to test
	 * this properly */

	remove_table();
}

static void
test_close__no_file(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	/* don't call init */
	ret = dpif_dpdk_vport_table_close();
	assert(ret == 0);
}

static void
test_close__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	init_table();
	dpif_dpdk_vport_table_open();

	ret = dpif_dpdk_vport_table_close();
	assert(ret == 0);

	/* TODO - need to mock system functions like `open` to test
	 * this properly */
	remove_table();
}

static void
test_exists__file_error(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_exists();
	assert(ret != 0);  /* don't really care what error is thrown */

	/* TODO - check for inaccessible files */
}

static void
test_exists__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	init_table();  /* we have to assume this works */

	ret = dpif_dpdk_vport_table_exists();
	assert(ret == 0);  /* don't really care what error is thrown */

	remove_table();
}

static void
test_add__invalid_type(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	/* boundary check */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_MAX, 0,
	                                      &name[0], &vportid);
	assert(ret == -EINVAL);

	/*
	 * we can assume that we will never have more than UINT32_MAX types
	 * of devices, hence this is a valid out of bounds check
	 */
	ret = dpif_dpdk_vport_table_entry_add(UINT32_MAX, 0,
	                                      &name[0], &vportid);
	assert(ret == -EINVAL);
}

static void
test_add__invalid_lcoreid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	/*
	 * we can assume that we will never have more than UINT32_MAX lcores
	 * available, hence this is a valid out of bounds check
	 */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_BRIDGE,
	                                      UINT32_MAX, &name[0], &vportid);
	assert(ret == -EINVAL);

	/* TODO - add boundary check */
}

static void
test_add__invalid_name(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = UINT32_MAX;

	/* null check */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_BRIDGE,
	                                      0, NULL, &vportid);
	assert(ret == -EINVAL);
}

static void
test_add__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = UINT32_MAX;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	/* boundary check */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_BRIDGE,
	                                      0, &name[0], NULL);
	assert(ret == -EINVAL);

	/*
	 * we can assume that we will never have more than UINT32_MAX vports
	 * available, hence this is a valid out of bounds check
	 */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_BRIDGE, 0,
	                                      &name[0], &vportid);
	assert(ret == -EINVAL);
}

static void
test_add__wraparound(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	init_table();

	vportid = OVDK_VPORT_TYPE_CLIENT + OVDK_MAX_CLIENTS - 1;
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT,
	                                      0, &name[0], &vportid);
	assert(ret == 0);

	/* Should "wrap around" and get the next value in range */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT,
	                                      0, &name[0], &vportid);
	assert(ret == 0);
	assert(vportid == OVDK_VPORT_TYPE_CLIENT);

	remove_table();
}

static void
test_add__table_full(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	init_table();

	/* Add maximum amount of entries */
	add_entries(OVDK_MAX_CLIENTS, OVDK_VPORT_TYPE_CLIENT);

	/*
	 * Now we try to search for another vport of type client. We dont
	 * care about which vportid to use
	 */
	vportid = OVDK_MAX_VPORTS;
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT,
	                                      0, &name[0], &vportid);
	assert(ret == -ENOENT);

	remove_table();
}

static void
test_add__duplicate_phy_entry(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	init_table();

	/* Add a phy entry */
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	/*
	 * Try to add a phy entry with the same vportid
	 */
	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_PHY,
	                                      0, &name[0], &vportid);
	assert(ret == -EBUSY);

	remove_table();
}

static void
test_add__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	init_table();

	ret = dpif_dpdk_vport_table_entry_add(OVDK_VPORT_TYPE_CLIENT,
	                                      0, &name[0], &vportid);
	assert(ret == 0);

	remove_table();
}

static void
test_get_next_inuse__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;

	init_table();

	ret = dpif_dpdk_vport_table_entry_get_next_inuse(NULL);
	assert(ret == -EINVAL);

	remove_table();
}


static void
test_get_next_inuse__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	const unsigned num_entries = 3;
	unsigned i = 0;
	/* Use top end value in order to get the first vportid */
	uint32_t vportid = 0;

	init_table();

	/* First check with a non-existent table entry */
	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret == -ENOENT);

	/* Add n entries, then remove these n entries one by one and assert it
	 * fails when all are removed */
	add_entries(num_entries, OVDK_VPORT_TYPE_PHY);

	vportid = 0;
	ret = dpif_dpdk_vport_table_entry_reset(vportid);
	assert(ret == 0);
	for (i = 0; i < num_entries - 1; i++) {
		ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
		assert(ret == 0);
		ret = dpif_dpdk_vport_table_entry_reset(vportid);
		assert(ret == 0);
	}
	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret = -ENOENT);

	remove_table();
}

static void
test_get_next_inuse__wraparound(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	/* An arbitrary number of vports */
	const unsigned num_entries = 13;
	uint32_t vportid = num_entries;

	init_table();
	add_entries(num_entries, OVDK_VPORT_TYPE_PHY);


	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret == -ENOENT);
	assert(vportid == OVDK_MAX_VPORTS);

	remove_table();
}

static void
test_get_next_inuse__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	/* An arbitrary number of vports */
	const unsigned num_entries = 13;
	uint32_t vportid = OVDK_VPORT_TYPE_PHY;

	init_table();
	add_entries(num_entries, OVDK_VPORT_TYPE_PHY);

	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret == 0);
	assert(vportid == OVDK_VPORT_TYPE_PHY);

	remove_table();
}
static void
test_reset__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	init_table();

	ret = dpif_dpdk_vport_table_entry_reset(OVDK_MAX_VPORTS);
	assert(ret == -EINVAL);

	remove_table();
}

static void
test_reset__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	const unsigned num_entries = 2;
	uint32_t vportid = 0;

	init_table();
	add_entries(num_entries, OVDK_VPORT_TYPE_PHY);

	ret = dpif_dpdk_vport_table_entry_reset(vportid);
	assert(ret == 0);

	/* Now when we check, that it should return one result then ENOENT, by
	 * assuming that get_next_inuse is working correctly */
	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret == 0);

	/* vportid is valid. Now we start from the next value */
	vportid++;

	ret = dpif_dpdk_vport_table_entry_get_next_inuse(&vportid);
	assert(ret == -ENOENT);

	remove_table();
}

static void
test_get_lcore_id__invalid_vportid(int argc OVS_UNUSED,
                                         char *argv[] OVS_UNUSED)
{
	int ret = 0;
	unsigned lcore_id = 0;

	ret = dpif_dpdk_vport_table_entry_get_lcore_id(UINT32_MAX, &lcore_id);
	assert(ret == -EINVAL);
}

static void
test_get_lcore_id__invalid_lcoreid(int argc OVS_UNUSED,
                                         char *argv[] OVS_UNUSED)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_entry_get_lcore_id(0, NULL);
	assert(ret == -EINVAL);
}

static void
test_get_lcore_id__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	unsigned lcore_id = UINT32_MAX;

	init_table();
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	/* Assume the element has been added at index 0 */
	ret = dpif_dpdk_vport_table_entry_get_lcore_id(vportid, &lcore_id);
	assert(ret == 0);
	/* We don't care what value has been set, so long as it's been set */
	assert(lcore_id != UINT32_MAX);
}

static void
test_get_first_inuse__invalid_params(int argc OVS_UNUSED,
                                     char *argv[] OVS_UNUSED)
{
	int ret = 0;

	ret = dpif_dpdk_vport_table_entry_get_first_inuse(NULL);
	assert(ret == -EINVAL);
}

static void
test_get_first_inuse__table_empty(int argc OVS_UNUSED,
                                  char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;

	init_table();

	ret = dpif_dpdk_vport_table_entry_get_first_inuse(&vportid);
	assert(ret == -ENOENT);
}

static void
test_get_first_inuse__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = UINT32_MAX;

	init_table();
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	/* Assume the element has been added at index 0 */
	ret = dpif_dpdk_vport_table_entry_get_first_inuse(&vportid);
	assert(ret == 0);
	/* We don't care what value has been set, so long as it's been set */
	assert(vportid != UINT32_MAX);
}

static void
test_get_type__invalid_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = OVDK_MAX_VPORTS;
	enum ovdk_vport_type type = OVDK_VPORT_TYPE_CLIENT;

	ret = dpif_dpdk_vport_table_entry_get_type(vportid, &type);
	assert(ret == -EINVAL);

	vportid = UINT32_MAX;
	ret = dpif_dpdk_vport_table_entry_get_type(vportid, &type);
	assert(ret == -EINVAL);

	vportid = 0;
	ret = dpif_dpdk_vport_table_entry_get_type(vportid, NULL);
	assert(ret == -EINVAL);
}

static void
test_get_type__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	enum ovdk_vport_type type = OVDK_VPORT_TYPE_CLIENT;

	init_table();
	add_entries(1, OVDK_VPORT_TYPE_PHY); /* this adds a phy-type port */

	/* Assume the element has been added at index 0 */
	ret = dpif_dpdk_vport_table_entry_get_type(vportid, &type);
	assert(ret == 0);
	assert(type == OVDK_VPORT_TYPE_PHY);
}

static void
test_get_name__invalid_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = OVDK_MAX_VPORTS;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	ret = dpif_dpdk_vport_table_entry_get_name(vportid, &name[0]);
	assert(ret == -EINVAL);

	vportid = UINT32_MAX;
	ret = dpif_dpdk_vport_table_entry_get_name(vportid, &name[0]);
	assert(ret == -EINVAL);

	vportid = 0;
	ret = dpif_dpdk_vport_table_entry_get_name(vportid, NULL);
	assert(ret == -EINVAL);
}

static void
test_get_name__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	uint32_t vportid = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";

	init_table();
	/* this adds all ports with name "dummyname" */
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	/* Assume the element has been added at index 0 */
	ret = dpif_dpdk_vport_table_entry_get_name(vportid, &name[0]);
	assert(ret == 0);
	printf("%s\n", name);
	assert(strncmp(name, "dummyname", OVDK_MAX_VPORT_NAMESIZE) == 0);
}

static void
test_get_vportid__invalid_params(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";
	uint32_t vportid = 0;

	ret = dpif_dpdk_vport_table_entry_get_vportid(NULL, &vportid);
	assert(ret == -EINVAL);

	ret = dpif_dpdk_vport_table_entry_get_vportid(&name[0], NULL);
	assert(ret == -EINVAL);
}

static void
test_get_vportid__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	char name[OVDK_MAX_VPORT_NAMESIZE] = "dummyname";
	uint32_t vportid = UINT32_MAX;

	init_table();
	/* this adds all ports with name "dummyname" */
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	/* Assume the element has been added at index 0 */
	ret = dpif_dpdk_vport_table_entry_get_vportid(&name[0], &vportid);
	assert(ret == 0);
	/* We don't care what value has been set, so long as it's been set */
	assert(vportid != UINT32_MAX);
}

static void
test_set_inuse__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	int vportid = 1;
	bool inuse = true;

	init_table();
	/* this adds all ports with name "dummyname" */
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	ret = dpif_dpdk_vport_table_entry_set_inuse(vportid, false);
	assert(ret == 0);

	ret = dpif_dpdk_vport_table_entry_get_inuse(vportid, &inuse);
	assert(ret == 0);
	assert(inuse == false);
}

static void
test_set_inuse__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	int vportid = UINT32_MAX;

	ret = dpif_dpdk_vport_table_entry_set_inuse(vportid, false);
	assert(ret == -EINVAL);

}

static void
test_get_inuse__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	int vportid = 1;
	bool inuse = true;

	init_table();
	/* this adds all ports with name "dummyname" */
	add_entries(1, OVDK_VPORT_TYPE_PHY);

	ret = dpif_dpdk_vport_table_entry_set_inuse(vportid, false);
	assert(ret == 0);

	ret = dpif_dpdk_vport_table_entry_get_inuse(vportid, &inuse);
	assert(ret == 0);
	assert(inuse == false);
}

static void
test_get_inuse__invalid_vportid(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	int vportid = UINT32_MAX;
	bool inuse = true;

	ret = dpif_dpdk_vport_table_entry_get_inuse(vportid, &inuse);
	assert(ret == -EINVAL);
}

static void
test_get_inuse__invalid_inuse(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int ret = 0;
	int vportid = 1;

	ret = dpif_dpdk_vport_table_entry_get_inuse(vportid, NULL);
	assert(ret == -EINVAL);
}

static const struct command commands[] = {
	/* Table tests */
	{"construct-default", 0, 0, test_construct__default},
	{"destroy-after-close", 0, 0, test_destroy__after_close},
	{"destroy-default", 0, 0, test_destroy__default},
	{"open-default", 0, 0, test_open__default},
	{"close-no-file", 0, 0, test_close__no_file},
	{"close-default", 0, 0, test_close__default},
	{"exists-file-error", 0, 0, test_exists__file_error},
	{"exists-default", 0, 0, test_exists__default},
	/* Table entry tests */
	{"add-invalid-type", 0, 0, test_add__invalid_type},
	{"add-invalid-lcoreid", 0, 0, test_add__invalid_lcoreid},
	{"add-invalid-vportid", 0, 0, test_add__invalid_vportid},
	{"add-invalid-name", 0, 0, test_add__invalid_name},
	{"add-wraparound", 0, 0, test_add__wraparound},
	{"add-duplicate-phy-entry", 0, 0, test_add__duplicate_phy_entry},
	{"add-table-full", 0, 0, test_add__table_full},
	{"add-default", 0, 0, test_add__default},
	{"get-next-inuse-invalid-vportid", 0, 0, test_get_next_inuse__invalid_vportid},
	{"get-next-inuse-table-empty", 0, 0, test_get_next_inuse__table_empty},
	{"get-next-inuse-wraparound", 0, 0, test_get_next_inuse__wraparound},
	{"get-next-inuse-default", 0, 0, test_get_next_inuse__default},
	{"reset-invalid-vportid", 0, 0, test_reset__invalid_vportid},
	{"reset-default", 0, 0, test_reset__default},
	{"set-inuse-default", 0, 0, test_set_inuse__default},
	{"set-inuse-invalid-vportid", 0, 0, test_set_inuse__invalid_vportid},
	{"get-inuse-default", 0, 0, test_get_inuse__default},
	{"get-inuse-invalid-vportid", 0, 0, test_get_inuse__invalid_vportid},
	{"get-inuse-invalid-inuse", 0, 0, test_get_inuse__invalid_inuse},
	{"get-lcoreid-invalid-vportid", 0, 0, test_get_lcore_id__invalid_vportid},
	{"get-lcoreid-invalid-lcoreid", 0, 0, test_get_lcore_id__invalid_lcoreid},
	{"get-lcoreid-default", 0, 0, test_get_lcore_id__default},
	{"get-first-inuse-invalid-params", 0, 0, test_get_first_inuse__invalid_params},
	{"get-first-inuse-table-empty", 0, 0, test_get_first_inuse__table_empty},
	{"get-first-inuse-default", 0, 0, test_get_first_inuse__default},
	{"get-type-invalid-params", 0, 0, test_get_type__invalid_params},
	{"get-type-default", 0, 0, test_get_type__default},
	{"get-name-invalid-params", 0, 0, test_get_name__invalid_params},
	{"get-name-default", 0, 0, test_get_name__default},
	{"get-vportid-invalid-params", 0, 0, test_get_vportid__invalid_params},
	{"get-vportid-default", 0, 0, test_get_vportid__default},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	set_program_name(argv[0]);
	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	time_alarm(10);

	run_command(argc - 1, argv + 1, commands);

	return 0;
}
