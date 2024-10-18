from types import TracebackType

import h5py
import yaml
from shepherd_core import Compression

from .commons import BUFFER_GPIO_SIZE
from .commons import GPIO_LOG_BIT_POSITIONS
from .h5_monitor_abc import Monitor
from .shared_memory import GPIOTrace


class GpioRecorder(Monitor):
    def __init__(
        self,
        target: h5py.Group,
        compression: Compression | None = Compression.default,
    ) -> None:
        super().__init__(target, compression, poll_intervall=0)

        self.data.create_dataset(
            name="value",
            shape=(self.increment,),
            dtype="u2",
            maxshape=(None,),
            chunks=True,
            compression=compression,
        )
        self.data["value"].attrs["unit"] = "n"
        self.data["value"].attrs["description"] = yaml.safe_dump(
            GPIO_LOG_BIT_POSITIONS,
            default_flow_style=False,
            sort_keys=False,
        )
        # reset increment AFTER creating all dsets are created, TODO: WHY?????
        self.increment = BUFFER_GPIO_SIZE

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        self.data["value"].resize((self.position,))
        super().__exit__()

    def write(self, data: GPIOTrace) -> None:
        len_new = len(data)
        if len_new < 1:
            return
        pos_end = self.position + len_new
        data_length = self.data["time"].shape[0]
        if pos_end >= data_length:
            data_length += max(self.increment, pos_end - data_length)
            self.data["time"].resize((data_length,))
            self.data["value"].resize((data_length,))
        self.data["time"][self.position : pos_end] = data.timestamps_ns
        self.data["value"][self.position : pos_end] = data.bitmasks
        self.position = pos_end

    def thread_fn(self) -> None:
        raise NotImplementedError
