#include <stdint.h>

#include <pru_cfg.h>

#include "gpio.h"
#include "iep.h"
#include "intc.h"
#include "resource_table_def.h"
#include "stdint_fast.h"

#include "commons.h"
#include "hw_config.h"
#include "msg_sys.h"
#include "shared_mem.h"
#include "shepherd_config.h"

#include "calibration.h"
#include "fw_config.h"
#include "sampling.h"
#include "virtual_converter.h"


/* PRU0 Feature Selection */
//#define ENABLE_DEBUG_MATH_FN	// reduces firmware by ~9 kByte
#define ENABLE_DBG_VSOURCE // disabling increases firmware-size? (~150 byte)

#ifdef ENABLE_DEBUG_MATH_FN
  #include "math64_safe.h"
#endif

#ifdef ENABLE_DBG_VSOURCE
  #include "virtual_harvester.h"
#endif


#ifdef ENABLE_DEBUG_MATH_FN
uint64_t debug_math_fns(const uint32_t factor, const uint32_t mode)
{
    uint64_t       result = 0;
    const uint64_t f2     = (uint64_t) factor + ((uint64_t) (factor) << 32u);
    const uint64_t f3     = factor - 10;
    GPIO_TOGGLE(DEBUG_PIN1_MASK);

    if (mode == 1)
    {
        const uint32_t r32 = factor * factor;
        result             = r32;
    } // ~ 28 ns, limits 0..65535
    //else if (mode == 2) result = factor * factor; // ~ 34 ns, limits 0..65535
    else if (mode == 3)
        result = (uint64_t) factor * factor; // ~ 42 ns, limits 0..65535 -> wrong behavior!!!
    else if (mode == 4)
        result = factor * (uint64_t) factor; // ~ 48 ns, limits 0..(2^32-1) -> works fine?
    else if (mode == 5)
        result = (uint64_t) factor * (uint64_t) factor; // ~ 54 ns, limits 0..(2^32-1)
    else if (mode == 6)
        result = ((uint64_t) factor) * ((uint64_t) factor); // ~ 54 ns, limits 0..(2^32-1)
    else if (mode == 11)
        result =
                factor *
                f2; // ~ 3000 - 4800 - 6400 ns, limits 0..(2^32-1) -> time depends on size (4, 16, 32 bit)
    else if (mode == 12) result = f2 * factor;                  // same as above
    else if (mode == 13) result = f2 * f2;                      // same as above
    else if (mode == 14) result = mul64(f2, f2);                //
    else if (mode == 15) result = mul64(factor, f2);            //
    else if (mode == 16) result = mul64(f2, factor);            //
    else if (mode == 17) result = mul64((uint64_t) factor, f2); //
    else if (mode == 18) result = mul64(f2, (uint64_t) factor); //
    else if (mode == 21) result = factor + f2;           // ~ 84 ns, limits 0..(2^31-1) or (2^63-1)
    else if (mode == 22) result = f2 + factor;           // ~ 90 ns, limits 0..(2^31-1) or (2^63-1)
    else if (mode == 23) result = f2 + f3;               // ~ 92 ns, limits 0..(2^31-1) or (2^63-1)
    else if (mode == 24) result = f2 + 1111ull;          // ~ 102 ns, overflow at 2^32
    else if (mode == 25) result = 1111ull + f2;          // ~ 110 ns, overflow at 2^32
    else if (mode == 26) result = f2 + (uint64_t) 1111u; //
    else if (mode == 27) result = add64(f2, f3);         //
    else if (mode == 28) result = add64(factor, f3);     //
    else if (mode == 29) result = add64(f3, factor);     //
    else if (mode == 31) result = factor - f3;           // ~ 100 ns, limits 0..(2^32-1)
    else if (mode == 32) result = f2 - factor;           // ~ 104 ns, limits 0..(2^64-1)
    else if (mode == 33) result = f2 - f3;               // same
    else if (mode == 41) result = ((uint64_t) (factor) << 32u); // ~ 128 ns, limit (2^32-1)
    else if (mode == 42) result = (f2 >> 32u);                  // ~ 128 ns, also works
    else if (mode == 51) result = get_size_in_bits(factor);     //
    GPIO_TOGGLE(DEBUG_PIN1_MASK);
    return result;
}
#endif

