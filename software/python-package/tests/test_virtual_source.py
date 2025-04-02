from collections.abc import Generator
from pathlib import Path

import pytest
from shepherd_core import CalibrationCape
from shepherd_core import CalibrationEmulator
from shepherd_core.data_models import EnergyDType
from shepherd_core.data_models import VirtualSourceConfig
from shepherd_core.vsource import VirtualSourceModel
from shepherd_sheep import ShepherdDebug


@pytest.fixture
def src_cfg(request: pytest.FixtureRequest) -> VirtualSourceConfig:
    marker = request.node.get_closest_marker("src_name")
    src_name = None if marker is None else marker.args[0]

    if isinstance(src_name, str):
        if ".yaml" in src_name:
            if Path(src_name).exists():
                path = Path(src_name)
            else:
                here = Path(__file__).resolve()
                path = here.parent / src_name
            return VirtualSourceConfig.from_file(path)
        return VirtualSourceConfig(name=src_name)
    raise AssertionError


@pytest.fixture
def cal_cape() -> CalibrationCape:
    return CalibrationCape()


@pytest.fixture
def pru_vsource(
    _shepherd_up: None,
    src_cfg: VirtualSourceConfig,
    cal_cape: CalibrationCape,
    dtype_in: EnergyDType = EnergyDType.ivsample,
    window_size: int = 0,
) -> Generator[ShepherdDebug, None, None]:
    with ShepherdDebug() as _d:
        _d.vsource_init(
            src_cfg=src_cfg,
            cal_emu=cal_cape.emulator,
            log_intermediate=False,
            dtype_in=dtype_in,
            window_size=window_size,
            voltage_step_V=1e-3,  # totally random
        )  # TODO: extend to be real vsource
        yield _d


@pytest.fixture
def pyt_vsource(
    src_cfg: VirtualSourceConfig,
    cal_cape: CalibrationCape,
    dtype_in: EnergyDType = EnergyDType.ivsample,
    window_size: int = 0,
) -> VirtualSourceModel:
    return VirtualSourceModel(
        vsrc=src_cfg,
        cal_emu=cal_cape.emulator,
        dtype_in=dtype_in,
        window_size=window_size,
    )


@pytest.fixture
def reference_vss() -> dict:
    # keep in sync with "_test_config_virtsource.yaml"
    return {
        "C_intermediate_uF": 100 * (10**0),
        "V_intermediate_init_mV": 3000,
        "eta_in": 0.5,
        "eta_out": 0.8,
        "I_intermediate_leak_nA": 9 * (10**0),
        "V_intermediate_disable_threshold_mV": 2300,
        "V_output_mV": 2000,
        "t_sample_s": 10 * (10**-6),
    }


def difference_percent(val1: float, val2: float, offset: float) -> float:
    # offset is used for small numbers
    return round(100 * abs((val1 + offset) / (val2 + offset) - 1), 3)


