/*
 * Copyright  Qosmos 2000-2014 - All rights reserved
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

#ifndef DPI_H
#define DPI_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include "vlog.h"
#include "dpif.h"

bool dpi_enabled;
#define RESET_DPI_ENABLED(a) { dpi_enabled = (0 == 1); }
#define SET_DPI_ENABLED(a)   { dpi_enabled = (0 == 0); }
#define IS_DPI_ENABLED(a)    dpi_enabled

/*
 * Number of registers holding DPI results (from regs[0] to regs[N-1]).
 * Maximum is FLOW_N_REGS, default is DPI_NREGS_DEFAULT.
 */
extern long dpi_nregs;

/**
 * Init the DPI engine:
 *  1/ load the dynamic library implementing the DPI engine
 *  2/ register the DPI engine functions
 *  3/ call the global/general init function of the DPI engine
 *
 * @argc:
 *  Number of command line parameters, transmitted as is to the DPI engine
 * @argv:
 *  Command line parameter array, transmitted as is to the DPI engine
 * return:
 *  0 - success
 *  Non zero - error code
 */
int dpi_init_once(int argc, char **argv);

/**
 * Release all ressources used by the DPI:
 *  1/ unregister the DPI engine functions
 *  2/ call the DPI engine general cleanup funtion
 *  3/ de-register the dynamic library
 */
void dpi_exit_once(void);

/**
 * Per thread initialization
 *  Returns the first parameter to be passed to dpi_process()
 *   - If non NULL, used by the DPI engine for a perthread DPI context
 *     retrieval without any indirection
 *   - If the DPI engine do not require to retrieve a DPI context,
 *     just call dpi_process() with a NULL first parameter
 *
 *  return value:
 *   Any value - first parameter to be passed to dpi_process()
 *
 */
void *dpi_init_perthread(void);

/**
 * Per thread ressources release
 *  The DPI engine release the ressources reserved by dpi_init_perthread()
 */
void dpi_exit_perthread(void *);

/**
 * Inject a packet in the DPI engine
 * Call the DPI engine injection function with unchanged parameters but:
 *   - packet: converted into (char *data, size_t data_len)
 *   - added packet timestamp
 *
 *  @perthread_opaque:
 *      per thread DPI opaque context, to be passed to the engine as-is
 *  @packet:
 *      packet to analyze
 *  @flow_need_dpi:
 *      output parameter indicating if the DPI engine need more packets
 *      to classify the flow:
 *        TRUE - the DPI engine need more packets to classify the flow
 *        FALSE - the DPI classificaition is done, sending additional packets
 *                is not necessary (but it won't harm).
 *  @classif:
 *      Array of DPI classification result, DPI engine
 *      specific. Current implementation supports "application ID" and
 *      "protocol tag" (bitfield: WEB, PEERtoPEER, DATABASE, ...)
 *  @len:
 *      classif bytes len
 * return:
 *      0 - success
 *      Non-zero - error code
 */
int dpi_process(void *perthread_opaque, struct ofpbuf *packet, bool *flow_need_dpi, uint32_t classif[], size_t len);

#ifdef  __cplusplus
}
#endif

#endif /* dpi.h */

