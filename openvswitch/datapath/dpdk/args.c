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
#include <stdint.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>

#include <rte_memory.h>
#include <rte_string_fns.h>

#include "args.h"
#include "init.h"
#include "vport.h"
#include "kni.h"
#include "veth.h"
#include "vhost.h"

#define PORT_OFFSET 0x10
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

struct cfg_params cfg_params_array[MAX_CFG_PARAMS];
struct cfg_params cfg_params_array_default[] = {
	{0, 0, 2},
	{0, 1, 2},
	{0, 2, 2},
	{1, 0, 2},
	{1, 1, 2},
	{1, 2, 2},
	{2, 0, 2},
	{3, 0, 3},
	{3, 1, 3},
};
struct cfg_params * cfg_params = cfg_params_array_default;
uint16_t nb_cfg_params = sizeof(cfg_params_array_default) /
				sizeof(cfg_params_array_default[0]);

static const char *progname;

/* Character device basename. Can be set by user. */
extern char dev_basename[MAX_BASENAME_SZ];
/* Character device index. Can be set by user. */
extern uint32_t dev_index;

/* Specify timeout (in useconds) between retries on TX. */
extern uint32_t burst_tx_delay_time;
/* Specify the number of retries on TX. */
extern uint32_t burst_tx_retry_num;

/**
 * Prints out usage information to stdout
 */
static void
usage(void)
{
	printf(
	    "%s [EAL options] -- -p PORTMASK -n NUM_CLIENTS [-k NUM_KNI] [-v NUM_VETH]\n"
	    " -p PORTMASK: hexadecimal bitmask of ports to use\n"
	    " -n NUM_CLIENTS: number of client processes to use\n"
	    " -k NUM_KNI: number of kni ports to use\n"
	    " -v NUM_VETH: number of host kni (veth) ports to use\n"
		" -h NUM_VHOST: number of vhost (devices) ports to use\n"
		" --vswitchd COREMASK\n"
		"   CPU ID of the core used to display statistics and communicate with the vswitch daemon\n"
		" --config (port,queue,lcore)[,(port,queue,lcore]\n"
		"   Each port/queue/core group specifies the CPU ID of the core that will handle\n"
		"   ingress traffic for the	specified queue on the specified port\n"
		" --client_switching_core COREMASK\n"
		"   CPU ID of the core used to manage client switching\n"
		" --stats UPDATE_TIME\n"
		"   Interval (in seconds) at which stats are updated. Set to 0 to disable (default)\n"
		" --vhost_dev_basename CHAR_DEV_NAME\n"
		"   Set the basename for the vhost character device\n"
		" --vhost_dev_index INDEX\n"
		"   Set the index to be appended to the vhost character device name\n"
		" --vhost_retry_count COUNT\n"
		"   Set the number of retries when sending packets to a vhost device\n"
		" --vhost_retry_wait WAIT_TIME_US\n"
		"   Wait time in useconds when retrying to send packets to a vhost device\n"
	    , progname);
}

/**
 * The ports to be used by the application are passed in
 * the form of a bitmask. This function parses the bitmask
 * and places the port numbers to be used into the port[]
 * array variable
 */
static int
parse_portmask(uint8_t max_ports, const char *portmask)
{
	char *end = NULL;
	unsigned long pm;
	uint8_t count = 0;

	if (portmask == NULL || *portmask == '\0')
		return -1;

	/* convert parameter to a number and verify */
	pm = strtoul(portmask, &end, 16);
	if (end == NULL || *end != '\0' || pm == 0)
		return -1;

	/* loop through bits of the mask and mark ports */
	while (pm != 0) {
		if (pm & 0x01) { /* bit is set in mask, use port */
			if (count >= max_ports)
				printf("WARNING: requested port %u not present"
				    " - ignoring\n", (unsigned)count);
			else
				port_cfg.id[port_cfg.num_phy_ports++] = count;
		}
		pm = (pm >> 1);
		count++;
	}

	return 0;
}

/**
 * Take the number of clients parameter passed to the app
 * and convert to a number to store in the num_clients variable
 */
static int
parse_num_clients(const char *clients)
{
	char *end = NULL;
	unsigned long temp;

	if (clients == NULL || *clients == '\0')
		return -1;

	temp = strtoul(clients, &end, 10);
	if (end == NULL || *end != '\0' || temp == 0)
		return -1;

	return temp;
}

/*
 * Set the vhost character device basename. If this is not set
 * the default vhost-net basename is used.
 */
static int
us_vhost_parse_basename(const char *q_arg)
{
	if (strlen(q_arg) > MAX_BASENAME_SZ)
		return -1;
	else
		rte_snprintf((char*)&dev_basename, MAX_BASENAME_SZ, "%s", q_arg);

	return 0;
}


int
parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_cfg_params = 0;

	while ((p = strchr(p0,'(')) != NULL) {
		++p;
		if((p0 = strchr(p,')')) == NULL)
			return -1;

		size = p0 - p;
		if(size >= sizeof(s))
			return -1;

		rte_snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++) {
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}
		if (nb_cfg_params >= MAX_CFG_PARAMS) {
			printf("exceeded max number of lcore params: %hu\n",
				nb_cfg_params);
			return -1;
		}
		/* Add port offset to calculate vport id for the port */
		cfg_params_array[nb_cfg_params].port_id = (uint8_t)int_fld[FLD_PORT] + PORT_OFFSET;
		printf("config = %d,", cfg_params_array[nb_cfg_params].port_id);
		cfg_params_array[nb_cfg_params].queue_id = (uint8_t)int_fld[FLD_QUEUE];
		printf("%d,", cfg_params_array[nb_cfg_params].queue_id);
		cfg_params_array[nb_cfg_params].lcore_id = (uint8_t)int_fld[FLD_LCORE];
		printf("%d\n", cfg_params_array[nb_cfg_params].lcore_id);
		++nb_cfg_params;
	}
	printf("nb_cfg_params = %d\n", nb_cfg_params);
	cfg_params = cfg_params_array;

	return 0;
}