@pytest.mark.hardware
@pytest.mark.src_name("./_test_config_virtsource.yaml")
def test_vsource_add_charge(
    pru_vsource: ShepherdDebug,
    pyt_vsource: VirtualSourceModel,
    reference_vss: dict,
) -> None:
    # set desired end-voltage of storage-cap:
    V_cap_mV = 3500
    dt_s = 0.100
    V_inp_mV = 1000
    dV_cap_mV = V_cap_mV - reference_vss["V_intermediate_init_mV"]
    I_cIn_nA = dV_cap_mV * reference_vss["C_intermediate_uF"] / dt_s
    P_inp_pW = I_cIn_nA * reference_vss["V_intermediate_init_mV"] / reference_vss["eta_in"]
    I_inp_nA = P_inp_pW / V_inp_mV
    # prepare fn-parameters
    V_inp_uV = int(V_inp_mV * 10**3)
    I_inp_nA = int(I_inp_nA * 10**0)
    n_samples = int(dt_s / reference_vss["t_sample_s"])
    print(
        f"CHARGE - feeding I = {I_inp_nA} nA, V = {V_inp_mV} mV "
        f"into vSource with {n_samples} steps",
    )
    print(f" PRU PInp = {pru_vsource.cnv_calc_inp_power(V_inp_uV, I_inp_nA)} fW")
    print(f" PRU VCap = {pru_vsource.cnv_update_cap_storage()} uV")
    print(f" Py  PInp = {pyt_vsource.cnv.calc_inp_power(V_inp_uV, I_inp_nA)} fW")
    print(f" Py  VCap = {pyt_vsource.cnv.update_cap_storage()} uV")

    for _ in range(n_samples):
        pru_vsource.cnv_charge(
            V_inp_uV,
            I_inp_nA,
        )  # combines P_in, P_out, V_cap, state_update
        pyt_vsource.cnv.calc_inp_power(V_inp_uV, I_inp_nA)
        pyt_vsource.cnv.update_cap_storage()

    pru_vsource.cnv_calc_inp_power(0, 0)
    V_cap_pru_mV = float(pru_vsource.cnv_update_cap_storage()) * 10**-3
    pyt_vsource.cnv.calc_inp_power(0, 0)
    V_cap_pyt_mV = float(pyt_vsource.cnv.update_cap_storage()) * 10**-3

    dVCap_pru = V_cap_pru_mV - reference_vss["V_intermediate_init_mV"]
    dVCap_pyt = V_cap_pyt_mV - reference_vss["V_intermediate_init_mV"]
    deviation_pru = difference_percent(dVCap_pru, dV_cap_mV, 40)  # %
    deviation_pyt = difference_percent(dVCap_pyt, dV_cap_mV, 40)  # %
    deviation_rel = difference_percent(dVCap_pru, dVCap_pyt, 40)  # %
    print(
        f"CHARGE - VCap goal = {V_cap_mV} mV, "
        f"py = {V_cap_pyt_mV:.3f} mV (dev={deviation_pyt} %), "
        f"pru = {V_cap_pru_mV:.3f} mV (dev={deviation_pru} %), "
        f"dev_rel = {deviation_rel} %",
    )
    assert deviation_pyt < 10.0  # %
    assert deviation_pru < 10.0  # %
    assert deviation_rel < 1.0  # %


@pytest.mark.hardware
@pytest.mark.src_name("./_test_config_virtsource.yaml")
def test_vsource_drain_charge(
    pru_vsource: ShepherdDebug,
    pyt_vsource: VirtualSourceModel,
    reference_vss: dict,
) -> None:
    # set desired end-voltage of storage-cap - low enough to disable output
    V_cap_mV = 2300
    dt_s = 0.50

    dV_cap_mV = V_cap_mV - reference_vss["V_intermediate_init_mV"]
    I_cOut_nA = (
        -dV_cap_mV * reference_vss["C_intermediate_uF"] / dt_s
        - reference_vss["I_intermediate_leak_nA"]
    )
    P_out_pW = I_cOut_nA * reference_vss["V_intermediate_init_mV"] * reference_vss["eta_out"]
    I_out_nA = P_out_pW / reference_vss["V_output_mV"]
    # prepare fn-parameters
    cal = CalibrationEmulator()
    I_out_adc_raw = cal.adc_C_A.si_to_raw(I_out_nA * 10**-9)
    n_samples = int(dt_s / reference_vss["t_sample_s"])

    print(
        f"DRAIN - feeding I = {I_out_nA} nA as {I_out_adc_raw} raw "
        f"into vSource with {n_samples} steps",
    )
    print(f" PRU POut = {pru_vsource.cnv_calc_out_power(I_out_adc_raw)} fW")
    print(f" PRU VCap = {pru_vsource.cnv_update_cap_storage()} uV")
    print(f" PRU VOut = {pru_vsource.cnv_update_states_and_output()} raw")
    print(f" Py  POut = {pyt_vsource.cnv.calc_out_power(I_out_adc_raw)} fW")
    print(f" Py  VCap = {pyt_vsource.cnv.update_cap_storage()} uV")
    print(f" Py  VOut = {pyt_vsource.cnv.update_states_and_output()} raw")

    for index in range(n_samples):
        _, v_raw1 = pru_vsource.cnv_drain(
            I_out_adc_raw,
        )  # combines P_in, P_out, V_cap, state_update
        pyt_vsource.cnv.calc_out_power(I_out_adc_raw)
        pyt_vsource.cnv.update_cap_storage()
        v_raw2 = pyt_vsource.cnv.update_states_and_output()
        if (v_raw1 < 1) or (v_raw2 < 1):
            print(
                f"Stopped Drain-loop after {index}/{n_samples} samples "
                f"({round(100 * index / n_samples)} %), because output was disabled",
            )
            break

    pru_vsource.cnv_calc_out_power(0)
    V_mid_pru_mV = float(pru_vsource.cnv_update_cap_storage()) * 10**-3
    V_out_pru_raw = pru_vsource.cnv_update_states_and_output()
    pyt_vsource.cnv.calc_out_power(0)
    V_mid_pyt_mV = float(pyt_vsource.cnv.update_cap_storage()) * 10**-3
    V_out_pyt_raw = pyt_vsource.cnv.update_states_and_output()

    dVCap_ref = (
        reference_vss["V_intermediate_init_mV"]
        - reference_vss["V_intermediate_disable_threshold_mV"]
    )
    dVCap_pru = reference_vss["V_intermediate_init_mV"] - V_mid_pru_mV
    dVCap_pyt = reference_vss["V_intermediate_init_mV"] - V_mid_pyt_mV
    deviation_pru = difference_percent(dVCap_pru, dVCap_ref, 40)  # %
    deviation_pyt = difference_percent(dVCap_pyt, dVCap_ref, 40)  # %
    deviation_rel = difference_percent(dVCap_pyt, dVCap_pru, 40)  # %
    print(
        f"DRAIN - VCap goal = {V_cap_mV} mV, "
        f"pyt = {V_mid_pyt_mV} mV (dev={deviation_pyt} %), "
        f"pru = {V_mid_pru_mV} mV (dev={deviation_pru} %), "
        f"dev_rel = {deviation_rel} %",
    )
    print(f"DRAIN - VOut goal = 0 n, py = {V_out_pyt_raw} n, pru = {V_out_pru_raw} n")
    assert deviation_pyt < 3.0  # %
    assert deviation_pru < 3.0  # %
    assert deviation_rel < 1.0  # %
    assert V_out_pru_raw < 1  # output disabled
    assert V_out_pyt_raw < 1