static bool_ft handle_kernel_com()
{
    struct ProtoMsg msg_in;

    if (msgsys_receive(&msg_in) == 0) return 1u;

    // TODO: remove debug mode? not needed anymore with py-to-c-interface
    if ((SHARED_MEM.shp_pru0_mode == MODE_DEBUG) && (SHARED_MEM.shp_pru_state == STATE_RUNNING))
    {
        uint32_t res;
#ifdef ENABLE_DEBUG_MATH_FN
        uint64_t res64;
#endif
        switch (msg_in.type)
        {

            case MSG_DBG_ADC:
                res = sample_dbg_adc(msg_in.value[0]);
                msgsys_send(MSG_DBG_ADC, res, 0);
                return 1u;

            case MSG_DBG_DAC: // TODO: better name: MSG_CTRL_DAC
                sample_dbg_dac(msg_in.value[0]);
                return 1u;

            case MSG_DBG_GP_BATOK: set_batok_pin(msg_in.value[0] > 0); return 1U;

            case MSG_DBG_GPI: msgsys_send(MSG_DBG_GPI, SHARED_MEM.gpio_pin_state, 0); return 1U;

#if (defined(ENABLE_DBG_VSOURCE) && defined(EMU_SUPPORT))
            case MSG_DBG_VSRC_HRV_P_INP:
                sample_ivcurve_harvester(&msg_in.value[0], &msg_in.value[1]);
                // fall through
            case MSG_DBG_VSRC_P_INP: // TODO: these can be done with normal emulator instantiation
                // TODO: get rid of these test, but first allow lib-testing of converter, then full virtual_X pru-test with artificial inputs
                converter_calc_inp_power(msg_in.value[0], msg_in.value[1]);
                msgsys_send(MSG_DBG_VSRC_P_INP, (uint32_t) (get_P_input_fW() >> 32u),
                            (uint32_t) get_P_input_fW());
                return 1u;

            case MSG_DBG_VSRC_P_OUT:
                converter_calc_out_power(msg_in.value[0]);
                msgsys_send(MSG_DBG_VSRC_P_OUT, (uint32_t) (get_P_output_fW() >> 32u),
                            (uint32_t) get_P_output_fW());
                return 1u;

            case MSG_DBG_VSRC_V_CAP:
                converter_update_cap_storage();
                msgsys_send(MSG_DBG_VSRC_V_CAP, get_V_intermediate_uV(), 0);
                return 1u;

            case MSG_DBG_VSRC_V_OUT:
                res = converter_update_states_and_output();
                msgsys_send(MSG_DBG_VSRC_V_OUT, res, 0);
                return 1u;

            case MSG_DBG_VSRC_INIT:
                calibration_initialize();
                converter_initialize();
                harvester_initialize();
                msgsys_send(MSG_DBG_VSRC_INIT, 0, 0);
                return 1u;

            case MSG_DBG_VSRC_CHARGE:
                converter_calc_inp_power(msg_in.value[0], msg_in.value[1]);
                converter_calc_out_power(0u);
                converter_update_cap_storage();
                res = converter_update_states_and_output();
                msgsys_send(MSG_DBG_VSRC_CHARGE, get_V_intermediate_uV(), res);
                return 1u;

            case MSG_DBG_VSRC_DRAIN:
                converter_calc_inp_power(0u, 0u);
                converter_calc_out_power(msg_in.value[0]);
                converter_update_cap_storage();
                res = converter_update_states_and_output();
                msgsys_send(MSG_DBG_VSRC_DRAIN, get_V_intermediate_uV(), res);
                return 1u;
#endif // ENABLE_DBG_VSOURCE

#ifdef ENABLE_DEBUG_MATH_FN
            case MSG_DBG_FN_TESTS:
                res64 = debug_math_fns(msg_in.value[0], msg_in.value[1]);
                msgsys_send(MSG_DBG_FN_TESTS, (uint32_t) (res64 >> 32u), (uint32_t) res64);
                return 1u;
#endif //ENABLE_DEBUG_MATH_FN

            default:
                msgsys_send(MSG_ERR_INVLD_CMD, msg_in.type, 0u);
                return 0U;
                // TODO: there are two msg_send() in here that send MSG_ERR
        }
    }
    else if (msg_in.type == MSG_TEST_ROUTINE)
    {
        if (msg_in.value[0] == 1)
        {
            // pipeline-test for msg-system
            msgsys_send(MSG_TEST_ROUTINE, msg_in.value[0], 0u);
        }
        else if (msg_in.value[0] == 2)
        {
            // pipeline-test for msg-system
            msgsys_send_status(MSG_TEST_ROUTINE, msg_in.value[0], 0u);
        }
        else { msgsys_send(MSG_ERR_INVLD_CMD, msg_in.type, 0u); }
    }
    return 0u;
}

