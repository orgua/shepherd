import contextlib
import time

import msgpack
import msgpack_numpy
import numpy as np
from shepherd_core import CalibrationCape
from shepherd_core import CalibrationEmulator
from shepherd_core.data_models import EnergyDType
from shepherd_core.data_models import GpioTracing
from shepherd_core.data_models import PowerTracing
from shepherd_core.data_models import VirtualSourceConfig
from shepherd_core.data_models.content.virtual_harvester import HarvesterPRUConfig
from shepherd_core.data_models.content.virtual_source import ConverterPRUConfig
from shepherd_core.data_models.testbed import TargetPort
from typing_extensions import Self

from . import commons
from . import sysfs_interface
from .eeprom import EEPROM
from .logger import log
from .shepherd_io import ShepherdIO
from .shepherd_io import ShepherdIOError
from .shepherd_io import ShepherdRxError
from .target_io import TargetIO


class ShepherdDebug(ShepherdIO):
    """API for direct access to ADC and DAC.

    For debugging purposes, running the GUI or for retrieving calibration
    values, we need to directly read values from the ADC and set voltage using
    the DAC. This class allows to put the underlying PRUs and kernel module in
    a mode, where they accept 'debug messages' that allow to directly interface
    with the ADC and DAC.
    """

    def __init__(self, use_io: bool = True) -> None:
        super().__init__("debug", trace_iv=PowerTracing(), trace_gpio=GpioTracing())

        self._io: TargetIO | None = TargetIO() if use_io else None

        # offer a default cali for debugging
        self._cal: CalibrationCape = CalibrationCape()

        try:
            with EEPROM() as storage:
                self._cal = storage.read_calibration()
        except ValueError:
            log.warning(
                "Couldn't read calibration from EEPROM (Val). Falling back to default values.",
            )
        except FileNotFoundError:
            log.warning(
                "Couldn't read calibration from EEPROM (FS). Falling back to default values.",
            )

        self.W_inp_fWs: float = 0.0
        self.W_out_fWs: float = 0.0

    def __enter__(self) -> Self:
        super().__enter__()
        super().set_power_recorder(state=True)
        super().set_power_emulator(state=True)
        super().reinitialize_prus()
        return self

    def adc_read(self, channel: str) -> int:
        """Read value from specified ADC channel.

        Args:
            channel (str): Specifies the channel to read from, e.g., 'v_in' for
                harvesting voltage or 'i_out' for current
        Returns:
            Binary ADC value read from corresponding channel
        """
        if channel.lower() in {"hrv_a_in", "hrv_i_in", "a_in", "i_in"}:
            channel_no = 0
        elif channel.lower() in {"hrv_v_in", "v_in"}:
            channel_no = 1
        elif channel.lower() in {
            "emu",
            "emu_a_out",
            "emu_i_out",
            "a_out",
            "i_out",
        }:
            channel_no = 2
        else:
            raise ValueError("ADC channel '%s' is unknown", channel)

        super()._send_msg(commons.MSG_DBG_ADC, channel_no)

        msg_type, values = self._get_msg(30)
        if msg_type != commons.MSG_DBG_ADC:
            raise ShepherdRxError(
                commons.MSG_DBG_ADC,
                msg_type,
                values,
            )
        return values[0]

    def gpi_read(self) -> int:
        """Issues a pru-read of the gpio-registers that monitor target-communication

        Returns: an int with the corresponding bits set
                -> see bit-definition in commons.py
        """
        super()._send_msg(commons.MSG_DBG_GPI, 0)
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_GPI:
            raise ShepherdRxError(
                commons.MSG_DBG_GPI,
                msg_type,
                values,
            )
        return values[0]

    def gp_set_batok(self, value: int) -> None:
        super()._send_msg(commons.MSG_DBG_GP_BATOK, value)

    def dac_write(self, channels: int, value: int) -> None:
        """Writes value to specified DAC channel, DAC8562

        Args:
            channels: 4 lower bits of int-num control
                b0: harvester-ch-a,
                b1: hrv-ch-b,
                b2: emulator-ch-a,
                b3: emu-ch-b
            value (int): 16 bit raw DAC value to be sent to corresponding channel
        """
        channels = (int(channels) & ((1 << 4) - 1)) << 20
        value = int(value) & ((1 << 16) - 1)
        message = channels | value
        super()._send_msg(commons.MSG_DBG_DAC, message)

    def dbg_fn_test(self, factor: int, mode: int) -> int:
        super()._send_msg(commons.MSG_DBG_FN_TESTS, [factor, mode])
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_FN_TESTS:
            raise ShepherdRxError(
                commons.MSG_DBG_FN_TESTS,
                msg_type,
                values,
            )
        return values[0] * (2**32) + values[1]  # P_out_pW

    def vsource_init(
        self,
        src_cfg: VirtualSourceConfig,
        cal_emu: CalibrationEmulator,
        log_intermediate: bool = False,
        dtype_in: EnergyDType = EnergyDType.ivsample,
        window_size: int | None = None,
    ) -> None:
        super().send_calibration_settings(cal_emu)
        src_pru = ConverterPRUConfig.from_vsrc(
            data=src_cfg,
            dtype_in=dtype_in,
            log_intermediate_node=log_intermediate,
        )
        super().send_virtual_converter_settings(src_pru)

        hrv_pru = HarvesterPRUConfig.from_vhrv(
            data=src_cfg.harvester,
            for_emu=True,
            dtype_in=dtype_in,
            window_size=window_size,
        )
        super().send_virtual_harvester_settings(hrv_pru)
        time.sleep(0.5)

        super().start()
        time.sleep(0.5)

        super()._flush_msgs()
        super()._send_msg(commons.MSG_DBG_VSRC_INIT, 0)
        msg_type, values = super()._get_msg()  # no data, just a confirmation
        if msg_type != commons.MSG_DBG_VSRC_INIT:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_INIT,
                msg_type,
                values,
                note="is ENABLE_DBG_VSOURCE defined in pru0/main.c??",
            )
        # TEST-SIMPLIFICATION - code below is not part of main pru-code
        self.W_inp_fWs = 0.0
        self.W_out_fWs = 0.0
        self._cal = CalibrationCape(emulator=cal_emu, harvester=self._cal.harvester)

    def cnv_calc_inp_power(
        self,
        input_voltage_uV: int,
        input_current_nA: int,
        include_hrv: bool = False,
    ) -> int:
        super()._send_msg(
            commons.MSG_DBG_VSRC_HRV_P_INP if include_hrv else commons.MSG_DBG_VSRC_P_INP,
            [int(input_voltage_uV), int(input_current_nA)],
        )
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_P_INP:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_P_INP,
                msg_type,
                values,
            )
        return values[0] * (2**32) + values[1]  # P_inp_pW

    def cnv_charge(
        self,
        input_voltage_uV: int,
        input_current_nA: int,
    ) -> tuple[int, int]:
        self._send_msg(
            commons.MSG_DBG_VSRC_CHARGE,
            [int(input_voltage_uV), int(input_current_nA)],
        )
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_CHARGE:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_CHARGE,
                msg_type,
                values,
            )
        return values[0], values[1]  # V_store_uV, V_out_dac_raw

    def cnv_calc_out_power(self, current_adc_raw: int) -> int:
        self._send_msg(commons.MSG_DBG_VSRC_P_OUT, int(current_adc_raw))
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_P_OUT:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_P_OUT,
                msg_type,
                values,
            )
        return values[0] * (2**32) + values[1]  # P_out_pW

    def cnv_drain(self, current_adc_raw: int) -> tuple[int, int]:
        self._send_msg(commons.MSG_DBG_VSRC_DRAIN, int(current_adc_raw))
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_DRAIN:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_DRAIN,
                msg_type,
                values,
            )
        return values[0], values[1]  # V_store_uV, V_out_dac_raw

    def cnv_update_cap_storage(self) -> int:
        self._send_msg(commons.MSG_DBG_VSRC_V_CAP, 0)
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_V_CAP:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_V_CAP,
                msg_type,
                values,
            )
        return values[0]  # V_store_uV

    def cnv_update_states_and_output(self) -> int:
        self._send_msg(commons.MSG_DBG_VSRC_V_OUT, 0)
        msg_type, values = self._get_msg()
        if msg_type != commons.MSG_DBG_VSRC_V_OUT:
            raise ShepherdRxError(
                commons.MSG_DBG_VSRC_V_OUT,
                msg_type,
                values,
            )
        return values[0]  # V_out_dac_raw

    # TEST-SIMPLIFICATION - code below is also part py-vsource with same interface
    def iterate_sampling(
        self,
        V_inp_uV: int = 0,
        A_inp_nA: int = 0,
        A_out_nA: int = 0,
    ) -> int:
        # NOTE: this includes the harvester
        P_inp_fW = self.cnv_calc_inp_power(V_inp_uV, A_inp_nA, include_hrv=True)
        A_out_raw = self._cal.emulator.adc_C_A.si_to_raw(A_out_nA * 10**-9)
        P_out_fW = self.cnv_calc_out_power(A_out_raw)
        self.cnv_update_cap_storage()
        V_out_raw = self.cnv_update_states_and_output()
        V_out_uV = int(self._cal.emulator.dac_V_A.raw_to_si(V_out_raw) * 10**6)
        self.W_inp_fWs += P_inp_fW
        self.W_out_fWs += P_out_fW
        return V_out_uV

    @staticmethod
    def is_alive() -> bool:
        """feedback-fn for RPC-usage to check for connection
        :return: True
        """
        return True

    # all methods below are wrapper for zerorpc - it seems
    # to have trouble with inheritance and runtime inclusion

    @staticmethod
    def set_shepherd_state(state: bool) -> None:
        if state:
            sysfs_interface.set_start()
        else:
            sysfs_interface.set_stop()

    @staticmethod
    def get_shepherd_state() -> str:
        return sysfs_interface.get_state()

    def set_power_cape_pcb(self, state: bool) -> None:
        super().set_power_cape_pcb(state=state)

    def select_port_for_power_tracking(
        self,
        target: TargetPort | bool | None,
    ) -> None:
        super().select_port_for_power_tracking(target)

    def select_port_for_io_interface(self, target: TargetPort) -> None:
        super().select_port_for_io_interface(target)

    def set_power_io_level_converter(self, state: bool) -> None:
        super().set_power_io_level_converter(state=state)

    def convert_raw_to_value(self, component: str, channel: str, raw: int) -> float:
        return self._cal[component][channel].raw_to_si(raw)

    def convert_value_to_raw(self, component: str, channel: str, value: float) -> int:
        return self._cal[component][channel].si_to_raw(value)

    def set_gpio_one_high(self, num: int) -> None:
        if self._io is not None:
            self._io.one_high(num)
        else:
            log.debug("Error: IO is not enabled in this shepherd-debug-instance")

    def get_gpio_state(self, num: int) -> bool:
        if self._io is not None:
            return self._io.get_pin_state(num)
        log.debug("Error: IO is not enabled in this shepherd-debug-instance")
        return False

    def set_gpio_direction(self, num: int, pdir: bool) -> None:
        if self._io is not None:
            self._io.set_pin_direction(num, pdir=pdir)
        else:
            log.debug("Error: IO is not enabled in this shepherd-debug-instance")

    def get_gpio_direction(self, num: int) -> bool:
        if self._io is not None:
            return self._io.get_pin_direction(num)
        log.debug("Error: IO is not enabled in this shepherd-debug-instance")
        return True

    def get_gpio_info(self) -> list:
        if self._io is not None:
            return self._io.pin_names
        log.debug("Error: IO is not enabled in this shepherd-debug-instance")
        return []

    def set_power_emulator(self, state: bool) -> None:
        super().set_power_emulator(state=state)

    def set_power_recorder(self, state: bool) -> None:
        super().set_power_recorder(state=state)

    def reinitialize_prus(self) -> None:
        super().reinitialize_prus()

    def get_power_state_shepherd(self) -> bool:
        return self.gpios["en_shepherd"].read()

    def get_power_state_recorder(self) -> bool:
        return self.gpios["en_recorder"].read()

    def get_power_state_emulator(self) -> bool:
        return self.gpios["en_emulator"].read()

    def get_main_target_for_power(self) -> bool:
        return self.gpios["target_pwr_sel"].read()

    def get_main_target_for_io(self) -> bool:
        return self.gpios["target_io_sel"].read()

    def get_target_io_level_conv(self) -> bool:
        return self.gpios["target_io_en"].read()

    @staticmethod
    def set_aux_target_voltage_raw(voltage_raw: int, link_channels: bool = False) -> None:
        sysfs_interface.write_dac_aux_voltage_raw(voltage_raw, link_channels=link_channels)

    def switch_shepherd_mode(self, mode: str) -> str:
        mode_old = sysfs_interface.get_mode()
        super().set_power_recorder(state=False)
        super().set_power_emulator(state=False)
        sysfs_interface.write_mode(mode, force=True)
        super().set_power_recorder(state=True)
        super().set_power_emulator(state=True)
        super().reinitialize_prus()
        if "debug" in mode:
            super().start(wait_blocking=True)
        return mode_old

    def sample_from_pru(self, length_n_buffers: int = 10) -> bytes | None:
        length_n_buffers = int(min(max(length_n_buffers, 1), 55))
        super().reinitialize_prus()
        time.sleep(0.1)
        super().start(wait_blocking=True)
        c_array = np.empty([0], dtype="=u4")
        v_array = np.empty([0], dtype="=u4")
        time.sleep(0.2)
        for _ in range(2):  # flush first 2 buffers out
            super().shared_mem.read_buffer_iv()
            time.sleep(self.segment_period_s)
        for _ in range(length_n_buffers):  # get Data
            _data_iv = None
            while _data_iv is None:
                _data_iv = super().shared_mem.read_buffer_iv()
                time.sleep(self.segment_period_s / 2)
            c_array = np.hstack((c_array, _data_iv.current))
            v_array = np.hstack((v_array, _data_iv.voltage))
        super().reinitialize_prus()
        base_array = np.vstack((c_array, v_array))
        return msgpack.packb(
            base_array,
            default=msgpack_numpy.encode,
        )  # zeroRPC / msgpack can not handle numpy-data without this

    def process_programming_messages(self) -> None:
        """Prints messages to console until timeout occurs"""
        with contextlib.suppress(ShepherdIOError):
            while True:
                msg_type, values = self._get_msg(5)
                if msg_type != commons.MSG_PGM_ERROR_WRITE:
                    # TODO: that should trigger an error
                    # TODO: programmer recently emits this at the end of process:
                    #       ..-WRITE-ERROR: ihex to target @0x0, data=0 [0x0]
                    log.error(
                        "PROGRAMMER-WRITE-ERROR: ihex to target @%s, data=%d [%s]",
                        f"0x{values[0]:X}",
                        values[1],
                        f"0x{values[1]:X}",
                    )
                elif msg_type != commons.MSG_PGM_ERROR_VERIFY:
                    log.error(
                        "PROGRAMMER-VERIFY-ERROR: read-back failed @%s, data=%d [%s]",
                        f"0x{values[0]:X}",
                        values[1],
                        f"0x{values[1]:X}",
                    )
                elif msg_type != commons.MSG_PGM_ERROR_PARSE:
                    log.error("PROGRAMMER-PARSE-ERROR: ihex_return=%d", values[0])
                else:
                    log.error(
                        "UNKNOWN PROGRAMMER-ERROR: type=%d, val0=%d, val1=%d",
                        msg_type,
                        values[0],
                        values[1],
                    )
