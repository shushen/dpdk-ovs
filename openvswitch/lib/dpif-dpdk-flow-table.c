/*
 * Copyright 2012-2014 Intel Corporation All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "dpif-dpdk-flow-table.h"
#include "flow.h"
#include "ovs-thread.h"
#include "datapath/dpdk/ovdk_config.h"

#define FILENAME "/tmp/dpdk_flow_table"
#define FILESIZE (sizeof(struct dpif_dpdk_flow_table_entry) * \
                  OVDK_FLOW_TABLE_SIZE)
#define INVALID -1
#define VALID    1
#define FLOW_TABLE_PERMISSIONS (S_IRUSR | S_IWUSR | S_IXUSR | \
                                S_IRGRP | S_IXGRP | \
                                S_IROTH | S_IXOTH)

#define flow_keys_cmp(key1, key2) \
        (memcmp((key1), (key2), sizeof(struct flow)))
#define flow_keys_cpy(key1, key2) \
        (memcpy((key1), (key2), sizeof(struct flow)))

/* A dpif_dpdk_flow_table_entry maps a unique flow 'key' to a flow 'handle'
 * (a pointer to the flow table entry stored in the datapath). When a flow is
 * added to a pipeline, it is stored in that pipeline's flow table - the
 * datapath returns the pointer to that entry to the daemon, where it is stored
 * in a dpif_dpdk_flow_table. The daemon consults this table whenever it needs
 * to send a request to the datapath involving the flow, e.g. to delete, get, or
 * dump a flow.
 */
struct dpif_dpdk_flow_table_entry {
    struct flow key;
    uint64_t handle;
    int valid;
};

/* The DPIF flow table stores a list of type dpdk_flow_table_entry - operation
 * is as described in dpdk_flow_table_entry.
 */
struct dpif_dpdk_flow_table {
    struct dpif_dpdk_flow_table_entry entries[OVDK_FLOW_TABLE_SIZE];
};

static struct dpif_dpdk_flow_table *flow_table;
static struct ovs_mutex dpif_dpdk_flow_table_mutex = OVS_MUTEX_INITIALIZER;
static int fd = 0;

/* Create the DPIF flow table.
 *
 * The flow table is stored in a memory-mapped file to allow it to persist if
 * the vswitchd is not running.
 *
 * On success, returns 0.
 * Returns -1 if: flow table file cannot be created
 *                lseek system call fails
 *                flow table file cannot be extended sufficiently
 *                flow table file cannot be memory-mapped
 */
