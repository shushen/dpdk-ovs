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

#ifndef __OVDK_STATS_H_
#define __OVDK_STATS_H_

#include <stdint.h>
#include <rte_mbuf.h>

#include "ovdk_stats_types.h"

void ovdk_stats_init(void);
void ovdk_stats_fini(void);
void ovdk_stats_clear(void);
void ovdk_stats_vport_clear_all(void);
void ovdk_stats_vport_clear(unsigned vportid);
void ovdk_stats_vswitch_clear(void);

int ovdk_stats_port_out_update(struct rte_mbuf *pkts, uint64_t *pkts_mask,
                               void *arg);
int ovdk_stats_port_out_update_bulk(struct rte_mbuf **pkts, uint64_t *pkts_mask,
                               void *arg);
int ovdk_stats_port_in_update(struct rte_mbuf **pkts, uint32_t n,
                              uint64_t *pkts_mask, void *arg);

/*
 * Vport statistics
 */

void ovdk_stats_vport_rx_increment(unsigned vportid, unsigned inc);
void ovdk_stats_vport_rx_drop_increment(unsigned vportid, unsigned inc);
void ovdk_stats_vport_tx_increment(unsigned vportid, unsigned inc);
void ovdk_stats_vport_tx_drop_increment(unsigned vportid, unsigned inc);
void ovdk_stats_vport_overrun_increment(unsigned vportid, unsigned inc);
uint64_t ovdk_stats_vport_rx_get(unsigned vportid);
uint64_t ovdk_stats_vport_rx_drop_get(unsigned vportid);
uint64_t ovdk_stats_vport_tx_get(unsigned vportid);
uint64_t ovdk_stats_vport_tx_drop_get(unsigned vportid);
uint64_t ovdk_stats_vport_overrun_get(unsigned vportid);

int ovdk_stats_vport_get(unsigned vportid, struct ovdk_port_stats *stats);

/*
 * Vswitch control message statistics
 */
void ovdk_stats_vswitch_control_clear(void);
void ovdk_stats_vswitch_control_rx_increment(unsigned inc);
void ovdk_stats_vswitch_control_rx_drop_increment(unsigned inc);
void ovdk_stats_vswitch_control_tx_increment(unsigned inc);
void ovdk_stats_vswitch_control_tx_drop_increment(unsigned inc);
void ovdk_stats_vswitch_control_overrun_increment(unsigned inc);
uint64_t ovdk_stats_vswitch_control_rx_drop_get(void);
uint64_t ovdk_stats_vswitch_control_tx_drop_get(void);
uint64_t ovdk_stats_vswitch_control_rx_get(void);
uint64_t ovdk_stats_vswitch_control_tx_get(void);
uint64_t ovdk_stats_vswitch_control_overrun_get(void);

/*
 * Vswitch data message statistics
 */
void ovdk_stats_vswitch_data_clear(void);
void ovdk_stats_vswitch_data_rx_increment(unsigned inc);
void ovdk_stats_vswitch_data_rx_drop_increment(unsigned inc);
void ovdk_stats_vswitch_data_tx_increment(unsigned inc);
void ovdk_stats_vswitch_data_tx_drop_increment(unsigned inc);
void ovdk_stats_vswitch_data_overrun_increment(unsigned inc);
uint64_t ovdk_stats_vswitch_data_rx_drop_get(void);
uint64_t ovdk_stats_vswitch_data_tx_drop_get(void);
uint64_t ovdk_stats_vswitch_data_rx_get(void);
uint64_t ovdk_stats_vswitch_data_tx_get(void);
uint64_t ovdk_stats_vswitch_data_overrun_get(void);

/*
 * Display stats to the screen
 */
void ovdk_stats_display(void);
#endif /* __OVDK_STATS_H_ */

