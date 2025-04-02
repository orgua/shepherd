"""Simulate behavior of virtual source algorithms.

The simulation recreates an observer-cape, the virtual Source and a virtual target
- input = hdf5-file with a harvest-recording
- output = optional as hdf5-file

The output file can be analyzed and plotted with shepherds tool suite.
"""

from contextlib import ExitStack
from pathlib import Path

from shepherd_core import CalibrationEmulator
from shepherd_core import Reader
from shepherd_core import Writer
from shepherd_core.data_models import VirtualSourceConfig
from shepherd_core.vsource.target_model import TargetABC
from tqdm import tqdm

from .pru_source_model import PruSourceModel as VirtualSourceModel


def simulate_source(
    config: VirtualSourceConfig,
    target: TargetABC,
    path_input: Path,
    path_output: Path | None = None,
) -> float:
    """Simulate behavior of virtual source algorithms.

    FN returns the consumed energy of the target.
    """
    stack = ExitStack()
    file_inp = Reader(path_input, verbose=False)
    stack.enter_context(file_inp)
    cal_emu = CalibrationEmulator()
    cal_inp = file_inp.get_calibration_data()

    if path_output:
        file_out = Writer(
            path_output, cal_data=cal_emu, mode="emulator", verbose=False, force_overwrite=True
        )
        stack.enter_context(file_out)
        file_out.store_hostname("emu_sim_" + config.name)
        file_out.store_config(config.model_dump())
        cal_out = file_out.get_calibration_data()

    src = VirtualSourceModel(
        config,
        cal_emu,
        dtype_in=file_inp.get_datatype(),
        log_intermediate=False,
        window_size=file_inp.get_window_samples(),
        voltage_step_V=file_inp.get_voltage_step(),
    )
    i_out_nA = 0
    e_out_Ws = 0.0

    for _t, v_inp, i_inp in tqdm(
        file_inp.read_buffers(is_raw=True), total=file_inp.buffers_n, desc="Buffers", leave=False
    ):
        v_uV = 1e6 * cal_inp.voltage.raw_to_si(v_inp)
        i_nA = 1e9 * cal_inp.current.raw_to_si(i_inp)

        for _n in range(len(_t)):
            v_uV[_n] = src.iterate_sampling(
                V_inp_uV=int(v_uV[_n]),
                I_inp_nA=int(i_nA[_n]),
                I_out_nA=i_out_nA,
            )
            i_out_nA = target.step(int(v_uV[_n]), pwr_good=src.cnv.get_power_good())
            i_nA[_n] = i_out_nA
            # TODO: src.cnv.get_I_mid_out_nA() has more internal drains

        e_out_Ws += (v_uV * i_nA).sum() * 1e-15 * file_inp.sample_interval_s
        if path_output:
            v_out = cal_out.voltage.si_to_raw(1e-6 * v_uV)
            i_out = cal_out.current.si_to_raw(1e-9 * i_nA)
            file_out.append_iv_data_raw(_t, v_out, i_out)

    stack.close()
    return e_out_Ws
