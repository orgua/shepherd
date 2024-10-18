#ifndef RINGBUFFER_H_
#define RINGBUFFER_H_

#include "commons.h"
#include "stdint_fast.h"

#define FIFO_BUFFER_SIZE (5u) // NOTE: just mocked

struct RingBuffer
{
    uint8_t  ring[FIFO_BUFFER_SIZE];
    uint32_t start; // TODO: these can be smaller, at least in documentation
    uint32_t end;
    uint32_t active;
};

void    ring_init(struct RingBuffer *buf);
void    ring_put(struct RingBuffer *buf, uint8_t element);
bool_ft ring_get(struct RingBuffer *buf, uint8_t *element);
bool_ft ring_empty(const struct RingBuffer *buf);

#endif
