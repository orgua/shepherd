import time
from itertools import product
from pathlib import Path

import h5py
import numpy as np
import pytest
from shepherd_core import CalibrationCape
from shepherd_core import CalibrationHarvester
from shepherd_core import CalibrationSeries
from shepherd_core import Reader as CoreReader
from shepherd_sheep import Writer
from shepherd_sheep.shared_memory import IVTrace


def random_data(length: int) -> np.ndarray:
    rng = np.random.default_rng()
    return rng.integers(low=0, high=2**18, size=length, dtype="u4")


@pytest.fixture
def data_buffer() -> IVTrace:
    len_ = 10_000
    voltage = random_data(len_)
    current = random_data(len_)
    return IVTrace(voltage, current, 1551848387472)


@pytest.fixture
def data_h5(tmp_path: Path) -> Path:
    name = tmp_path / "record_example.h5"
    with Writer(name, cal_data=CalibrationHarvester(), force_overwrite=True) as store:
        store.store_hostname("Pinky")
        for i in range(100):
            len_ = 10_000
            mock_data = IVTrace(random_data(len_), random_data(len_), i)
            store.write_iv_buffer(mock_data)
    return name


@pytest.fixture
def cal_cape() -> CalibrationCape:
    return CalibrationCape()


@pytest.mark.parametrize("mode", ["harvester"])
def test_create_h5writer(mode: str, tmp_path: Path, cal_cape: CalibrationCape) -> None:
    d = tmp_path / f"{ mode }.h5"
    h = Writer(file_path=d, cal_data=cal_cape[mode], mode=mode)
    # assert not exists
    h.__enter__()
    assert d.exists()
    h.__exit__()


def test_create_h5writer_with_force(tmp_path: Path, cal_cape: CalibrationCape) -> None:
    d = tmp_path / "harvest.h5"
    d.touch()
    stat = d.stat()
    time.sleep(0.1)

    h = Writer(file_path=d, cal_data=cal_cape.harvester, force_overwrite=False)
    h.__enter__()
    h.__exit__()
    # This should have created the following alternative file:
    d_altered = tmp_path / "harvest.0.h5"
    assert h.file_path == d_altered
    assert d_altered.exists()

    h = Writer(file_path=d, cal_data=cal_cape.harvester, force_overwrite=True)
    h.__enter__()
    h.__exit__()
    new_stat = d.stat()
    assert new_stat.st_mtime > stat.st_mtime


@pytest.mark.parametrize("mode", ["harvester"])
def test_h5writer_data(
    mode: str,
    tmp_path: Path,
    data_buffer: IVTrace,
    cal_cape: CalibrationCape,
) -> None:
    d = tmp_path / "harvest.h5"
    with Writer(file_path=d, cal_data=cal_cape.harvester, mode=mode) as log:
        log.write_iv_buffer(data_buffer)

    with h5py.File(d, "r") as written:
        assert "data" in written
        assert "time" in written["data"]
        for variable in ["voltage", "current"]:
            assert variable in written["data"]  # .keys()
            ref_var = getattr(data_buffer, variable)
            assert all(written["data"][variable][:] == ref_var)


@pytest.mark.parametrize("mode", ["harvester"])
def test_calibration_logging(
    mode: str,
    tmp_path: Path,
    cal_cape: CalibrationCape,
) -> None:
    d = tmp_path / "recording.h5"
    with Writer(file_path=d, mode=mode, cal_data=cal_cape.harvester) as _:
        pass

    h5store = h5py.File(d, "r")
    # hint: CoreReader would be more direct, but less untouched
    cal_series = CalibrationSeries.from_cal(cal_cape.harvester)
    for channel_entry, parameter in product(
        ["voltage", "current", "time"],
        ["gain", "offset"],
    ):
        assert (
            h5store["data"][channel_entry].attrs[parameter] == cal_series[channel_entry][parameter]
        )


def test_key_value_store(tmp_path: Path, cal_cape: CalibrationCape) -> None:
    d = tmp_path / "harvest.h5"

    with Writer(file_path=d, cal_data=cal_cape.harvester) as writer:
        writer["some string"] = "this is a string"
        writer["some value"] = 5

    with h5py.File(d, "r+") as hf:
        assert hf.attrs["some value"] == 5
        assert hf.attrs["some string"] == "this is a string"


@pytest.mark.timeout(2)
def test_h5writer_performance(
    tmp_path: Path,
    data_buffer: IVTrace,
    cal_cape: CalibrationCape,
) -> None:
    d = tmp_path / "harvest_perf.h5"
    with Writer(
        file_path=d,
        force_overwrite=True,
        cal_data=cal_cape.harvester,
    ) as log:
        log.write_iv_buffer(data_buffer)


def test_reader_performance(data_h5: Path) -> None:
    read_durations = []
    with CoreReader(file_path=data_h5) as reader:
        past = time.time()
        for _ in reader.read_buffers():
            now = time.time()
            elapsed = now - past
            read_durations.append(elapsed)
            past = time.time()
    assert np.mean(read_durations) < 0.05
