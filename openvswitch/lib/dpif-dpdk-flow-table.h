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

#ifndef __DPIF_DPDK_FLOW_TABLE_H__
#define __DPIF_DPDK_FLOW_TABLE_H__

#include <stdint.h>

#include "dpif.h"

int dpif_dpdk_flow_table_construct(void);
int dpif_dpdk_flow_table_destroy(void);
int dpif_dpdk_flow_table_open(void);
int dpif_dpdk_flow_table_close(void);
int dpif_dpdk_flow_table_entry_add(struct flow *key, uint64_t *flow_handle);
int dpif_dpdk_flow_table_entry_find(struct flow *key, uint64_t *flow_handle);
int dpif_dpdk_flow_table_entry_next(struct flow *key, uint64_t *flow_handle,
                                    uint16_t *index);
int dpif_dpdk_flow_table_entry_del(struct flow *key);

#endif
