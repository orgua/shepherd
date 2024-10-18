"""
shepherd.shepherd_io
~~~~~
Interface layer, abstracting low-level functionality provided by PRUs and
kernel module. User-space part of the double-buffered data exchange protocol.
"""

import time
from contextlib import suppress
from types import TracebackType

from pydantic import validate_call
from shepherd_core import CalibrationEmulator
from shepherd_core import CalibrationHarvester
from shepherd_core import Reader
from shepherd_core.data_models import GpioTracing
from shepherd_core.data_models import PowerTracing
from shepherd_core.data_models.content.virtual_harvester import HarvesterPRUConfig
from shepherd_core.data_models.content.virtual_source import ConverterPRUConfig
from shepherd_core.data_models.testbed import TargetPort
from typing_extensions import Self
from typing_extensions import TypedDict
from typing_extensions import Unpack

from . import commons
from . import sysfs_interface as sfs
from .logger import log
from .shared_memory import SharedMemory
from .sysfs_interface import check_sys_access

# allow importing shepherd on x86 - for testing
with suppress(ModuleNotFoundError):
    from periphery import GPIO

gpio_pin_nums = {
    "target_pwr_sel": 31,
    "target_io_en": 60,
    "target_io_sel": 30,
    "en_shepherd": 23,
    "en_recorder": 50,
    "en_emulator": 51,
}


ShepherdIOError = IOError


class ShepherdTimeoutError(ShepherdIOError):
    def __init__(self, id_num: int | None = None, value: int | list | None = None) -> None:
        super().__init__("Timeout waiting for message [id=0x%X, val=%s]", id_num, value)
        self.id_num = id_num
        self.value = value


class ShepherdRxError(ShepherdIOError):
    def __init__(
        self,
        id_expected: int,
        id_num: int = 0,
        value: int | list | None = 0,
        note: str | None = None,
    ) -> None:
        message = "Expected msg-type %X, but got [id=0x%X, val=%s]"
        if isinstance(note, str):
            message = message + " - " + note

        super().__init__(message, id_expected, id_num, value)
        self.id_num = id_num
        self.value = value


class ShepherdPRUError(ShepherdIOError):
    def __init__(self, message: str, id_num: int = 0, value: int | list | None = 0) -> None:
        super().__init__(message + " with [id=0x%X, val=%s]", id_num, value)
        self.id_num = id_num
        self.value = value


