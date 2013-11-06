/*
 * Copyright 2012-2013 Intel Corporation All Rights Reserved.
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

/* Implementation of the DPIF interface for a Intel DPDK vSwitch. */

#include <rte_config.h>
#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_branch_prediction.h>

#include <inttypes.h>

#include "common.h"
#include "dpif-dpdk.h"
#include "dpdk-link.h"
#include "dpif-provider.h"
#include "flow.h"
#include "netlink.h"
#include "netdev-provider.h"
#include "odp-util.h"
#include "vlog.h"

#define VLAN_CFI 0x1000

#ifdef PG_DEBUG
#define DPDK_DEBUG() printf("DPIF-DPDK.c %s Line %d\n", __FUNCTION__, __LINE__);
#else
#define DPDK_DEBUG()
#endif

VLOG_DEFINE_THIS_MODULE(dpif_dpdk);

static void dpif_dpdk_flow_init(struct dpif_dpdk_flow_message *);
static int dpif_dpdk_flow_transact(struct dpif_dpdk_flow_message *request,
                                   struct dpif_dpdk_flow_message *reply);
static void dpif_dpdk_flow_get_stats(const struct dpif_dpdk_flow_message *,
                                     struct dpif_flow_stats *);
static void dpif_dpdk_flow_key_from_flow(struct dpif_dpdk_flow_key *,
                                         const struct flow *);
static void dpif_dpdk_flow_key_to_flow(const struct dpif_dpdk_flow_key *,
                                       struct flow *);
static int dpif_dpdk_init(void);
static void flow_message_get_create(const struct dpif *dpif_ OVS_UNUSED,
                                    const struct nlattr *key, size_t key_len,
                                    struct dpif_dpdk_flow_message *request);
static void flow_message_put_create(struct dpif *dpif OVS_UNUSED,
                                    enum dpif_flow_put_flags flags,
                                    const struct nlattr *key, size_t key_len,
                                    const struct nlattr *actions,
                                    size_t actions_len OVS_UNUSED,
                                    struct dpif_dpdk_flow_message *request);
static void flow_message_del_create(struct dpif_dpdk_flow_message *request,
                                    const struct nlattr *key, size_t key_len);
static void flow_message_flush_create(struct dpif_dpdk_flow_message *request);

static int
dpif_dpdk_open(const struct dpif_class *dpif_class_p, const char *name,
               bool create OVS_UNUSED, struct dpif **dpifp)
{
    int error = 0;

    if(dpif_class_p == NULL) {
        return EINVAL;
    }

    error = dpif_dpdk_init();

    DPDK_DEBUG()

    if (error) {
        return error;
    }

    *dpifp = xzalloc(sizeof(**dpifp));
    dpif_init(*dpifp, dpif_class_p, name, 0, 0);

    return 0;
}

static void
dpif_dpdk_close(struct dpif *dpif_)
{
    dpif_assert_class(dpif_, &dpif_dpdk_class);
    DPDK_DEBUG()

    free(dpif_);
}

