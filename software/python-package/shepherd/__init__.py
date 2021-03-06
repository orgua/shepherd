# -*- coding: utf-8 -*-

"""
shepherd.__init__
~~~~~
Provides main API functionality for recording and emulation with shepherd.


:copyright: (c) 2019 Networked Embedded Systems Lab, TU Dresden.
:license: MIT, see LICENSE for more details.
"""
import datetime
import logging
import time
import sys
from logging import NullHandler
from pathlib import Path
from contextlib import ExitStack
import invoke
import signal

from shepherd.shepherd_io import ShepherdIO
from shepherd.shepherd_io import VirtualSourceData
from shepherd.shepherd_io import ShepherdIOException
from shepherd.datalog import LogReader
from shepherd.datalog import LogWriter
from shepherd.datalog import ExceptionRecord
from shepherd.eeprom import EEPROM
from shepherd.eeprom import CapeData
from shepherd.calibration import CalibrationData
from shepherd.calibration import cal_channel_list
from shepherd import commons
from shepherd import sysfs_interface

# Set default logging handler to avoid "No handler found" warnings.
logging.getLogger(__name__).addHandler(NullHandler())

logger = logging.getLogger(__name__)


class Recorder(ShepherdIO):
    """API for recording data with shepherd.

    Provides an easy to use, high-level interface for recording data with
    shepherd. Configures all hardware and initializes the communication
    with kernel module and PRUs.

    Args:
        mode (str): Should be 'harvesting' to record harvesting data TODO: extend with iv-curve-sweep, mppt,
        # TODO: DAC-Calibration would be nice to have, in case of active mppt even both adc-cal
    """

    def __init__(self, mode: str = "harvesting"):
        super().__init__(mode)

    def __enter__(self):
        super().__enter__()

        # Give the PRU empty buffers to begin with
        for i in range(self.n_buffers):
            time.sleep(0.2 * float(self.buffer_period_ns) / 1e9)
            self.return_buffer(i)
            logger.debug(f"sent empty buffer {i}")

        return self

    def return_buffer(self, index: int):
        """Returns a buffer to the PRU

        After reading the content of a buffer and potentially filling it with
        emulation data, we have to release the buffer to the PRU to avoid it
        running out of buffers.

        Args:
            index (int): Index of the buffer. 0 <= index < n_buffers
        """
        self._return_buffer(index)


class Emulator(ShepherdIO):
    """API for emulating data with shepherd.

    Provides an easy to use, high-level interface for emulating data with
    shepherd. Configures all hardware and initializes the communication
    with kernel module and PRUs.

    Args:
        shepherd_mode:
        initial_buffers: recorded data  # TODO: initial_ is not the best name, is this a yield/generator?
        calibration_recording (CalibrationData): Shepherd calibration data
            belonging to the IV data that is being emulated
        calibration_emulation (CalibrationData): Shepherd calibration data
            belonging to the cape used for emulation
        set_target_io_lvl_conv: Enables or disables the GPIO level converter to targets.
        sel_target_for_io: choose which targets gets the io-connection (serial, swd, gpio) from beaglebone, True = Target A, False = Target B
        sel_target_for_pwr: choose which targets gets the supply with current-monitor, True = Target A, False = Target B
        aux_target_voltage: Sets, Enables or disables the voltage for the second target, 0.0 or False for Disable, True for linking it to voltage of other Target
        settings_virtsource (dict): Settings which define the behavior of virtual source emulation
    """

    def __init__(self,
                 shepherd_mode: str = "emulation",
                 initial_buffers: list = None,
                 calibration_recording: CalibrationData = None,  # TODO: make clearer that these is "THE RECORDING"
                 calibration_emulation: CalibrationData = None,
                 set_target_io_lvl_conv: bool = False,
                 sel_target_for_io: bool = True,
                 sel_target_for_pwr: bool = True,
                 aux_target_voltage: float = 0.0,
                 settings_virtsource: VirtualSourceData = None) -> object:

        super().__init__(shepherd_mode)
        self._initial_buffers = initial_buffers

        if calibration_emulation is None:
            calibration_emulation = CalibrationData.from_default()
            logger.warning("No emulation calibration data provided - using defaults")
        if calibration_recording is None:
            calibration_recording = CalibrationData.from_default()
            logger.warning("No recording calibration data provided - using defaults")

        self._cal_recording = calibration_recording
        self._cal_emulation = calibration_emulation
        self._settings_virtsource = settings_virtsource

        self._set_target_io_lvl_conv = set_target_io_lvl_conv
        self._sel_target_for_io = sel_target_for_io
        self._sel_target_for_pwr = sel_target_for_pwr
        self._aux_target_voltage = aux_target_voltage

    def __enter__(self):
        super().__enter__()

        self.send_calibration_settings(self._cal_emulation)

        self.set_target_io_level_conv(self._set_target_io_lvl_conv)
        self.select_main_target_for_io(self._sel_target_for_io)
        self.select_main_target_for_power(self._sel_target_for_pwr)
        self.set_aux_target_voltage(self._cal_emulation, self._aux_target_voltage)

        self.send_virtsource_settings(self._settings_virtsource)

        # Preload emulator with some data
        for idx, buffer in enumerate(self._initial_buffers):
            time.sleep(0.2 * float(self.buffer_period_ns) / 1e9)
            self.return_buffer(idx, buffer)

        return self

    def return_buffer(self, index, buffer):

        ts_start = time.time()

        v_gain = 1e6 * self._cal_recording["harvesting"]["adc_voltage"]["gain"]
        v_offset = 1e6 * self._cal_recording["harvesting"]["adc_voltage"]["offset"]
        i_gain = 1e9 * self._cal_recording["harvesting"]["adc_current"]["gain"]
        i_offset = 1e9 * self._cal_recording["harvesting"]["adc_current"]["offset"]

        # Convert raw ADC data to SI-Units -> the virtual-source-emulator in PRU expects uV and nV
        voltage_transformed = (buffer.voltage * v_gain + v_offset).astype("u4")
        current_transformed = (buffer.current * i_gain + i_offset).astype("u4")

        self.shared_mem.write_buffer(index, voltage_transformed, current_transformed)
        self._return_buffer(index)

        logger.debug(
            (
                f"Returning buffer #{ index } to PRU took "
                f"{ round(1e3 * (time.time()-ts_start), 2) } ms"
            )
        )