int
dpif_dpdk_flow_table_construct()
{
    int ret = -1;
    int i = 0;

    fd = open(FILENAME, O_RDWR | O_CREAT, FLOW_TABLE_PERMISSIONS);
    if (fd < 0) {
        return -errno;
    }

    /* Increase the offset of the file's endpoint */
    ret = lseek(fd, FILESIZE, SEEK_SET);
    if (ret == (FILESIZE - 1)) {
        return -errno;
    }

    /* Although the file's endpoint offset has been adjusted, its size won't
     * actually increase until it is written to.
     */
    ret = write(fd, "", 1);
    if (ret == 0) {
        return -errno;
    }

    flow_table = (struct dpif_dpdk_flow_table *)(mmap(0, FILESIZE,
                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    if (flow_table == MAP_FAILED) {
        close(fd);
        remove(FILENAME);
        return -errno;
    }

    memset(flow_table, 0, FILESIZE);
    for (i = 0; i < OVDK_FLOW_TABLE_SIZE; i++) {
        flow_table->entries[i].valid = INVALID;
    }

    ovs_mutex_init(&dpif_dpdk_flow_table_mutex);

    return 0;
}

/* Open the DPIF flow table.
 *
 * On success, returns 0.
 * If the open or mmap operations fail, returns -errno.
 */
int
dpif_dpdk_flow_table_open(void)
{
    fd = open(FILENAME, O_RDWR, FLOW_TABLE_PERMISSIONS);

    if (fd < 0) {
        return -errno;
    }

    flow_table = (struct dpif_dpdk_flow_table *)(mmap(0, FILESIZE,
                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    if (flow_table == MAP_FAILED) {
        close(fd);
        return -errno;
    }

    return 0;
}

/* Unmap, close and remove the DPIF flow table file.
 *
 * On success, returns 0.
 * Returns -errno if: the flow table file cannot be unmapped
 *                    the flow table file descriptor cannot be closed
 *                    the flow table file cannot be removed
 */
int
dpif_dpdk_flow_table_destroy(void)
{
    int ret = 0;

    ovs_mutex_destroy(&dpif_dpdk_flow_table_mutex);

    ret = munmap(flow_table, FILESIZE);
    if (ret == -1) {
        return -errno;
    }

    ret = close(fd);
    if (ret == -1) {
        return -errno;
    }

    ret = remove(FILENAME);

    if (ret == -1) {
        return -errno;
    } else {
        return 0;
    }
}

/* Unmap and close the DPIF flow table file.
 *
 * On success, returns 0.
 * Returns -errno if: the flow table file cannot be unmapped
 *                    the flow table file descriptor cannot be closed
 *                    the flow table file cannot be removed
 */
int
dpif_dpdk_flow_table_close(void)
{
    int ret = 0;

    ovs_mutex_destroy(&dpif_dpdk_flow_table_mutex);

    /* If a memory unmap fails, we still want to continue. Perhaps only
     * the unmapping has failed and the file still exists */
    if (munmap(flow_table, FILESIZE) == -1) {
        ret = -errno;
    }

    /* Check if file is open before trying to close it */
    if(fcntl(fd, F_GETFD) != -1 && errno != EBADF) {
        if (close(fd) == -1) {
            ret = -errno;  /* Keep only the most recent error code */
        }
    }

    return ret;
}

/* Add an entry ('key', 'flow_handle') to the DPIF flow table.
 *
 * On success, returns 0.
 * If an entry already exists for 'key', returns -EEXIST.
 * If the flow_table is full, returns -ENOSPC.
 */
int
dpif_dpdk_flow_table_entry_add(struct flow *key, uint64_t *flow_handle)
{
    int i = 0;
    int ret = 0;

    if (key == NULL || flow_handle == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_flow_table_mutex);

    for (i = 0; i < OVDK_FLOW_TABLE_SIZE; i++) {
        if (flow_keys_cmp(&flow_table->entries[i].key, key) == 0 &&
            flow_table->entries[i].valid == VALID) {
            ret = -EEXIST;
            goto end;
        } else if ( flow_table->entries[i].valid == INVALID) {
            flow_keys_cpy(&flow_table->entries[i].key, key);
            flow_table->entries[i].handle = *flow_handle;
            flow_table->entries[i].valid = VALID;
            ret = 0;
            goto end;
        }
    }
    ret = -ENOSPC;

end:
    ovs_mutex_unlock(&dpif_dpdk_flow_table_mutex);
    return ret;
}

/* Find the flow handle indexed by 'key' in the DPIF flow table.
 *
 * If found, store the handle's address in 'flow_handle' and return 0.
 * If no matching entry is found return -ENOENT.
 * If 'flow_handle' is NULL, return -EINVAL.
 */
int
dpif_dpdk_flow_table_entry_find(struct flow *key, uint64_t *flow_handle)
{
    int i = 0;
    int ret = 0;

    if (key == NULL || flow_handle == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_flow_table_mutex);

    for (i = 0; i < OVDK_FLOW_TABLE_SIZE; i++) {
        if ((flow_keys_cmp(&flow_table->entries[i].key, key) == 0) &&
           (flow_table->entries[i].valid == VALID)) {
            *flow_handle = flow_table->entries[i].handle;
            ret = 0;
            goto end;
        }
    }
    ret = -ENOENT;

end:
    ovs_mutex_unlock(&dpif_dpdk_flow_table_mutex);
    return ret;
}

/* Find the next entry in the DPIF flow table.
 *
 * On success, stores the members of the next table entry in *key and
 * *flow_handle; also stores the index of the entry in *index, and returns
 * -EAGAIN to alert the calling function that the end of the table hasn't been
 * reached, implying that additonal entries potentially exist.
 * 'index' represents the position in the table at which the last entry was
 * found. If *index is UINT16_MAX, it signifies that the search for the next
 * entry should begin at entry 0 of the flow_table.
 * If any of the supplied arguments are NULL, returns -EINVAL.
 * If no entry is found, returns 0.
 */
int
dpif_dpdk_flow_table_entry_next(struct flow *key, uint64_t *flow_handle,
                                    uint16_t *index)
{
    int i = 0;
    int ret = 0;

    if (key == NULL || flow_handle == NULL || index == NULL) {
        return -EINVAL;
    }

    if (*index == UINT16_MAX) {
        i = 0;
    } else {
        i = ++(*index);
    }

    ovs_mutex_lock(&dpif_dpdk_flow_table_mutex);

    for (; i < OVDK_FLOW_TABLE_SIZE; i++) {
        if (flow_table->entries[i].valid == VALID) {
            flow_keys_cpy(key, &flow_table->entries[i].key);
            *flow_handle = flow_table->entries[i].handle;
            *index = i;
            ret = -EAGAIN;
            goto end;
        }
    }
    ret = 0;

end:
    ovs_mutex_unlock(&dpif_dpdk_flow_table_mutex);
    return ret;
}

/* Delete the entry indexed by 'key' from the DPIF flow table.
 *
 * On success, return 0.
 * If no entry matching 'key' is found, return -ENOENT.
 */
int
dpif_dpdk_flow_table_entry_del(struct flow *key)
{
    int i = 0;
    int ret = 0;

    if (key == NULL) {
        return -EINVAL;
    }

    ovs_mutex_lock(&dpif_dpdk_flow_table_mutex);

    for (i = 0; i < OVDK_FLOW_TABLE_SIZE; i++) {
        if (flow_keys_cmp(&flow_table->entries[i].key, key) == 0 &&
            flow_table->entries[i].valid == VALID) {
            flow_table->entries[i].valid = INVALID;
            ret = 0;
            goto end;
        }
    }
    ret = -ENOENT;

end:
    ovs_mutex_unlock(&dpif_dpdk_flow_table_mutex);
    return ret;
}

