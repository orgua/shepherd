"""Launcher allows to start and stop shepherd service with the press of a button.

Relies on systemd service.

"""

import logging
import os
import signal
import sys
import time
from contextlib import suppress
from types import FrameType
from types import TracebackType

from typing_extensions import Self

__version__ = "0.9.0"

# Top-Level Package-logger
log = logging.getLogger("ShpLauncher")
log.addHandler(logging.StreamHandler())
log.setLevel(logging.DEBUG)
log.propagate = 0


# allow importing shepherd on x86 - for testing
with suppress(ModuleNotFoundError):
    import dbus
    from periphery import GPIO


def exit_gracefully(_signum: int, _frame: FrameType | None) -> None:
    log.warning("Exiting!")
    sys.exit(0)


class Launcher:
    """Stores data coming from PRU's in HDF5 format.

    Args:
        pin_button (int): pin-number where button is connected. Must be
            configured as input with pull up and connected against ground
        pin_led (int): pin-number of LED for displaying launcher status
        service_name (str): Name of shepherd systemd service

    """

    def __init__(
        self,
        pin_button: int,
        pin_led: int,
        service_name: str,
    ) -> None:
        log.debug(
            "Initializing Launcher v%s for '%s' (pin_button = %d, pin_led = %d)",
            __version__,
            service_name,
            pin_button,
            pin_led,
        )
        self.pin_button = pin_button
        self.pin_led = pin_led
        self.service_name = service_name

    def __enter__(self) -> Self:
        self.gpio_led = GPIO(self.pin_led, "out")
        self.gpio_button = GPIO(self.pin_button, "in")
        self.gpio_button.edge = "falling"
        log.debug("Configured GPIO")

        sys_bus = dbus.SystemBus()
        systemd1 = sys_bus.get_object(
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1",
        )
        self.sd_man = dbus.Interface(systemd1, "org.freedesktop.systemd1.Manager")

        sd_object = self.sd_man.LoadUnit(f"{self.service_name}.service")
        self.sd_service = sys_bus.get_object(
            "org.freedesktop.systemd1",
            str(sd_object),
        )
        log.debug("Configured dbus for systemd")
        return self

    def __exit__(
        self,
        typ: type[BaseException] | None = None,
        exc: BaseException | None = None,
        tb: TracebackType | None = None,
        extra_arg: int = 0,
    ) -> None:
        self.gpio_led.close()
        self.gpio_button.close()

    def run(self) -> None:
        """Infinite loop waiting for button presses.

        Waits for falling edge on configured button pin. On detection of the
        edge, shepherd service is either started or stopped. Double button
        press while idle causes system shutdown.
        """
        try:
            while True:
                log.info("Waiting for falling edge..")
                self.gpio_led.write(value=True)
                if not self.gpio_button.poll(timeout=None):
                    # NOTE poll is suspected to exit after ~ 1-2 weeks running
                    #      -> fills mmc with random measurement otherwise
                    log.debug("Button.Poll() exited without detecting edge")
                    continue
                self.gpio_led.write(value=False)
                log.debug("Edge detected")
                if not self.get_state():
                    time.sleep(0.25)
                    if self.gpio_button.poll(timeout=2):
                        log.debug("2nd falling edge detected")
                        log.info("Shutdown requested")
                        self.initiate_shutdown()
                        self.gpio_led.write(value=False)
                        time.sleep(3)
                        continue
                self.set_service(requested_state=not self.get_state())
                time.sleep(10)
        except SystemExit:
            return

    def get_state(self, timeout: float = 10) -> bool:
        """Queries systemd for state of shepherd service.

        Args:
            timeout (float): Time to wait for service state to settle

        Raises:
            TimeoutError: If state remains changing for longer than timeout
        """
        ts_end = time.time() + timeout

        while True:
            systemd_state = self.sd_service.Get(
                "org.freedesktop.systemd1.Unit",
                "ActiveState",
                dbus_interface="org.freedesktop.DBus.Properties",
            )
            if systemd_state in {"deactivating", "activating"}:
                time.sleep(0.1)
            else:
                break
            if time.time() > ts_end:
                raise TimeoutError("Timed out waiting for service state")

        log.debug("Service ActiveState: %s", systemd_state)

        if systemd_state == "active":
            return True
        if systemd_state == "inactive":
            return False
        raise OSError("Unknown state '%s'", systemd_state)

    def set_service(self, *, requested_state: bool) -> bool | None:
        """Changes state of shepherd service.

        Args:
            requested_state (bool): Target state of service
        """
        active_state = self.get_state()

        if requested_state == active_state:
            log.debug("service already in requested state")
            self.gpio_led.write(value=active_state)
            return None

        if active_state:
            log.info("Stopping service")
            self.sd_man.StopUnit("shepherd.service", "fail")
        else:
            log.info("Starting service")
            self.sd_man.StartUnit("shepherd.service", "fail")

        time.sleep(1)

        new_state = self.get_state()
        if new_state != requested_state:
            raise OSError("State didn't change")

        return new_state

    def initiate_shutdown(self, timeout: int = 5) -> None:
        """Initiates system shutdown.

        Args:
            timeout (int): Number of seconds to wait before powering off
                system
        """
        log.debug("Initiating shutdown routine..")
        time.sleep(0.25)
        for _ in range(timeout):
            if self.gpio_button.poll(timeout=0.5):
                log.debug("Edge detected")
                log.info("Shutdown canceled")
                return
            self.gpio_led.write(value=True)
            if self.gpio_button.poll(timeout=0.5):
                log.debug("Edge detected")
                log.info("Shutdown canceled")
                return
            self.gpio_led.write(value=False)
        os.sync()
        log.info("Shutting down now")
        self.sd_man.PowerOff()


def main() -> None:
    signal.signal(signal.SIGTERM, exit_gracefully)
    signal.signal(signal.SIGINT, exit_gracefully)
    with Launcher(pin_button=65, pin_led=22, service_name="shepherd") as launch:
        launch.run()


if __name__ == "__main__":
    main()
