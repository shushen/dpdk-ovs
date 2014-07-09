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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_config.h>
#include <rte_common.h>
#include <rte_ivshmem.h>
#include <rte_string_fns.h>
#include <rte_log.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#include "ovs-vport.h"

#define rpl_strnlen strnlen
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
#define OVS_IVSHMEM_MNGR_METADATA_DELIM ":"
#define OVS_IVSHMEM_MNGR_PORTS_DELIM ","
#define OVS_IVSHMEM_MNGR_CLIENT_NAME_LEN 32

/* Template used to create QEMU's command line files */
#define QEMU_CMD_FILE_FMT "/tmp/.ovs_ivshmem_qemu_cmdline_%s"

#define usage(...) do {				\
	RTE_LOG(ERR, APP, __VA_ARGS__);	\
	print_usage();					\
} while(0);

#define metadata_name_is_too_long(metadata_name) \
		(strnlen(metadata_name, IVSHMEM_NAME_LEN + 1) == IVSHMEM_NAME_LEN + 1)

struct ovs_ivshmem_config {
	char *metadata_name;	/* IVSHMEM metadata file name */
	char *port_names[RTE_LIBRTE_IVSHMEM_MAX_ENTRIES];
							/* Ports (DPDK objects) to be shared */
};

/* Global ivshmem config containing all metadata names and all its
 * DPDK objects to be shared */
static struct ovs_ivshmem_config
	ivshmem_config[RTE_LIBRTE_IVSHMEM_MAX_METADATA_FILES];

static int ivshmem_mngr_share_with_guests(void);
static int parse_arguments(int argc, char *argv[]);
static void print_usage(void);
static int save_ivshmem_cmdline_to_file(const char *cmdline,
		const char *metadata_name);
static int ivshmem_mngr_share_object(const char *metadata_name,
		const char *port_name);
static int ivshmem_mngr_share_vport_client(const char *metadata_name,
		const char *port_name);
static int
ivshmem_mngr_share_vport_kni(const char *metadata_name, const char *port_name);
static int ivshmem_mngr_share_mempool(const char *metadata_name);

static void
print_usage(void)
{
	printf("\nUsage:\n"
			"   ovs-ivshm-mngr [EAL options] --proc-type=secondary --"
			" metadata0:port0[,port1...] [metadata1:port0[,port1...]]\n\n"
			"Options:\n"
			"   metadata: IVSHMEM metadata file name to be used."
			" %d metadata files are allowed\n"
			"   ports: list of ports to be shared over metadata file."
			" %d ports per metadata are allowed\n\n",
			RTE_LIBRTE_IVSHMEM_MAX_METADATA_FILES,
			RTE_LIBRTE_IVSHMEM_MAX_ENTRIES);
}

/*
 * Print to a file the QEMU's command line for a given metadata name.
 */
static int
save_ivshmem_cmdline_to_file(const char *cmdline, const char *metadata_name)
{
	FILE *file = NULL;
	char path[PATH_MAX] = {0};

	rte_snprintf(path, sizeof(path), QEMU_CMD_FILE_FMT, metadata_name);

	file = fopen(path, "w");
	if (file == NULL) {
		RTE_LOG(ERR, APP, "Cannot create QEMU cmdline for metadata '%s'\n",
				metadata_name);
		return -1;
	}

	RTE_LOG(INFO, APP, "QEMU cmdline for metadata '%s': %s \n", metadata_name,
			cmdline);
	fprintf(file, "%s\n", cmdline);
	fclose(file);

	return 0;
}

static int
check_for_duplicated_metadata_names(const char *metadata_name)
{
	int i = 0;

	for (i = 0; ivshmem_config[i].metadata_name != NULL; i++)
		if (strcmp(ivshmem_config[i].metadata_name, metadata_name) == 0)
			return -1;
	return 0;
}

/*
 * Parse arguments and build global ivshmem config table.
 */
