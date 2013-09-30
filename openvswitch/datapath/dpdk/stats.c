/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
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
#include <rte_string_fns.h>
#include <rte_ether.h>

#include "stats.h"
#include "vport.h"
#include "init.h"

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define NUM_BYTES_MAC_ADDR  6
#define MAC_ADDR_STR_INT_L  3
#define NEWLINE_CHAR_OFFSET 1

static const char *get_printable_mac_addr(uint8_t port);
/*
 * Function to set all the client statistic values to zero.
 * Called at program startup.
 */
void
clear_stats(void)
{
	unsigned i = 0;

	for (i = 0; i < MAX_VPORTS; i++) {
		vport_stats[i].rx = 0;
		vport_stats[i].rx_drop = 0;
		vport_stats[i].tx = 0;
		vport_stats[i].tx_drop = 0;
	}
}

/*
 * Returns MAC address for port in a string
 */
static const char *
get_printable_mac_addr(uint8_t port)
{
	static const char err_address[] = "00:00:00:00:00:00";
	static char addresses[RTE_MAX_ETHPORTS][sizeof(err_address)] = {0};
	struct ether_addr mac = {0};
	int ether_addr_len = sizeof(err_address) - NEWLINE_CHAR_OFFSET;
	int i = 0;
	int j = 0;

	if (unlikely(port >= RTE_MAX_ETHPORTS))
		return err_address;

	/* first time run for this port so we populate addresses */
	if (unlikely(addresses[port][0] == '\0')) {
		rte_eth_macaddr_get(port, &mac);
		while(j < NUM_BYTES_MAC_ADDR) {
			rte_snprintf(&addresses[port][0] + i,
			             MAC_ADDR_STR_INT_L + NEWLINE_CHAR_OFFSET,
			             "%02x:",
			             mac.addr_bytes[j]);
			i += MAC_ADDR_STR_INT_L;
			j++;
		}
		/* Overwrite last ":" and null terminate the string */
		addresses[port][ether_addr_len] = '\0';
	}
	return addresses[port];
}


/*
 * This function displays the recorded statistics for each port
 * and for each client. It uses ANSI terminal codes to clear
 * screen when called. It is called from a single non-master
 * thread in the server process, when the process is run with more
 * than one lcore enabled.
 */
void
do_stats_display(void)
{
	unsigned i = 0;
	unsigned j = 0;
	/* ANSI escape sequences for terminal display.
	 * 27 = ESC, 2J = Clear screen */
	const char clr[] = {27, '[', '2', 'J', '\0'};
	/* H = Home position for cursor*/
	const char topLeft[] = {27, '[', '1', ';', '1', 'H','\0'};

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("Physical Ports\n");
	printf("-----\n");
	for (i = 0; i < ports->num_ports; i++)
		printf("Port %u: '%s'\t", ports->id[i],
				get_printable_mac_addr(ports->id[i]));
	printf("\n\n");

	printf("\nVport Statistics\n"
		     "============   ============  ============  ============  ============\n"
		     "Interface      rx_packets    rx_dropped    tx_packets    tx_dropped  \n"
		     "------------   ------------  ------------  ------------  ------------\n");
	for (i = 0; i < MAX_VPORTS; i++) {
		const volatile struct statistics vstats = vport_stats[i];
		if (i == 0) {
			printf("vswitchd   ");
		} else if (i <= PORT_MASK) {
			printf("Client   %2u", i);
		} else if (i <= KNI_MASK) {
			printf("Port     %2u", i & PORT_MASK);
		} else {
			printf("KNI Port %2u", i & KNI_MASK);
		}
		printf("%13"PRIu64" %13"PRIu64" %13"PRIu64" %13"PRIu64"\n",
					vstats.rx,
					vstats.rx_drop,
					vstats.tx,
					vstats.tx_drop);
	}
	printf("============   ============  ============  ============  ============\n");

	printf("\n Switch rx dropped %d\n", switch_rx_drop);
	printf("\n Switch tx dropped %d\n", switch_tx_drop);
	printf("\n Queue overruns   %d\n",  overruns);
	printf("\n Mempool count    %9u\n", rte_mempool_count(pktmbuf_pool));
	printf("\n");
}


