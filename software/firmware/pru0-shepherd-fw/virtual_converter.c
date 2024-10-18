#include "virtual_converter.h"
#include "calibration.h"
#include "commons.h"
#include "hw_config.h"
#include "math64_safe.h"
#include "stdint_fast.h"
#include <stddef.h>
#include <stdint.h>

#include "fw_config.h"
#include "shared_mem.h"

/* ---------------------------------------------------------------------
 * Virtual Converter, TODO: update description
 * ----------------------------------------------------------------------
 */

#ifdef EMU_SUPPORT
/* private FNs */
static uint32_t get_input_efficiency_n8(uint32_t voltage_uV, uint32_t current_nA);
static uint32_t get_output_inv_efficiency_n4(uint32_t current_nA);

  #define DIV_SHIFT    (17u) // 2^17 as uV is ~ 131 mV
  #define DIV_LUT_SIZE (40u)

/* LUT for faster division
 * Generation:
 * - array[n] = (1u << 27) / (n * (1u << 17)) = (1u << 10u) / (n + 0.5)
 * - limit array[0] to 1023 -> not needed anymore due to mult with overflow-protection, instead overprovision
 * - the largest value array[39] is 5.11 V
 * python:
 *    LUT = [(2**10)/(n + 0.5) for n in range(40)]
 */
static const uint32_t LUT_div_uV_n27[DIV_LUT_SIZE] = {
        16383, 683, 410, 293, 228, 186, 158, 137, 120, 108, 98, 89, 82, 76, 71, 66, 62, 59, 55, 53,
        50,    48,  46,  44,  42,  40,  39,  37,  36,  35,  34, 33, 32, 31, 30, 29, 28, 27, 27, 26};

static uint64_t div_uV_n4(const uint64_t power_fW_n4, const uint32_t voltage_uV)
{
    uint8_t lut_pos = (voltage_uV >> DIV_SHIFT);
    if (lut_pos >= DIV_LUT_SIZE) lut_pos = DIV_LUT_SIZE - 1u;
    return mul64((power_fW_n4 >> 10u), LUT_div_uV_n27[lut_pos]) >> 17u;
}
#endif // EMU_SUPPORT

/* data-structure that hold the state - variables for local / direct use */
struct ConverterState
{
    uint32_t interval_startup_disabled_drain_n;
    bool_ft  enable_storage;
    uint32_t V_input_uV;

    /* Boost converter */
    bool_ft  enable_boost;
    bool_ft  enable_log_mid;
    uint64_t P_inp_fW_n8;
    uint64_t P_out_fW_n4;
    uint64_t V_mid_uV_n32;
    /* Buck converter */
    bool_ft  enable_buck;
    uint32_t V_out_dac_uV;
    uint32_t V_out_dac_raw;
    /* hysteresis */
    uint64_t V_enable_output_threshold_uV_n32;
    uint64_t V_disable_output_threshold_uV_n32;
    uint64_t dV_enable_output_uV_n32;
    bool_ft  power_good;
};

/* feedback to harvester - global vars */
bool_ft                      feedback_to_hrv    = 0u;
uint32_t                     V_input_request_uV = 0u;

/* (local) global vars to access in update function */
static struct ConverterState state;
#define CNV_CFG                                                                                    \
    (*((volatile struct ConverterConfig *) (PRU_SHARED_MEM_OFFSET +                                \
                                            offsetof(struct SharedMem, converter_settings))))