class ShepherdDebug(ShepherdIO):
    """API for direct access to ADC and DAC.

    For debugging purposes, running the GUI or for retrieving calibration
    values, we need to directly read values from the ADC and set voltage using
    the DAC. This class allows to put the underlying PRUs and kernel module in
    a mode, where they accept 'debug messages' that allow to directly interface
    with the ADC and DAC.
    """

    def __init__(self):
        super().__init__("debug")

    def adc_read(self, channel: str):
        """Reads value from specified ADC channel.

        Args:
            channel (str): Specifies the channel to read from, e.g., 'v_in' for
                harvesting voltage or 'i_out' for current
        Returns:
            Binary ADC value read from corresponding channel
        """
        if channel.lower() in ["hrv_a_in", "hrv_i_in", "a_in", "i_in"]:
            channel_no = 0
        elif channel.lower() in ["hrv_v_in", "v_in"]:
            channel_no = 1
        elif channel.lower() in ["emu", "emu_a_out", "emu_i_out", "a_out", "i_out"]:
            channel_no = 2
        else:
            raise ValueError(f"ADC channel { channel } unknown")

        self._send_msg(commons.MSG_DBG_ADC, channel_no)

        msg_type, value = self._get_msg(3.0)
        if msg_type != commons.MSG_DBG_ADC:
            raise ShepherdIOException(
                    f"Expected msg type { commons.MSG_DBG_ADC } "
                    f"got t{ msg_type } v{ value }"
                    )
        return value

    def gpi_read(self) -> int:
        """ issues a pru-read of the gpio-registers that monitor target-communication

        Returns: an int with the corresponding bits set
            #define TARGET_GPIO0            BIT_SHIFT(P8_45) // r31_00
            #define TARGET_GPIO1            BIT_SHIFT(P8_46) // r31_01
            #define TARGET_GPIO2            BIT_SHIFT(P8_43) // r31_02
            #define TARGET_GPIO3            BIT_SHIFT(P8_44) // r31_03
            #define TARGET_UART_TX          BIT_SHIFT(P8_41) // r31_04
            #define TARGET_UART_RX          BIT_SHIFT(P8_42) // r31_05
            #define TARGET_SWD_CLK          BIT_SHIFT(P8_39) // r31_06
            #define TARGET_SWD_IO           BIT_SHIFT(P8_40) // r31_07
            #define TARGET_BAT_OK           BIT_SHIFT(P8_27) // r31_08
            #define TARGET_GPIO4            BIT_SHIFT(P8_29) // r31_09
        """
        self._send_msg(commons.MSG_DBG_GPI, 0)
        msg_type, value = self._get_msg()
        if msg_type != commons.MSG_DBG_GPI:
            raise ShepherdIOException(
                    f"Expected msg type { commons.MSG_DBG_GPI } "
                    f"got type { msg_type } val { value }"
                    )
        return value

    def dac_write(self, channels: int, value: int):
        """Writes value to specified DAC channel, DAC8562

        Args:
            channels: 4 lower bits of int-num control b0: harvest-ch-a, b1: harv-ch-b, b2: emulation-ch-a, b3: emu-ch-b
            value (int): 16 bit raw DAC value to be sent to corresponding channel
        """
        channels = (int(channels) & ((1 << 4) - 1)) << 20
        value = int(value) & ((1 << 16) - 1)
        message = channels | value
        self._send_msg(commons.MSG_DBG_DAC, message)

    def get_buffer(self, timeout=None):
        raise NotImplementedError("Method not implemented for debugging mode")


