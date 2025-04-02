#ifndef PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H
#define PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H

#include "commons.h"
#include "stdint.h"

void            harvester_initialize();

void            sample_adc_harvester(uint32_t sample_idx);

void            sample_ivcurve_harvester(uint32_t *p_voltage_uV, uint32_t *p_current_nA);

/* global vars to allow feedback from vsource */
extern uint32_t voltage_set_uV;

#endif //PRU_FIRMWARE_PRU0_SHEPHERD_FW_HARVESTER_H
