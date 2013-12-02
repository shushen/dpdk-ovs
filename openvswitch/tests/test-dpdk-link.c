#include <config.h>
#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_memzone.h>

#include "dpdk-link.h"
#include "dpif-dpdk.h"

#include <assert.h>

#define VSWITCHD_RINGSIZE   2048
#define VSWITCHD_PACKET_RING_NAME  "MProc_Vswitchd_Packet_Ring"
#define VSWITCHD_REPLY_RING_NAME   "MProc_Vswitchd_Reply_Ring"
#define VSWITCHD_MESSAGE_RING_NAME "MProc_Vswitchd_Message_Ring"
#define NO_FLAGS            0
#define SOCKET0             0

static struct rte_mempool *pktmbuf_pool = NULL;
static struct rte_mempool *pktmbuf_pool_largej = NULL;
/* ring to send packets to vswitchd */
static struct rte_ring *vswitchd_packet_ring = NULL;
/* ring to receive messages from vswitchd */
static struct rte_ring *vswitchd_message_ring = NULL;
/* ring to send reply messages to vswitchd */
static struct rte_ring *vswitchd_reply_ring = NULL;

void init_test_rings(void);

int
main(int argc, char *argv[])
{
	struct dpif_dpdk_message request;
	const struct ofpbuf test_ofpbuf[20];
	const struct ofpbuf *const *test_ofpbuf_array = &test_ofpbuf;

	int result = 0;

	rte_eal_init(argc, argv);
	init_test_rings();

	/* Test dpdk_link_send_bulk(), num_pkts > PKT_BURST_SIZE */
	result = dpdk_link_send_bulk(&request, test_ofpbuf_array, 500);
	assert(result == EINVAL);

	/* Test dpdk_link_send_bulk(), can't alloc enough mbufs */
	result = dpdk_link_send_bulk(&request, test_ofpbuf_array, 10);
	assert(result == ENOBUFS);

	return 0;
}



/* dpdk_link_send() looks up each of these rings and will exit if
 * it doesn't find them so we must declare them.
 *
 * We have to call dpdk_link send to initialise the "mp" pktmbuf pool
 * pointer used throughout dpdk_link.c
 */
void
init_test_rings(void)
{

	pktmbuf_pool = rte_mempool_create("MProc_pktmbuf_pool",
	                     3, /* num mbufs */
	                     2048 + sizeof(struct rte_mbuf) + 128, /* pktmbuf size */
	                     0, /*cache size */
	                     sizeof(struct rte_pktmbuf_pool_private),
	                     rte_pktmbuf_pool_init,
	                     NULL, rte_pktmbuf_init, NULL, 0, 0);

	vswitchd_packet_ring = rte_ring_create(VSWITCHD_PACKET_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_packet_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create packet ring for vswitchd");

	vswitchd_reply_ring = rte_ring_create(VSWITCHD_REPLY_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_reply_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create reply ring for vswitchd");

	vswitchd_message_ring = rte_ring_create(VSWITCHD_MESSAGE_RING_NAME,
			         VSWITCHD_RINGSIZE, SOCKET0, NO_FLAGS);
	if (vswitchd_message_ring == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create message ring for vswitchd");

	dpdk_link_init();
}


