"""
shepherd.cli
~~~~~
Provides the CLI utility 'shepherd-sheep', exposing most of shepherd's
functionality to a command line user.

"""

import atexit
import signal
import sys
import time
from datetime import datetime
from pathlib import Path
from types import FrameType
from typing import TypedDict

import click
import gevent
import yaml
import zerorpc
from shepherd_core import CalibrationCape
from shepherd_core.data_models.task import ProgrammingTask
from shepherd_core.data_models.testbed import ProgrammerProtocol
from shepherd_core.data_models.testbed import TargetPort
from shepherd_core.inventory import Inventory
from typing_extensions import Unpack

from . import __version__
from . import run_task
from . import sysfs_interface
from .eeprom import EEPROM
from .logger import log
from .logger import set_verbosity
from .shepherd_debug import ShepherdDebug
from .shepherd_io import gpio_pin_nums
from .sysfs_interface import check_sys_access
from .sysfs_interface import disable_ntp
from .sysfs_interface import reload_kernel_module
from .usage_log import get_last_usage
from .usage_log import usage_logger

# allow importing shepherd on x86 - for testing
try:
    from periphery import GPIO
except ModuleNotFoundError:
    log.warning("Periphery-Package missing - hardware-access will not work")


# TODO: correct docs
# --length -l is now --duration -d ->
# --input --output is now --output_path -> correct docs
# --virtsource replaces vcap, is not optional anymore,
#   maybe prepare preconfigured converters (bq-series) to choose from
#   possible choices: nothing, converter-name like BQ25570 / BQ25504, path to yaml-config
#   -> vSource contains vharvester and vConverter
# - the options get repeated all the time, is it possible to define them
#   upfront and just include them where needed?
# - ditch sudo, add user to allow sys_fs-access and other things
# - default-cal -> use_cal_default
# - start-time -> start_time
# - sheep run record -> sheep run harvester, same with sheep record
# - cleaned up internal naming (only harvester/emulator instead of record)
#   - TODO: even the commands should be "sheep harvester config"
# - redone programmer, emulation


def exit_gracefully(_signum: int, _frame: FrameType | None) -> None:
    log.warning("Exiting!")
    sys.exit(0)


@click.group(context_settings={"help_option_names": ["-h", "--help"], "obj": {}})
@click.option(
    "--verbose",
    "-v",
    is_flag=True,
    help="4 Levels, but level 4 has serious performance impact",
)
@click.option(
    "--version",
    is_flag=True,
    help="Prints version-info at start (combinable with -v)",
)
@click.pass_context
def cli(ctx: click.Context, *, verbose: bool, version: bool) -> None:
    """Shepherd: Synchronized Energy Harvesting Emulator and Recorder"""
    signal.signal(signal.SIGTERM, exit_gracefully)
    signal.signal(signal.SIGINT, exit_gracefully)

    if verbose:
        set_verbosity()

    if ctx.invoked_subcommand and ctx.invoked_subcommand not in ["usage"]:
        # this adds a usage-entry when sheep exits
        atexit.register(usage_logger, datetime.now().astimezone(), ctx.invoked_subcommand)

    if version:
        log.info("Shepherd-Sheep v%s", __version__)
        log.debug("Python v%s", sys.version)
        log.debug("Click v%s", click.__version__)
    if check_sys_access():
        ctx.exit(1)
    if not ctx.invoked_subcommand:
        click.echo("Please specify a valid command")