void converter_initialize()
{
    /* Power-flow in and out of system */
    state.V_input_uV                        = 0u; // TODO: is it used?
    state.P_inp_fW_n8                       = 0ull;
    state.P_out_fW_n4                       = 0ull;
    state.interval_startup_disabled_drain_n = CNV_CFG.interval_startup_delay_drain_n;

    /* container for the stored energy: */
    state.V_mid_uV_n32                      = ((uint64_t) CNV_CFG.V_intermediate_init_uV) << 32u;

    /* Buck Boost */
    state.enable_storage                    = (CNV_CFG.converter_mode & 0b0001) > 0;
    state.enable_boost                      = (CNV_CFG.converter_mode & 0b0010) > 0;
    state.enable_buck                       = (CNV_CFG.converter_mode & 0b0100) > 0;
    state.enable_log_mid                    = (CNV_CFG.converter_mode & 0b1000) > 0;

    state.V_out_dac_uV                      = CNV_CFG.V_output_uV;
    state.V_out_dac_raw                     = cal_conv_uV_to_dac_raw(CNV_CFG.V_output_uV);
    state.power_good                        = true;

    /* prepare hysteresis-thresholds */
    state.dV_enable_output_uV_n32           = ((uint64_t) CNV_CFG.dV_enable_output_uV) << 32u;
    state.V_enable_output_threshold_uV_n32  = ((uint64_t) CNV_CFG.V_enable_output_threshold_uV)
                                             << 32u;
    state.V_disable_output_threshold_uV_n32 = ((uint64_t) CNV_CFG.V_disable_output_threshold_uV)
                                              << 32u;

    if (state.dV_enable_output_uV_n32 > state.V_enable_output_threshold_uV_n32)
    {
        // safe V_mid_uV_n32 from underflow in vsource_update_states_and_output()
        // this should not happen, but better safe than ...
        state.V_enable_output_threshold_uV_n32 = state.dV_enable_output_uV_n32;
    }

    /* feedback to harvester */
    feedback_to_hrv    = (CNV_CFG.converter_mode & 0b10000) > 0u;
    V_input_request_uV = CNV_CFG.V_intermediate_init_uV;

    /* compensate for (hard to detect) current-surge of real capacitors when converter gets turned on
	 * -> this can be const value, because the converter always turns on with "V_intermediate_enable_threshold_uV"
	 * TODO: currently neglecting: delay after disabling converter, boost only has simpler formula, second enabling when VCap >= V_out
	 * TODO: this can be done in python, even both enable-cases
	 * Math behind this calculation:
	 * Energy-Change in Storage Cap -> 	E_new = E_old - E_output
	 * with Energy of a Cap 	-> 	E_x = C_x * V_x^2 / 2
	 * combine formulas 		-> 	C_store * V_store_new^2 / 2 = C_store * V_store_old^2 / 2 - C_out * V_out^2 / 2
	 * convert formula to V_new 	->	V_store_new^2 = V_store_old^2 - (C_out / C_store) * V_out^2
	 * convert into dV	 	->	dV = V_store_new - V_store_old
	 * in case of V_cap = V_out 	-> 	dV = V_store_old * (sqrt(1 - C_out / C_store) - 1)
	 */
    /*
	const ufloat V_old_sq_uV = mul0(CNV_CFG.V_intermediate_enable_threshold_uV, 0, CNV_CFG.V_intermediate_enable_threshold_uV, 0);
	const ufloat V_out_sq_uV = mul2(state.V_out_dac_uV, state.V_out_dac_uV);
	const ufloat cap_ratio   = div0(CNV_CFG.C_output_nF, 0, CNV_CFG.C_storage_nF, 0);
	const ufloat V_new_sq_uV = sub2(V_old_sq_uV, mul2(cap_ratio, V_out_sq_uV));
	GPIO_ON(DEBUG_PIN1_MASK);
	state.dV_stor_en_uV = sub1r(CNV_CFG.V_intermediate_enable_threshold_uV, 0, sqrt_rounded(V_new_sq_uV)); // reversed, because new voltage is lower then old
	*/
    // TODO: add tests for valid ranges -> not here
    // TODO: redo unit-test so that normal emulation is used, no special messages anymore (or substantially less)
}

// TODO: explain design goals and limitations... why does the code looks that way
/* Math behind this Converter
 * Individual drains / sources -> 	P_x = I_x * V_x
 * Efficiency 				eta_x = P_out_x / P_inp_x  -> P_out_x = P_inp_x * eta_x
 * Power in and out of Converter -> 	P = P_in - P_out
 * Current in storage cap -> 		I = P / V_cap
 * voltage change for Cap -> 		dV = I * dt / C
 * voltage of storage cap -> 		V += dV
 *
 */
