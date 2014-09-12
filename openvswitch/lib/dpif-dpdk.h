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

#ifndef DPIF_DPDK_H
#define DPIF_DPDK_H 1

#include "datapath/dpdk/ovdk_stats_types.h"

int dpif_dpdk_port_get_stats(const char *name, struct ovdk_port_stats *stats);

#endif /* dpif-dpdk.h */
