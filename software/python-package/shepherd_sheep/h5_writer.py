"""
shepherd.datalog
~~~~~
Provides classes for storing and retrieving sampled IV data to/from
HDF5 files.

"""

from pathlib import Path
from types import TracebackType
from typing import TYPE_CHECKING
from typing import ClassVar

from typing_extensions import Self

from . import commons

if TYPE_CHECKING:
    import h5py

    from .h5_monitor_abc import Monitor

import numpy as np
from shepherd_core import CalibrationEmulator as CalEmu
from shepherd_core import CalibrationHarvester as CalHrv
from shepherd_core import CalibrationSeries as CalSeries
from shepherd_core import Writer as CoreWriter
from shepherd_core.data_models import GpioTracing
from shepherd_core.data_models import SystemLogging
from shepherd_core.data_models.task import Compression

from .h5_monitor_kernel import KernelMonitor
from .h5_monitor_ptp import PTPMonitor
from .h5_monitor_sheep import SheepMonitor
from .h5_monitor_sysutil import SysUtilMonitor
from .h5_monitor_uart import UARTMonitor
from .h5_recorder_gpio import GpioRecorder
from .h5_recorder_pru import PruRecorder
from .shared_memory import GPIOTrace
from .shared_memory import IVTrace
from .shared_memory import UtilTrace


class Writer(CoreWriter):
    """Stores data coming from PRU's in HDF5 format

    Args:
        file_path (Path): Name of the HDF5 file that data will be written to
        cal_data (CalibrationEmulator or CalibrationHarvester): Data is written as raw ADC
            values. We need calibration data in order to convert to physical
            units later.
        mode (str): Indicates if this is data from harvester or emulator
        force_overwrite (bool): Overwrite existing file with the same name
    """

    mode_dtype_dict: ClassVar[dict[str, list]] = {
        "harvester": ["ivsample", "ivcurve", "isc_voc"],
        "emulator": ["ivsample"],
    }

    def __init__(
        self,
        file_path: Path,
        mode: str | None = None,
        datatype: str | None = None,
        window_samples: int | None = None,
        cal_data: CalSeries | CalEmu | CalHrv | None = None,
        compression: Compression = Compression.default,
        *,
        modify_existing: bool = False,
        force_overwrite: bool = False,
        verbose: bool | None = True,
    ) -> None:
        # hopefully overwrite defaults from Reader
        self.samplerate_sps: int = 10**9 // commons.SAMPLE_INTERVAL_NS

        # TODO: derive verbose-state
        super().__init__(
            file_path,
            mode,
            datatype,
            window_samples,
            cal_data,
            compression,
            modify_existing=modify_existing,
            force_overwrite=force_overwrite,
            verbose=verbose,
        )

        self.buffer_timeseries = self.sample_interval_ns * np.arange(
            self.samples_per_buffer,
        ).astype("u8")
        # TODO: keep this optimization

        self.grp_data: h5py.Group = self.h5file["data"]

        # Optimization: allowing larger more efficient resizes
        #               (before .resize() was called per element)
        # h5py v3.4 is taking 20% longer for .write_buffer() than v2.1
        # this change speeds up v3.4 by 30% (even system load drops from 90% to 70%), v2.1 by 16%
        self.data_pos = 0
        self.data_inc = int(100 * self.samplerate_sps)
        # NOTE for possible optimization: align resize with chunk-size
        #      -> rely on autochunking -> inc = h5ds.chunks

        # prepare Monitors
        self.sysutil_log_enabled: bool = True
        self.monitors: list[Monitor] = []

    def __enter__(self) -> Self:
        """Initializes the structure of the HDF5 file

        HDF5 is hierarchically structured and before writing data, we have to
        set up this structure, i.e. creating the right groups with corresponding
        data types. We will store 3 types of data in a Writer database: The
        actual IV samples recorded either from the harvester (during recording)
        or the target (during emulation). Any log messages, that can be used to
        store relevant events or tag some parts of the recorded data. And lastly
        the state of the GPIO pins.

        """
        super().__enter__()

        # Create group for additional recorders
        self.gpio_grp = self.h5file.create_group("gpio")
        self.pru_util_grp = self.h5file.create_group("pru_util")
        # prepare recorders
        self.rec_gpio = GpioRecorder(self.gpio_grp, compression=self._compression)
        self.rec_pru = PruRecorder(self.pru_util_grp, compression=self._compression)

        # targets for logging-monitor # TODO: redesign? all should be kept in data_0
        self.sheep_grp = self.h5file.create_group("sheep")
        self.uart_grp = self.h5file.create_group("uart")
        self.sys_util_grp = self.h5file.create_group("sys_util")
        self.kernel_grp = self.h5file.create_group("kernel")
        self.ptp_grp = self.h5file.create_group("ptp")
        return self

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        # trim over-provisioned parts
        self.grp_data["time"].resize((self.data_pos,))
        self.grp_data["voltage"].resize((self.data_pos,))
        self.grp_data["current"].resize((self.data_pos,))

        # end recorders
        self.rec_gpio.__exit__()
        self.rec_pru.__exit__()

        # end monitors
        for monitor in self.monitors:
            monitor.__exit__()

        super().__exit__()

    def write_iv_buffer(self, data: IVTrace) -> None:
        """Writes data from buffer to file.

        Args:
            data: buffer-segment containing IV data
        """
        # First, we have to resize the corresponding datasets
        data_length_new = len(data)
        if data_length_new > 0:
            data_end_pos = self.data_pos + data_length_new
            data_length_h5 = self.grp_data["voltage"].shape[0]
            if data_end_pos >= data_length_h5:
                data_length_h5 += self.data_inc
                self.grp_data["voltage"].resize((data_length_h5,))
                self.grp_data["current"].resize((data_length_h5,))
                self.grp_data["time"].resize((data_length_h5,))

            self.grp_data["voltage"][self.data_pos : data_end_pos] = data.voltage
            self.grp_data["current"][self.data_pos : data_end_pos] = data.current
            if isinstance(data.timestamp_ns, int):
                self.grp_data["time"][self.data_pos : data_end_pos] = (
                    self.buffer_timeseries + data.timestamp_ns
                )
            elif isinstance(data.timestamp_ns, np.ndarray):
                self.grp_data["time"][self.data_pos : data_end_pos] = data.timestamp_ns
            self.data_pos = data_end_pos

    def write_gpio_buffer(self, data: GPIOTrace) -> None:
        self.rec_gpio.write(data)

    def write_util_buffer(self, data: UtilTrace) -> None:
        self.rec_pru.write(data)

    def start_monitors(
        self,
        sys: SystemLogging | None = None,
        gpio: GpioTracing | None = None,
        # TODO: add gpio-callFN & pru_util
    ) -> None:
        if sys is not None and sys.dmesg:
            self.monitors.append(KernelMonitor(self.kernel_grp, self._compression))
        if sys is not None and sys.ptp:
            self.monitors.append(PTPMonitor(self.ptp_grp, self._compression))
        if self.sysutil_log_enabled:
            self.monitors.append(SysUtilMonitor(self.sys_util_grp, self._compression))
        if gpio is not None and gpio.uart_decode:
            self.monitors.append(
                UARTMonitor(
                    self.uart_grp,
                    self._compression,
                    baudrate=gpio.uart_baudrate,
                ),
            )
        self.monitors.append(SheepMonitor(self.sheep_grp, self._compression))
