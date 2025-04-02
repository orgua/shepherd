"""
test_sheep_cli
~~~~~
Tests the shepherd sheep CLI implemented with python click.

CAVEAT: For some reason, tests fail when invoking CLI two times within the
same test. Either find a solution or put every CLI call in a separate test.
"""

import time
from datetime import datetime
from pathlib import Path

import numpy as np
import pytest
from click.testing import CliRunner
from pydantic import ValidationError
from shepherd_core import CalibrationHarvester
from shepherd_core import local_tz
from shepherd_core.data_models import Firmware
from shepherd_core.data_models import GpioTracing
from shepherd_core.data_models import PowerTracing
from shepherd_core.data_models import VirtualSourceConfig
from shepherd_core.data_models.task import EmulationTask
from shepherd_core.data_models.task import FirmwareModTask
from shepherd_core.data_models.task import HarvestTask
from shepherd_core.data_models.task import ProgrammingTask
from shepherd_core.data_models.testbed import ProgrammerProtocol
from shepherd_sheep import Writer
from shepherd_sheep.cli import cli
from shepherd_sheep.shared_memory import IVTrace


def random_data(length: int) -> np.ndarray:
    rng = np.random.default_rng()
    return rng.integers(low=0, high=2**18, size=length, dtype="u4")


@pytest.fixture
def data_h5(tmp_path: Path) -> Path:
    store_path = tmp_path / "harvest_example.h5"
    with Writer(
        store_path,
        cal_data=CalibrationHarvester(),
        force_overwrite=True,
    ) as store:
        store.store_hostname("Blinky")
        for i in range(100):
            len_ = 10_000
            mock_data = IVTrace(random_data(len_), random_data(len_), i)
            store.write_iv_buffer(mock_data)
    return store_path


@pytest.fixture
def tmp_yaml(tmp_path: Path) -> Path:
    return tmp_path / "cfg.yaml"


@pytest.fixture
def path_h5(tmp_path: Path) -> Path:
    return tmp_path / "out.h5"


@pytest.fixture
def path_here() -> Path:
    return Path(__file__).resolve().parent


