"""
shepherd.eeprom
~~~~~
Defines the format of BeagleBone cape info and shepherd calibration data that
is stored on the shepherd cape's EEPROM. Provides a class for accessing EEPROM
through Linux I2C device driver.


:copyright: (c) 2019 Networked Embedded Systems Lab, TU Dresden.
:license: MIT, see LICENSE for more details.
"""

import os
import struct
from contextlib import suppress
from datetime import datetime
from pathlib import Path
from typing import Dict
from typing import Optional
from typing import Union

import yaml
from shepherd_core import CalibrationCape

from .logger import log

# allow importing shepherd on x86 - for testing
with suppress(ModuleNotFoundError):
    from periphery import GPIO

eeprom_format = {
    "header": {"offset": 0, "size": 4, "type": "binary"},
    "eeprom_revision": {"offset": 4, "size": 2, "type": "str"},
    "board_name": {"offset": 6, "size": 32, "type": "str"},
    "version": {"offset": 38, "size": 4, "type": "str"},
    "manufacturer": {"offset": 42, "size": 16, "type": "str"},
    "part_number": {"offset": 58, "size": 16, "type": "str"},
    "serial_number": {"offset": 76, "size": 12, "type": "str"},
    "cal_date": {"offset": 88, "size": 12, "type": "str"},
}

# The shepherd calibration data is stored in binary format
calibration_data_format = {"offset": 512, "size": 128, "type": "binary"}


class CapeData:
    """Representation of Beaglebone Cape information

    According to BeagleBone specifications, each cape should host an EEPROM
    that contains some standardized information about the type of cape,
    manufacturer, version etc.

    TODO: could inherit from ShpModel

    `See<https://github.com/beagleboard/beaglebone-black/wiki/System-Reference-Manual#824_EEPROM_Data_Format>`_
    """

    def __init__(self, data: dict):
        self.data = data

    @classmethod
    def from_values(
        cls,
        serial_number: Optional[str],
        version: Optional[str] = None,
        cal_date: Optional[str] = None,
    ):
        """Build the object from defaults and user-provided values

        Args:

            serial_number (str): Cape serial number according to BeagleBone
                specification, e.g. 0119XXXX0001
            version (str): Cape version, e.g. 24B0 for board-revision
            cal_date: YYYY-MM-DD

        """

        if serial_number in [None, ""]:
            raise ValueError("Please provide a valid Serial-Number")
        if version in [None, ""]:
            version = "24B0"
        if cal_date in [None, ""]:
            cal_date = datetime.now().strftime("%Y-%m-%d")

        data: Dict[str, Union[str, bytes, None]] = {
            "header": b"\xAA\x55\x33\xEE",
            "eeprom_revision": "A2",
            "board_name": "BeagleBone SHEPHERD Cape",
            "version": version,
            "manufacturer": "NES TU DRESDEN",
            "part_number": "BB-SHPRD",
            "serial_number": serial_number,
            "cal_date": cal_date,
        }
        return cls(data)

    @classmethod
    def from_yaml(cls, filename: Path):
        """Build the object from a yaml file

        Args:
            filename (Path): Name of the yaml file. Should contain all
                required properties

        """
        data = {"header": b"\xAA\x55\x33\xEE"}
        with open(Path(filename).resolve()) as stream:
            yaml_dict = yaml.safe_load(stream)

        data.update(yaml_dict)
        for key in eeprom_format:
            if key not in data:
                raise KeyError(f"Missing { key } from yaml file")

        return cls(data)

    def __getitem__(self, key: str):
        return self.data[key]

    def __repr__(self):
        print_dict = {}
        for key in self.data:
            if eeprom_format[key]["type"] in ["ascii", "str"]:
                print_dict[key] = self.data[key]
        return yaml.safe_dump(print_dict, default_flow_style=False)

    def keys(self):
        return self.data.keys()

    def items(self):
        for key in self.data:
            yield key, self.data[key]