/**
 * The application specific arguments follow the DPDK-specific
 * arguments which are stripped by the DPDK init. This function
 * processes these application arguments, printing usage info
 * on error.
 */
int
parse_app_args(uint8_t max_ports, int argc, char *argv[])
{
	int option_index, opt, ret, temp;
	char **argvopt = argv;
	static struct option lgopts[] = {
			{PARAM_STATS, 1, 0, 0},
			{PARAM_CONFIG, 1, 0, 0},
			{PARAM_VSWITCHD, 1, 0, 0},
			{PARAM_CSC, 1, 0, 0},
			{VHOST_CHAR_DEV_NAME, 1, 0, 0},
			{VHOST_CHAR_DEV_IDX, 1, 0, 0},
			{VHOST_RETRY_COUNT, 1, 0, 0},
			{VHOST_RETRY_WAIT, 1, 0, 0},
			{NULL, 0, 0, 0}
	};

	progname = argv[0];

	/* Initialize counters to "not used" */
	num_clients = num_kni = num_veth = num_vhost = 0;

	while ((opt = getopt_long(argc, argvopt, "n:p:k:v:h:", lgopts,
		&option_index)) != EOF) {
		switch (opt) {
			case 'p':  /* Physical ports */
				if (parse_portmask(max_ports, optarg) != 0) {
					usage();
					return -1;
				}
				break;
			case 'n':  /* Client ports */
				temp = parse_num_clients(optarg);
				if (temp <= 0) {
					usage();
					return -1;
				}
				num_clients = (uint8_t)temp;
				break;
			case 'k':  /* KNI ports */
				temp = parse_num_clients(optarg);
				if (temp <= 0) {
					usage();
					return -1;
				}
				num_kni = (uint8_t)temp;
				break;
			case 'v':  /* vEth ports */
				temp = parse_num_clients(optarg);
				if (temp <= 0) {
					usage();
					return -1;
				}
				num_veth = (uint8_t)temp;
				break;
			case 'h':  /* vHost ports */
				temp = parse_num_clients(optarg);
				if (temp <= 0) {
					usage();
					return -1;
				}
				num_vhost = (uint8_t)temp;
				break;
			case 0:
				if (!strcmp(lgopts[option_index].name, PARAM_CONFIG)) {
					ret = parse_config(optarg);
					if (ret) {
						printf("invalid config\n");
					}
				}
				if (strncmp(lgopts[option_index].name, PARAM_STATS, 5) == 0) {
					stats_display_interval = atoi(optarg);
				} else if (strncmp(lgopts[option_index].name, PARAM_VSWITCHD, 8) == 0) {
					vswitchd_core = atoi(optarg);
				} else if (strncmp(lgopts[option_index].name, PARAM_CSC, 16) == 0) {
					client_switching_core = atoi(optarg);
				} else if (strncmp(lgopts[option_index].name, VHOST_CHAR_DEV_NAME, 18) == 0) {
					 temp = us_vhost_parse_basename(optarg);
					 if (temp < 0) {
						 printf ("Invalid argument for character device basename\n");
						 usage();
						 return -1;
					 }
				} else if (strncmp(lgopts[option_index].name, VHOST_CHAR_DEV_IDX, 15) == 0) {
					temp = atoi(optarg);
					if (temp < 0) {
						printf("Invalid argument for character device index\n");	
						usage();
						return -1;
					}
					dev_index = (uint32_t)temp;
				} else if (strncmp(lgopts[option_index].name, VHOST_RETRY_COUNT, 17) == 0) {
					temp = atoi(optarg);
					if (temp < 0) {
						printf("Invalid argument for retry count\n");	
						usage();
						return -1;
					}
					burst_tx_retry_num = (uint32_t)temp;
				} else if (strncmp(lgopts[option_index].name, VHOST_RETRY_WAIT, 16) == 0) {
					temp = atoi(optarg);
					if (temp < 0) {
						printf("Invalid argument for retry wait time\n");	
						usage();
						return -1;
					}
					burst_tx_delay_time = (uint32_t)temp;
				}
				break;
			default:
				printf("ERROR: Unknown option '%c'\n", opt);
				usage();
				return -1;
		}
	}

	if (num_clients == 0 || num_clients > MAX_CLIENTS) {
		printf ("Number of clients is invalid\n");
		usage();
		return -1;
	}

	if (num_kni > MAX_KNI_PORTS) {
		printf ("Number of KNI ports is invalid\n");
		usage();
		return -1;
	}

	if (num_veth > MAX_VETH_PORTS) {
		printf ("Number of host KNI (vEth) ports is invalid\n");
		usage();
		return -1;
	}

	if (num_vhost > MAX_VHOST_PORTS) {
		printf ("Number of vhost ports is invalid\n");
		usage();
		return -1;
	}

	return 0;
}

