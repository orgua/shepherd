#ifndef SHEPHERD_PRU0_RESOURCE_TABLE_H_
#define SHEPHERD_PRU0_RESOURCE_TABLE_H_

#include <rsc_types.h>

struct my_resource_table
{
    struct resource_table  base;

    /* offsets to entries */
    uint32_t               offset[1]; /* Should match 'num' in actual definition */

    /* mem-resource definition */
    struct fw_rsc_carveout shared_memory;
};

#endif /* SHEPHERD_PRU0_RESOURCE_TABLE_H_ */