#ifdef EMU_SUPPORT

void converter_calc_inp_power(uint32_t input_voltage_uV, uint32_t input_current_nA)
{
    // info input: voltage is max 5V => 23 bit, current is max 50 mA => 26 bit
    // info output: with eta being 8 bit in size, there is 56 bit headroom for P = U*I = ~ 72 W
    // NOTE: p_inp_fW could be calculated in python, even with efficiency-interpolation -> hand voltage and power to pru
    /* BOOST, Calculate current flowing into the storage capacitor */
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
    if (input_voltage_uV > CNV_CFG.V_input_drop_uV) { input_voltage_uV -= CNV_CFG.V_input_drop_uV; }
    else { input_voltage_uV = 0u; }

    if (input_voltage_uV > CNV_CFG.V_input_max_uV) { input_voltage_uV = CNV_CFG.V_input_max_uV; }

    if (input_current_nA > CNV_CFG.I_input_max_nA) { input_current_nA = CNV_CFG.I_input_max_nA; }

    state.V_input_uV = input_voltage_uV;

    if (state.enable_boost)
    {
        /* disable boost if input voltage too low for boost to work, TODO: is this also in 65ms interval? */
        if (input_voltage_uV < CNV_CFG.V_input_boost_threshold_uV) { input_voltage_uV = 0u; }

        // if (input_voltage_uV > (state.V_mid_uV_n32 >> 32u) + CNV_CFG.V_input_drop_uV)
        // TODO: vdrop in case of v_input > v_storage (non-boost)?
    }
    else if (state.enable_storage)
    {
        // no boost, but cap, for ie. diode+cap (+resistor)
        const uint32_t V_mid_uV = (state.V_mid_uV_n32 >> 32u);
        const uint32_t V_diff_uV =
                (input_voltage_uV >= V_mid_uV) ? input_voltage_uV - V_mid_uV : 0u;
        const uint32_t V_res_drop_uV =
                (uint32_t) (((uint64_t) input_current_nA * (uint64_t) CNV_CFG.R_input_kOhm_n22) >>
                            22u);
        if (V_res_drop_uV > V_diff_uV) { input_voltage_uV = V_mid_uV; }
        else { input_voltage_uV -= V_res_drop_uV; }

        if (feedback_to_hrv)
        {
            // IF input==ivcurve request new CV
            V_input_request_uV = V_mid_uV + V_res_drop_uV + CNV_CFG.V_input_drop_uV;
        }
        else if (input_voltage_uV < V_mid_uV)
        {
            // without feedback there is no usable energy here
            input_voltage_uV = 0u;
        }
    }
    else
    {
        /* direct connection
           modifying V_mid here is not clean, but simpler
           -> V_mid is needed in calc_out, before cap is updated
        */
        state.V_mid_uV_n32 = ((uint64_t) input_voltage_uV) << 32u;
        input_voltage_uV   = 0u;
        // ⤷ input will not be evaluated
    }

    const uint32_t eta_inp_n8 =
            (state.enable_boost) ? get_input_efficiency_n8(input_voltage_uV, input_current_nA)
                                 : (1u << 8u);
    state.P_inp_fW_n8 =
            mul64((uint64_t) eta_inp_n8 * (uint64_t) input_voltage_uV, input_current_nA);

    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
}

void converter_calc_out_power(const uint32_t current_adc_raw)
{
    // input: current is max 50 mA => 26 bit
    // states: voltage is 23 bit,
    // output: with eta being 14 bit in size, there is 50 bit headroom for P = U*I = ~ 1 W
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
    /* BUCK, Calculate current flowing out of the storage capacitor */
    const uint64_t V_mid_uV_n4  = (state.V_mid_uV_n32 >> 28u);
    const uint64_t P_leak_fW_n4 = mul64(CNV_CFG.I_intermediate_leak_nA, V_mid_uV_n4);
    const uint32_t I_out_nA     = cal_conv_adc_raw_to_nA(current_adc_raw);
    const uint32_t eta_inv_out_n4 =
            (state.enable_buck) ? get_output_inv_efficiency_n4(I_out_nA) : (1u << 4u);
    state.P_out_fW_n4 =
            add64(mul64((uint64_t) eta_inv_out_n4 * (uint64_t) state.V_out_dac_uV, I_out_nA),
                  P_leak_fW_n4);

    // allows target to initialize and go to sleep
    if (state.interval_startup_disabled_drain_n > 0u)
    {
        state.interval_startup_disabled_drain_n--;
        state.P_out_fW_n4 = 0u;
    }
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
}

