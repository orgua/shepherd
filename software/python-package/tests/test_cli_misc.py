from pathlib import Path

import pytest
from shepherd_sheep.cli import cli


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_target_power_min_arg_a(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "target-power",
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_target_power_min_arg_b(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "target-power",
            "--off",
            "-p",
            "B",
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_target_power_min_arg_c(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "target-power",
            "--on",
            "-v",
            "2.0",
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_target_power_explicit_a(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "target-power",
            "--off",
            "--gpio-omit",
            "--target-port",
            "A",
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_target_power_explicit_b(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "target-power",
            "--on",
            "--voltage",
            "2.0",
            "--gpio-pass",
            "--target-port",
            "B",
        ],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_eeprom_read_min_arg_a(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        [
            "eeprom",
            "read",
        ],
    )
    assert res.exit_code in [0, 2, 3]


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_eeprom_read_min_arg_b(
    shepherd_up,
    cli_runner,
    tmp_path: Path,
) -> None:
    path_inf = tmp_path / "info"
    path_cal = tmp_path / "cal"
    res = cli_runner.invoke(
        cli,
        [
            "eeprom",
            "read",
            "-i",
            path_inf.as_posix(),
            "-c",
            path_cal.as_posix(),
        ],
    )
    assert res.exit_code in [0, 2, 3]


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_eeprom_read_explicit(
    shepherd_up,
    cli_runner,
    tmp_path: Path,
) -> None:
    path_inf = tmp_path / "info"
    path_cal = tmp_path / "cal"
    res = cli_runner.invoke(
        cli,
        [
            "eeprom",
            "read",
            "--info-file",
            path_inf.as_posix(),
            "--cal-file",
            path_cal.as_posix(),
        ],
    )
    assert res.exit_code in [0, 2, 3]


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_inventorize_min_arg_a(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        ["inventorize"],
    )
    assert res.exit_code == 0


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_inventorize_min_arg_b(
    shepherd_up,
    cli_runner,
    tmp_path: Path,
) -> None:
    file = tmp_path / "inv.yaml"
    res = cli_runner.invoke(
        cli,
        ["inventorize", "-o", file.as_posix()],
    )
    assert res.exit_code == 0
    assert file.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_inventorize_explicit(
    shepherd_up,
    cli_runner,
    tmp_path: Path,
) -> None:
    file = tmp_path / "inv.yaml"
    res = cli_runner.invoke(
        cli,
        ["inventorize", "--output-path", file.as_posix()],
    )
    assert res.exit_code == 0
    assert file.exists()


@pytest.mark.hardware
@pytest.mark.timeout(60)
def test_cli_fix_kmod(
    shepherd_up,
    cli_runner,
) -> None:
    res = cli_runner.invoke(
        cli,
        ["fix"],
    )
    assert res.exit_code == 0


# TODO: untested CLI
#   - eeprom write
#   - eeprom make
#   - rpc
#   - launcher