@cli.command(short_help="Turns target power supply on or off (i.e. for programming)")
@click.option("--on/--off", default=True)
@click.option(
    "--voltage",
    "-v",
    type=click.FLOAT,
    default=3.0,
    help="Target supply voltage",
)
@click.option(
    "--gpio-pass/--gpio-omit",
    default=True,
    help="Route UART, Programmer-Pins and other GPIO to this target",
)
@click.option(
    "--target-port",
    "-p",
    type=click.Choice(["A", "B"]),
    default="A",
    help="Choose Target-Port of Cape for powering",
)
def target_power(target_port: str, voltage: float, *, on: bool, gpio_pass: bool) -> None:
    if not on:
        voltage = 0.0
    # TODO: output would be nicer when this uses shepherdDebug as base
    a_is_aux = "a" in target_port.lower()
    for pin_name in ["en_shepherd"]:
        pin = GPIO(gpio_pin_nums[pin_name], "out")
        pin.write(value=on)
        log.info("Shepherd-State \t= %s", "enabled" if on else "disabled")
    for pin_name in ["target_pwr_sel"]:
        pin = GPIO(gpio_pin_nums[pin_name], "out")
        pin.write(value=not a_is_aux)  # switched because rail A is AUX
        log.info("Select Target \t= %s", "A" if a_is_aux else "B")
    for pin_name in ["target_io_sel"]:
        pin = GPIO(gpio_pin_nums[pin_name], "out")
        pin.write(value=a_is_aux)
    for pin_name in ["target_io_en"]:
        pin = GPIO(gpio_pin_nums[pin_name], "out")
        pin.write(value=gpio_pass)
        log.info("IO passing \t= %s", "enabled" if gpio_pass else "disabled")
    log.info("Target Voltage \t= %.3f V", voltage)
    sysfs_interface.write_dac_aux_voltage(voltage)
    sysfs_interface.write_mode("emulator", force=True)
    sysfs_interface.set_stop(force=True)  # forces reset
    log.info("Re-Initialized PRU to finalize settings")
    # NOTE: this FN needs persistent IO, (old GPIO-Lib)


@cli.command(
    short_help="Runs a task or set of tasks with provided config/task file (YAML).",
)
@click.argument(
    "config",
    type=click.Path(exists=True, readable=True, file_okay=True, dir_okay=False),
    default=Path("/etc/shepherd/config.yaml"),
)
@click.pass_context
def run(ctx: click.Context, config: Path) -> None:
    reload_kernel_module()  # more reliable with fresh states
    disable_ntp()
    failed = run_task(config)
    if failed:
        log.debug("Tasks signaled an error (failed).")
    ctx.exit(int(failed))


@cli.group(
    context_settings={"help_option_names": ["-h", "--help"], "obj": {}},
    short_help="Read/Write data from EEPROM",
)
def eeprom() -> None:
    pass


@eeprom.command(short_help="Write Calibration of Cape (YAML) to EEPROM")
@click.argument(
    "cal-file",
    type=click.Path(exists=True, readable=True, file_okay=True, dir_okay=False),
)
@click.pass_context
def write(
    ctx: click.Context,
    cal_file: Path | None,
) -> None:
    cal_cape = CalibrationCape.from_file(cal_file)
    try:
        log.debug("Will write Cal-Data:\n\n%s", str(cal_cape))
        with EEPROM() as storage:
            storage.write_calibration(cal_cape)
    except FileNotFoundError:
        log.error("Access to EEPROM failed (FS) -> is Shepherd-Cape missing?")
        ctx.exit(2)


@eeprom.command(short_help="Read cape info and calibration data from EEPROM")
@click.option(
    "--cal-file",
    "-c",
    type=click.Path(dir_okay=False, executable=False),
    default=None,
    help="If provided, calibration data is dumped to this file",
)
@click.option(
    "--revision",
    "-r",
    is_flag=True,
    help="only output version of cape hardware on console",
)
@click.option(
    "--full",
    "-f",
    is_flag=True,
    help="output all fields on console",
)
@click.pass_context
def read(ctx: click.Context, cal_file: Path | None, *, revision: bool, full: bool) -> None:
    try:
        with EEPROM() as storage:
            cal = storage.read_calibration()
    except ValueError:
        log.warning(
            "Reading from EEPROM failed (Val) -> no plausible data found",
        )
        ctx.exit(2)
    except FileNotFoundError:
        log.error("Access to EEPROM failed (FS) -> is Shepherd-Cape missing?")
        ctx.exit(3)

    if revision:
        log.info("%s", cal.cape.version)
    elif cal_file is None:
        _data = (
            yaml.safe_dump(
                cal.model_dump(exclude_unset=True, exclude_defaults=False),
                default_flow_style=False,
                sort_keys=False,
            )
            if full
            else str(cal)
        )
        log.info("Retrieved Cal-Data:\n\n%s", str(_data))
    else:
        cal.to_file(cal_file)


@cli.command(short_help="Start ZeroRPC Server")
@click.option("--port", "-p", type=click.INT, default=4242)
@click.pass_context
def rpc(ctx: click.Context, port: int | None) -> None:
    shepherd_io = ShepherdDebug()
    shepherd_io.__enter__()
    log.info("Shepherd Debug Interface: Initialized")
    time.sleep(1)

    server = zerorpc.Server(shepherd_io)
    server.bind(f"tcp://0.0.0.0:{port}")
    time.sleep(1)

    def stop_server() -> None:
        server.stop()
        shepherd_io.__exit__()
        ctx.exit(0)

    gevent.signal_handler(signal.SIGTERM, stop_server)
    gevent.signal_handler(signal.SIGINT, stop_server)

    success = shepherd_io.start()
    if not success:
        return
    log.info("Shepherd RPC Interface: Started")
    server.run()


