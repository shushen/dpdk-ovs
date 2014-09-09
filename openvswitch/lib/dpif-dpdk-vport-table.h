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

#ifndef __DPIF_DPDK_VPORT_TABLE_H_
#define __DPIF_DPDK_VPORT_TABLE_H_

#include <stdint.h>

#include "datapath/dpdk/ovdk_vport_types.h"

int dpif_dpdk_vport_table_construct(void);
int dpif_dpdk_vport_table_destroy(void);
int dpif_dpdk_vport_table_open(void);
int dpif_dpdk_vport_table_close(void);
int dpif_dpdk_vport_table_exists(void);
int dpif_dpdk_vport_table_entry_add(enum ovdk_vport_type type,
                                    unsigned *lcore_id,
                                    const char *name,
                                    uint32_t *vportid);
int dpif_dpdk_vport_table_entry_get_first_inuse(uint32_t *vportid);
int dpif_dpdk_vport_table_entry_get_next_inuse(uint32_t *vportid);
int dpif_dpdk_vport_table_entry_get_inuse(uint32_t vportid,
                                       bool *in_use);
int dpif_dpdk_vport_table_entry_reset(uint32_t vportid);
int dpif_dpdk_vport_table_entry_reset_lcore_id(uint32_t vportid);
int dpif_dpdk_vport_table_entry_set_inuse(uint32_t vportid,
                                           bool in_use);
int dpif_dpdk_vport_table_entry_get_lcore_id(uint32_t vportid,
                                             unsigned *lcore_id);
int dpif_dpdk_vport_table_entry_get_type(uint32_t vportid,
                                         enum ovdk_vport_type *type);
int dpif_dpdk_vport_table_entry_get_name(uint32_t vportid,
                                         char *name);
int dpif_dpdk_vport_table_entry_get_vportid(const char *name ,
                                            unsigned *vportid);
#endif /* __DPIF_DPDK_VPORT_TABLE_H_ */