@pytest.mark.hardware
@pytest.mark.timeout(120)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_harvest_no_cal(
    cli_runner: CliRunner,
    tmp_yaml: Path,
    path_h5: Path,
) -> None:
    HarvestTask(
        output_path=path_h5,
        force_overwrite=True,
        duration=10,
        use_cal_default=True,
        verbose=3,
    ).to_file(tmp_yaml)
    res = cli_runner.invoke(cli, ["-v", "run", tmp_yaml.as_posix()])
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_harvest_parameters_most(
    cli_runner: CliRunner,
    tmp_yaml: Path,
    path_h5: Path,
) -> None:
    HarvestTask(
        output_path=path_h5,
        force_overwrite=True,
        duration=10,
        use_cal_default=True,
        time_start=datetime.fromtimestamp(round(time.time() + 20), tz=local_tz()),
        abort_on_error=False,
        verbose=3,
    ).to_file(tmp_yaml)
    res = cli_runner.invoke(cli, ["-v", "run", tmp_yaml.as_posix()])
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_harvest_parameters_minimal(
    cli_runner: CliRunner,
    tmp_yaml: Path,
    path_h5: Path,
) -> None:
    HarvestTask(
        output_path=path_h5,
        force_overwrite=True,
        duration=10,
        verbose=3,
    ).to_file(tmp_yaml)
    res = cli_runner.invoke(cli, ["-v", "run", tmp_yaml.as_posix()])
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_harvest_preconfigured(
    cli_runner: CliRunner,
    path_here: Path,
) -> None:
    file_path = path_here / "_test_config_harvest.yaml"
    res = cli_runner.invoke(cli, ["run", file_path.as_posix()])
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_harvest_preconf_etc_shp_examples(
    cli_runner: CliRunner,
    path_here: Path,
) -> None:
    file_path = path_here.parent / "example_config_harvest.yaml"
    res = cli_runner.invoke(cli, ["run", f"{file_path}"])
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    EmulationTask(
        duration=10,
        force_overwrite=True,
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        verbose=3,
    ).to_file(tmp_yaml)

    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_with_custom_virtsource(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
    path_here: Path,
) -> None:
    EmulationTask(
        duration=10,
        force_overwrite=True,
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        virtual_source=VirtualSourceConfig.from_file(
            path_here / "_test_config_virtsource.yaml",
        ),
        verbose=3,
    ).to_file(tmp_yaml)

    res = cli_runner.invoke(
        cli,
        [
            "-v",
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_with_bq25570(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    EmulationTask(
        duration=10,
        force_overwrite=True,
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        virtual_source=VirtualSourceConfig(name="BQ25570"),
        verbose=3,
    ).to_file(tmp_yaml)

    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_aux_voltage(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    EmulationTask(
        duration=10,
        force_overwrite=True,
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        voltage_aux=2.5,
        verbose=3,
    ).to_file(tmp_yaml)

    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_parameters_long(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    EmulationTask(
        duration=10,
        force_overwrite=True,
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        voltage_aux=2.5,
        time_start=datetime.fromtimestamp(round(time.time() + 20), tz=local_tz()),
        use_cal_default=True,
        enable_io=True,
        io_port="B",
        pwr_port="B",
        abort_on_error=False,
        gpio_tracing=GpioTracing(uart_baudrate=9600),
        power_tracing=PowerTracing(discard_current=False, discard_voltage=True),
        verbose=3,
    ).to_file(tmp_yaml)

    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_h5.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_parameters_minimal(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    EmulationTask(
        input_path=data_h5.as_posix(),
        output_path=path_h5.as_posix(),
        verbose=3,
    ).to_file(tmp_yaml)
    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_preconfigured(
    cli_runner: CliRunner,
    path_here: Path,
) -> None:
    file_path = path_here / "_test_config_emulation.yaml"
    res = cli_runner.invoke(cli, ["run", file_path.as_posix()])
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(80)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_preconf_etc_shp_examples(
    cli_runner: CliRunner,
    path_here: Path,
) -> None:
    # NOTE: this needs prior run of example_config_harvest
    file_path = path_here.parent / "example_config_emulation.yaml"
    res = cli_runner.invoke(cli, ["run", file_path.as_posix()])
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_emulate_aux_voltage_fail(
    cli_runner: CliRunner,
    data_h5: Path,
    path_h5: Path,
    tmp_yaml: Path,
) -> None:
    with pytest.raises(ValidationError):
        EmulationTask(
            duration=10,
            input_path=data_h5.as_posix(),
            output_path=path_h5.as_posix(),
            voltage_aux=5.5,
            verbose=3,
        ).to_file(tmp_yaml)
    res = cli_runner.invoke(
        cli,
        [
            "run",
            tmp_yaml.as_posix(),
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_fw_mod_task(
    cli_runner: CliRunner,
    tmp_path: Path,
    path_here: Path,
) -> None:
    path_yaml = tmp_path / "test_config_fw_mod.yaml"
    path_file = tmp_path / "nrf52_demo_rf.hex"
    fw = Firmware.from_firmware(
        file=path_here / "firmware_nrf52_demo_rf.elf",
        name="firmware_nrf52_demo_rf",
        owner="example",
        group="test",
    )
    path_yaml = FirmwareModTask(
        data=fw.data,
        data_type=fw.data_type,
        custom_id=666,
        firmware_file=path_file,
        verbose=3,
    ).to_file(path_yaml)
    res = cli_runner.invoke(
        cli,
        [
            "run",
            path_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
    assert path_file.exists()
    assert path_file.is_file()


@pytest.mark.hardware
@pytest.mark.timeout(60)
@pytest.mark.usefixtures("_shepherd_up")
def test_cli_programming(
    path_here: Path,
    tmp_path: Path,
) -> None:
    path_file = path_here / "firmware_nrf52_sleep.hex"
    path_yaml = tmp_path / "test_config_programmer.yaml"
    ProgrammingTask(
        firmware_file=path_file.as_posix(),
        mcu_type="nrf52",
        protocol=ProgrammerProtocol.SWD,
        simulate=True,
        verbose=4,
    ).to_file(path_yaml)
    res = CliRunner().invoke(
        cli,
        [
            "run",
            path_yaml.as_posix(),
        ],
    )
    assert res.exit_code == 0
