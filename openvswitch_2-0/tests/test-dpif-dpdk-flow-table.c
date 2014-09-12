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

#include <stdint.h>
#include <config.h>
#include <assert.h>
#include <errno.h>

#include "flow.h"
#include "command-line.h"
#include "dpif-dpdk-flow-table.h"
#include "datapath/dpdk/ovdk_config.h"
#include "vlog.h"

void test_construct__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_destroy__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_open__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_add__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_add__key_exists(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_add__table_full(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_add__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_add__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_find__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_find__key_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_find__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_find__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_find__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_next__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_next__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_next__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_next__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_next__index_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_del__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_del__key_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_del__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);
void test_del__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED);

static inline void
init_table(void)
{
	int ret = 0;

	ret = dpif_dpdk_flow_table_construct();
	assert(ret == 0);
}

static inline void
remove_table(void)
{
	int ret = 0;

	ret = dpif_dpdk_flow_table_destroy();
	assert(ret == 0);
}

/* Global flow handle used in a number of tests -
 * the assigned value is arbitrary */
static uint64_t flow_handle = UINT64_MAX - 10;
static uint64_t *handle = &flow_handle;

/* Global flow key used in a number of tests -
 * the field values are arbitrary*/
static struct flow flow_key = {
	.nw_src = 0x01010101,
	.nw_dst = 0x01010102,
	.in_port.ofp_port = 1,
	.dl_type = 0x800,
	.dl_src[0] = 0xDE,
	.dl_src[1] = 0xAD,
	.dl_src[2] = 0xBE,
	.dl_src[3] = 0xEF,
	.dl_src[4] = 0xCA,
	.dl_src[5] = 0xFE,
	.dl_dst[0] = 0xD0,
	.dl_dst[1] = 0x0D,
	.dl_dst[2] = 0xFA,
	.dl_dst[3] = 0xCE,
	.dl_dst[4] = 0xBE,
	.dl_dst[5] = 0xEF,
};
static struct flow *key = &flow_key;

/* Construct a flow key containing (arbitrary) predefined fields.
 * 'in_port' allows for the creation of different keys, for tests
 * in which unique keys are required.
 */
static inline void
flow_key_construct(struct flow *key, uint16_t in_port)
{
	key->nw_src = 0x010101A1;
	key->nw_dst = 0x010101A2;
	key->in_port.ofp_port = in_port;
	key->dl_type = 0x800;
	key->dl_src[0] = 0xDE;
	key->dl_src[1] = 0xAD;
	key->dl_src[2] = 0xBE;
	key->dl_src[3] = 0xEF;
	key->dl_src[4] = 0xCA;
	key->dl_src[5] = 0xFE;
	key->dl_dst[0] = 0xD0;
	key->dl_dst[1] = 0x0D;
	key->dl_dst[2] = 0xFA;
	key->dl_dst[3] = 0xCE;
	key->dl_dst[4] = 0xBE;
	key->dl_dst[5] = 0xEF;
}

static inline void
add_entry(struct flow *k, uint64_t *h)
{
	int result = -1;

	result = dpif_dpdk_flow_table_entry_add(k, h);
	assert(result == 0);
}

static inline void
add_entries(uint64_t num_entries)
{
	struct flow new_key;
	uint64_t i = 0, new_handle = 0;;

	for (i = 0; i < num_entries; i++) {
		flow_key_construct(&new_key, i);
		new_handle = i;
		add_entry(&new_key, &new_handle);
	}
}


void
test_construct__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	dpif_dpdk_flow_table_destroy();
}

void
test_destroy__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	/* Create a flow info table for testing */
	result = dpif_dpdk_flow_table_construct();
	assert(result == 0);

	result = dpif_dpdk_flow_table_destroy();
	assert(result == 0);
}

void
test_open__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	result = dpif_dpdk_flow_table_open();
	assert(result == 0);

	remove_table();
}

void
test_add__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_add(key, handle);
	assert(result == 0);
	/* Ensure that entry was added to table */
	dpif_dpdk_flow_table_entry_find(key, &ret_handle);
	assert(ret_handle == *handle);

	remove_table();
}

void
test_add__key_exists(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;

	init_table();

	/* Add entry to table */
	result = dpif_dpdk_flow_table_entry_add(key, handle);
	assert(result == 0);
	/* Ensure that entry was added to table */
	dpif_dpdk_flow_table_entry_find(key, &ret_handle);
	assert(ret_handle == *handle);

	/* Attempt to add entry containing key already present in table  */
	ret_handle++;
	result = dpif_dpdk_flow_table_entry_add(key, &ret_handle);
	assert(result == -EEXIST);

	remove_table();
}

void
test_add__table_full(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t i = 0;

	init_table();

	/* Fill up the table */
	add_entries(OVDK_FLOW_TABLE_SIZE);

	/* Attempt to add another entry */
	result = dpif_dpdk_flow_table_entry_add(key, &i);
	assert(result == -ENOSPC);

	remove_table();
}

void
test_add__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	/* Add entry to table */
	result = dpif_dpdk_flow_table_entry_add(NULL, handle);
	assert(result == -EINVAL);

	remove_table();
}

void
test_add__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	/* Add entry to table */
	result = dpif_dpdk_flow_table_entry_add(key, NULL);
	assert(result == -EINVAL);

	remove_table();
}

void
test_del__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	add_entry(key, handle);

	result = dpif_dpdk_flow_table_entry_del(key);
	assert(result == 0);

	remove_table();
}