void event_loop()
{
    uint32_t iep_tmr_cmp_sts;
    uint64_t last_sample_timestamp_ns = 0u;

    while (1)
    {
        // take a snapshot of current triggers until something happens -> ensures prioritized handling
        // edge case: sample0 @cnt=0, cmp0&1 trigger, but cmp0 needs to get handled before cmp1
        // NOTE: pru1 manages the irq, but pru0 reacts to it directly -> less jitter
        while (!(iep_tmr_cmp_sts = iep_get_tmr_cmp_sts())); // read iep-reg -> 12 cycles, 60 ns
        // TODO: could just check, and assign later

        // pre-trigger for extra low jitter and up-to-date samples, ADCs will be triggered to sample on rising edge
        if (iep_tmr_cmp_sts & IEP_CMP1_MASK) // LogicAnalyzer: 104 ns
        {
            GPIO_OFF(SPI_CS_ADCs_MASK);
            // determine minimal low duration for starting sampling -> datasheet not clear, but 15-50 ns could be enough
            __delay_cycles(100 / 5);
            GPIO_ON(SPI_CS_ADCs_MASK);
            // TODO: make sure that 1 us passes before trying to get that value
        }
        // timestamp pru0 to monitor utilization
        const uint32_t timer_start = iep_get_cnt_val();

        // Activate new Buffer-Cycle & Ensure proper execution order on pru1 -> cmp0_event (E2) must be handled before cmp1_event (E3)!
        if (iep_tmr_cmp_sts & IEP_CMP0_MASK) // LogicAnalyzer: 204 ns
        {
            // TODO: move back to PRU1 - not needed here anymore?
            GPIO_TOGGLE(DEBUG_PIN0_MASK);
            /* Clear Timer Compare 0 */
            iep_clear_evt_cmp(IEP_CMP0);
            /* update timestamp
            *  NOTE: incrementing of next_sync_timestamp_ns is done by PRU1
            * */
            SHARED_MEM.last_sync_timestamp_ns = SHARED_MEM.next_sync_timestamp_ns;
            /* orward interrupt to pru1 */
            SHARED_MEM.cmp0_trigger_for_pru1  = 1u;
            /* go dark if not running */
            if (SHARED_MEM.shp_pru_state != STATE_RUNNING) GPIO_OFF(DEBUG_PIN0_MASK);
        }

        // Sample and receive messages
        if (iep_tmr_cmp_sts & IEP_CMP1_MASK)
        {
            /* Clear Timer Compare 1 and forward it to pru1 */
            SHARED_MEM.cmp1_trigger_for_pru1 = 1u;
            iep_clear_evt_cmp(IEP_CMP1); // CT_IEP.TMR_CMP_STS.bit1

            /* update current time (if not already done) */
            if (iep_tmr_cmp_sts & IEP_CMP0_MASK)
                last_sample_timestamp_ns = SHARED_MEM.last_sync_timestamp_ns;
            else last_sample_timestamp_ns += SAMPLE_INTERVAL_NS;

            /* The actual sampling takes place here */
            if (SHARED_MEM.shp_pru_state == STATE_RUNNING)
            {
                GPIO_ON(DEBUG_PIN1_MASK);
                sample();
                GPIO_OFF(DEBUG_PIN1_MASK);

                /* counter write & incrementation */
                const uint32_t idx                              = SHARED_MEM.buffer_iv_idx;
                SHARED_MEM.buffer_iv_out_ptr->timestamp_ns[idx] = last_sample_timestamp_ns;
                SHARED_MEM.buffer_iv_out_ptr->idx_pru           = idx;

                if (idx >= BUFFER_IV_SIZE - 1u) { SHARED_MEM.buffer_iv_idx = 0u; }
                else { SHARED_MEM.buffer_iv_idx = idx + 1u; }
            }

            /* Did the Linux kernel module ask for reset? */
            if (SHARED_MEM.shp_pru_state == STATE_RESET) return;
            else // LogicAnalyzer: 148 ns for just checking, till loop-restart
            {
                /* only handle kernel-communications if this is not the last sample */
                GPIO_ON(DEBUG_PIN1_MASK);
                handle_kernel_com();
            }
        }

        /* record loop-duration, compensate for CS -> gets further processed by pru1 */
        SHARED_MEM.pru0_ns_per_sample = iep_get_cnt_val() - timer_start + 110u;
        GPIO_OFF(DEBUG_PIN1_MASK);
    }
}

