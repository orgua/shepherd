import signal
import sys
from pathlib import Path
from types import FrameType

import click
import pandas as pd

from . import __version__
from .logger import activate_verbosity
from .logger import logger as log
from .logic_trace import LogicTrace
from .logic_traces import LogicTraces


def exit_gracefully(_signum: int, _frame: FrameType | None) -> None:
    """Signal-handling for a clean exit-strategy."""
    log.warning("Exiting!")
    sys.exit(0)


@click.command(short_help="Analyze sync measurement files")
@click.option(
    "--input-path",
    "-i",
    type=click.Path(exists=True, readable=True, file_okay=True, dir_okay=True),
    default=None,
    help="Path to .csv-files from sync measurement (search CWD if omitted)",
)
@click.option("--pickle", "-p", is_flag=True, help="store input-files as compressed pickle")
@click.option("--verbose", "-v", is_flag=True)
@click.option(
    "--version",
    is_flag=True,
    help="Prints version-infos (combinable with -v)",
)
def cli(
    input_path: Path | None = None,
    *,
    pickle: bool = False,
    verbose: bool = False,
    version: bool = False,
) -> None:
    """Analyze sync measurement files."""
    signal.signal(signal.SIGTERM, exit_gracefully)
    signal.signal(signal.SIGINT, exit_gracefully)

    if verbose:
        activate_verbosity()

    if version:
        log.info("sync-analysis v%s", __version__)
        log.debug("Python v%s", sys.version)
        log.debug("Click v%s", click.__version__)

    if input_path is None:
        input_path = Path.cwd()
    input_path = Path(input_path)
    dir_path = input_path
    if not dir_path.is_dir():
        dir_path = dir_path.parent

    ltraces = LogicTraces(input_path, glitch_ns=20)
    # TODO: make glitch timing configurable

    if len(ltraces.traces) < 1:
        log.warning("No traces found! Will exit now")
        return

    _stat: dict[str, list] = {
        "diff": [],
        "rising": [],
        "falling": [],
        "low": [],
        "high": [],
    }

    for trace in ltraces.traces:
        if pickle:
            trace.to_file(input_path)

        for _ch in range(trace.channel_count):
            _data_r = trace.calc_durations_ns(_ch, edge_a_rising=True, edge_b_rising=True)
            _expt_r = trace.calc_expected_value(_data_r, mode_log10=True)  # in nsec
            _name = trace.name + f"_ch{_ch}_rising_{round(_expt_r / 1e3)}us"
            _data_r[:, 1] = _data_r[:, 1] - _expt_r
            trace.plot_series_jitter(_data_r, _name, dir_path)
            _stat["rising"].append(trace.get_statistics(_data_r, _name))

            _data_f = trace.calc_durations_ns(_ch, edge_a_rising=False, edge_b_rising=False)
            _expt_f = trace.calc_expected_value(_data_f, mode_log10=True)
            _name = trace.name + f"_ch{_ch}_falling_{round(_expt_f / 1e3)}us"
            _data_f[:, 1] = _data_f[:, 1] - _expt_f
            trace.plot_series_jitter(_data_f, _name, dir_path)
            _stat["falling"].append(trace.get_statistics(_data_f, _name))

            _data_l = trace.calc_durations_ns(_ch, edge_a_rising=False, edge_b_rising=True)
            _name = trace.name + f"_ch{_ch}_low"
            _stat["low"].append(trace.get_statistics(_data_l, _name))

            _data_h = trace.calc_durations_ns(_ch, edge_a_rising=True, edge_b_rising=False)
            _name = trace.name + f"_ch{_ch}_high"
            _stat["high"].append(trace.get_statistics(_data_h, _name))

        # sync between channels
        for _ch1 in range(trace.channel_count):
            _data1 = trace.get_edge_timestamps(_ch1, rising=True)
            for _ch2 in range(_ch1 + 1, trace.channel_count):
                _data2 = trace.get_edge_timestamps(_ch2, rising=True)
                _diff = trace.calc_duration_free_ns(_data1, _data2)
                _name = trace.name + f"_diff_{_ch1}u{_ch2}"
                trace.plot_series_jitter(_diff, _name, dir_path)
                _stat["diff"].append(trace.get_statistics(_diff, _name))

    ltraces.plot_comparison_series(start=0)
    _stat_df = {
        _k: pd.DataFrame(_v, columns=LogicTrace.get_statistics_header()) for _k, _v in _stat.items()
    }
    for _k, _v in _stat_df.items():
        log.info("")
        log.info("TYPE: %s", _k)
        log.info(_v.to_string())

    # Trigger-Experiment:
    # - watch P8_19-low variance under load (currently 29.3 - 49.3 us)
    #   - busy wait is 50 us, this should not be close to 0
    #   - example: [ 29348 <| 43416 || 46416 || 48726 |> 49276 ]
    # - watch P8_19-rising
    #   - example: [ -662 <| -404 || -142 || 128 |> 378 ]


if __name__ == "__main__":
    cli()