@pytest.mark.hardware
@pytest.mark.src_name("direct")  # easiest case: v_inp == v_out, current not
def test_vsource_direct(
    pru_vsource: ShepherdDebug,
    pyt_vsource: VirtualSourceModel,
) -> None:
    for voltage_mV in [0, 100, 500, 1000, 2000, 3000, 4000, 4500]:
        V_pru_mV = pru_vsource.iterate_sampling(voltage_mV * 10**3, 0, 0) * 10**-3
        V_pyt_mV = pyt_vsource.iterate_sampling(voltage_mV * 10**3, 0, 0) * 10**-3
        print(
            f"DirectSRC - Inp = {voltage_mV} mV, "
            f"OutPru = {V_pru_mV:.3f} mV, "
            f"OutPy = {V_pyt_mV:.3f} mV",
        )
        assert difference_percent(V_pru_mV, voltage_mV, 50) < 3
        assert difference_percent(V_pyt_mV, voltage_mV, 50) < 3


@pytest.mark.hardware
@pytest.mark.src_name("diode+capacitor")
def test_vsource_diodecap(
    pru_vsource: ShepherdDebug,
    pyt_vsource: VirtualSourceModel,
) -> None:
    voltages_mV = [1000, 1100, 1500, 2000, 2500, 3000, 3500, 4000, 4500]

    # input with lower voltage should not change (open) output
    print("DiodeCap Input different Voltages BELOW capacitor voltage -> no change in output")
    V_pru_mV = pru_vsource.iterate_sampling(0, 0, 0) * 10**-3
    V_pyt_mV = pyt_vsource.iterate_sampling(0, 0, 0) * 10**-3
    A_inp_nA = 10**3
    for V_inp_mV in voltages_mV:
        if V_inp_mV > pyt_vsource.cfg_src.V_intermediate_init_mV:
            # selection must be below cap-init-voltage
            continue
        V_pru2_mV = pru_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
        V_pyt2_mV = pyt_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
        assert V_pru_mV == V_pru2_mV
        assert V_pyt_mV == V_pyt2_mV
        print(
            f" -> Inp = {V_inp_mV} mV, OutPru = {V_pru2_mV:.3f} mV, OutPy = {V_pyt2_mV:.3f} mV",
        )
    assert pyt_vsource.W_inp_fWs >= pyt_vsource.W_out_fWs
    assert pru_vsource.W_inp_fWs >= pru_vsource.W_out_fWs

    # drain Cap for next tests
    # NOTE: must be above V_intermediate_disable_threshold_mV
    V_target_mV = max(2200, pyt_vsource.cfg_src.V_intermediate_disable_threshold_mV + 100)
    A_out_nA = 10**6  # 1mA
    steps_needed = [0, 0]
    while pru_vsource.iterate_sampling(0, 0, A_out_nA) > V_target_mV * 10**3:
        steps_needed[0] += 1
    while pyt_vsource.iterate_sampling(0, 0, A_out_nA) > V_target_mV * 10**3:
        steps_needed[1] += 1
    print(
        f"DiodeCap Draining to {V_target_mV} mV needed {steps_needed} (pru, py) steps",
    )
    pru_vsource.W_inp_fWs = 0
    pru_vsource.W_out_fWs = 0
    pyt_vsource.W_inp_fWs = 0
    pyt_vsource.W_out_fWs = 0

    # zero current -> no change in output
    print("DiodeCap Input different Voltages but ZERO current -> no change in output")
    A_inp_nA = 0
    for V_inp_mV in voltages_mV:
        V_pru_mV = pru_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
        V_pyt_mV = pyt_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
        print(
            f" -> inp=0nA - Inp = {V_inp_mV} mV, "
            f"OutPru = {V_pru_mV:.3f} mV, "
            f"OutPy  = {V_pyt_mV:.3f} mV",
        )
        assert difference_percent(V_pru_mV, V_target_mV, 50) < 3
        assert difference_percent(V_pyt_mV, V_target_mV, 50) < 3

    # feed 200 mA -> fast charging cap
    A_inp_nA = 200 * 10**6
    print(f"DiodeCap input different voltage with {A_inp_nA * 1e-6} mA -> fast charge")
    V_diode_mV = pyt_vsource.cfg_src.V_input_drop_mV
    for V_inp_mV in voltages_mV:
        V_postDiode_mV = max(V_inp_mV - V_diode_mV, 0)  # diode drop voltage
        if V_postDiode_mV < V_pru_mV and V_postDiode_mV < V_pyt_mV:
            # input must be above cap-voltage
            continue
        for _ in range(100):
            V_pru_mV = pru_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
            V_pyt_mV = pyt_vsource.iterate_sampling(V_inp_mV * 10**3, A_inp_nA, 0) * 10**-3
        print(
            " -> inp=200mA - "
            f"Inp = {V_inp_mV} mV, "
            f"PostDiode = {V_postDiode_mV} mV, "
            f"OutPru = {V_pru_mV:.3f} mV, "
            f"OutPy = {V_pyt_mV:.3f} mV",
        )
        assert difference_percent(V_pru_mV, V_postDiode_mV, 50) < 3
        assert difference_percent(V_pyt_mV, V_postDiode_mV, 50) < 3

    # feed 5 mA, drain double of that -> output should settle at (V_in - V_drop)/2
    A_inp_nA = 5 * 10**6
    A_out_nA = 2 * A_inp_nA
    V_settle_mV = max(2200, pyt_vsource.cfg_src.V_intermediate_disable_threshold_mV + 100)
    V_inp_uV = (V_settle_mV * 2 + V_diode_mV) * 1e3
    assert V_inp_uV <= pyt_vsource.cfg_src.V_input_max_mV * 1e3
    # how many steps? charging took 9 steps at 200mA, so roughly 9 * 200 / (10 - 5)
    print(
        f"DiodeCap Drain #### Inp = 5mA @ {V_inp_uV / 10**3} mV , Out = 10mA "
        f"-> V_out should settle @ {V_settle_mV} mV ",
    )
    for _ in range(25):
        for _ in range(200):
            V_pru_mV = pru_vsource.iterate_sampling(V_inp_uV, A_inp_nA, A_out_nA) * 10**-3
            V_pyt_mV = pyt_vsource.iterate_sampling(V_inp_uV, A_inp_nA, A_out_nA) * 10**-3
        print(
            f" -> OutPru = {V_pru_mV:.3f} mV, OutPy = {V_pyt_mV:.3f} mV",
        )
    assert difference_percent(V_pru_mV, V_settle_mV, 50) < 3
    assert difference_percent(V_pyt_mV, V_settle_mV, 50) < 3
    assert pyt_vsource.W_inp_fWs >= pyt_vsource.W_out_fWs
    assert pru_vsource.W_inp_fWs >= pru_vsource.W_out_fWs


# TODO: add IO-Test with very small and very large values
# unit-test low and high power inputs 72W, 1W, 195 nA * 19 uV = 3.7 pW, what is with 1fW?
# unit test different converters
