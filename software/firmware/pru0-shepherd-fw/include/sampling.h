#ifndef __SAMPLING_H_
#define __SAMPLING_H_

#include "commons.h"

void sample_init(volatile const struct SharedMem * shared_mem);
void sample(struct SampleBuffer *current_buffer_far, uint32_t sample_idx, enum ShepherdMode mode);
uint32_t sample_dbg_adc(uint32_t channel_num);
void sample_dbg_dac(uint32_t value);

#endif /* __SAMPLING_H_ */