static int
parse_arguments(int argc, char *argv[])
{
	char *port_names[RTE_LIBRTE_IVSHMEM_MAX_ENTRIES] = {0};
	char *metadata_name = NULL, *token = NULL;
	int metadata_index = 0, port_index = 0;
	int token_n = 0, port_n = 0;

	/* Discard the program name at argv[0] */
	argc--;
	argv++;

	if (argc == 0 || argc > RTE_LIBRTE_IVSHMEM_MAX_METADATA_FILES) {
		usage("Wrong number of metadata files.\n");
		return -1;
	}

	for (metadata_index = 0; metadata_index < argc; metadata_index++) {
		token_n = 0;
		port_n = 0;

		token = strtok(argv[metadata_index], OVS_IVSHMEM_MNGR_METADATA_DELIM);

		while (token != NULL) {
			if (token_n == 0) {
				metadata_name = token;
				if (metadata_name_is_too_long(metadata_name)) {
					usage("Metadata name '%s' is too long.\n", metadata_name);
						return -1;
				}

				if (check_for_duplicated_metadata_names(metadata_name) < 0) {
					usage("Metadata names must be unique. "
						  "Metadata name '%s' is duplicated.\n", metadata_name);
					return -1;
				}
			}
			else {
				if (port_n == RTE_LIBRTE_IVSHMEM_MAX_ENTRIES) {
					usage("Too many ports for metadata '%s'\n", metadata_name);
					return -1;
				}
				port_names[port_n++] = token;
			}
			token = strtok(NULL, OVS_IVSHMEM_MNGR_PORTS_DELIM);
			token_n++;
		}

		if (token_n == 0) {
			usage("No metadata name found\n");
			return -1;
		}

		if (port_n == 0) {
			usage("No ports found for metadata '%s'\n",	metadata_name);
			return -1;
		}

		ivshmem_config[metadata_index].metadata_name = metadata_name;

		/* Assigns the pointer only. These point to argv memory so there is no
		 * collision if multiple metadatas are used and we reuse port_names
		 * more than once */
		for (port_index = 0; port_index < port_n; port_index++) {
			ivshmem_config[metadata_index].port_names[port_index] =
					port_names[port_index];
		}
	}

	return 0;
}

/*
 * Share global packet's mempool with a given metadata name
 */
static int
ivshmem_mngr_share_mempool(const char *metadata_name)
{
	struct rte_mempool *mp = NULL;

	mp = ovs_vport_host_lookup_packet_mempool();
	if (mp == NULL)
		return -1;

	if (rte_ivshmem_metadata_add_mempool(mp, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding mempool '%s' to metadata '%s'\n",
							mp->name, metadata_name);
		return -1;
	}

	return 0;
}

/*
 * Share all KNI fifos of KNI port port_name with a given metadata name
 */
