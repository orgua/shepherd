#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "commons.h"
#include "float_pseudo.h"

void vsource_init(volatile const struct VirtSource_Config *vsc_arg, volatile const struct Calibration_Config *cal_arg);

void vsource_calc_inp_power(uint32_t input_current_nA, uint32_t input_voltage_uV);
void vsource_calc_out_power(uint32_t current_adc_raw);
void vsource_update_capacitor(void);
uint32_t vsource_update_buckboost(void);

void vsource_struct_init(volatile struct VirtSource_Config * vsc_arg);

void set_input_power_pW(uint32_t P_pW);
void set_output_power_pW(uint32_t P_pW);
void set_storage_Capacitor_uV(uint32_t C_uV);
uint32_t get_input_power_pW(void);
uint32_t get_output_power_pW(void);
uint32_t get_storage_Capacitor_uV(void);


/* Direct Connection
 * - Voltage-value in buffer is written to DAC
 * - (optional) current-value in buffer is used as a limiter (power to target shuts down if it is drawing to much)
 * - (optional) output-capacitor (C != 0) is catching current-spikes of target
 * - this regulator is currently the closest possible simulation of solar -> diode -> target (with voltage-value set to threshold of target)
 * - further usage: on/off-patterns
 */

/* Boost Converter
 * - boost converter with storage_cap and output_cap on output (i.e. BQ25504)
 * - storage-capacitor has capacitance, init-voltage, current-leakage
 * - converter has min input threshold voltage, max capacitor voltage (shutoff), efficiency-LUT (depending on input current & voltage)
 * - capacitor-guard has enable and disable threshold voltage (hysteresis) to detach target
 * - target / output disconnect check is only every 65 ms
 * - TODO: to disable set V_storage_max_uV to 0
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
 * - needs IV-curves and feedback to
 * TODO: later
 */
