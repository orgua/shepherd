#ifndef PRU_FIRMWARE_PRU0_INCLUDE_PROGRAMMER_H
#define PRU_FIRMWARE_PRU0_INCLUDE_PROGRAMMER_H

#include "commons.h"

void programmer(volatile struct ProgrammerCtrl *const pctrl, const uint32_t *const fw_data);

#endif //PRU_FIRMWARE_PRU0_INCLUDE_PROGRAMMER_H
