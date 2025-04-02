"""
shepherd.__init__
~~~~~
Provides main API functionality for harvesting and emulating with shepherd.

"""

import platform
import shutil
import subprocess
import tempfile
import time
from contextlib import ExitStack
from pathlib import Path

from shepherd_core.data_models import FirmwareDType
from shepherd_core.data_models import ShpModel
from shepherd_core.data_models.content.firmware import suffix_to_DType
from shepherd_core.data_models.task import EmulationTask
from shepherd_core.data_models.task import FirmwareModTask
from shepherd_core.data_models.task import HarvestTask
from shepherd_core.data_models.task import ProgrammingTask
from shepherd_core.data_models.task import extract_tasks
from shepherd_core.data_models.task import prepare_task
from shepherd_core.fw_tools import extract_firmware
from shepherd_core.fw_tools import firmware_to_hex
from shepherd_core.fw_tools import modify_uid

from . import sysfs_interface
from .eeprom import EEPROM
from .h5_writer import Writer
from .logger import log
from .logger import reset_verbosity
from .logger import set_verbosity
from .shepherd_debug import ShepherdDebug
from .shepherd_emulator import ShepherdEmulator
from .shepherd_harvester import ShepherdHarvester
from .shepherd_io import ShepherdIOError
from .sysfs_interface import check_sys_access
from .sysfs_interface import flatten_list
from .target_io import TargetIO

__version__ = "0.9.0"

__all__ = [
    "EEPROM",
    "ShepherdDebug",
    "ShepherdEmulator",
    "ShepherdHarvester",
    "ShepherdIOError",
    "TargetIO",
    "Writer",
    "flatten_list",
    "log",
    "run_emulator",
    "run_firmware_mod",
    "run_harvester",
    "run_programmer",
    "run_task",
]

# NOTE:
#   ExitStack enables a cleaner Exit-Behaviour
#   -> ShepherdIo.exit should always be called


def run_harvester(cfg: HarvestTask) -> bool:
    stack = ExitStack()
    set_verbosity(state=cfg.verbose, temporary=True)
    failed = True
    try:
        hrv = ShepherdHarvester(cfg=cfg)
        stack.enter_context(hrv)
        hrv.run()
        failed = False
    except SystemExit:
        pass
    except ShepherdIOError:
        log.exception("Caught an unrecoverable error")
    stack.close()
    return failed


def run_emulator(cfg: EmulationTask) -> bool:
    stack = ExitStack()
    set_verbosity(state=cfg.verbose, temporary=True)
    failed = True
    try:
        emu = ShepherdEmulator(cfg=cfg)
        stack.enter_context(emu)
        emu.run()
        failed = False
    except SystemExit:
        pass
    except ShepherdIOError:
        log.exception("Caught an unrecoverable error")
    stack.close()
    return failed


def run_firmware_mod(cfg: FirmwareModTask) -> bool:
    set_verbosity(state=cfg.verbose, temporary=True)
    if check_sys_access():  # not really needed here
        return True
    file_path = extract_firmware(cfg.data, cfg.data_type, cfg.firmware_file)
    if cfg.data_type in {FirmwareDType.path_elf, FirmwareDType.base64_elf}:
        modify_uid(file_path, cfg.custom_id)
        file_path = firmware_to_hex(file_path)
    if file_path.as_posix() != cfg.firmware_file.as_posix():
        shutil.move(file_path, cfg.firmware_file)
    return False