class EEPROM:
    """Represents EEPROM device

    Convenient wrapper of Linux I2C EEPROM device. Knows about the format
    of the shepherd EEPROM info, including Beaglebone cape data and
    shepherd calibration data.

    """

    def __init__(self, bus_num: int = 2, address: int = 0x54, wp_pin: int = 49):
        """Initializes EEPROM by bus number and address.

        Args:
            bus_num (int): I2C bus number, e.g. 1 for I2C1 on BeagleBone
            address (int): Address of EEPROM, usually fixed in hardware or
                by DIP switch
        """
        self.dev_path = f"/sys/bus/i2c/devices/{bus_num}" f"-{address:04X}/eeprom"
        self._write_protect_pin: GPIO = GPIO(wp_pin, "out")
        self._write_protect_pin.write(True)

    def __enter__(self):
        self.fd = os.open(self.dev_path, os.O_RDWR | os.O_SYNC)
        return self

    def __exit__(self, *args):  # type: ignore
        os.close(self.fd)

    def _read(self, address: int, n_bytes: int) -> bytes:
        """Reads a given number of bytes from given address.

        Args:
            address (int): Start address for read operation
            n_bytes (int): Number of bytes to read from address
        """
        os.lseek(self.fd, address, 0)
        return os.read(self.fd, n_bytes)

    def _write(self, address: int, buffer: bytes) -> None:
        """Writes binary data from byte buffer to given address.

        Args:
            address (int): Start address for write operation
            buffer (bytes): Binary data to write

        Raises:
            TimeoutError: If write operation times out
        """
        self._write_protect_pin.write(False)
        os.lseek(self.fd, address, 0)
        try:
            os.write(self.fd, buffer)
        except TimeoutError:
            log.error("Timeout writing to EEPROM. Is write protection disabled?")
            raise
        self._write_protect_pin.write(True)

    def __getitem__(self, key: str):
        """Retrieves attribute from EEPROM.

        Args:
            key (str): Name of requested attribute

        Raises:
            KeyError: If key is not a valid attribute
        """
        if key not in eeprom_format:
            raise KeyError(f"{ key } is not a valid EEPROM parameter")
        raw_data = self._read(eeprom_format[key]["offset"], eeprom_format[key]["size"])
        if eeprom_format[key]["type"] == "ascii":
            return raw_data.decode("utf-8")
        if eeprom_format[key]["type"] == "str":
            str_data = raw_data.split(b"\x00")
            return str_data[0].decode("utf-8")
        else:
            return raw_data

    def __setitem__(self, key: str, value):  # type: ignore
        """Writes attribute to EEPROM.

        Args:
            key (str): Name of the attribute
            value: Value of the attribute

        Raises:
            KeyError: If key is not a valid attribute
            ValueError: If value does not meet specification of corresponding
                attribute
        """
        if key not in eeprom_format:
            raise KeyError(f"{ key } is not a valid EEPROM parameter")
        if eeprom_format[key]["type"] == "ascii":
            # TODO: ascii not used anymore -> why limit some fields to exact length?
            if len(value) != eeprom_format[key]["size"]:
                raise ValueError(
                    f"Value { value } has wrong size. "
                    f"Required size is { eeprom_format[key]['size'] }",
                )
            self._write(eeprom_format[key]["offset"], value.encode("utf-8"))
        elif eeprom_format[key]["type"] == "str":
            if len(value) < eeprom_format[key]["size"]:
                value += "\0"
            elif len(value) > eeprom_format[key]["size"]:
                raise ValueError(
                    f"Value { value } is longer than maximum "
                    f"size { eeprom_format[key]['size'] }",
                )
            self._write(eeprom_format[key]["offset"], value.encode("utf-8"))
        else:
            self._write(eeprom_format[key]["offset"], value)

    def write_cape_data(self, cape_data: CapeData) -> None:
        """Writes complete BeagleBone cape data to EEPROM

        Args:
            cape_data (CapeData): Cape data that should be written
        """
        for key, value in cape_data.items():
            self[key] = value

    def read_cape_data(self) -> CapeData:
        """Reads and returns BeagleBone cape data from EEPROM

        Returns:
            CapeData object containing data extracted from EEPROM
        """
        data = {}
        for key in eeprom_format:
            data[key] = self[key]
        return CapeData(data)

    def write_calibration(self, cal_cape: CalibrationCape) -> None:
        """Writes complete BeagleBone cape data to EEPROM

        Args:
            cal_cape (CalibrationCape): Calibration data that is going
                to be stored in EEPROM
        """
        data_serialized = cal_cape.to_bytestr()
        if len(data_serialized) != calibration_data_format["size"]:
            raise ValueError(
                f"WriteCal: data-size is wrong! "
                f"expected = {calibration_data_format['size']} bytes, "
                f"but got {len(data_serialized)}",
            )
        self._write(calibration_data_format["offset"], data_serialized)

    def read_calibration(self) -> CalibrationCape:
        """Reads and returns shepherd calibration data from EEPROM

        Returns:
            CalibrationCape object containing data extracted from EEPROM
        """
        data = self._read(
            calibration_data_format["offset"],
            calibration_data_format["size"],
        )
        try:
            cal = CalibrationCape.from_bytestr(data)
            log.debug("EEPROM provided calibration-settings")
        except struct.error:
            cal = CalibrationCape()
            log.warning(
                "EEPROM seems to have no usable data - will set calibration from default-values",
            )
        return cal


def retrieve_calibration(use_default_cal: bool = False) -> CalibrationCape:
    if use_default_cal:
        return CalibrationCape()
    else:
        try:
            with EEPROM() as storage:
                return storage.read_calibration()
        except ValueError:
            log.warning(
                "Couldn't read calibration from EEPROM (ValueError). "
                "Falling back to default values.",
            )
            return CalibrationCape()
        except FileNotFoundError:
            log.warning(
                "Couldn't read calibration from EEPROM (FileNotFoundError). "
                "Falling back to default values.",
            )
            return CalibrationCape()