void
test_del__key_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	/*
	 * Populate the table with some entries - the keys added are guaranteed
	 * to be different than 'key' */
	add_entries(3);

	/*
	 * Attempt to delete an entry indexed by 'key', which is not present in
	 * the flow table */
	result = dpif_dpdk_flow_table_entry_del(key);
	assert(result == -ENOENT);

	remove_table();
}

void
test_del__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	result = dpif_dpdk_flow_table_entry_del(key);
	assert(result == -ENOENT);

	remove_table();
}

void
test_del__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	result = dpif_dpdk_flow_table_entry_del(NULL);
	assert(result == -EINVAL);

	remove_table();
}

void
test_find__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;

	init_table();

	/* Ensure that at least one key is present in the table */
	add_entry(key, handle);

	result = dpif_dpdk_flow_table_entry_find(key, &ret_handle);
	assert(result == 0);
	assert(ret_handle == *handle);

	remove_table();
}

void
test_find__key_not_found(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;
	struct flow other_key;

	init_table();

	/* Ensure that at least one key is present in the table */
	add_entry(key, handle);

	flow_key_construct(&other_key, 1);
	result = dpif_dpdk_flow_table_entry_find(&other_key, &ret_handle);
	assert(result == -ENOENT);

	remove_table();
}

void
test_find__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_find(key, &ret_handle);
	assert(result == -ENOENT);

	remove_table();
}

void
test_find__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_find(NULL, &ret_handle);
	assert(result == -EINVAL);

	remove_table();
}

void
test_find__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;

	init_table();

	result = dpif_dpdk_flow_table_entry_find(key, NULL);
	assert(result == -EINVAL);

	remove_table();
}

void
test_next__default(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct flow key1, key2, ret_key;
	uint64_t handle1    = 1,
	         handle2    = 2,
	         ret_handle = 0;
	uint16_t ofp_port1  = 1,
	         ofp_port2  = 2,
	         index      = UINT16_MAX;

	flow_key_construct(&key1, ofp_port1);
	flow_key_construct(&key2, ofp_port2);

	init_table();

	/* Add entries which will be subsequently retrieved */
	add_entry(&key1, &handle1);
	add_entry(&key2, &handle2);

	/* Retrieve entries */
	result = dpif_dpdk_flow_table_entry_next(&ret_key, &ret_handle, &index);
	assert(result == -EAGAIN);
	assert(index  == 0);
	assert(ret_handle == handle1);
	assert(key1.in_port.ofp_port == ofp_port1);

	result = dpif_dpdk_flow_table_entry_next(&ret_key, &ret_handle, &index);
	assert(result == -EAGAIN);
	assert(index  == 1);
	assert(ret_handle == handle2);
	assert(key2.in_port.ofp_port == ofp_port2);

	remove_table();
}

void
test_next__table_empty(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct flow ret_key;
	uint64_t ret_handle = 0;
	uint16_t index      = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_next(&ret_key, &ret_handle, &index);
	assert(result == 0 );

	remove_table();
}

void
test_next__key_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	uint64_t ret_handle = 0;
	uint16_t index      = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_next(NULL, &ret_handle, &index);
	assert(result == -EINVAL);

	remove_table();
}

void
test_next__handle_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct flow ret_key;
	uint16_t index = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_next(&ret_key, NULL, &index);
	assert(result == -EINVAL);

	remove_table();
}

void
test_next__index_null(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
	int result = -1;
	struct flow ret_key;
	uint64_t ret_handle = 0;

	init_table();

	result = dpif_dpdk_flow_table_entry_next(&ret_key, &ret_handle, NULL);
	assert(result == -EINVAL);

	remove_table();
}

static const struct command commands[] = {
	/* Table tests */
	{"construct-default",  0, 0, test_construct__default},
	{"destroy-default",    0, 0, test_destroy__default},
	{"open-default",       0, 0, test_open__default},
	/* Table entry tests */
	{"add-default",        0, 0, test_add__default},
	{"add-key-exists",     0, 0, test_add__key_exists},
	{"add-table-full",     0, 0, test_add__table_full},
	{"add-key-null",       0, 0, test_add__key_null},
	{"add-handle-null",    0, 0, test_add__handle_null},
	{"del-default",        0, 0, test_del__default},
	{"del-key-not-found",  0, 0, test_del__key_not_found},
	{"del-table-empty",    0, 0, test_del__table_empty},
	{"del-key-null",       0, 0, test_del__key_null},
	{"find-default",       0, 0, test_find__default},
	{"find-table-empty",   0, 0, test_find__default},
	{"find-key-not-found", 0, 0, test_find__key_not_found},
	{"find-key-null",      0, 0, test_find__key_null},
	{"find-handle-null",   0, 0, test_find__handle_null},
	{"next-default",       0, 0, test_next__default},
	{"next-table-empty",   0, 0, test_next__table_empty},
	{"next-key-null",      0, 0, test_next__key_null},
	{"next-handle-null",   0, 0, test_next__handle_null},
	{"next-index-null",    0, 0, test_next__index_null},
	{NULL, 0, 0, NULL},
};

int
main(int argc, char *argv[])
{
	set_program_name(argv[0]);

	vlog_set_levels(NULL, VLF_ANY_FACILITY, VLL_EMER);
	vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

	run_command(argc - 1, argv + 1, commands);

	return 0;
}