void converter_update_cap_storage(void)
{
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
    /* Sum up Power and calculate new Capacitor Voltage
	 */
    if (state.enable_storage)
    {
        uint32_t V_mid_uV = state.V_mid_uV_n32 >> 32u;
        if (V_mid_uV < 1u) V_mid_uV = 1u; // avoid and possible div0
        const uint64_t P_inp_fW_n4 = state.P_inp_fW_n8 >> 4u;
        // avoid mixing in signed data-types -> slows pru and reduces resolution
        if (P_inp_fW_n4 > state.P_out_fW_n4)
        {
            const uint64_t I_mid_nA_n4   = div_uV_n4(P_inp_fW_n4 - state.P_out_fW_n4, V_mid_uV);
            const uint64_t dV_mid_uV_n32 = mul64(CNV_CFG.Constant_us_per_nF_n28, I_mid_nA_n4);
            state.V_mid_uV_n32           = add64(state.V_mid_uV_n32, dV_mid_uV_n32);
        }
        else
        {
            const uint64_t I_mid_nA_n4   = div_uV_n4(state.P_out_fW_n4 - P_inp_fW_n4, V_mid_uV);
            const uint64_t dV_mid_uV_n32 = mul64(CNV_CFG.Constant_us_per_nF_n28, I_mid_nA_n4);
            state.V_mid_uV_n32           = sub64(state.V_mid_uV_n32, dV_mid_uV_n32);
        }
    }

    // Make sure the voltage stays in it's boundaries, TODO: is this also in 65ms interval?
    if ((uint32_t) (state.V_mid_uV_n32 >> 32u) > CNV_CFG.V_intermediate_max_uV)
    {
        state.V_mid_uV_n32 = ((uint64_t) CNV_CFG.V_intermediate_max_uV) << 32u;
    }
    if ((uint32_t) (state.V_mid_uV_n32 >> 32u) < 1u)
    {
        state.V_mid_uV_n32 = ((uint64_t) 1u) << 32u;
    }
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
}

// TODO: not optimized
uint32_t converter_update_states_and_output()
{
    //GPIO_TOGGLE(DEBUG_PIN1_MASK);

    /* connect or disconnect output on certain events */
    static uint32_t sample_count     = 0xFFFFFFF0u;
    static bool_ft  is_outputting    = true;
    const bool_ft   check_thresholds = (++sample_count >= CNV_CFG.interval_check_thresholds_n);

    if (check_thresholds)
    {
        sample_count = 0;
        if (is_outputting)
        {
            if (state.V_mid_uV_n32 < state.V_disable_output_threshold_uV_n32)
            {
                is_outputting = false;
            }
        }
        else
        {
            if (state.V_mid_uV_n32 >= state.V_enable_output_threshold_uV_n32)
            {
                is_outputting      = true;
                /* fast charge external virtual output-cap */
                state.V_mid_uV_n32 = sub64(state.V_mid_uV_n32, state.dV_enable_output_uV_n32);
            }
        }
    }

    const uint32_t V_mid_uV = (uint32_t) (state.V_mid_uV_n32 >> 32u);

    if (check_thresholds || CNV_CFG.immediate_pwr_good_signal)
    {
        /* emulate power-good-signal */
        if (state.power_good)
        {
            if (V_mid_uV <= CNV_CFG.V_pwr_good_disable_threshold_uV) { state.power_good = false; }
        }
        else
        {
            if (V_mid_uV >= CNV_CFG.V_pwr_good_enable_threshold_uV)
            {
                state.power_good = is_outputting;
            }
        }
        set_batok_pin(state.power_good);
    }

    if (is_outputting || (state.interval_startup_disabled_drain_n > 0u))
    {
        if ((state.enable_buck == false) ||
            (V_mid_uV <= CNV_CFG.V_output_uV + CNV_CFG.V_buck_drop_uV))
        {
            state.V_out_dac_uV =
                    (V_mid_uV > CNV_CFG.V_buck_drop_uV) ? V_mid_uV - CNV_CFG.V_buck_drop_uV : 0u;
        }
        else { state.V_out_dac_uV = CNV_CFG.V_output_uV; }
        state.V_out_dac_raw = cal_conv_uV_to_dac_raw(state.V_out_dac_uV);
    }
    else
    {
        state.V_out_dac_uV =
                0u; /* needs to be higher or equal min(V_mid_uV) to avoid jitter on low voltages */
        state.V_out_dac_raw = 0u;
    }

    // helps to prevent jitter-noise in gpio-traces
    SHARED_MEM.vsource_skip_gpio_logging =
            (state.V_out_dac_uV < CNV_CFG.V_output_log_gpio_threshold_uV);

    //GPIO_TOGGLE(DEBUG_PIN1_MASK);
    /* output proper voltage to dac */
    return state.V_out_dac_raw;
}


