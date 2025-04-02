import time
from types import TracebackType

import h5py
from shepherd_core import Compression

from . import commons
from .h5_monitor_abc import Monitor
from .shared_memory import UtilTrace


class PruRecorder(Monitor):
    def __init__(
        self,
        target: h5py.Group,
        compression: Compression | None = Compression.default,
    ) -> None:
        super().__init__(target, compression, poll_intervall=0)

        self.data.create_dataset(
            name="values",
            shape=(self.increment, 3),
            dtype="u2",
            maxshape=(None, 3),
            chunks=(self.increment, 3),
            compression=compression,
        )

        self.data["values"].attrs["unit"] = "ns, ns, ns"
        self.data["values"].attrs["description"] = (
            "pru0_vsrc_tsample_mean [ns], "
            "pru0_vsrc_tsample_max [ns],"
            "pru1_gpio_tsample_max [ns],"
            f"with {commons.SAMPLE_INTERVAL_NS} ns per sample-step"
        )
        # reset increment AFTER creating all dsets are created
        self.increment = 1000  # 100 s
        # TODO: make dependent from commons.BUFFER_GPIO_SIZE

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        self.data["values"].resize((self.position, 3))
        super().__exit__()

    def write(self, data: UtilTrace) -> None:
        """This data allows to
        - reconstruct timestamp-stream later (runtime-optimization, 33% less load)
        - identify critical pru0-timeframes
        """
        len_new = len(data)
        if len_new < 1:
            return
        pos_end = self.position + len_new
        data_length = self.data["time"].shape[0]
        if pos_end >= data_length:
            data_length += max(self.increment, pos_end - data_length)
            self.data["values"].resize((data_length, 3))
            self.data["time"].resize((data_length,))
        self.data["time"][self.position : pos_end] = int(time.time() * 1e9)
        self.data["values"][self.position : pos_end, 0] = data.pru0_tsample_mean
        self.data["values"][self.position : pos_end, 1] = data.pru0_tsample_max
        self.data["values"][self.position : pos_end, 2] = data.pru1_tsample_max
        self.position = pos_end

    def thread_fn(self) -> None:
        raise NotImplementedError
