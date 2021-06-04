from typing import NoReturn
from shepherd import VirtualSourceData, CalibrationData
import math

# NOTE: DO NOT OPTIMIZE -> stay close to original code-base
sample_count = 0
is_outputting = False


class VirtualSource(object):
    """
    this is ported py-version of the pru-code, goals:
    - stay close to original code-base
    - offer a comparison for the tests
    - step 1 to a virtualization of emulation

    """

    vsc = dict()
    cal = CalibrationData.from_default()

    def __init__(self, vs_settings, cal_setting):
        """

        :param vs_settings: YAML-Path, dict, or regulator-name
        """
        if cal_setting is not None:
            self.cal = cal_setting

        # NOTE:
        #  - yaml is based on nA, mV, ms, uF
        #  - c-code and py-copy is using nA, uV, ns, nF, pW
        vs_settings = VirtualSourceData(vs_settings)
        values = vs_settings.export_for_sysfs()

        # CONSTs, TODO: bring to commons (datalog can probably benefit as well)
        ADC_SAMPLES_PER_BUFFER = 10000
        BUFFER_PERIOD_NS = 100000000
        SAMPLE_INTERVAL_NS = (BUFFER_PERIOD_NS / ADC_SAMPLES_PER_BUFFER)
        LUT_SIZE = 12
        self.vsc["LUT_size"] = LUT_SIZE

        # generate a new dict from raw_list (that is intended for PRU / sys_fs, see commons.h)
        self.vsc["converter_mode"] = values[0]

        # direct regulator
        self.vsc["C_output_nF"] = values[1]  # final (always last) stage to catch current spikes of target

        # boost regulator
        self.vsc["V_inp_boost_threshold_uV"] = values[2]  # min input-voltage for the boost converter to work
        self.vsc["C_storage_nF"] = values[3]
        self.vsc["V_storage_init_uV"] = values[4]  # allow a proper / fast startup
        self.vsc["V_storage_max_uV"] = values[5]  # -> boost shuts off

        self.vsc["I_storage_leak_nA"] = values[6]

        self.vsc["V_storage_enable_threshold_uV"] = values[7]  # -> target gets connected (hysteresis-combo with next value)
        self.vsc["V_storage_disable_threshold_uV"] = values[8]  # -> target gets disconnected

        self.vsc["interval_check_thresholds_ns"] = values[9]  # some BQs check every 65 ms if output should be disconnected

        self.vsc["V_pwr_good_low_threshold_uV"] = values[10]  # range where target is informed by output-pin
        self.vsc["V_pwr_good_high_threshold_uV"] = values[11]

        self.vsc["dV_stor_en_thrs_uV"] = values[12]

        # Buck Boost, ie. BQ25570)
        self.vsc["V_output_uV"] = values[13]
        self.vsc["dV_stor_low_uV"] = values[14]

        # LUTs
        # NOTE: config sets input_n10 but the list transmits n8 (for PRU)
        self.vsc["LUT_inp_efficiency_n8"] = values[15]  # depending on inp_voltage, inp_current, (cap voltage),
        self.vsc["LUT_out_inv_efficiency_n10"] = values[16]  # depending on output_current

        # boost internal state
        self.vsc["P_inp_pW"] = 0.0
        self.vsc["P_out_pW"] = 0.0
        self.vsc["dt_us_per_C_nF"] = SAMPLE_INTERVAL_NS / (1000 * self.vsc["C_storage_nF"])

        self.vsc["interval_check_thrs_sample"] = self.vsc["interval_check_thresholds_ns"] / SAMPLE_INTERVAL_NS
        self.vsc["V_store_uV"] = self.vsc["V_storage_init_uV"]
        # buck internal state
        self.vsc["V_out_uV"] = self.vsc["V_output_uV"]
        self.vsc["V_out_dac_raw"] = 1023  # TODO: unimplemented

    def calc_inp_power(self, input_voltage_uV: int, input_current_nA: int) -> int:

        V_inp_uV = 0
        if input_voltage_uV > self.vsc["V_inp_boost_threshold_uV"]:
            V_inp_uV = input_voltage_uV
        if V_inp_uV > self.vsc["V_store_uV"]:
            V_inp_uV = self.vsc["V_store_uV"]

        eta_inp = self.get_input_efficiency(input_voltage_uV, input_current_nA)
        self.vsc["P_inp_pW"] = V_inp_uV * input_current_nA * eta_inp
        return self.vsc["P_inp_pW"]  # return NOT original, added for easier testing

    def calc_out_power(self, current_adc_raw) -> int:

        I_out_nA = self.conv_adc_raw_to_nA(current_adc_raw)
        eta_inv_out = self.get_output_inv_efficiency(current_adc_raw)
        dP_leak_pW = self.vsc["V_store_uV"] * self.vsc["I_storage_leak_nA"]
        self.vsc["P_out_pW"] = I_out_nA * self.vsc["V_out_uV"] * eta_inv_out + dP_leak_pW
        return self.vsc["P_out_pW"]  # return NOT original, added for easier testing

    def update_capacitor(self) -> int:

        P_sum_pW = self.vsc["P_inp_pW"] - self.vsc["P_out_pW"]
        I_cStor_nA = P_sum_pW / self.vsc["V_store_uV"]
        dV_cStor_uV = I_cStor_nA * self.vsc["dt_us_per_C_nF"]
        self.vsc["V_store_uV"] = self.vsc["V_store_uV"] + dV_cStor_uV
        return self.vsc["V_store_uV"]  # return NOT original, added for easier testing

    def update_buckboost(self) -> int:
        global sample_count, is_outputting

        if self.vsc["V_store_uV"] > self.vsc["V_storage_max_uV"]:
            self.vsc["V_store_uV"] = self.vsc["V_storage_max_uV"]

        sample_count += 1
        if sample_count == self.vsc["interval_check_thrs_sample"]:
            sample_count = 0
            if is_outputting:
                if (self.vsc["V_store_uV"] < self.vsc["V_out_uV"]) or \
                        (self.vsc["V_store_uV"] < self.vsc["V_storage_disable_threshold_uV"]):
                    is_outputting = False
            else:
                if self.vsc["V_store_uV"] > self.vsc["V_out_uV"]:
                    is_outputting = True
                    self.vsc["V_store_uV"] = self.vsc["V_store_uV"] - self.vsc["dV_stor_low_uV"]
                if self.vsc["V_store_uV"] > self.vsc["V_storage_disable_threshold_uV"]:
                    is_outputting = True
                    self.vsc["V_store_uV"] = self.vsc["V_store_uV"] - self.vsc["dV_stor_en_thrs_uV"]

        return self.vsc["V_out_dac_raw"] if is_outputting else 0

    def conv_adc_raw_to_nA(self, current_raw: int) -> float:
        return self.cal.convert_raw_to_value("emulation", "adc_current", current_raw) * (10 ** 9)

    def conv_uV_to_dac_raw(self, voltage_uV: int) -> int:
        return self.cal.convert_value_to_raw("emulation", "dac_voltage_b", float(voltage_uV) / (10 ** 6))

    def get_input_efficiency(self, voltage_uV: int, current_nA: int) -> float:
        pos_v = int(round(math.log2(voltage_uV))) if voltage_uV > 0 else 0  # TODO: round is wrong
        pos_c = int(round(math.log2(current_nA))) if current_nA > 0 else 0
        if pos_v >= self.vsc["LUT_size"]:
            pos_v = self.vsc["LUT_size"] - 1
        if pos_c >= self.vsc["LUT_size"]:
            pos_c = self.vsc["LUT_size"] - 1
        return self.vsc["LUT_inp_efficiency_n8"][pos_v * self.vsc["LUT_size"] + pos_c] / (2 ** 8)

    def get_output_inv_efficiency(self, current) -> float:
        pos_c = int(round(math.log2(current))) if current > 0 else 0  # TODO: round is wrong
        if pos_c >= self.vsc["LUT_size"]:
            pos_c = self.vsc["LUT_size"] - 1
        return self.vsc["LUT_out_inv_efficiency_n10"][pos_c] / (2 ** 10)

    def set_input_power_pW(self, value):
        self.vsc["P_inp_pW"] = value

    def set_output_power_pW(self, value):
        self.vsc["P_out_pW"] = value

    def set_storage_Capacitor_uV(self, value):
        self.vsc["V_store_uV"] = value

    def get_input_power_pW(self, value):
        return self.vsc["P_inp_pW"]

    def get_output_power_pW(self, value):
        return self.vsc["P_out_pW"]

    def get_storage_Capacitor_uV(self, value):
        return self.vsc["V_store_uV"]