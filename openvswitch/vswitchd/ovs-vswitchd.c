/* Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
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

#include <config.h>

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_MLOCKALL
#include <sys/mman.h>
#endif

#include "bridge.h"
#include "command-line.h"
#include "compiler.h"
#include "daemon.h"
#include "dirs.h"
#include "dpif.h"
#include "dummy.h"
#include "memory.h"
#include "netdev.h"
#include "openflow/openflow.h"
#include "ovsdb-idl.h"
#include "poll-loop.h"
#include "process.h"
#include "signals.h"
#include "simap.h"
#include "stream-ssl.h"
#include "stream.h"
#include "svec.h"
#include "timeval.h"
#include "unixctl.h"
#include "util.h"
#include "vconn.h"
#include "vlog.h"
#include "lib/vswitch-idl.h"

#ifdef HAVE_DPI
#include "dpi.h"
#include "flow.h"

/* Number of registers holding DPI results (from regs[0] to regs[N-1]) */
long dpi_nregs = 2; /* default value */

/* DPI plugin parameters related variables */
char *dpi_argv[256];
int dpi_argc = 0;
/* string holding all DPI arguments separated by ',' */
char dpi_parameters[2048] = "";

/** Parse DPI engine arguments (engine,arg1,arg2,...) into (argc, argv) */
static int parse_dpi_args(char *args, char *subargv[])
{
    int subargc, i;

    for (i = 0, subargc = 0, subargv[0] = args; ; i++) {
        if (args[i] == ',') {
            args[i] = '\0';
            subargv[subargc + 1] = &args[i + 1];
            subargc++;
        } else if (args[i] == '\0') {
            return ++subargc;
        }
    }

}
#endif /* HAVE_DPI */

VLOG_DEFINE_THIS_MODULE(vswitchd);

/* --mlockall: If set, locks all process memory into physical RAM, preventing
 * the kernel from paging any of its memory to disk. */
static bool want_mlockall;

static unixctl_cb_func ovs_vswitchd_exit;

static char *parse_options(int argc, char *argv[], char **unixctl_path);
static void usage(void) NO_RETURN;

extern int
rte_eal_init(int argc, char **argv);
int
main(int argc, char *argv[])
{
    char *unixctl_path = NULL;
    struct unixctl_server *unixctl;
    struct signal *sighup;
    char *remote;
    bool exiting;
    int retval;

    if ((retval = rte_eal_init(argc, argv)) < 0)
        return -1;
    argc -= retval;
    argv += retval;
    proctitle_init(argc, argv);
    set_program_name(argv[0]);
    remote = parse_options(argc, argv, &unixctl_path);
#ifdef HAVE_DPI
    dpi_argc = parse_dpi_args(dpi_parameters, dpi_argv);
    if (dpi_init_once(dpi_argc, dpi_argv)) {
        RESET_DPI_ENABLED();
        VLOG_INFO("dpi global init failed");
    } else {
        VLOG_INFO("dpi global init done");
        SET_DPI_ENABLED();
    }
#endif /* HAVE_DPI */

    signal(SIGPIPE, SIG_IGN);
    sighup = signal_register(SIGHUP);
    process_init();
    ovsrec_init();

    daemonize_start();

    if (want_mlockall) {
#ifdef HAVE_MLOCKALL
        if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
            VLOG_ERR("mlockall failed: %s", ovs_strerror(errno));
        }
#else
        VLOG_ERR("mlockall not supported on this system");
#endif
    }

    retval = unixctl_server_create(unixctl_path, &unixctl);
    if (retval) {
        exit(EXIT_FAILURE);
    }
    unixctl_command_register("exit", "", 0, 0, ovs_vswitchd_exit, &exiting);

    bridge_init(remote);
    free(remote);

    exiting = false;
    while (!exiting) {
        if (signal_poll(sighup)) {
            vlog_reopen_log_file();
        }
        memory_run();
        if (memory_should_report()) {
            struct simap usage;

            simap_init(&usage);
            bridge_get_memory_usage(&usage);
            memory_report(&usage);
            simap_destroy(&usage);
        }
        bridge_run();
        unixctl_server_run(unixctl);
        netdev_run();

        signal_wait(sighup);
        memory_wait();
        bridge_wait();
        unixctl_server_wait(unixctl);
        netdev_wait();
        if (exiting) {
            poll_immediate_wake();
        }
        poll_block();
    }
    bridge_exit();
    unixctl_server_destroy(unixctl);
#ifdef HAVE_DPI
    if (IS_DPI_ENABLED()) {
        dpi_exit_once();
    }
#endif /* HAVE_DPI */

    return 0;
}

