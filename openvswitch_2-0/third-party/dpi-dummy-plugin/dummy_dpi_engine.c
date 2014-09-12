/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2000-2014 Qosmos. All rights reserved.
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   
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
#include <sys/time.h>
#include <stdint.h>
#include <assert.h>


struct dpi_engine_proto_info
{
    char protocol_name[80];
    char group_name[80];
    unsigned int protocol_id;
    unsigned int group_id;
};

struct dpi_engine_proto_info dpi_engine_protocols[] ={
    /* XXX to be enhanced: 
     * group could be a bitfield in case of multi-goup per protocol */
    /* protocol name, protocol group name, procol id, group id */
    {"HTTP", "Web", 0, 0},
    {"HTTPS", "Web", 1, 0},
    {"DHCP", "Standard", 2, 1},
    {"ICMP", "Standard", 3, 1},
    {"QQ", "Instant Message", 4, 2},
    {"UbuntuONE", "Instant Message", 5, 2},
    {"PPlive", "Media", 6, 3},
    {"ppStream", "Media", 7, 30},
    {"Thunder", "Web Service", 8, 4},
    {"Bittorrent", "Web Service", 9, 4},
    {"MySQL", "Database", 10, 5},
    {"PostgresSQL", "Database", 11, 5},
};

#define PROTOCOLS_NB (sizeof(dpi_engine_protocols)/sizeof(struct dpi_engine_proto_info))

int dpi_engine_init_once(int argc, char **argv)
{
    int i;

    printf("%s: supported protocols:\n", __FUNCTION__);
    for (i = 0; i < PROTOCOLS_NB  ; i++) {
        printf("\t%s (%s): %d (%d)\n",
            dpi_engine_protocols[i].protocol_name,
            dpi_engine_protocols[i].group_name,
            dpi_engine_protocols[i].protocol_id,
            dpi_engine_protocols[i].group_id);
    }
    srandom(PROTOCOLS_NB);
    return 0;
}

void dpi_engine_exit_once(void)
{
    return;
}

#define DPI_OPAQUE_MAGIC ((void *) 0xcafedeca)
void *dpi_engine_init_perthread(void)
{
    /* same value for all theads */
    return DPI_OPAQUE_MAGIC;
}

void dpi_engine_exit_perthread(void *opaque)
{
    assert(opaque == DPI_OPAQUE_MAGIC);
    return; 
}

int dpi_engine_inject_packet(
    void *opaque,
    char *packet,
    size_t len,
    struct timeval *tv,
    void* classif,
    size_t classif_len)
{
    assert(opaque == DPI_OPAQUE_MAGIC);
    *(uint32_t *) classif = random() % PROTOCOLS_NB;
    return random() & 0x1; /*0: noy classified, caller should keep sending packets
                             1: classified, caller can stop sending packets */
}
