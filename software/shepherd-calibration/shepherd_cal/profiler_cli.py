from datetime import datetime
from pathlib import Path
from time import time
from typing import Dict
from typing import Optional

import click
import numpy as np
import typer

from .calibrator import INSTR_4WIRE
from .calibrator import Calibrator
from .cli_helper import cli_setup_callback
from .cli_helper import host_arg_t
from .cli_helper import ofile_opt_t
from .cli_helper import pass_opt_t
from .cli_helper import smu_2w_opt_t
from .cli_helper import smu_ip_opt_t
from .cli_helper import smu_nc_opt_t
from .cli_helper import user_opt_t
from .cli_helper import verbose_opt_t
from .logger import logger
from .profile_analyzer import analyze_directory
from .profiler import INSTR_PROFILE_SHP
from .profiler import Profiler

cli_pro = typer.Typer(
    name="profile",
    help="Sub-commands for profiling the analog frontends",
)

hrv_opt_t = typer.Option(default=False, help="only handle harvester")
emu_opt_t = typer.Option(default=False, help="only handle emulator")
short_opt_t = (typer.Option(default=False, help="reduce I&V steps (2x faster)"),)
quiet_opt_t = typer.Option(default=False, help="unattended (setup prompt)")


@cli_pro.command()
def measure(
    host: str = host_arg_t,
    user: str = user_opt_t,
    password: Optional[str] = pass_opt_t,
    outfile: Optional[Path] = ofile_opt_t,
    smu_ip: str = smu_ip_opt_t,
    smu_2wire: bool = smu_2w_opt_t,
    smu_nplc: float = smu_nc_opt_t,
    harvester: bool = hrv_opt_t,
    emulator: bool = emu_opt_t,
    short: bool = short_opt_t,
    quiet: bool = quiet_opt_t,
    verbose: bool = verbose_opt_t,
):
    """Measure profile-data for shepherd cape"""
    cli_setup_callback(verbose)
    if not any([harvester, emulator]):
        harvester = True
        emulator = True

    smu_4wire = not smu_2wire
    time_now = time()
    components = ("_emu" if emulator else "") + ("_hrv" if harvester else "")
    if outfile is None:
        timestamp = datetime.fromtimestamp(time_now)
        timestring = timestamp.strftime("%Y-%m-%d_%H-%M-%S")
        outfile = Path(f"./{timestring}_shepherd_cape")
    if short:
        file_path = outfile.stem + "_profile_short" + components + ".npz"
    else:
        file_path = outfile.stem + "_profile_full" + components + ".npz"

    shpcal = Calibrator(host, user, password, smu_ip, smu_4wire, smu_nplc)
    profiler = Profiler(shpcal, short)
    # results_hrv = results_emu_a = results_emu_b = None. TODO: check function, replaced by dict
    results: Dict[str, np.ndarray] = {}

    if not quiet:
        click.echo(INSTR_PROFILE_SHP)
        if not smu_4wire:
            click.echo(INSTR_4WIRE)
        logger.info(
            " -> Profiler will sweep through %d voltages and %d currents",
            len(profiler.voltages_V),
            len(profiler.currents_A),
        )
        click.confirm("Confirm that everything is set up ...", default=True)

    if harvester:
        results["hrv"] = profiler.measure_harvester()
    if emulator:
        results["emu_a"] = profiler.measure_emulator_a()
        results["emu_b"] = profiler.measure_emulator_b()

    np.savez_compressed(
        file_path,
        **results,
    )
    logger.info("Data was written to '%s'", file_path)
    logger.debug("Profiling took %.1f s", time() - time_now)


in_files_arg_t = typer.Argument(
    default=...,
    dir_okay=True,
    file_okay=True,
    exists=True,
    help="Input-Files",
)
out_file_opt_t = typer.Option(
    default=None,
    exists=False,
    dir_okay=False,
    file_okay=True,
    help="CSV-File with meta-data of each profile (will be extended if existing)",
)
plot_opt_t = typer.Option(
    default=False,
    help="visualize the profile",
)


@cli_pro.command()
def analyze(
    infiles: Path = in_files_arg_t,
    outfile: Optional[Path] = out_file_opt_t,
    plot: bool = plot_opt_t,
    verbose: bool = verbose_opt_t,
):
    """Analyze profile-data"""
    cli_setup_callback(verbose)
    analyze_directory(infiles, outfile, plot)