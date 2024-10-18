import datetime
import platform
import sys
import time
from contextlib import ExitStack
from types import TracebackType

from shepherd_core import local_tz
from shepherd_core.data_models.content.virtual_harvester import HarvesterPRUConfig
from shepherd_core.data_models.task import HarvestTask
from tqdm import tqdm
from typing_extensions import Self

from .eeprom import retrieve_calibration
from .h5_writer import Writer
from .logger import get_verbosity
from .logger import log
from .shepherd_io import ShepherdIO


class ShepherdHarvester(ShepherdIO):
    """API for recording a harvest with shepherd.

    Provides an easy-to-use, high-level interface for recording data with
    shepherd. Configures all hardware and initializes the communication
    with kernel module and PRUs.

    Args:
        cfg: harvester task setting
        mode (str): Should be 'harvester' to record harvesting data
    """

    def __init__(
        self,
        cfg: HarvestTask,
        mode: str = "harvester",
    ) -> None:
        log.debug("ShepherdHarvester-Init in %s-mode", mode)
        super().__init__(
            mode=mode,
            trace_iv=cfg.power_tracing,
            trace_gpio=None,
        )
        self.cfg = cfg
        self.stack = ExitStack()

        # performance-critical, allows deep insight between py<-->pru-communication
        self.verbose_extra = False

        self.cal_hrv = retrieve_calibration(use_default_cal=cfg.use_cal_default).harvester

        if cfg.time_start is None:
            self.start_time = round(time.time() + 10)
        else:
            self.start_time = cfg.time_start.timestamp()

        self.hrv_pru = HarvesterPRUConfig.from_vhrv(
            data=cfg.virtual_harvester,
            for_emu=False,
            dtype_in=None,
        )

        store_path = cfg.output_path.resolve()
        if store_path.is_dir():
            timestamp = datetime.datetime.fromtimestamp(self.start_time, tz=local_tz())
            timestring = timestamp.strftime("%Y-%m-%d_%H-%M-%S")
            # ⤷ closest to ISO 8601, avoids ":"
            store_path = store_path / f"hrv_{timestring}.h5"

        self.writer = Writer(
            file_path=store_path,
            mode=mode,
            datatype=cfg.virtual_harvester.get_datatype(),
            window_samples=cfg.virtual_harvester.calc_window_size(for_emu=True),
            cal_data=self.cal_hrv,
            compression=cfg.output_compression,
            force_overwrite=cfg.force_overwrite,
            verbose=get_verbosity(),
        )

    def __enter__(self) -> Self:
        super().__enter__()
        super().set_power_emulator(state=False)
        super().set_power_recorder(state=True)

        super().send_virtual_harvester_settings(self.hrv_pru)
        super().send_calibration_settings(self.cal_hrv)

        super().reinitialize_prus()  # needed for ADCs

        self.stack.enter_context(self.writer)
        # add hostname to file
        self.writer.store_hostname(platform.node().strip())
        self.writer.store_config(self.cfg.model_dump())
        self.writer.start_monitors(
            sys=self.cfg.sys_logging,
        )

        # Give the PRU empty buffers to begin with
        time.sleep(1)
        return self

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        super()._power_down_shp()
        self.stack.close()
        super().__exit__()

    def run(self) -> None:
        success = self.start(self.start_time, wait_blocking=False)
        if not success:
            return
        log.info("waiting %.2f s until start", self.start_time - time.time())
        self.wait_for_start(self.start_time - time.time() + 15)
        self.handle_pru_messages()
        log.info("shepherd started! T_sys = %f", time.time())

        if self.cfg.duration is None:
            ts_end = sys.float_info.max
            duration_s = None
        else:
            duration_s = self.cfg.duration.total_seconds()
            ts_end = self.start_time + duration_s
            log.debug("Duration = %s (forced runtime)", duration_s)

        # Progress-Bar
        prog_bar = tqdm(
            total=duration_s,
            desc="Measurement",
            mininterval=2,
            unit="s",
            leave=False,
        )

        ts_data_last = self.start_time
        while True:
            data_iv = self.shared_mem.read_buffer_iv(verbose=self.verbose_extra)
            data_ut = self.shared_mem.read_buffer_util(verbose=self.verbose_extra)
            if data_ut:
                self.writer.write_util_buffer(data_ut)

            if data_iv is not None:
                prog_bar.update(n=data_iv.duration())
                if data_iv.timestamp() >= ts_end:
                    log.debug("FINISHED! Out of bound timestamp collected -> begin to exit now")
                    break
                ts_data_last = time.time()
                try:
                    self.writer.write_iv_buffer(data_iv)
                except OSError as _xpt:
                    log.error(
                        "Failed to write data to HDF5-File - will STOP! error = %s",
                        _xpt,
                    )
                    break
            # TODO: implement cleaner exit from pru-statechange or end-TS
            self.handle_pru_messages()
            if not (data_iv or data_ut):
                if ts_data_last - time.time() > 10:
                    log.error("Main sheep-routine ran dry for 10s, will STOP")
                    break
                # rest of loop is non-blocking, so we better doze a while if nothing to do
                time.sleep(self.segment_period_s)

        prog_bar.close()