static char *
parse_options(int argc, char *argv[], char **unixctl_pathp)
{
    enum {
        OPT_PEER_CA_CERT = UCHAR_MAX + 1,
        OPT_MLOCKALL,
        OPT_UNIXCTL,
        VLOG_OPTION_ENUMS,
        OPT_BOOTSTRAP_CA_CERT,
        OPT_ENABLE_DUMMY,
        OPT_DISABLE_SYSTEM,
        OPT_DPI_NREGS,
        OPT_DPI_ENGINE_OPTIONS,
        DAEMON_OPTION_ENUMS
    };
    static const struct option long_options[] = {
        {"help",        no_argument, NULL, 'h'},
        {"version",     no_argument, NULL, 'V'},
        {"mlockall",    no_argument, NULL, OPT_MLOCKALL},
        {"unixctl",     required_argument, NULL, OPT_UNIXCTL},
        DAEMON_LONG_OPTIONS,
        VLOG_LONG_OPTIONS,
        STREAM_SSL_LONG_OPTIONS,
        {"peer-ca-cert", required_argument, NULL, OPT_PEER_CA_CERT},
        {"bootstrap-ca-cert", required_argument, NULL, OPT_BOOTSTRAP_CA_CERT},
        {"enable-dummy", optional_argument, NULL, OPT_ENABLE_DUMMY},
        {"disable-system", no_argument, NULL, OPT_DISABLE_SYSTEM},
        {"dpi-nregs", required_argument, NULL, OPT_DPI_NREGS},
        {"dpi-engine", required_argument, NULL, OPT_DPI_ENGINE_OPTIONS},
        {NULL, 0, NULL, 0},
    };
    char *short_options = long_options_to_short_options(long_options);

    for (;;) {
        int c;

        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage();

        case 'V':
            ovs_print_version(OFP10_VERSION, OFP10_VERSION);
            exit(EXIT_SUCCESS);

        case OPT_MLOCKALL:
            want_mlockall = true;
            break;

        case OPT_UNIXCTL:
            *unixctl_pathp = optarg;
            break;

        VLOG_OPTION_HANDLERS
        DAEMON_OPTION_HANDLERS
        STREAM_SSL_OPTION_HANDLERS

        case OPT_PEER_CA_CERT:
            stream_ssl_set_peer_ca_cert_file(optarg);
            break;

        case OPT_BOOTSTRAP_CA_CERT:
            stream_ssl_set_ca_cert_file(optarg, true);
            break;

        case OPT_ENABLE_DUMMY:
            dummy_enable(optarg && !strcmp(optarg, "override"));
            break;

        case OPT_DISABLE_SYSTEM:
            dp_blacklist_provider("system");
            break;

#ifdef HAVE_DPI
        case OPT_DPI_NREGS:
            dpi_nregs = strtol(optarg, (char **) NULL, 10);
            if (dpi_nregs > FLOW_N_REGS) {
                VLOG_FATAL("Allowed value range for %s: 0-%d\n",
                    long_options[OPT_DPI_NREGS].name, FLOW_N_REGS);
                abort();
            }
            break;

        case OPT_DPI_ENGINE_OPTIONS:
            if (strlen(optarg) >= sizeof(dpi_argv)) {
                VLOG_FATAL("%s string too large\n", optarg);
                abort();
            }
            strncpy(dpi_parameters, optarg, sizeof(dpi_argv));
            break;
#endif /* HAVE_DPI */

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }

    free(short_options);

    argc -= optind;
    argv += optind;

    switch (argc) {
    case 0:
        return xasprintf("unix:%s/db.sock", ovs_rundir());

    case 1:
        return xstrdup(argv[0]);

    default:
        VLOG_FATAL("at most one non-option argument accepted; "
                   "use --help for usage");
    }
}

static void
usage(void)
{
    printf("%s: Open vSwitch daemon\n"
           "usage: %s [OPTIONS] [DATABASE]\n"
           "where DATABASE is a socket on which ovsdb-server is listening\n"
           "      (default: \"unix:%s/db.sock\").\n",
           program_name, program_name, ovs_rundir());
    stream_usage("DATABASE", true, false, true);
    daemon_usage();
    vlog_usage();
    printf("\nOther options:\n"
           "  --unixctl=SOCKET        override default control socket name\n"
           "  -h, --help              display this help message\n"
           "  -V, --version           display version information\n");

#ifdef HAVE_DPI
    printf("\nDPI options:\n"
           "--dpi-nregs=N Number of OpenFlow registers holding DPI results\n"
           "--dpi-engine=path-to-engine,arg1,arg2,arg3,..."
                "DPI engine and its arguments\n");
#endif /* HAVE_DPI */
    exit(EXIT_SUCCESS);
}

static void
ovs_vswitchd_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                  const char *argv[] OVS_UNUSED, void *exiting_)
{
    bool *exiting = exiting_;
    *exiting = true;
    unixctl_command_reply(conn, NULL);
}
