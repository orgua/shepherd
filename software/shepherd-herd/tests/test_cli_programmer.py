from pathlib import Path

import pytest
from shepherd_herd.cli import cli

# NOTE: (almost) direct copy between shepherd-herd & python-package


@pytest.fixture
def firmware_example():
    here = Path(__file__).absolute()
    name = "firmware_nrf52_powered.hex"
    return here.parent / name


@pytest.fixture
def firmware_empty(tmp_path):
    store_path = tmp_path / "firmware_null.hex"
    with open(store_path, "w") as f:
        f.write("")
    return store_path


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_minimal(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_swd_explicit(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--sel_a",
            "--voltage",
            "2.0",
            "--datarate",
            "600000",
            "--target",
            "nrf52",
            "--prog1",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_swd_explicit_short(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--sel_a",
            "-v",
            "2.0",
            "-d",
            "600000",
            "-t",
            "nrf52",
            "--prog1",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_sbw_explicit(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--sel_b",
            "--voltage",
            "1.5",
            "--datarate",
            "300000",
            "--target",
            "msp430",
            "--prog2",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_file_defective_a(stopped_herd, cli_runner, firmware_empty):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--simulate",
            str(firmware_empty),
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_file_defective_b(stopped_herd, cli_runner, tmp_path):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--simulate",
            str(tmp_path),  # Directory
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_file_defective_c(stopped_herd, cli_runner, tmp_path):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--simulate",
            str(tmp_path / "file_abc.bin"),  # non_existing file
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_datarate_invalid_a(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--datarate",
            "2000000",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_datarate_invalid_b(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--datarate",
            "0",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code != 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_program_target_invalid(stopped_herd, cli_runner, firmware_example):
    res = cli_runner.invoke(
        cli,
        [
            "-vvv",
            "programmer",
            "--target",
            "arduino",
            "--simulate",
            str(firmware_example),
        ],
    )
    assert res.exit_code != 0


# not testable ATM (through CLI)
#   - fail pins 3x (pin_num is identical)
#   - fail wrong target (internally, fail in kModule)
#   - datasize > mem_size