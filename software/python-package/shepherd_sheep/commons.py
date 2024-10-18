"""
shepherd.commons
~~~~~
Defines details of the data exchange protocol between PRU0 and the python code.
The various parameters need to be the same on both sides. Refer to the
corresponding implementation in `software/firmware/include/commons.h`

"""

# ############################################################################
# PRU - CONFIG  ##############################################################
# ############################################################################

# The IEP of the PRUs is clocked with 200 MHz -> 5 nanoseconds per tick
TICK_INTERVAL_NS: int = 5
SAMPLE_INTERVAL_NS: int = 10_000
SAMPLE_INTERVAL_S: float = SAMPLE_INTERVAL_NS * 1e-9
SAMPLE_INTERVAL_TICKS: int = SAMPLE_INTERVAL_NS // TICK_INTERVAL_NS
SYNC_INTERVAL_NS: int = 100_000_000
SYNC_INTERVAL_TICKS: int = SYNC_INTERVAL_NS // TICK_INTERVAL_NS
SAMPLES_PER_SYNC: int = SYNC_INTERVAL_NS // SAMPLE_INTERVAL_NS

# Length of buffer for storing harvest & emulation, gpio- and util- data
BUFFER_IV_SIZE: int = 1_000_000  # for ~10s
BUFFER_IV_INTERVAL_MS: int = BUFFER_IV_SIZE * SAMPLE_INTERVAL_NS // 10**6
BUFFER_GPIO_SIZE: int = 1_000_000
BUFFER_UTIL_SIZE: int = 400
IDX_OUT_OF_BOUND: int = 0xFFFFFFFF

CANARY_VALUE_U32: int = 0xDEBAC1E5  # read as '0-debacles'

# ############################################################################
# PRU - COMMONS  #############################################################
# ############################################################################

MSG_PRU0_ENTER_ROUTINE = 0x12
MSG_PRU0_EXIT_ROUTINE = 0x13

MSG_PGM_ERROR_WRITE = 0x93  # val0: addr, val1: data
MSG_PGM_ERROR_VERIFY = 0x94  # val0: addr, val1: data(original)
MSG_PGM_ERROR_PARSE = 0x96  # val0: ihex_return

MSG_DBG_ADC = 0xA0
MSG_DBG_DAC = 0xA1
MSG_DBG_GPI = 0xA2
MSG_DBG_GP_BATOK = 0xA3
MSG_DBG_PRINT = 0xA6

MSG_DBG_VSRC_P_INP = 0xA8
MSG_DBG_VSRC_P_OUT = 0xA9
MSG_DBG_VSRC_V_CAP = 0xAA
MSG_DBG_VSRC_V_OUT = 0xAB
MSG_DBG_VSRC_INIT = 0xAC
MSG_DBG_VSRC_CHARGE = 0xAD
MSG_DBG_VSRC_DRAIN = 0xAE
MSG_DBG_FN_TESTS = 0xAF
MSG_DBG_VSRC_HRV_P_INP = 0xB1

# NOTE: below messages are exclusive to kernel space
MSG_STATUS_RESTARTING_ROUTINE = 0xF0

pru_errors: dict[int, str] = {
    0xE0: "[ERR_INVLD_CMD] PRU received an invalid command",
    0xE1: "[ERR_MEM_CORRUPTION] PRU received a faulty msg.id from kernel",
    0xE2: "[ERR_BACKPRESSURE] PRUs msg-buffer to kernel still full",
    0xE3: "[ERR_TIMESTAMP] PRU received a faulty timestamp",
    0xE4: "[ERR_CANARY] PRU detected a dead canary",
    0xE5: "[ERR_SYNC_STATE_NOT_IDLE] PRUs sync-state not idle at host interrupt",
    0xE6: "[ERR_VALUE] PRUs msg-content failed test",
    0xE7: "[ERR_SAMPLE_MODE] no valid sample mode found",
    0xE8: "[ERR_HRV_ALGO] no valid hrv algo found",
}

# fmt: off
# ruff: noqa: E241, E501
GPIO_LOG_BIT_POSITIONS = {
    0: {"pru_reg": "r31_00", "name": "tgt_gpio0",   "bb_pin": "P8_45", "sys_pin": "P8_14", "sys_reg": "26"},
    1: {"pru_reg": "r31_01", "name": "tgt_gpio1",   "bb_pin": "P8_46", "sys_pin": "P8_17", "sys_reg": "27"},
    2: {"pru_reg": "r31_02", "name": "tgt_gpio2",   "bb_pin": "P8_43", "sys_pin": "P8_16", "sys_reg": "14"},
    3: {"pru_reg": "r31_03", "name": "tgt_gpio3",   "bb_pin": "P8_44", "sys_pin": "P8_15", "sys_reg": "15"},
    4: {"pru_reg": "r31_04", "name": "tgt_gpio4",   "bb_pin": "P8_41", "sys_pin": "P8_26", "sys_reg": "29"},
    5: {"pru_reg": "r31_05", "name": "tgt_gpio5",   "bb_pin": "P8_42", "sys_pin": "P8_36", "sys_reg": "16"},
    6: {"pru_reg": "r31_06", "name": "tgt_gpio6",   "bb_pin": "P8_39", "sys_pin": "P8_34", "sys_reg": "17"},
    7: {"pru_reg": "r31_07", "name": "tgt_uart_rx", "bb_pin": "P8_40", "sys_pin": "P9_26", "sys_reg": "14"},
    8: {"pru_reg": "r31_08", "name": "tgt_uart_tx", "bb_pin": "P8_27", "sys_pin": "P9_24", "sys_reg": "15"},
    9: {"pru_reg": "r31_09", "name": "tgt_bat_ok",  "bb_pin": "P8_29", "sys_pin": "",      "sys_reg": ""},
}
# Note: this table is copied (for hdf5-reference) from pru1/main.c, HW-Rev2.4b
# Note: datalib has gpio-models + data! this lives now in
#       shepherd_core/shepherd_core/data_models/testbed/gpio_fixture.yaml
# fmt: on
