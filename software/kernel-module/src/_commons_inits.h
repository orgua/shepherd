#ifndef KERNELMODULE_COMMONS_INITS_H
#define KERNELMODULE_COMMONS_INITS_H

#include "_commons.h"
#include "_shepherd_config.h"

/* Struct-Initializers
 * why? this is (safe) nonsense, that is testable for byteorder and proper values
 * */

const struct ProgrammerCtrl ProgrammerCtrl_default = {
        .state        = PRG_STATE_IDLE,
        .target       = PRG_TARGET_NONE,
        .datarate     = 1000000ul,
        .datasize     = 0u,
        .pin_tck      = 1001ul,
        .pin_tdio     = 1002ul,
        .pin_dir_tdio = 1003ul,
        .pin_tdo      = 1004ul,
        .pin_tms      = 1005ul,
        .pin_dir_tms  = 1006ul,
        .canary       = CANARY_VALUE_U32,
};

const struct CalibrationConfig CalibrationConfig_default = {
        .adc_current_factor_nA_n8      = 255u,
        .adc_current_offset_nA         = -1,
        .adc_voltage_factor_uV_n8      = 254u,
        .adc_voltage_offset_uV         = -2,
        .dac_voltage_inv_factor_uV_n20 = 253u,
        .dac_voltage_offset_uV         = -3,
        .canary                        = CANARY_VALUE_U32,
};

const struct ConverterConfig ConverterConfig_default = {
        .converter_mode                  = 100u,
        .interval_startup_delay_drain_n  = 101u,

        .V_input_max_uV                  = 102u,
        .I_input_max_nA                  = 103u,
        .V_input_drop_uV                 = 104u,
        .R_input_kOhm_n22                = 105u,

        .Constant_us_per_nF_n28          = 106u,
        .V_intermediate_init_uV          = 107u,
        .I_intermediate_leak_nA          = 108u,

        .V_enable_output_threshold_uV    = 109u,
        .V_disable_output_threshold_uV   = 110u,
        .dV_enable_output_uV             = 111u,
        .interval_check_thresholds_n     = 112u,

        .V_pwr_good_enable_threshold_uV  = 113u,
        .V_pwr_good_disable_threshold_uV = 114u,
        .immediate_pwr_good_signal       = 115u,

        .V_output_log_gpio_threshold_uV  = 116u,

        .V_input_boost_threshold_uV      = 117u,
        .V_intermediate_max_uV           = 118u,

        .V_output_uV                     = 119u,
        .V_buck_drop_uV                  = 120u,

        .LUT_input_V_min_log2_uV         = 121u,
        .LUT_input_I_min_log2_nA         = 122u,
        .LUT_output_I_min_log2_nA        = 123u,

        .LUT_inp_efficiency_n8 =
                {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u},
                 {12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u},
                 {24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u, 35u},
                 {36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u},
                 {48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u},
                 {60u, 61u, 62u, 63u, 64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u},
                 {72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u, 80u, 81u, 82u, 83u},
                 {84u, 85u, 86u, 87u, 88u, 89u, 90u, 91u, 92u, 93u, 94u, 95u},
                 {96u, 97u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 107u},
                 {108u, 109u, 110u, 111u, 112u, 113u, 114u, 115u, 116u, 117u, 118u, 119u},
                 {120u, 121u, 122u, 123u, 124u, 125u, 126u, 127u, 128u, 129u, 130u, 131u},
                 {132u, 133u, 134u, 135u, 136u, 137u, 138u, 139u, 140u, 141u, 142u, 143u}},
        .LUT_out_inv_efficiency_n4 = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u},
        .canary                    = CANARY_VALUE_U32,
};

const struct HarvesterConfig HarvesterConfig_default = {
        .algorithm        = 0u,
        .hrv_mode         = 200u,
        .window_size      = 201u,
        .voltage_uV       = 202u,
        .voltage_min_uV   = 203u,
        .voltage_max_uV   = 204u,
        .voltage_step_uV  = 205u,
        .current_limit_nA = 206u,
        .setpoint_n8      = 207u,
        .interval_n       = 208u,
        .duration_n       = 209u,
        .wait_cycles_n    = 210u,
        .canary           = CANARY_VALUE_U32,
};

const struct ProtoMsg ProtoMsg_default = {.id     = 0u,
                                          .unread = 0u,
                                          .type   = 0u,
                                          .value  = {0u, 0u},
                                          .canary = CANARY_VALUE_U32};

#endif //KERNELMODULE_COMMONS_INITS_H
