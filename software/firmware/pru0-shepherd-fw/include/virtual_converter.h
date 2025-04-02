#ifndef PRU_FIRMWARE_PRU0_SHEPHERD_FW_CONVERTER_H
#define PRU_FIRMWARE_PRU0_SHEPHERD_FW_CONVERTER_H

#include "commons.h"
#include <stdint.h>

void            converter_initialize();

void            converter_calc_inp_power(uint32_t input_voltage_uV, uint32_t input_current_nA);
void            converter_calc_out_power(uint32_t current_adc_raw);
void            converter_update_cap_storage(void);
uint32_t        converter_update_states_and_output();

void            set_P_input_fW(uint32_t P_fW);
void            set_P_output_fW(uint32_t P_fW);
void            set_V_intermediate_uV(uint32_t C_uV);
uint64_t        get_P_input_fW(void);
uint64_t        get_P_output_fW(void);
uint32_t        get_V_intermediate_uV(void);
uint32_t        get_V_intermediate_raw(void);
uint32_t        get_I_mid_out_nA(void);
uint32_t        get_V_output_uV(void);
bool_ft         get_state_log_intermediate(void);

void            set_batok_pin(bool_ft value);

/* feedback to harvester - global vars */
extern bool_ft  feedback_to_hrv;
extern uint32_t V_input_request_uV;

/* Direct Connection
 * - Voltage-value in buffer is written to DAC
 * - (optional) current-value in buffer is used as a limiter (power to target shuts down if it is drawing too much)
 * - (optional) output-capacitor (C != 0) is catching current-spikes of target
 * - this converter is currently the closest possible simulation of solar -> diode -> target (with voltage-value set to threshold of target)
 * - further usage: on/off-patterns
 */

/* Boost Converter
 * - boost converter with storage_cap and output_cap on output (i.e. BQ25504)
 * - storage-capacitor has capacitance, init-voltage, current-leakage
 * - converter has min input threshold voltage, max capacitor voltage (shutoff), efficiency-LUT (depending on input current & voltage)
 * - capacitor-guard has enable and disable threshold voltage (hysteresis) to detach target
 * - target / output disconnect check is only every 65 ms
 * - TODO: to disable set V_intermediate_max_uV to 0
 * - input voltage can not be higher than cap_voltage and will be limited by algo
 * - the power point setting will be handled in pyPackage and work with IV-Curves
 */

/* Buck-Boost-Converter
 * - uses boost stage from before, but output is regulated (i.e. BQ25570)
 * - buck-converter has output_voltage and efficiency-LUT (depending on output-current)
 * - it will disconnect output when disable threshold voltage is reached or v_storage < v_out
 * - to disable set output_voltage to 0
 */

/* Solar - Diode - Target
 * -> currently not possible to emulate
 * - needs IV-curves and feedback
 */
#endif // PRU_FIRMWARE_PRU0_SHEPHERD_FW_CONVERTER_H
