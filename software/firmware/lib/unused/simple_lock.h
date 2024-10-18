#ifndef SHEPHERD_SIMPLE_LOCK_H_
#define SHEPHERD_SIMPLE_LOCK_H_

#include "stdint_fast.h"
#include <stdint.h>

typedef struct
{
    bool_ft lock_pru0;
    bool_ft lock_pru1;
} __attribute__((packed)) simple_mutex_t;

void                      simple_mutex_enter(volatile simple_mutex_t *mutex);
void                      simple_mutex_exit(volatile simple_mutex_t *mutex);


#endif /* SHEPHERD_SIMPLE_LOCK_H_ */
