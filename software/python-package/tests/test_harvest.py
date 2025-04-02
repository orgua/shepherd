import time
from collections.abc import Generator
from pathlib import Path

import h5py
import numpy as np
import pytest
from shepherd_core import CalibrationHarvester
from shepherd_core.data_models.task import HarvestTask
from shepherd_sheep import ShepherdHarvester
from shepherd_sheep import Writer
from shepherd_sheep import run_harvester


@pytest.fixture(params=["harvester"])  # TODO: there is a second mode now
def mode(request: pytest.FixtureRequest) -> str:
    return request.param


@pytest.fixture
def writer(tmp_path: Path, mode: str) -> Generator[Writer, None, None]:
    with Writer(
        mode=mode,
        cal_data=CalibrationHarvester(),
        force_overwrite=True,
        file_path=tmp_path / "test.h5",
    ) as _w:
        yield _w


@pytest.fixture
def harvester(
    _shepherd_up: None,
    mode: str,
    tmp_path: Path,
) -> Generator[ShepherdHarvester, None, None]:
    cfg = HarvestTask(output_path=tmp_path / "hrv_123.h5")
    with ShepherdHarvester(cfg=cfg, mode=mode) as _h:
        yield _h


@pytest.mark.hardware
@pytest.mark.usefixtures("_shepherd_up")
def test_instantiation(tmp_path: Path) -> None:
    cfg = HarvestTask(output_path=tmp_path / "hrv_123.h5")
    with ShepherdHarvester(cfg) as _h:
        assert _h is not None
    del _h


@pytest.mark.hardware
def test_harvester(writer: Writer, harvester: ShepherdHarvester) -> None:
    harvester.start(wait_blocking=False)
    harvester.wait_for_start(15)

    for _ in range(100):
        _data = None
        while _data is None:
            _data = harvester.shared_mem.read_buffer_iv()
            time.sleep(harvester.segment_period_s / 2)
        writer.write_iv_buffer(_data)


@pytest.mark.hardware  # TODO: extend with new harvester-options
@pytest.mark.timeout(40)
@pytest.mark.usefixtures("_shepherd_up")
def test_harvester_fn(tmp_path: Path) -> None:
    path = tmp_path / "rec.h5"
    time_start = int(time.time() + 10)
    cfg = HarvestTask(
        output_path=path,
        time_start=time_start,
        duration=10,
        force_overwrite=True,
        use_cal_default=True,
    )
    run_harvester(cfg)

    with h5py.File(path, "r+") as hf:
        n_samples = hf["data"]["time"].shape[0]
        assert 900_000 < n_samples <= 1_100_000
        assert hf["data"]["time"][0] == time_start * 10**9
        # test for equidistant timestamps
        time_series = hf["data"]["time"]
        diff_series = time_series[1:] - time_series[:-1]
        unique = np.unique(diff_series)
        assert len(unique) == 1