static int
dpif_dpdk_destroy(struct dpif *dpif_ OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static void
dpif_dpdk_run(struct dpif *dpif OVS_UNUSED)
{
    DPDK_DEBUG()
}

static void
dpif_dpdk_wait(struct dpif *dpif OVS_UNUSED)
{
    DPDK_DEBUG()
}

static int
dpif_dpdk_get_stats(const struct dpif *dpif_ OVS_UNUSED,
                    struct dpif_dp_stats *stats)
{
    DPDK_DEBUG()

    if(stats == NULL){
        return EINVAL;
    }

    stats->n_hit = 0;
    stats->n_missed = 0;
    stats->n_lost = 0;
    stats->n_flows = 0;

    return 0;
}

static int
dpif_dpdk_port_add(struct dpif *dpif_, struct netdev *netdev,
                   uint16_t *port_no)
{
    const char *name = NULL;
    dpif_assert_class(dpif_, &dpif_dpdk_class);

    if((netdev == NULL) || (port_no == NULL)) {
        return EINVAL;
    }

    name = netdev_get_name(netdev);

    DPDK_DEBUG()

    /* If port name has DPDK_PORT_PREFIX as prefix, then use the
     * following uint16_t as the port number.
     *
     * If not, then return ENODEV.
     *
     * dpif_base_name is a special case for the bridge interface. */
    if (!strncmp(name, DPDK_PORT_PREFIX, DPDK_PORT_PREFIX_LEN)) {
        *port_no = strtoumax(name + DPDK_PORT_PREFIX_LEN,
                             NULL, BASE10);
    } else if (!strcmp(name, dpif_base_name(dpif_))) {
        *port_no = 0;
    } else {
        return ENODEV;
    }

    return 0;
}

static int
dpif_dpdk_port_del(struct dpif *dpif_ OVS_UNUSED, uint16_t port_no OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_port_query_by_number(const struct dpif *dpif OVS_UNUSED,
                               uint16_t port_no OVS_UNUSED,
                               struct dpif_port *dpif_port OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_port_query_by_name(const struct dpif *dpif, const char *devname,
                             struct dpif_port *dpif_port)
{
    uint16_t port_no = 0;
    dpif_assert_class(dpif, &dpif_dpdk_class);

    DPDK_DEBUG()
    if((dpif_port == NULL) || (devname == NULL)) {
        return EINVAL;
    }
    if (!strncmp(devname, DPDK_PORT_PREFIX, DPDK_PORT_PREFIX_LEN)) {
        port_no = strtoumax(devname + DPDK_PORT_PREFIX_LEN,
                            NULL, BASE10);
        dpif_port->type = xstrdup("dpdk");
    } else if (!strcmp(devname, dpif->base_name)) {
        port_no = 0;
        dpif_port->type = xstrdup("internal");
    } else {
        return ENODEV;
    }

    dpif_port->name = xstrdup(devname);
    dpif_port->port_no = port_no;

    return 0;
}

static int
dpif_dpdk_get_max_ports(const struct dpif *dpif OVS_UNUSED)
{
    DPDK_DEBUG()

    return MAX_VPORTS;
}

static int
dpif_dpdk_port_dump_start(const struct dpif *dpif_ OVS_UNUSED,
                          void **statep OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_port_dump_next(const struct dpif *dpif OVS_UNUSED,
                         void *state_ OVS_UNUSED,
                         struct dpif_port *dpif_port OVS_UNUSED)
{
    DPDK_DEBUG()

    return EOF;
}

static int
dpif_dpdk_port_dump_done(const struct dpif *dpif_ OVS_UNUSED,
                         void *state_ OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_port_poll(const struct dpif *dpif_ OVS_UNUSED,
                    char **devnamep OVS_UNUSED)
{
    DPDK_DEBUG()

    return EAGAIN;
}

static void
dpif_dpdk_port_poll_wait(const struct dpif *dpif_ OVS_UNUSED)
{
    DPDK_DEBUG()
}

/*
 * This function will initialize a dpif_dpdk_flow_message for get.
 */
static void
flow_message_get_create(const struct dpif *dpif_ OVS_UNUSED,
                        const struct nlattr *key, size_t key_len,
                        struct dpif_dpdk_flow_message *request)
{
    struct flow flow;

    dpif_dpdk_flow_init(request);
    request->cmd = OVS_FLOW_CMD_GET;

    odp_flow_key_to_flow(key, key_len, &flow);
    dpif_dpdk_flow_key_from_flow(&request->key, &flow);
}

static int
dpif_dpdk_flow_get(const struct dpif *dpif_,
                   const struct nlattr *key, size_t key_len,
                   struct ofpbuf **actionsp, struct dpif_flow_stats *stats)
{
    struct dpif_dpdk_flow_message request;
    struct dpif_dpdk_flow_message reply;
    int error = 0;
    dpif_assert_class(dpif_, &dpif_dpdk_class);

    DPDK_DEBUG()

    if (key == NULL) {
        return EINVAL;
    }

    flow_message_get_create(dpif_, key, key_len, &request);

    error =  dpif_dpdk_flow_transact(&request, &reply);
    if (!error) {
        if (stats) {
            dpif_dpdk_flow_get_stats(&reply, stats);
        }
        if (actionsp) {
            if (request.action.type == ACTION_OUTPUT) {
                nl_msg_put_u32(*actionsp, OVS_ACTION_ATTR_OUTPUT,
                                reply.action.data.output.port);
            }
        }
    }

    return error;
}

/*
 * This function will initialize a dpif_dpdk_flow_message for put.
 */
static void
flow_message_put_create(struct dpif *dpif OVS_UNUSED,
                        enum dpif_flow_put_flags flags,
                        const struct nlattr *key, size_t key_len,
                        const struct nlattr *actions,
                        size_t actions_len OVS_UNUSED,
                        struct dpif_dpdk_flow_message *request)
{
    struct flow flow;

    DPDK_DEBUG()

    dpif_dpdk_flow_init(request);
    request->cmd = OVS_FLOW_CMD_NEW;

    odp_flow_key_to_flow(key, key_len, &flow);
    dpif_dpdk_flow_key_from_flow(&request->key, &flow);

    /* Currently, only drop and output actions are supported */
    if(likely(actions != NULL)) {
        switch(nl_attr_type(actions)) {
            case OVS_ACTION_ATTR_OUTPUT:
                request->action.type = ACTION_OUTPUT;
                request->action.data.output.port = nl_attr_get_u32(actions);
                break;
        default:
                rte_exit(EXIT_FAILURE, "Unsupported action");
        }
    } else { /* Drop action */
        request->action.type = ACTION_NULL;
    }

    if (flags & DPIF_FP_ZERO_STATS) {
        request->clear = true;
    }

    request->flags = 0;

    if (flags & DPIF_FP_MODIFY) {
        request->flags |= NLM_F_REPLACE;
    }
    if (flags & DPIF_FP_CREATE) {
        request->flags |= NLM_F_CREATE;
    }
}

static int
dpif_dpdk_flow_put(struct dpif *dpif_, enum dpif_flow_put_flags flags,
                   const struct nlattr *key, size_t key_len,
                   const struct nlattr *actions, size_t actions_len,
                   struct dpif_flow_stats *stats)
{
    struct dpif_dpdk_flow_message request;
    struct dpif_dpdk_flow_message reply;
    int error = 0;
    dpif_assert_class(dpif_, &dpif_dpdk_class);

    DPDK_DEBUG()

    if (key == NULL) {
        return EINVAL;
    }

    flow_message_put_create(dpif_, flags, key, key_len, actions, actions_len,
                            &request);
    error = dpif_dpdk_flow_transact(&request, stats ? &reply : NULL);
    if (!error && stats) {
        dpif_dpdk_flow_get_stats(&reply, stats);
    }

    return error;
}

/*
 * This function will initialize a dpif_dpdk_flow_message for del.
 */
static void
flow_message_del_create(struct dpif_dpdk_flow_message *request,
                        const struct nlattr *key, size_t key_len)
{
    struct flow flow;

    dpif_dpdk_flow_init(request);
    request->cmd = OVS_FLOW_CMD_DEL;

    odp_flow_key_to_flow(key, key_len, &flow);
    dpif_dpdk_flow_key_from_flow(&request->key, &flow);
}

static int
dpif_dpdk_flow_del(struct dpif *dpif_ OVS_UNUSED,
                   const struct nlattr *key, size_t key_len,
                   struct dpif_flow_stats *stats)
{
    struct dpif_dpdk_flow_message request;
    struct dpif_dpdk_flow_message reply;
    int error = 0;

    DPDK_DEBUG()

    if (key == NULL) {
        return EINVAL;
    }

    flow_message_del_create(&request, key, key_len);

    error = dpif_dpdk_flow_transact(&request,
                                    stats ? &reply : NULL);
    if (!error && stats) {
        dpif_dpdk_flow_get_stats(&reply, stats);
    }

    return error;
}

/*
 * This function will initialize a dpif_dpdk_flow_message for flush.
 */
static void
flow_message_flush_create(struct dpif_dpdk_flow_message *request)
{
    dpif_dpdk_flow_init(request);
    request->cmd = OVS_FLOW_CMD_DEL;
}

static int
dpif_dpdk_flow_flush(struct dpif *dpif_ OVS_UNUSED)
{
    struct dpif_dpdk_flow_message request;

    DPDK_DEBUG()

    flow_message_flush_create(&request);

    return dpif_dpdk_flow_transact(&request, NULL);
}

static int
dpif_dpdk_flow_dump_start(const struct dpif *dpif_ OVS_UNUSED, void **statep)
{
    struct dpif_dpdk_flow_state *state = NULL;

    DPDK_DEBUG()

    /* Maintains state between iterations of flow dump. */
    *statep = state = xmalloc(sizeof(*state));

    if(state == NULL) {
        return ENOMEM;
    }

    dpif_dpdk_flow_init(&state->flow);
    state->flow.cmd = OVS_FLOW_CMD_GET;
    state->flow.flags = NLM_F_DUMP;

    memset(&state->stats, 0, sizeof(state->stats));

    /* Initially set ofpbuf size to zero. */
    ofpbuf_init(&state->actions_buf, 0);
    ofpbuf_init(&state->key_buf, 0);

    return 0;
}

static int
dpif_dpdk_flow_dump_next(const struct dpif *dpif_ OVS_UNUSED, void *state_,
                         const struct nlattr **key, size_t *key_len,
                         const struct nlattr **actions, size_t *actions_len,
                         const struct dpif_flow_stats **stats)
{
    struct dpif_dpdk_flow_state *state = NULL;
    struct dpif_dpdk_flow_message reply;
    struct flow flow;
    int error = 0;

    DPDK_DEBUG()

    if(state_ == NULL) {
        return EINVAL;
    }

    state = state_; /* state from prev iteration */

    /* Using reply from previous iteration, get reply for this iteration. */
    error = dpif_dpdk_flow_transact(&state->flow, &reply);
    if (error) {
        return error;
    }

    /* Save reply in state for next iteration. */
    state->flow = reply;

    /* If actions, key or stats are not null, retrieve from state. */
    if (actions) {
        ofpbuf_reinit(&state->actions_buf, 0); /* zero buf again */
        switch (reply.action.type) {
            case ACTION_OUTPUT:
                nl_msg_put_u32(&state->actions_buf, OVS_ACTION_ATTR_OUTPUT,
                                reply.action.data.output.port);
                break;
            case ACTION_NULL:
            case ACTION_MAX:
                break;
        }
        *actions = state->actions_buf.data;
        *actions_len = state->actions_buf.size;
    }
    if (key) {
       ofpbuf_reinit(&state->key_buf, 0); /* zero buf again */
       dpif_dpdk_flow_key_to_flow(&reply.key, &flow);
       odp_flow_key_from_flow(&state->key_buf, &flow);
       *key = state->key_buf.data;
       *key_len = state->key_buf.size;
    }
    if (stats) {
        dpif_dpdk_flow_get_stats(&reply, &state->stats);
        *stats = &state->stats;
    }

    return error;
}

static int
dpif_dpdk_flow_dump_done(const struct dpif *dpif OVS_UNUSED, void *state_)
{
    struct dpif_dpdk_flow_state *state = NULL;

    DPDK_DEBUG()

    if(state_ == NULL) {
        return EINVAL;
    }

    state = state_;

    ofpbuf_uninit(&state->actions_buf);
    ofpbuf_uninit(&state->key_buf);
    free(state);

    return 0;
}

static int
dpif_dpdk_execute(struct dpif *dpif_ OVS_UNUSED,
                  const struct nlattr *key OVS_UNUSED,
                  size_t key_len OVS_UNUSED,
                  const struct nlattr *actions,
                  size_t actions_len OVS_UNUSED,
                  const struct ofpbuf *packet)
{
    struct dpif_dpdk_message request;
    int error = 0;

    DPDK_DEBUG()

    if(packet == NULL) {
        return EINVAL;
    }

    /* Currently, only drop and output actions are supported */
    if (likely(actions != NULL)) {
        switch (nl_attr_type(actions)) {
            case OVS_ACTION_ATTR_OUTPUT:
                request.packet_msg.action.type = ACTION_OUTPUT;
                request.packet_msg.action.data.output.port =
                                           nl_attr_get_u32(actions);
                break;
            default:
                rte_exit(EXIT_FAILURE, "Unsupported action");

        }
    } else { /* Drop action */
        request.packet_msg.action.type = ACTION_NULL;
    }

    error = dpdk_link_send(&request, packet);

    return error;
}

static void
dpif_dpdk_operate(struct dpif *dpif_, union dpif_op **ops, size_t n_ops)
{
    struct dpif_dpdk_message requests[n_ops];
    const struct ofpbuf *packets[n_ops];
    size_t i = 0;
    size_t exec = 0;
    dpif_assert_class(dpif_, &dpif_dpdk_class);

    DPDK_DEBUG()

    if(!((dpif_ == NULL) || (ops == NULL))) {
        for (i = 0; i < n_ops; i++) {
            union dpif_op *op = ops[i];

            if (op->type == DPIF_OP_FLOW_PUT) {
                struct dpif_flow_put *put = &op->flow_put;

                put->error = dpif_dpdk_flow_put(dpif_, put->flags, put->key,
                          put->key_len, put->actions, put->actions_len, put->stats);
            } else if (op->type == DPIF_OP_EXECUTE) {
                struct dpif_execute *execute = &op->execute;

                /* Currently, only drop and output actions are supported */
                if (likely(execute->actions != NULL )) {
                    switch (nl_attr_type(execute->actions)) {
                        case OVS_ACTION_ATTR_OUTPUT:
                            requests[exec].packet_msg.action.type = ACTION_OUTPUT;
                            requests[exec].packet_msg.action.data.output.port =
                                nl_attr_get_u32(execute->actions);
                            break;
                        default:
                            rte_exit(EXIT_FAILURE, "Unsupported action");
                    }
                } else { /* Drop action */
                    requests[exec].packet_msg.action.type = ACTION_NULL;
                }
                requests[exec].type = DPIF_DPDK_PACKET_FAMILY;
                packets[exec] = execute->packet;
                exec++;
            } else {
                NOT_REACHED();
            }
        }

        dpdk_link_send_bulk(requests, packets, exec);
    }
}

static int
dpif_dpdk_recv_get_mask(const struct dpif *dpif_ OVS_UNUSED,
                        int *listen_mask OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_recv_set_mask(struct dpif *dpif_ OVS_UNUSED,
                        int listen_mask OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_queue_to_priority(const struct dpif *dpif OVS_UNUSED,
                            uint32_t queue_id OVS_UNUSED,
                            uint32_t *priority OVS_UNUSED)
{
    DPDK_DEBUG()

    return 0;
}

static int
dpif_dpdk_recv(struct dpif *dpif_ OVS_UNUSED, struct dpif_upcall *upcall)
{
    struct ofpbuf *buf = NULL;
    struct ofpbuf key;
    struct flow flow;
    struct dpif_dpdk_upcall info;
    int type = 0;
    int error = 0;

    DPDK_DEBUG()

    if(upcall == NULL) {
        return EINVAL;
    }

    error = dpdk_link_recv_packet(&buf, &info);

    if (!error) {
        switch (info.cmd) {
        case OVS_PACKET_CMD_MISS:
            type = DPIF_UC_MISS;
            break;

        case OVS_PACKET_CMD_ACTION:
            type = DPIF_UC_ACTION;
            break;

        default:
            return EINVAL;
        }

        dpif_dpdk_flow_key_to_flow(&info.key, &flow);
        ofpbuf_init(&key, 0);
        odp_flow_key_from_flow(&key, &flow);
        ofpbuf_put(buf, key.data, key.size);
        buf->size -= key.size;

        memset(upcall, 0, sizeof(*upcall));
        upcall->type = type;
        upcall->packet = buf;
        upcall->key = ofpbuf_tail(buf);
        upcall->key_len = key.size;
        upcall->userdata = 0;
    }

    return error;
}

static void
dpif_dpdk_recv_wait(struct dpif *dpif_ OVS_UNUSED)
{
    DPDK_DEBUG()
}

static void
dpif_dpdk_recv_purge(struct dpif *dpif_ OVS_UNUSED)
{
    DPDK_DEBUG()
}

const struct dpif_class dpif_dpdk_class =
{
    "dpdk",
    NULL,
    dpif_dpdk_open,
    dpif_dpdk_close,
    dpif_dpdk_destroy,
    dpif_dpdk_run,
    dpif_dpdk_wait,
    dpif_dpdk_get_stats,
    dpif_dpdk_port_add,
    dpif_dpdk_port_del,
    dpif_dpdk_port_query_by_number,
    dpif_dpdk_port_query_by_name,
    dpif_dpdk_get_max_ports,
    NULL,
    dpif_dpdk_port_dump_start,
    dpif_dpdk_port_dump_next,
    dpif_dpdk_port_dump_done,
    dpif_dpdk_port_poll,
    dpif_dpdk_port_poll_wait,
    dpif_dpdk_flow_get,
    dpif_dpdk_flow_put,
    dpif_dpdk_flow_del,
    dpif_dpdk_flow_flush,
    dpif_dpdk_flow_dump_start,
    dpif_dpdk_flow_dump_next,
    dpif_dpdk_flow_dump_done,
    dpif_dpdk_execute,
    dpif_dpdk_operate,
    dpif_dpdk_recv_get_mask,
    dpif_dpdk_recv_set_mask,
    dpif_dpdk_queue_to_priority,
    dpif_dpdk_recv,
    dpif_dpdk_recv_wait,
    dpif_dpdk_recv_purge
};


static int
dpif_dpdk_init(void)
{
    int error = -1;
    static int init = 0;

    DPDK_DEBUG()

    /* Check if already initialized. */
    if (init) {
        return 0;
    }

    error = dpdk_link_init();
    if (!error) {
        init = 1;
    }

    return error;
}

/* Clears 'flow' to "empty" values. */
static void
dpif_dpdk_flow_init(struct dpif_dpdk_flow_message *flow_msg)
{
    DPDK_DEBUG()

    memset(flow_msg, 0, sizeof(*flow_msg));
}

/*
 * Carries out a transaction with the datapath specified with request.
 * If there is an error this function returns a positive errno value.
 * If the reply to this request is null, this function returns 0.
 * If the reply is not null, this functions stores the reply in '*reply'.
 * The type of this reply is the return type of this function.
 */
static int
dpif_dpdk_flow_transact(struct dpif_dpdk_flow_message *request,
                        struct dpif_dpdk_flow_message *reply)
{
    struct dpif_dpdk_message request_buf;
    int error = 0;

    DPDK_DEBUG()

    request_buf.type = DPIF_DPDK_FLOW_FAMILY;

    request_buf.flow_msg = *request;

    error = dpdk_link_send(&request_buf, NULL);
    if (error) {
        return error;
    }

    error = dpdk_link_recv_reply(&request_buf);
    if (error) {
        return error;
    }

    if (reply) {
        *reply = request_buf.flow_msg;
    }

    return request_buf.type;
}

/*
 * Parse dpif_dpdk_flow_message to get stats and return to caller as
 * dpif_flow_stats.
 */
static void
dpif_dpdk_flow_get_stats(const struct dpif_dpdk_flow_message *flow_msg,
                         struct dpif_flow_stats *stats)
{
    DPDK_DEBUG()

    stats->n_packets = flow_msg->stats.packet_count;
    stats->n_bytes = flow_msg->stats.byte_count;
    stats->used = flow_msg->stats.used;
    stats->tcp_flags = flow_msg->stats.tcp_flags;
}

/*
 * Convert flow key type from struct flow to struct dpif_dpdk_flow_key.
 */
static void
dpif_dpdk_flow_key_from_flow(struct dpif_dpdk_flow_key *key,
                             const struct flow *flow)
{
    uint16_t vlan_tci = 0;

    memset(key, 0, sizeof(*key));
    key->in_port = flow->in_port;
    memcpy(key->ether_dst.addr_bytes, flow->dl_dst, ETHER_ADDR_LEN);
    memcpy(key->ether_src.addr_bytes, flow->dl_src, ETHER_ADDR_LEN);
    key->ether_type = rte_be_to_cpu_16(flow->dl_type);
    vlan_tci = rte_be_to_cpu_16(flow->vlan_tci);
    key->vlan_id = vlan_tci & VLAN_ID_MASK;
    key->vlan_prio = vlan_tci >> VLAN_PRIO_SHIFT;
    key->ip_src = rte_be_to_cpu_32(flow->nw_src);
    key->ip_dst = rte_be_to_cpu_32(flow->nw_dst);
    key->ip_proto = flow->nw_proto;
    key->ip_tos = flow->nw_tos;
    key->ip_ttl = flow->nw_ttl;
    key->tran_src_port = rte_be_to_cpu_16(flow->tp_src);
    key->tran_dst_port = rte_be_to_cpu_16(flow->tp_dst);
}

/*
 * Convert flow key type from struct dpif_dpdk_flow_key to struct flow.
 */
static void
dpif_dpdk_flow_key_to_flow(const struct dpif_dpdk_flow_key *key,
                           struct flow *flow)
{
    memset(flow, 0, sizeof(*flow));
    flow->in_port = key->in_port;
    memcpy(flow->dl_dst, key->ether_dst.addr_bytes, ETHER_ADDR_LEN);
    memcpy(flow->dl_src, key->ether_src.addr_bytes, ETHER_ADDR_LEN);
    flow->dl_type = rte_cpu_to_be_16(key->ether_type);
    if (key->vlan_id != 0)
        flow->vlan_tci = rte_cpu_to_be_16(key->vlan_prio << VLAN_PRIO_SHIFT | key->vlan_id | VLAN_CFI);
    flow->nw_src = rte_cpu_to_be_32(key->ip_src);
    flow->nw_dst = rte_cpu_to_be_32(key->ip_dst);
    flow->nw_proto = key->ip_proto;
    flow->nw_tos = key->ip_tos;
    flow->nw_ttl = key->ip_ttl;
    flow->tp_src = rte_cpu_to_be_16(key->tran_src_port);
    flow->tp_dst = rte_cpu_to_be_16(key->tran_dst_port);
}
