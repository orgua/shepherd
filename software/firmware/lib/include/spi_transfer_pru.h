#ifndef PRU_FIRMWARE_PRU0_SHEPHERD_FW_INCLUDE_SPI_TRANSFER_PRU_H
#define PRU_FIRMWARE_PRU0_SHEPHERD_FW_INCLUDE_SPI_TRANSFER_PRU_H

#include "commons.h"

/* DAC8562 Register Config */
#define DAC_CH_A_ADDR   (0U << 16U)
#define DAC_CH_B_ADDR   (1U << 16U)
#define DAC_CH_AB_ADDR  (7U << 16U)

#define DAC_CMD_OFFSET  (19U)
#define DAC_ADDR_OFFSET (16U)

#define DAC_MAX_mV      (5000u)
#define DAC_MAX_VAL     (0xFFFFu)
#define DAC_M_BIT       (16u)
#define DAC_V_LSB       (76.2939e-6)

/* DAC Shift OPs */
#define DAC_V_LSB_nV    (76294u)
#define DAC_V_SHIFT     (10u)
#define DAC_V_FACTOR    (1000000u * (1u << DAC_V_SHIFT) / DAC_V_LSB_nV)
#define DAC_mV_2_raw(x) ((DAC_V_FACTOR * (x)) >> DAC_V_SHIFT)
// TODO: add calibration data
// Test range and conversion
ASSERT(dac_interm, (DAC_V_FACTOR * DAC_MAX_mV) < ((1ull << 32u) - 1u));
ASSERT(dac_convert, DAC_mV_2_raw(DAC_MAX_mV) <= DAC_MAX_VAL);

/* ADS8691 Register Config */
#define REGISTER_WRITE  (0b11010000u << 24u)
#define REGISTER_READ   (0b11001000u << 24u) /* lower half word */
//#define REGISTER_READ   (0b01001000u << 24u)  /* full u32 */

#define ADDR_REG_PWRCTL (0x04u << 16u)
#define WRITE_KEY       (0x69u << 8u)
#define PWRDOWN         (1u)
#define NOT_PWRDOWN     (0u)
#define NAP_EN          (1u << 1u)

#define ADDR_REG_RANGE  (0x14u << 16u)
#define RANGE_SEL_P125  (0b00001011u) // only positive
#define RANGE_SEL_125   (0b00000011u) // +- 1.25 VRef,

#define ADC_V_LSB       (19.5313e-6)
#define ADC_C_LSB       (195.313e-9)

/* VIn = DOut * Gain * Vref  / 2^n */
/* VIn = DOut * 1.25 * 4.096 / 2^18 */
/* VIn = DOut * 19.5313 uV */
/* CIn = DOut * 195.313 nA */
extern uint32_t adc_readwrite(uint32_t cs_pin, uint32_t val);
extern uint32_t adc_fastread(uint32_t cs_pin);

/* VOut = (DIn / 2^n ) * VRef * Gain */
/* VOut = (DIn / 2^16) * 2.5  * 2 */
/* VOut = DIn * 76.2939 uV  */
extern void     dac_write(uint32_t cs_pin, uint32_t val);

// NOTE: FNs are defined in spi_transfer_pru.asm

#endif //PRU_FIRMWARE_PRU0_SHEPHERD_FW_INCLUDE_SPI_TRANSFER_PRU_H
