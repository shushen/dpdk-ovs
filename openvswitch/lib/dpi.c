/*
 *   Copyright  Qosmos 2000-2014 - All rights reserved
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
#include <dlfcn.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include "ofpbuf.h"
#include "dpi.h"

VLOG_DEFINE_THIS_MODULE(dpi);

static int (*engine_init_once)(int , char **);
static void (*engine_exit_once)(void);
static void *(*engine_init_perthread)(void);
static void (*engine_exit_perthread)(void *);
static bool (*engine_inject_packet)(void *, char *, size_t , struct timeval *,
                void *, size_t);

static void *dpi_lib;

int
dpi_init_once(int argc, char *argv[])
{

    VLOG_DBG(__FUNCTION__);
    if (argc <= 0) {
        VLOG_INFO("no DPI library specified");
        return -1;
    }
    dpi_lib = dlopen(argv[0], RTLD_NOW|RTLD_GLOBAL);
    if (!dpi_lib) {
        VLOG_ERR("failed to open DPI library");
        return -1;
    }
    engine_init_once = dlsym(dpi_lib, "dpi_engine_init_once");
    if (!engine_init_once) {
        goto error;
    }
    engine_exit_once = dlsym(dpi_lib, "dpi_engine_exit_once");
    if (!engine_exit_once) {
        goto error;
    }
    engine_init_perthread = dlsym(dpi_lib, "dpi_engine_init_perthread");
    if (!engine_init_perthread) {
        goto error;
    }
    engine_exit_perthread = dlsym(dpi_lib, "dpi_engine_exit_perthread");
    if (!engine_exit_perthread) {
        goto error;
    }
    engine_inject_packet = dlsym(dpi_lib, "dpi_engine_inject_packet");
    if (!engine_inject_packet) {
        goto error;
    }

    optind = 0;
    return engine_init_once(argc, argv);

error:
    VLOG_INFO("missing symbol in DPI library");
    dlclose(dpi_lib);
    return -1;
}


void
dpi_exit_once()
{
    VLOG_DBG(__FUNCTION__);

    engine_init_once = NULL;
    engine_exit_once = NULL;
    engine_init_perthread = NULL;
    engine_exit_perthread = NULL;
    engine_inject_packet = NULL;

    if (engine_exit_once) {
        engine_exit_once();
    }
    if (dpi_lib) {
        dlclose(dpi_lib);
    }
	return;
}


void *
dpi_init_perthread(void)
{
    VLOG_DBG(__FUNCTION__);
    if (engine_init_perthread) {
        return engine_init_perthread();
    }
    return NULL;
}


void
dpi_exit_perthread(void *opaque)
{
    VLOG_DBG(__FUNCTION__);
    if (engine_exit_perthread) {
        engine_exit_perthread(opaque);
    }
    return;
}

int
dpi_process(
    void *perthread_opaque,
    struct ofpbuf *packet,
    bool *flow_need_dpi,
    uint32_t classif[],
    size_t len) /* classif[] len (bytes, not elements nb!) */
{
    struct timeval tv;

    if (!engine_inject_packet) {
        return 0;
    }
    if (gettimeofday(&tv, NULL)) { /* XXX: may affect performance */
	    VLOG_ERR("%s: gettimeofday failed(errno=%d), skipping DPI",
            __FUNCTION__, errno);
        return -1;
    }
    *flow_need_dpi = engine_inject_packet(perthread_opaque,
                        (char *)packet->data, packet->size, &tv,
                        (void *)classif, len);
    VLOG_DBG("%s: app_id=<%d> tags=<0x%x> <%s>", __FUNCTION__, classif[0],
                classif[1], *flow_need_dpi ? "need DPI" : "offloaded");
	return 0;
}