// TODO: global /nonstatic for tests
uint32_t get_input_efficiency_n8(const uint32_t voltage_uV, const uint32_t current_nA)
{
    uint8_t pos_v = voltage_uV >> CNV_CFG.LUT_input_V_min_log2_uV; // V-Scale is Linear!
    uint8_t pos_c = log2safe(current_nA >> CNV_CFG.LUT_input_I_min_log2_nA);
    if (pos_v >= LUT_SIZE) pos_v = LUT_SIZE - 1;
    if (pos_c >= LUT_SIZE) pos_c = LUT_SIZE - 1;
    /* TODO: could interpolate here between 4 values, if there is time for overhead */
    return (uint32_t) CNV_CFG.LUT_inp_efficiency_n8[pos_v][pos_c];
}

uint32_t get_output_inv_efficiency_n4(const uint32_t current_nA)
{
    uint8_t pos_c = log2safe(current_nA >> CNV_CFG.LUT_output_I_min_log2_nA);
    if (pos_c >= LUT_SIZE) pos_c = LUT_SIZE - 1u;
    /* TODO: could interpolate here between 2 values, if there is space for overhead */
    return CNV_CFG.LUT_out_inv_efficiency_n4[pos_c];
}

void set_P_input_fW(const uint32_t P_fW) { state.P_inp_fW_n8 = ((uint64_t) P_fW) << 8u; }

void set_P_output_fW(const uint32_t P_fW) { state.P_out_fW_n4 = ((uint64_t) P_fW) << 4u; }

void set_V_intermediate_uV(const uint32_t C_uV) { state.V_mid_uV_n32 = ((uint64_t) C_uV) << 32u; }

uint64_t get_P_input_fW(void) { return (state.P_inp_fW_n8 >> 8u); }

uint64_t get_P_output_fW(void) { return (state.P_out_fW_n4 >> 4u); }

uint32_t get_V_intermediate_uV(void) { return (uint32_t) (state.V_mid_uV_n32 >> 32u); }

uint32_t get_V_intermediate_raw(void)
{
    return cal_conv_uV_to_dac_raw((uint32_t) (state.V_mid_uV_n32 >> 32u));
}

uint32_t get_V_output_uV(void) { return state.V_out_dac_uV; }

uint32_t get_I_mid_out_nA(void)
{
    return (uint32_t) div_uV_n4(state.P_out_fW_n4, state.V_mid_uV_n32 >> 28u);
}

bool_ft get_state_log_intermediate(void) { return state.enable_log_mid; }

#endif // EMU_SUPPORT

void set_batok_pin(const bool_ft value)
{
    SHARED_MEM.vsource_batok_pin_value        = value;
    SHARED_MEM.vsource_batok_trigger_for_pru1 = true;
}