class ShepherdIO:
    """Generic ShepherdIO interface.

    This class acts as interface between kernel module and firmware on the PRUs,
    and user space code. It handles the user space part of the double-buffered
    data-exchange protocol between user space and PRUs and configures the
    hardware by setting corresponding GPIO pins. This class should usually not
    be instantiated, but instead serve as parent class for e.g. Recorder or
    Emulator (see __init__.py).
    """

    # This _instance-element is part of the singleton implementation
    _instance: Self | None = None

    @classmethod
    def __new__(cls, *_args: tuple, **_kwargs: Unpack[TypedDict]) -> Self:
        """Implements singleton class."""
        if cls._instance is None:
            cls._instance = object.__new__(cls)
            # was raising on reuse and stored weakref.ref before
            return cls._instance
        log.debug("ShepherdIO-Singleton reused")
        return cls._instance

    def __init__(
        self,
        mode: str,
        trace_iv: PowerTracing | None,
        trace_gpio: GpioTracing | None,
    ) -> None:
        """Initializes relevant variables.

        Args:
            mode (str): Shepherd mode, see sysfs_interface for more
        """
        check_sys_access()

        if mode == "harvester":
            sfs.load_pru_firmware("pru0-shepherd-HRV")
        else:
            sfs.load_pru_firmware("pru0-shepherd-EMU")
        sfs.load_pru_firmware("pru1-shepherd")

        self.mode = mode
        if mode in {"harvester", "emulator"}:
            self.component = mode  # TODO: still needed?
        else:
            self.component = "emulator"
        self.gpios = {}

        self.trace_iv = trace_iv
        self.trace_gpio = trace_gpio

        # placeholders
        self.samples_per_segment = Reader.samples_per_buffer
        self.segment_period_s: float = self.samples_per_segment * commons.SAMPLE_INTERVAL_S
        self.shared_mem: SharedMemory | None = None

    def __del__(self) -> None:
        ShepherdIO._instance = None

    def __enter__(self) -> Self:
        try:
            for name, pin in gpio_pin_nums.items():
                self.gpios[name] = GPIO(pin, "out")

            self.set_power_cape_pcb(state=True)
            self.set_power_io_level_converter(state=False)

            log.debug("Shepherd hardware is powered up")

            # If shepherd hasn't been terminated properly
            self.reinitialize_prus()
            log.debug("Switching to '%s'-mode", self.mode)
            sfs.write_mode(self.mode)

            self.refresh_shared_mem()

            # clean up msg-channel provided by kernel module
            self._flush_msgs()

        except Exception:
            log.exception("ShepherdIO.Init caught an exception -> exit now")
            self._power_down_shp()
            self._unload_shared_mem()
            raise

        sfs.wait_for_state("idle", 3)
        return self

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        log.info("Now exiting ShepherdIO")
        self._power_down_shp()
        self._unload_shared_mem()
        ShepherdIO._instance = None

    @staticmethod
    def _send_msg(msg_type: int, values: int | list) -> None:
        """Sends a formatted message to PRU0.

        Args:
            msg_type (int): Indicates type of message, must be one of the agreed
                message types part of the data exchange protocol
            values (int): Actual content of the message
        """
        sfs.write_pru_msg(msg_type, values)

    def _get_msg(self, timeout_n: int = 5) -> tuple[int, list[int]]:
        """Tries to retrieve formatted message from PRU0.

        Args:
            timeout_n (int): Maximum number of buffer_periods to wait for a message
                before raising timeout exception

        """
        # TODO: cleanest way without exception: ask sysfs-file with current msg-count
        for _ in range(timeout_n):
            try:
                return sfs.read_pru_msg()
            except sfs.SysfsInterfaceError:  # noqa: PERF203
                time.sleep(self.segment_period_s)
                continue
        raise ShepherdTimeoutError

    @staticmethod
    def _flush_msgs() -> None:
        """Flushes msg_channel by reading all available bytes."""
        try:
            while True:
                sfs.read_pru_msg()
        except sfs.SysfsInterfaceError:
            pass

    def start(
        self,
        start_time: float | None = None,
        *,
        wait_blocking: bool = True,
    ) -> bool:
        """Starts sampling either now or at later point in time.

        Args:
            start_time (int): Desired start time in unix time
            wait_blocking (bool): If true, block until start has completed
        """
        if isinstance(start_time, float | int):
            log.debug("asking kernel module for start at %.2f", start_time)
        success = sfs.set_start(start_time)
        if wait_blocking:
            self.wait_for_start(3_000_000)
        return success

    @staticmethod
    def wait_for_start(timeout: float) -> None:
        """Waits until shepherd has started sampling.

        Args:
            timeout (float): Time to wait in seconds
        """
        sfs.wait_for_state("running", timeout)

    @staticmethod
    def reinitialize_prus() -> None:
        sfs.set_stop(force=True)  # forces idle
        sfs.wait_for_state("idle", 5)

    def refresh_shared_mem(self) -> None:
        if hasattr(self, "shared_mem") and isinstance(self.shared_mem, SharedMemory):
            self.shared_mem.__exit__()

        start_time = self.start_time if hasattr(self, "start_time") else time.time()

        self.shared_mem = SharedMemory(
            self.trace_iv,
            self.trace_gpio,
            start_timestamp_ns=int(1e9 * start_time),
            iv_segment_size=self.samples_per_segment,
        )
        self.shared_mem.__enter__()

    def _unload_shared_mem(self) -> None:
        if self.shared_mem is not None:
            self.shared_mem.__exit__()
            self.shared_mem = None

    def _power_down_shp(self) -> None:
        log.debug("ShepherdIO is commanded to power down / cleanup")
        count = 1
        while count < 6 and sfs.get_state() != "idle":
            try:
                sfs.set_stop(force=True)
            except sfs.SysfsInterfaceError:
                log.exception(
                    "CleanupRoutine caused an exception while trying to stop PRU (n=%d)",
                    count,
                )
            try:
                sfs.wait_for_state("idle", 3.0)
            except sfs.SysfsInterfaceError:
                log.warning(
                    "CleanupRoutine caused an exception while waiting for PRU to go to idle (n=%d)",
                    count,
                )
            count += 1
        if sfs.get_state() != "idle":
            log.warning(
                "CleanupRoutine gave up changing state, still '%s'",
                sfs.get_state(),
            )
        self.set_aux_target_voltage(0.0)

        self.set_power_io_level_converter(state=False)
        self.set_power_emulator(state=False)
        self.set_power_recorder(state=False)
        self.set_power_cape_pcb(state=False)
        log.debug("Shepherd hardware is now powered down")

    def set_power_cape_pcb(self, *, state: bool) -> None:
        """Controls state of power supplies on shepherd cape.

        Args:
            state (bool): True for on, False for off
        """
        state_str = "enabled" if state else "disabled"
        log.debug("Set power-supplies of shepherd-cape to %s", state_str)
        self.gpios["en_shepherd"].write(value=state)
        if state:
            time.sleep(0.5)  # time to stabilize voltage-drop

    def set_power_recorder(self, *, state: bool) -> None:
        """
        triggered pin is currently connected to ADCs reset-line
        NOTE: this might be extended to DAC as well

        :param state: bool, enable to get ADC out of reset
        :return:
        """
        state_str = "enabled" if state else "disabled"
        log.debug("Set Recorder of shepherd-cape to %s", state_str)
        self.gpios["en_recorder"].write(value=state)
        if state:
            time.sleep(0.3)  # time to stabilize voltage-drop

    def set_power_emulator(self, *, state: bool) -> None:
        """
        triggered pin is currently connected to ADCs reset-line
        NOTE: this might be extended to DAC as well

        :param state: bool, enable to get ADC out of reset
        :return:
        """
        state_str = "enabled" if state else "disabled"
        log.debug("Set Emulator of shepherd-cape to %s", state_str)
        self.gpios["en_emulator"].write(value=state)
        if state:
            time.sleep(0.3)  # time to stabilize voltage-drop

    @staticmethod
    def convert_target_port_to_bool(target: TargetPort | str | bool | None) -> bool:
        if target is None:
            return True
        if isinstance(target, str):
            return TargetPort[target] == TargetPort.A
        if isinstance(target, TargetPort):
            return target == TargetPort.A
        if isinstance(target, bool):
            return target
        raise TypeError(
            "Parameter 'target' must be A or B (was %s, type = %s)", target, type(target)
        )

    def select_port_for_power_tracking(
        self,
        target: TargetPort | bool | None,
    ) -> None:
        """
        choose which targets (A or B) gets the supply with current-monitor,

        shepherd hw-rev2 has two ports for targets and two separate power supplies,
        but only one is able to measure current, the other is considered "auxiliary"

        Args:
            target: A or B for that specific Target-Port
        """
        current_state = sfs.get_state()
        if current_state != "idle":
            self.reinitialize_prus()
        value = self.convert_target_port_to_bool(target)
        log.debug(
            "Set routing for (main) supply with current-monitor to target %s",
            target,
        )
        self.gpios["target_pwr_sel"].write(value=value)
        if current_state != "idle":
            self.start(wait_blocking=True)

    def select_port_for_io_interface(
        self,
        target: TargetPort | bool | None,
    ) -> None:
        """Choose which targets (A or B) gets the io-connection (serial, swd, gpio) from beaglebone,

        shepherd hw-rev2 has two ports for targets and can switch independently
        between power supplies

        Args:
            target: A or B for that specific Target-Port
        """
        value = self.convert_target_port_to_bool(target)
        log.debug("Set routing for IO to Target %s", target)
        self.gpios["target_io_sel"].write(value=value)

    def set_power_io_level_converter(self, *, state: bool) -> None:
        """Enables or disables the GPIO level converter to targets.

        The shepherd cape has bidirectional logic level translators (LSF0108)
        for translating UART, GPIO and SWD signals between BeagleBone and target
        voltage levels. This function enables or disables the converter and
        additional switches (NLAS4684) to keep leakage low.

        Args:
            state (bool): True for enabling converter, False for disabling
        """
        if state is None:
            state = False
        state_str = "enabled" if state else "disabled"
        log.debug("Set target-io level converter to %s", state_str)
        self.gpios["target_io_en"].write(value=state)

    @staticmethod
    def set_aux_target_voltage(
        voltage: float,
        cal_emu: CalibrationEmulator | None = None,
    ) -> None:
        """Enables or disables the voltage for the second target

        The shepherd cape has two DAC-Channels that each serve as power supply for a target

        Args:
            cal_emu: CalibrationEmulator,
            voltage (float): Desired output voltage in volt. Providing 0 or
                False disables supply, setting it to True will link it
                to the other channel
        """
        sfs.write_dac_aux_voltage(voltage, cal_emu)

    @staticmethod
    def get_aux_voltage(cal_emu: CalibrationEmulator | None = None) -> float:
        """Reads the auxiliary voltage (dac channel B) from the PRU core.

        Args:
            cal_emu: dict with offset/gain

        Returns:
            aux voltage
        """
        return sfs.read_dac_aux_voltage(cal_emu)

    @validate_call
    def send_calibration_settings(
        self,
        cal_: CalibrationEmulator | CalibrationHarvester | None,
    ) -> None:
        """Sends calibration settings to PRU core

        For the virtual source it is required to have the calibration settings.
        Note: to apply these settings the pru has to do a re-init (reset)

        Args:
            cal_ (CalibrationEmulation or CalibrationHarvester): Contains the device's
            calibration settings.
        """
        if not cal_:
            if self.component == "harvester":
                cal_ = CalibrationHarvester()
            else:
                cal_ = CalibrationEmulator()
        log.debug("Calibration-Settings (%s):", self.component)
        for key, value in cal_.model_dump(exclude_unset=False, exclude_defaults=False).items():
            log.debug("\t%s: %s", key, value)
        cal_dict = cal_.export_for_sysfs()
        sfs.write_calibration_settings(cal_dict)

    @staticmethod
    def send_virtual_converter_settings(
        settings: ConverterPRUConfig,
    ) -> None:
        """Sends virtsource settings to PRU core
        looks like a simple one-liner but is needed by the child-classes
        Note: to apply these settings the pru has to do a re-init (reset)

        :param settings: Contains the settings for the virtual source.
        """
        sfs.write_virtual_converter_settings(settings)

    @staticmethod
    def send_virtual_harvester_settings(
        settings: HarvesterPRUConfig,
    ) -> None:
        """Sends virtsource settings to PRU core
        looks like a simple one-liner but is needed by the child-classes
        Note: to apply these settings the pru has to do a re-init (reset)

        :param settings: Contains the settings for the virtual source.
        """
        sfs.write_virtual_harvester_settings(settings)

    @staticmethod
    def handle_pru_messages() -> None:
        """checks message inbox coming from both PRUs.

        Raises:
            ShepherdPRUError: If unrecoverable error was detected
        """
        while True:
            try:
                msg_type, values = sfs.read_pru_msg()
            except sfs.SysfsInterfaceError:
                return

            if msg_type == commons.MSG_DBG_PRINT:
                log.info("Received cmd to print: %d, %d", values[0], values[1])
                continue

            if msg_type == commons.MSG_STATUS_RESTARTING_ROUTINE:
                log.debug(
                    "PRU%d is restarting its main routine, val2=%s",
                    values[0],
                    f"0x{values[0]:X}",
                )
                # TODO: this should raise when needed (during normal OP)
                continue

            error_msg: str | None = commons.pru_errors.get(msg_type)
            if error_msg is not None:
                raise ShepherdPRUError(error_msg, msg_type, values)
