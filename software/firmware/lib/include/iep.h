#ifndef SHEPHERD_IEP_H_
#define SHEPHERD_IEP_H_

#include "stdint_fast.h"
#include <stdbool.h>
#include <stdint.h>

#define IEP_CMP0      (0U)
#define IEP_CMP0_MASK (1U << IEP_CMP0)
#define IEP_CMP1      (1U)
#define IEP_CMP1_MASK (1U << IEP_CMP1)
#define IEP_CMP2      (2U)
#define IEP_CMP3      (3U)
#define IEP_CMP4      (4U)
#define IEP_CMP5      (5U)
#define IEP_CMP6      (6U)
#define IEP_CMP7      (7U)


void     iep_init();
void     iep_set_us(uint32_t value);
void     iep_reset();
void     iep_start();
void     iep_stop();
bool_ft  iep_check_evt_cmp(uint8_ft compare_channel);
uint32_t iep_get_tmr_cmp_sts();
uint32_t iep_check_evt_cmp_fast(uint32_t tmr_cmp_sts, uint32_t compare_channel_mask);
void     iep_clear_evt_cmp(uint8_ft compare_channel);
void     iep_enable_evt_cmp(uint8_ft compare_channel);
bool_ft  iep_enable_status_evt_cmp(uint8_ft compare_channel);
void     iep_disable_evt_cmp(uint8_ft compare_channel);
void     iep_set_cmp_val(uint8_ft compare_channel, uint32_t value);
uint32_t iep_get_cmp_val(uint8_ft compare_channel);
uint32_t iep_get_cnt_val();

void     iep_set_compensation_inc(const uint32_t value);
void     iep_compensate();
void     iep_set_increment(const uint32_t value);


#endif /* SHEPHERD_IEP_H_ */