def record(
    output_path: Path,
    mode: str = "harvesting",
    duration: float = None,
    force_overwrite: bool = False,
    no_calib: bool = False,
    start_time: float = None,
    warn_only: bool = False,
):
    """Starts recording.

    Args:
        output_path (Path): Path of hdf5 file where IV measurements should be
            stored
        mode (str): 'harvesting' for recording harvesting data
        duration (float): Maximum time duration of emulation in seconds
        force_overwrite (bool): True to overwrite existing file under output path,
            False to store under different name
        no_calib (bool): True to use default calibration values, False to
            read calibration data from EEPROM
        start_time (float): Desired start time of emulation in unix epoch time
        warn_only (bool): Set true to continue recording after recoverable
            error
    """
    if no_calib:
        calib = CalibrationData.from_default()
    else:
        try:
            with EEPROM() as eeprom:
                calib = eeprom.read_calibration()
        except ValueError:
            logger.warning("Couldn't read calibration from EEPROM (Val). Falling back to default values.")
            calib = CalibrationData.from_default()
        except FileNotFoundError:
            logger.warning("Couldn't read calibration from EEPROM (FS). Falling back to default values.")
            calib = CalibrationData.from_default()

    if start_time is None:
        start_time = time.time() + 15

    if not output_path.is_absolute():
        raise ValueError("Output must be absolute path")
    if output_path.is_dir():
        timestamp = datetime.datetime.fromtimestamp(start_time)
        timestamp = timestamp.strftime("%Y-%m-%d_%H-%M-%S")  # closest to ISO 8601, avoid ":"
        store_path = output_path / f"rec_{timestamp}.h5"
    else:
        store_path = output_path


    recorder = Recorder(mode=mode)
    log_writer = LogWriter(
        store_path=store_path, calibration_data=calib, mode=mode, force_overwrite=force_overwrite
    )
    with ExitStack() as stack:

        stack.enter_context(recorder)
        stack.enter_context(log_writer)

        # in_stream has to be disabled to avoid trouble with pytest
        res = invoke.run("hostname", hide=True, warn=True, in_stream=False)
        log_writer["hostname"] = res.stdout

        recorder.start(start_time, wait_blocking=False)

        logger.info(f"waiting {start_time - time.time():.2f}s until start")
        recorder.wait_for_start(start_time - time.time() + 15)

        logger.info("shepherd started!")

        def exit_gracefully(signum, frame):
            stack.close()
            sys.exit(0)

        signal.signal(signal.SIGTERM, exit_gracefully)
        signal.signal(signal.SIGINT, exit_gracefully)

        if duration is None:
            ts_end = sys.float_info.max
        else:
            ts_end = time.time() + duration

        while time.time() < ts_end:
            try:
                idx, buf = recorder.get_buffer()
            except ShepherdIOException as e:
                logger.error(
                    f"ShepherdIOException(ID={e.id}, val={e.value}): {str(e)}"
                )
                err_rec = ExceptionRecord(
                    int(time.time() * 1e9), str(e), e.value
                )
                log_writer.write_exception(err_rec)
                if not warn_only:
                    raise

            log_writer.write_buffer(buf)
            recorder.return_buffer(idx)


