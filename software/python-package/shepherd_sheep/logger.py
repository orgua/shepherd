import logging

from shepherd_core.logger import set_log_verbose_level

log = logging.getLogger("Shp")
log.addHandler(logging.NullHandler())
verbose_level: int = 2
log.setLevel(logging.INFO)


def get_verbose_level() -> int:
    return verbose_level


def set_verbose_level(verbose: int) -> None:
    global verbose_level
    verbose_level = min(max(verbose, 0), 3)
    set_log_verbose_level(log, verbose)