static int
ivshmem_mngr_share_vport_kni(const char *metadata_name, const char *port_name)
{
	const struct rte_memzone *mz = NULL;

	/* TX fifo */
	mz = ovs_vport_kni_lookup_tx_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI tx_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* RX fifo */
	mz = ovs_vport_kni_lookup_rx_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI rx_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* ALLOC fifo */
	mz = ovs_vport_kni_lookup_alloc_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI alloc_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* FREE fifo */
	mz = ovs_vport_kni_lookup_free_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI free_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* REQ fifo */
	mz = ovs_vport_kni_lookup_req_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI req_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* RESPONSE fifo */
	mz = ovs_vport_kni_lookup_resp_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI resp_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* SYNC fifo */
	mz = ovs_vport_kni_lookup_sync_fifo(port_name);
	if (mz == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_memzone(mz, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding KNI sync_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}
	return 0;
}

/*
 * Share all client queues of client port port_name with a given metadata name
 */
static int
ivshmem_mngr_share_vport_client(const char *metadata_name, const char *port_name)
{
	struct rte_ring *ring = NULL;

	/* RX queue */
	ring = ovs_vport_client_lookup_rx_q(port_name);
	if (ring == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_ring(ring, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding rx_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* TX queue */
	ring = ovs_vport_client_lookup_tx_q(port_name);
	if (ring == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_ring(ring, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding tx_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* FREE queue */
	ring = ovs_vport_client_lookup_free_q(port_name);
	if (ring == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_ring(ring, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding free_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}

	/* ALLOC queue */
	ring = ovs_vport_client_lookup_alloc_q(port_name);
	if (ring == NULL)
		return -1;
	if (rte_ivshmem_metadata_add_ring(ring, metadata_name) < 0) {
		RTE_LOG(ERR, APP, "Failed adding alloc_q to metadata '%s'\n",
				metadata_name);
		return -1;
	}
	return 0;
}

/*
 * Share vport port_name with a given metadata name. Use OVS-DPDK vport
 * struct to know the vport type. Fail if port_name is not found.
 */
static int
ivshmem_mngr_share_object(const char *metadata_name, const char *port_name)
{
	if (ovs_vport_is_vport_client(port_name) == 0)
		return ivshmem_mngr_share_vport_client(metadata_name, port_name);
	else if (ovs_vport_is_vport_kni(port_name) == 0)
		return ivshmem_mngr_share_vport_kni(metadata_name, port_name);
	else
		RTE_LOG(ERR, APP, "Port name '%s' not found or invalid\n", port_name);

	return -1;
}

/*
 * Go through ivshmem global config table creating all metadata files and
 * sharing the specified objects with it.
 */
static int
ivshmem_mngr_share_with_guests(void)
{
	const struct rte_memzone *vports_mz = NULL;
	char *metadata_name = NULL;
	char cmdline[PATH_MAX] = {0};
	int metadata_index = 0, port_index = 0;

	vports_mz = ovs_vport_lookup_vport_info();
	if (vports_mz == NULL)
		return -1;

	for (metadata_index = 0;
			metadata_index < RTE_LIBRTE_IVSHMEM_MAX_METADATA_FILES;
			metadata_index++) {
		if (ivshmem_config[metadata_index].metadata_name == NULL)
			break;
		metadata_name = ivshmem_config[metadata_index].metadata_name;

		if (rte_ivshmem_metadata_create(metadata_name) < 0) {
			RTE_LOG(ERR, APP, "Cannot create ivshmem config '%s'\n",
					metadata_name);
			return -1;
		}

		for (port_index = 0; port_index < RTE_LIBRTE_IVSHMEM_MAX_ENTRIES;
				port_index++) {
			if (ivshmem_config[metadata_index].port_names[port_index] == NULL)
				break;

			if (ivshmem_mngr_share_object(metadata_name,
					ivshmem_config[metadata_index].port_names[port_index]) < 0)
				return -1;
		}

		/* Share the mempool with the guest. mempool address space needs to be
		 * mapped into all clients otherwise they won't be able to access to
		 * mbufs data. */
		if (ivshmem_mngr_share_mempool(metadata_name) < 0)
			return -1;

		/* Share the vports memzone so the clients can access to vport info */
		if (rte_ivshmem_metadata_add_memzone(vports_mz, metadata_name) < 0)
			return -1;

		/* Generate QEMU's command line */
		if (rte_ivshmem_metadata_cmdline_generate(cmdline, sizeof(cmdline),
				metadata_name) < 0)
			return -1;

		if (save_ivshmem_cmdline_to_file(cmdline, metadata_name) < 0)
			return -1;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	int retval = 0;

	/* Init EAL, parsing EAL args */
	retval = rte_eal_init(argc, argv);
	if (retval < 0)
		return -1;

	if (rte_eal_process_type() != RTE_PROC_SECONDARY)
		rte_exit(EXIT_FAILURE, "Must be executed as secondary process\n");

	argc -= retval;
	argv += retval;

	memset(ivshmem_config, 0, sizeof(ivshmem_config));

	if (parse_arguments(argc, argv) < 0)
		return -1;

	if (ivshmem_mngr_share_with_guests() < 0)
		return -1;

	return 0;
}