int main(void)
{
    GPIO_OFF(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);


    /* Initialize struct-Members Part A, must come first - this blocks PRU1! */
    SHARED_MEM.cmp0_trigger_for_pru1 = 0u; // Reset Token-System to init-values
    SHARED_MEM.cmp1_trigger_for_pru1 = 0u;

    /* establish safety-boundary around critical sections */
    SHARED_MEM.canary1               = CANARY_VALUE_U32;
    SHARED_MEM.canary2               = CANARY_VALUE_U32;
    SHARED_MEM.canary3               = CANARY_VALUE_U32;

    /* Initialize all struct-Members Part B */
    SHARED_MEM.buffer_iv_inp_ptr     = (struct IVTraceInp *) resourceTable.shared_memory.pa;
    SHARED_MEM.buffer_iv_out_ptr =
            (struct IVTraceOut *) (resourceTable.shared_memory.pa + sizeof(struct IVTraceInp));
    SHARED_MEM.buffer_gpio_ptr =
            (struct GPIOTrace *) (resourceTable.shared_memory.pa + sizeof(struct IVTraceInp) +
                                  sizeof(struct IVTraceOut));
    SHARED_MEM.buffer_util_ptr =
            (struct UtilTrace *) (resourceTable.shared_memory.pa + sizeof(struct IVTraceInp) +
                                  sizeof(struct IVTraceOut) + sizeof(struct GPIOTrace));

    SHARED_MEM.buffer_size                       = resourceTable.shared_memory.len;
    SHARED_MEM.buffer_iv_inp_size                = sizeof(struct IVTraceInp);
    SHARED_MEM.buffer_iv_out_size                = sizeof(struct IVTraceOut);
    SHARED_MEM.buffer_gpio_size                  = sizeof(struct GPIOTrace);
    SHARED_MEM.buffer_util_size                  = sizeof(struct UtilTrace);

    SHARED_MEM.buffer_iv_inp_sys_idx             = IDX_OUT_OF_BOUND;
    SHARED_MEM.buffer_iv_inp_ptr->idx_sys        = IDX_OUT_OF_BOUND;
    SHARED_MEM.buffer_iv_inp_ptr->idx_pru        = IDX_OUT_OF_BOUND;
    SHARED_MEM.buffer_iv_out_ptr->idx_pru        = IDX_OUT_OF_BOUND;
    SHARED_MEM.buffer_gpio_ptr->idx_pru          = IDX_OUT_OF_BOUND;
    SHARED_MEM.buffer_util_ptr->idx_pru          = IDX_OUT_OF_BOUND;

    /* accumulated length is documented in resourceTable.shared_memory.len */

    SHARED_MEM.dac_auxiliary_voltage_raw         = 0u;
    SHARED_MEM.shp_pru_state                     = STATE_IDLE;
    SHARED_MEM.shp_pru0_mode                     = MODE_NONE;

    SHARED_MEM.last_sync_timestamp_ns            = 0u;
    SHARED_MEM.next_sync_timestamp_ns            = 0u;

    SHARED_MEM.gpio_pin_state                    = 0u;

    SHARED_MEM.vsource_batok_trigger_for_pru1    = false;
    SHARED_MEM.vsource_batok_pin_value           = false;

    /* minimal init for these structs to make them safe */
    /* NOTE: more inits are done in kernel */
    SHARED_MEM.converter_settings.converter_mode = 0u;
    SHARED_MEM.harvester_settings.algorithm      = 0u;
    SHARED_MEM.programmer_ctrl.state             = PRG_STATE_IDLE;
    SHARED_MEM.programmer_ctrl.target            = PRG_TARGET_NONE;

    msgsys_init();

    /* Allow OCP primary port access by the PRU so the PRU can read external memories */
    CT_CFG.SYSCFG_bit.STANDBY_INIT   = 0u;

    /* allow PRU1 to enter event-loop */
    SHARED_MEM.cmp0_trigger_for_pru1 = 1u;

reset:
    msgsys_send(MSG_STATUS_RESTARTING_ROUTINE, 0u, SHARED_MEM.shp_pru0_mode);
    SHARED_MEM.pru0_ns_per_sample = 0u;

    SHARED_MEM.buffer_iv_idx      = 0u;
    SHARED_MEM.buffer_gpio_idx    = 0u;
    SHARED_MEM.buffer_util_idx    = 0u;

    GPIO_ON(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);
    sample_init();
    GPIO_OFF(DEBUG_PIN0_MASK | DEBUG_PIN1_MASK);

    SHARED_MEM.vsource_skip_gpio_logging = false;

    SHARED_MEM.shp_pru_state             = STATE_IDLE;

    event_loop();

    goto reset;
}