def emulate(
        input_path: Path,
        output_path: Path = None,
        duration: float = None,
        force_overwrite: bool = False,
        no_calib: bool = False,
        start_time: float = None,
        set_target_io_lvl_conv: bool = False,
        sel_target_for_io: bool = True,
        sel_target_for_pwr: bool = True,
        aux_target_voltage: float = 0.0,
        settings_virtsource: VirtualSourceData = None,
        warn_only: bool = False,
):
    """ Starts emulation.

    Args:
        input_path (Path): path of hdf5 file containing recorded harvesting data
        output_path (Path): Path of hdf5 file where power measurements should
            be stored
        duration (float): Maximum time duration of emulation in seconds
        force_overwrite (bool): True to overwrite existing file under output,
            False to store under different name
        no_calib (bool): True to use default calibration values, False to
            read calibration data from EEPROM
        start_time (float): Desired start time of emulation in unix epoch time
        set_target_io_lvl_conv: Enables or disables the GPIO level converter to targets.
        sel_target_for_io: choose which targets gets the io-connection (serial, swd, gpio) from beaglebone, True = Target A, False = Target B
        sel_target_for_pwr: choose which targets gets the supply with current-monitor, True = Target A, False = Target B
        aux_target_voltage: Sets, Enables or disables the voltage for the second target, 0.0 or False for Disable, True for linking it to voltage of other Target
        settings_virtsource (VirtualSourceData): Settings which define the behavior of virtsource emulation
        warn_only (bool): Set true to continue emulation after recoverable
            error
    """

    if no_calib:
        calib = CalibrationData.from_default()
    else:
        try:
            with EEPROM() as eeprom:
                calib = eeprom.read_calibration()
        except ValueError:
            logger.warning("Couldn't read calibration from EEPROM (Val). Falling back to default values.")
            calib = CalibrationData.from_default()
        except FileNotFoundError:
            logger.warning("Couldn't read calibration from EEPROM (FS). Falling back to default values.")
            calib = CalibrationData.from_default()

    if start_time is None:
        start_time = time.time() + 15

    if output_path is not None:
        if not output_path.is_absolute():
            raise ValueError("Output must be absolute path")
        if output_path.is_dir():
            timestamp = datetime.datetime.fromtimestamp(start_time)
            timestamp = timestamp.strftime("%Y-%m-%d_%H-%M-%S")  # closest to ISO 8601, avoid ":"
            store_path = output_path / f"emu_{timestamp}.h5"
        else:
            store_path = output_path

        log_writer = LogWriter(
            store_path=store_path,
            force_overwrite=force_overwrite,
            mode="emulation",
            calibration_data=calib,
        )

    if input_path is str:
        input_path = Path(input_path)
    if input_path is None:
        raise ValueError("No Input-File configured for emulation")
    if not input_path.exists():
        raise ValueError("Input-File does not exist")

    log_reader = LogReader(input_path, 10_000)

    with ExitStack() as stack:
        if output_path is not None:
            stack.enter_context(log_writer)

        stack.enter_context(log_reader)

        emu = Emulator(
            shepherd_mode="emulation",
            initial_buffers=log_reader.read_buffers(end=64),
            calibration_recording=log_reader.get_calibration_data(),
            calibration_emulation=calib,
            set_target_io_lvl_conv=set_target_io_lvl_conv,
            sel_target_for_io=sel_target_for_io,
            sel_target_for_pwr=sel_target_for_pwr,
            aux_target_voltage=aux_target_voltage,
            settings_virtsource=settings_virtsource,
        )
        stack.enter_context(emu)

        emu.start(start_time, wait_blocking=False)

        logger.info(f"waiting {start_time - time.time():.2f}s until start")
        emu.wait_for_start(start_time - time.time() + 15)

        logger.info("shepherd started!")

        def exit_gracefully(signum, frame):
            stack.close()
            sys.exit(0)

        signal.signal(signal.SIGTERM, exit_gracefully)
        signal.signal(signal.SIGINT, exit_gracefully)

        if duration is None:
            ts_end = sys.float_info.max
        else:
            ts_end = time.time() + duration

        for hrvst_buf in log_reader.read_buffers(start=64):
            try:
                idx, emu_buf = emu.get_buffer(timeout=1)
            except ShepherdIOException as e:
                logger.error(
                    f"ShepherdIOException(ID={e.id}, val={e.value}): {str(e)}"
                )

                err_rec = ExceptionRecord(int(time.time() * 1e9), str(e), e.value)
                if output_path is not None:
                    log_writer.write_exception(err_rec)
                if not warn_only:
                    raise

            if output_path is not None:
                log_writer.write_buffer(emu_buf)
            emu.return_buffer(idx, hrvst_buf)

            if time.time() > ts_end:
                break

        # Read all remaining buffers from PRU
        while True:
            try:
                idx, emu_buf = emu.get_buffer(timeout=1)
                if output_path is not None:
                    log_writer.write_buffer(emu_buf)
            except ShepherdIOException as e:
                # We're done when the PRU has processed all emulation data buffers
                if e.id == commons.MSG_DEP_ERR_NOFREEBUF:
                    break
                else:
                    if not warn_only:
                        raise
