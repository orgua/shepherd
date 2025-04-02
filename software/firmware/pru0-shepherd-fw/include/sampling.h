#ifndef SHEPHERD_PRU0_SAMPLING_H_
#define SHEPHERD_PRU0_SAMPLING_H_

#include "commons.h"

void     sample_init();

void     sample();

uint32_t sample_dbg_adc(uint32_t channel_num);
void     sample_dbg_dac(uint32_t value);

#endif /* SHEPHERD_PRU0_SAMPLING_H_ */