def run_programmer(cfg: ProgrammingTask, rate_factor: float = 1.0) -> bool:
    stack = ExitStack()
    set_verbosity(state=cfg.verbose, temporary=True)
    failed = False

    try:
        dbg = ShepherdDebug(use_io=False)  # TODO: this could all go into ShepherdDebug
        stack.enter_context(dbg)

        dbg.select_port_for_power_tracking(
            not dbg.convert_target_port_to_bool(cfg.target_port),
        )
        dbg.set_power_emulator(True)
        dbg.select_port_for_io_interface(cfg.target_port)
        dbg.set_power_io_level_converter(True)

        sysfs_interface.write_dac_aux_voltage(cfg.voltage)
        # switching target may restart pru
        sysfs_interface.wait_for_state("idle", 5)

        sysfs_interface.load_pru_firmware(cfg.protocol)
        dbg.refresh_shared_mem()  # address might have changed

        log.info("processing file %s", cfg.firmware_file.name)
        d_type = suffix_to_DType.get(cfg.firmware_file.suffix.lower())
        if d_type != FirmwareDType.base64_hex:
            log.warning("Firmware seems not to be HEX - but will try to program anyway")

        # derive target-info
        target = cfg.mcu_type.lower()
        if "msp430" in target:
            target = "msp430"
        elif "nrf52" in target:
            target = "nrf52"
        else:
            log.warning(
                "MCU-Type needs to be [msp430, nrf52] but was: %s",
                target,
            )

        # WORKAROUND that realigns hex for misguided programmer
        path_str = cfg.firmware_file.as_posix()
        path_tmp = tempfile.TemporaryDirectory()
        stack.enter_context(path_tmp)
        file_tmp = Path(path_tmp.name) / "aligned.hex"
        log.debug("\taligned firmware")
        # tmp_path because firmware can be in readonly content-dir
        cmd = [
            "/usr/bin/srec_cat",
            # BL51 hex files are not sorted for ascending addresses. Suppress this warning
            "-disable-sequence-warning",
            # load input HEX file
            path_str,
            "-Intel",
            # fill all incomplete 16-bit words with 0xFF. The range is limited to the application
            "-fill=0xFF",
            "-within",
            path_str,
            "-Intel",
            "-range-padding=4",
            # generate hex records with 16 byte data length (default 32 byte)
            "-Output_Block_Size=16",
            # generate 16- or 32-bit address records. Do not use 16-bit for address ranges > 64K
            f"-address-length={2 if 'msp' in target else 4}",
            # generate a Intel hex file
            "-o",
            file_tmp.as_posix(),
            "-Intel",
        ]
        ret = subprocess.run(cmd, timeout=30, check=False)  # noqa: S603
        if ret.returncode > 0:
            log.error("Error during realignment (srec_cat): %s", ret.stderr)
            failed = True
            raise SystemExit  # noqa: TRY301
        log.debug("\tconverted to ihex")

        if not (0.1 <= rate_factor <= 1.0):
            raise ValueError("Scaler for data-rate must be between 0.1 and 1.0")
        _data_rate = int(rate_factor * cfg.datarate)

        with file_tmp.resolve().open("rb") as fw:
            try:
                dbg.shared_mem.write_firmware(fw.read())

                if cfg.simulate:
                    target = "dummy"
                if cfg.mcu_port == 1:
                    sysfs_interface.write_programmer_ctrl(
                        target,
                        _data_rate,
                        5,
                        4,
                        10,
                    )
                else:
                    sysfs_interface.write_programmer_ctrl(
                        target,
                        _data_rate,
                        8,
                        9,
                        11,
                    )
                log.info(
                    "Programmer initialized, will start now (data-rate = %d bit/s)", _data_rate
                )
                sysfs_interface.start_programmer()
            # except OSError as xpt:
            #    log.exception("OSError - Failed to initialize Programmer", str(xpt))
            #    failed = True
            except ValueError as xpt:
                log.exception("ValueError: %s", str(xpt))
                failed = True

        state = "init"
        while state != "idle" and not failed:
            log.info(
                "Programming in progress,\tpgm_state = %s, shp_state = %s",
                state,
                sysfs_interface.get_state(),
            )
            time.sleep(1)
            state = sysfs_interface.check_programmer()
            if "error" in state:
                log.error(
                    "SystemError - Failed during Programming, p_state = %s",
                    state,
                )
                failed = True
        if failed:
            log.info("Programming - Procedure failed - will exit now!")
        else:
            log.info("Finished Programming!")
        log.debug("\tshepherdState   = %s", sysfs_interface.get_state())
        log.debug("\tprogrammerState = %s", state)
        log.debug("\tprogrammerCtrl  = %s", sysfs_interface.read_programmer_ctrl())
        dbg.process_programming_messages()
    except SystemExit:
        pass
    stack.close()

    sysfs_interface.load_pru_firmware("pru0-shepherd-EMU")
    sysfs_interface.load_pru_firmware("pru1-shepherd")
    return failed  # TODO: all run_() should emit error and abort_on_error should decide


def run_task(cfg: ShpModel | Path | str) -> bool:
    observer_name = platform.node().strip()
    try:
        wrapper = prepare_task(cfg, observer_name)
        content = extract_tasks(wrapper)
    except ValueError as xcp:
        log.error(
            "Task-Set was not usable for this observer '%s', with original error = %s",
            observer_name,
            xcp,
        )
        return True

    log.debug("Got set of tasks: %s", [type(_e).__name__ for _e in content])
    # TODO: parameters currently not handled:
    #   time_prep, root_path, abort_on_error (but used in emuTask)
    failed = False
    limit_char = 1000
    for element in content:
        if element is None:
            continue

        element_str = str(element)
        if len(element_str) > limit_char:
            element_str = element_str[:limit_char] + f" [first {limit_char} chars]"

        log.info(
            "\n###~###~###~###~###~### Starting %s ###~###~###~###~###~###\n\n%s\n",
            type(element).__name__,
            element_str,
        )

        if isinstance(element, EmulationTask):
            failed |= run_emulator(element)
        elif isinstance(element, HarvestTask):
            failed |= run_harvester(element)
        elif isinstance(element, FirmwareModTask):
            failed |= run_firmware_mod(element)
        elif isinstance(element, ProgrammingTask):
            retries = 1 if element.simulate else 5
            rate_factor = 1.0
            had_error = True
            while retries > 0 and had_error:
                log.info("Starting Programmer (%d retries left)", retries)
                retries -= 1
                had_error = run_programmer(element, rate_factor)
                rate_factor *= 0.6  # 40% slower each failed attempt
            failed |= had_error
        else:
            raise TypeError("Task not implemented: %s", type(element))
        reset_verbosity()
        # TODO: handle "failed": retry?
    return failed