@cli.command(short_help="Collects information about this host")
@click.option(
    "--output-path",
    "-o",
    type=click.Path(file_okay=True, dir_okay=False),
    default=Path("/var/shepherd/inventory.yaml"),
    help="Path to resulting YAML-formatted calibration data file",
)
def inventorize(output_path: Path) -> None:
    output_path = Path(output_path)
    sheep_inv = Inventory.collect()
    sheep_inv.to_file(path=output_path, minimal=True)
    log.info("Written inventory to %s", output_path.as_posix())


@cli.command(
    short_help="Programmer for Target-Controller (flashes intel Hex)",
    context_settings={"ignore_unknown_options": True},
)
@click.argument(
    "firmware-file",
    type=click.Path(exists=True, file_okay=True, dir_okay=False, readable=True),
)
@click.option(
    "--target-port",
    "-p",
    type=click.Choice(["A", "B"]),
    default="A",
    help="Choose Target-Port of Cape for programming",
)
@click.option(
    "--mcu-port",
    "-m",
    type=click.INT,
    default=1,
    help="Choose MCU on Target-Port (only valid for SBW & SWD)",
)
@click.option(
    "--voltage",
    "-v",
    type=click.FLOAT,
    default=3.0,
    help="Target supply voltage",
)
@click.option(
    "--datarate",
    "-d",
    type=click.INT,
    default=200_000,
    help="Bit rate of Programmer (bit/s)",
)
@click.option(
    "--mcu-type",
    "-t",
    type=click.Choice(["nrf52", "msp430"]),
    default="nrf52",
    help="Target MCU",
)
@click.option(
    "--simulate",
    is_flag=True,
    help="dry-run the programmer - no data gets written",
)
@click.pass_context
def program(ctx: click.Context, **kwargs: Unpack[TypedDict]) -> None:
    protocol_dict = {
        "nrf52": ProgrammerProtocol.swd,
        "msp430": ProgrammerProtocol.sbw,
    }
    kwargs["protocol"] = protocol_dict[kwargs["mcu_type"]]
    cfg = ProgrammingTask(**kwargs)
    failed = run_task(cfg)
    ctx.exit(int(failed))


@cli.command(
    short_help="Reloads the shepherd-kernel-module",
    context_settings={"ignore_unknown_options": True},
)
def fix() -> None:
    set_verbosity()
    reload_kernel_module()


@cli.command(
    short_help="Loads a specific firmware to the PRUs",
    context_settings={"ignore_unknown_options": True},
)
@click.argument(
    "firmware",
    type=click.Choice(["default", "emu", "hrv", "swd", "sbw", "sync"]),
    default="default",
)
def pru(firmware: str) -> None:
    set_verbosity()
    sysfs_interface.load_pru_firmware(firmware)


@cli.command(
    short_help="Helps to identify Observers by flashing LEDs near Targets (IO, EMU)",
    context_settings={"ignore_unknown_options": True},
)
@click.argument("duration", type=click.INT, default=30)
def blink(duration: int) -> None:
    set_verbosity()
    log.info("Blinks LEDs IO & EMU next to Target-Ports for %d s", duration)
    with ShepherdDebug(use_io=False) as dbg:
        dbg.set_power_emulator(True)
        dbg.set_power_io_level_converter(True)
        for _ in range(duration * 2):
            dbg.select_port_for_io_interface(TargetPort.A)
            time.sleep(0.125)
            dbg.select_port_for_power_tracking(TargetPort.A)
            time.sleep(0.125)
            dbg.select_port_for_power_tracking(TargetPort.B)
            time.sleep(0.125)
            dbg.select_port_for_io_interface(TargetPort.B)
            time.sleep(0.125)


@cli.command(
    short_help="Returns statistic about last usage: timestamp, total runtime, sub-command",
    context_settings={"ignore_unknown_options": True},
)
@click.pass_context
def usage(ctx: click.Context) -> None:
    log.info(get_last_usage())
    ctx.exit(0)


if __name__ == "__main__":
    cli()
