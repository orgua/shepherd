"""Allows to periodically reset hardware-watchdog on Cape."""

import logging
import signal
import sys
import time
from contextlib import suppress
from types import FrameType
from types import TracebackType

from typing_extensions import Self

__version__ = "0.9.0"

# Top-Level Package-logger
log = logging.getLogger("ShpWatchdog")
log.addHandler(logging.StreamHandler())
log.setLevel(logging.DEBUG)
log.propagate = 0

# allow importing shepherd on x86 - for testing
with suppress(ModuleNotFoundError):
    from periphery import GPIO


def exit_gracefully(_signum: int, _frame: FrameType | None) -> None:
    log.warning("Exiting!")
    sys.exit(0)


class Watchdog:
    """Allows to periodically reset hardware-watchdog on Cape.

    Args:
        pin_ack: pin that is resetting the hardware watchdog
    """

    def __init__(
        self,
        pin_ack: int,
        interval: int,
    ) -> None:
        log.debug(
            "Initializing Watchdog-Resetter v%s (pin = %d, interval = %d s)",
            __version__,
            pin_ack,
            interval,
        )
        self.pin_ack = pin_ack
        self.interval = interval

    def __enter__(self) -> Self:
        self.gpio_ack = GPIO(self.pin_ack, "out")
        log.debug("Configured GPIO")
        return self

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        self.gpio_ack.close()

    def run(self) -> None:
        """Prevent system-reset from watchdog.

        cape-rev2 has a watchdog that can turn on the BB every ~60 min
        """
        try:
            while True:
                self.gpio_ack.write(value=True)
                time.sleep(0.002)
                self.gpio_ack.write(value=False)
                log.debug("Signaled ACK to Watchdog")
                time.sleep(self.interval)
        except SystemExit:
            return


def main() -> None:
    signal.signal(signal.SIGTERM, exit_gracefully)
    signal.signal(signal.SIGINT, exit_gracefully)
    with Watchdog(pin_ack=68, interval=600) as watchdog:
        watchdog.run()


if __name__ == "__main__":
    main()
