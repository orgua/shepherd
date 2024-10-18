/*
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the
 *	  distribution.
 *
 *	* Neither the name of Texas Instruments Incorporated nor the names of
 *	  its contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SHEPHERD_PRU0_RESOURCE_TABLE_DEF_H_
#define SHEPHERD_PRU0_RESOURCE_TABLE_DEF_H_

#include <stddef.h>

#include "resource_table.h"

#include "commons.h"

/* Definition for unused interrupts */
#define HOST_UNUSED (255U)

#define SIZE_CARVEOUT                                                                              \
    (sizeof(struct IVTraceInp) + sizeof(struct IVTraceOut) + sizeof(struct GPIOTrace) +            \
     sizeof(struct UtilTrace))

// pseudo-assertion to test for correct struct-size, zero cost -> signoff changes here
/* // removed - there are better control structures by now (canaries)
extern uint32_t
        CHECK_CARVEOUT[1 / (SIZE_CARVEOUT == ((2u * 4u + (1000000u) * (4u + 4u)) +      // IV-INP
                                              (2u * 4u + (1000000u) * (8u + 4u + 4u)) + // IV-OUT
                                              (2u * 4u + (1000000u) * (8u + 2u)) +      // GPIO
                                              (2u * 4u + (400u) * (8u + 4u * 4u))  // Util
                                              ))];
*/

#if !defined(__GNUC__)
  #pragma DATA_SECTION(resourceTable, ".resource_table")
  #pragma RETAIN(resourceTable)
  #define __resource_table /* */
#else
  #define __resource_table __attribute__((section(".resource_table")))
#endif

struct my_resource_table resourceTable = {
        {
                1, /* Resource table version: only version 1 is supported by the current driver */
                1, /* number of entries in the table */
                {0U, 0U} /* reserved, must be zero */
        },
        /* offsets to entries */
        {
                offsetof(struct my_resource_table, shared_memory),
        },

        /* resource entries */
        {TYPE_CARVEOUT, 0x0, /* Memory address */
         0x0,                /* Physical address */
         SIZE_CARVEOUT,      /* ~ 34 MB */
         0,                  /* Flags */
         0,                  /* Reserved */
         "PRU_HOST_SHARED_MEM"},
};

#endif /* SHEPHERD_PRU0_RESOURCE_TABLE_DEF_H_ */
