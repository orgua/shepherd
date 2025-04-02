import ctypes as ct
from typing import ClassVar

from shepherd_core.data_models.content.virtual_harvester import HarvesterPRUConfig


class HarvesterConfig(ct.Structure):
    _pack_: ClassVar[int] = 1
    _fields_: ClassVar[list] = [(_key, ct.c_uint32) for _key in HarvesterPRUConfig.model_fields] + [
        ("canary", ct.c_uint32)
    ]


class CalibrationConfig(ct.Structure):
    _pack_: ClassVar[int] = 1
    _fields_: ClassVar[list] = [
        ("adc_current_gain", ct.c_uint32),  # adc_current_factor_nA_n8
        ("adc_current_offset", ct.c_int32),  # adc_current_offset_nA
        ("adc_voltage_gain", ct.c_uint32),  # adc_voltage_factor_uV_n8
        ("adc_voltage_offset", ct.c_int32),  # adc_voltage_offset_uV
        ("dac_voltage_gain", ct.c_uint32),  # dac_voltage_inv_factor_uV_n20
        ("dac_voltage_offset", ct.c_int32),  # dac_voltage_offset_uV
        # NOTE: above are the py-names as the c-struct is handed raw
        ("canary", ct.c_uint32),
    ]


LUT_SIZE: int = 12
LUT_INP = ct.c_uint8 * (LUT_SIZE * LUT_SIZE)
LUT_OUT = ct.c_uint32 * LUT_SIZE


class ConverterConfig(ct.Structure):
    _pack_: ClassVar[int] = 1
    _fields_: ClassVar[list] = [
        ("converter_mode", ct.c_uint32),
        ("interval_startup_delay_drain_n", ct.c_uint32),
        ("V_input_max_uV", ct.c_uint32),
        ("I_input_max_nA", ct.c_uint32),
        ("V_input_drop_uV", ct.c_uint32),
        ("R_input_kOhm_n22", ct.c_uint32),
        ("Constant_us_per_nF_n28", ct.c_uint32),
        ("V_intermediate_init_uV", ct.c_uint32),
        ("I_intermediate_leak_nA", ct.c_uint32),
        ("V_enable_output_threshold_uV", ct.c_uint32),
        ("V_disable_output_threshold_uV", ct.c_uint32),
        ("dV_enable_output_uV", ct.c_uint32),
        ("interval_check_thresholds_n", ct.c_uint32),
        ("V_pwr_good_enable_threshold_uV", ct.c_uint32),
        ("V_pwr_good_disable_threshold_uV", ct.c_uint32),
        ("immediate_pwr_good_signal", ct.c_uint32),
        ("V_output_log_gpio_threshold_uV", ct.c_uint32),
        ("V_input_boost_threshold_uV", ct.c_uint32),
        ("V_intermediate_max_uV", ct.c_uint32),
        ("V_output_uV", ct.c_uint32),
        ("V_buck_drop_uV", ct.c_uint32),
        ("LUT_input_V_min_log2_uV", ct.c_uint32),
        ("LUT_input_I_min_log2_nA", ct.c_uint32),
        ("LUT_output_I_min_log2_nA", ct.c_uint32),
        ("LUT_inp_efficiency_n8", LUT_INP),
        ("LUT_out_inv_efficiency_n4", LUT_OUT),
        ("canary", ct.c_uint32),
    ]


class SharedMemLight(ct.Structure):
    _pack_: ClassVar[int] = 1
    _fields_: ClassVar[list] = [
        ("pre_stuff", ct.c_uint32 * 9),  # TODO: update all below
        ("calibration_settings", CalibrationConfig),
        ("converter_settings", ConverterConfig),
        ("harvester_settings", HarvesterConfig),
        ("programmer_ctrl", ct.c_uint32 * 11),
        ("proto_msgs", ct.c_uint32 * (4 * 5)),
        ("sync_msgs", ct.c_uint32 * 7),
        ("canary", ct.c_uint32 * 1),
        ("timestamps", ct.c_uint64 * 2),
        # ("mutex_x", ct.c_uint32 * 2),  # bool_ft
        ("gpio_pin_state", ct.c_uint32),
        ("buffer_idxs", ct.c_uint32 * 2),
        ("buffer_iv_idx", ct.c_uint32),  # Pointer
        ("buffer_gpio_idx", ct.c_uint32),  # Pointer
        ("analog_x", ct.c_uint32 * 5),  # TODO: ivsample_fetch
        ("trigger_x", ct.c_uint32 * 2),  # bool_ft
        ("vsource_batok_trigger_for_pru1", ct.c_uint32),  # bool_ft
        ("vsource_skip_gpio_logging", ct.c_uint32),  # bool_ft
        ("vsource_batok_pin_value", ct.c_uint32),  # bool_ft
    ]
