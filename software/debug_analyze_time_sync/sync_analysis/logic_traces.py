from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

from . import logger
from .logic_trace import LogicTrace


class LogicTraces:
    def __init__(
        self,
        path: Path,
        glitch_ns: int = 0,
    ) -> None:
        self.traces: list[LogicTrace] = []
        _fcsv = list(path.rglob("*.csv"))
        logger.debug(" -> got %s csv-files", len(_fcsv))

        for _f in _fcsv:
            self.traces.append(LogicTrace.from_file(_f, glitch_ns=glitch_ns))

    def plot_comparison_series(self, start: int = 0) -> None:
        _names: list = [_t.name for _t in self.traces]
        _data: list = [
            _t.calc_durations_ns(0, edge_a_rising=True, edge_b_rising=True) for _t in self.traces
        ]
        _data = [pd.Series(data[:, 1] - LogicTrace.calc_expected_value(data)) for data in _data]

        _len = len(_names)
        _names = _names[start:]
        _data = _data[start:]

        if len(_names) < 1 or len(_data) < 1:
            return
        # TODO: this just takes first CH0
        # file_names_short.reverse()
        fig_title = f"improvement_trigger_statistics_boxplot_{start}to{_len}"
        # TODO: could also print a histogram-overlay for some
        _df = pd.concat(_data, axis=1)
        _df.columns = _names
        ax = _df.plot.box(
            figsize=(20, 8),
            return_type="axes",
            ylim=[-10_000, 10_000],
            # ylim=[-1_000, +1_000], TODO: make it variable
        )
        ax.set_ylabel("trigger_delay [ns]")
        ax.set_title(fig_title)
        plt.grid(
            visible=True,
            which="major",
            axis="y",
            color="gray",
            linewidth="0.6",
            linestyle=":",
            alpha=0.8,
        )
        plt.savefig(fig_title + ".png")
        plt.close()
