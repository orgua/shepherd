"""shepherd_herd command line utility.

click-based command line utility for controlling a group of shepherd nodes
remotely through ssh. Provides commands for starting/stopping harvester and
emulator, retrieving recordings to the local machine and flashing firmware
images to target sensor nodes.

"""

from .herd import Herd
from .logger import activate_verbosity
from .logger import get_verbosity
from .logger import logger

__version__ = "0.9.0"

__all__ = [
    "Herd",
    "activate_verbosity",
    "get_verbosity",
    "logger",
    "logger",
]
