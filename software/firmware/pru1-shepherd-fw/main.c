#include <gpio.h>
#include <pru_cfg.h>
#include <pru_iep.h>
#include <pru_intc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "iep.h"
#include "intc.h"
#include "msg_sys.h"

#include "commons.h"
#include "debug_routines.h"
#include "resource_table.h"
#include "shared_mem.h"
#include "shepherd_config.h"
#include "stdint_fast.h"

/* The Arm to Host interrupt for the timestamp event is mapped to Host interrupt 0 -> Bit 30 (see resource_table.h) */
#define HOST_INT_TIMESTAMP_MASK (1U << 30U)

// both pins have a LED
#define DEBUG_PIN0_MASK         BIT_SHIFT(P8_28)
#define DEBUG_PIN1_MASK         BIT_SHIFT(P8_30)

#define GPIO_BATOK              BIT_SHIFT(P8_29)
#define GPIO_BATOK_POS          (9u)

#define GPIO_MASK               (0x03FF)

#define SANITY_CHECKS           (0) // warning: costs performance, but is helpful for dev / debugging

/* overview for pin-mirroring - HW-Rev2.4b

pru_reg     name            BB_pin	sys_pin sys_reg
r31_00      TARGET_GPIO0    P8_45	P8_14, g0[26] -> 26
r31_01      TARGET_GPIO1    P8_46	P8_17, g0[27] -> 27
r31_02      TARGET_GPIO2    P8_43	P8_16, g1[14] -> 46
r31_03      TARGET_GPIO3    P8_44	P8_15, g1[15] -> 47
r31_04      TARGET_GPIO4    P8_41	P8_26, g1[29] -> 61
r31_05      TARGET_GPIO5    P8_42	P8_36, g2[16] -> 80
r31_06      TARGET_GPIO6    P8_39	P8_34, g2[17] -> 81
r31_07      TARGET_UART_RX  P8_40	P9_26, g0[14] -> 14
r31_08      TARGET_UART_TX  P8_27	P9_24, g0[15] -> 15
r30_09/out  TARGET_BAT_OK   P8_29	-

Note: this table is copied (for hdf5-reference) in commons.py
*/

enum SyncState
{
    IDLE,
    REPLY_PENDING
};


static inline bool_ft receive_sync_reply(struct ProtoMsg *const msg)
{
    if (msgsys_receive(msg))
    {
        switch (msg->type)
        {
            case MSG_SYNC_ROUTINE:
            case MSG_SYNC_RESET: return 1u; // hand to caller
            case MSG_TEST_ROUTINE:
                // pipeline-test for msg-system
                msgsys_send_status(MSG_TEST_ROUTINE, 3, 0u);
                // NOTE: msgsys_send() is deliberatedly NOT used
                //       (sync-reset does test pipeline)
                return 0u; // hide from caller
            default:
                msgsys_send_status(MSG_ERR_INVLD_CMD, msg->type, 0u);
                return 0u; // hide from caller
        }
    }
    return 0u;
}

/*
 * Here, we sample the GPIO pins from a connected sensor node. We repeatedly
 * poll the state via the R31 register and keep the last state in a static
 * variable. Once we detect a change, the new value (V1=4bit, V2=10bit) is written to the
 * corresponding buffer (which is managed by PRU0). The tricky part is the
 * synchronization between the PRUs to avoid inconsistent state, while
 * minimizing sampling delay
 */
static inline void check_gpio(const uint32_t last_sync_offset_ns)
{
    static uint32_t prev_gpio_status = 0x00;
    /*
	* Only continue if shepherd is running
	*/
    if (SHARED_MEM.shp_pru_state != STATE_RUNNING)
    {
        prev_gpio_status = 0x00;
        SHARED_MEM.gpio_pin_state =
                (read_r31() | (SHARED_MEM.vsource_batok_pin_value << GPIO_BATOK_POS)) & GPIO_MASK;
        return;
    }
    else if (SHARED_MEM.vsource_skip_gpio_logging) { return; }

    // batOK is on r30 (output), but that does not mean it is in R31
    // -> workaround: splice in SHARED_MEM.vsource_batok_pin_value
    const uint32_t gpio_status =
            (read_r31() | (SHARED_MEM.vsource_batok_pin_value << GPIO_BATOK_POS)) & GPIO_MASK;
    const uint32_t gpio_diff = gpio_status ^ prev_gpio_status;

    prev_gpio_status         = gpio_status;

    if (gpio_diff > 0)
    {
        DEBUG_GPIO_STATE_2;
        // local copy reduces reads to far-ram to current minimum
        volatile struct GPIOTrace *const buf_gpio = SHARED_MEM.buffer_gpio_ptr;
        const uint32_t                   cIDX     = SHARED_MEM.buffer_gpio_idx;

        /* Calculate timestamp of gpio event, cnt_val should be equal to offset_ns */
        // TODO: maybe just store TS and counter or even u32 sync-counter + u32 tick_counter
        buf_gpio->timestamp_ns[cIDX] = SHARED_MEM.last_sync_timestamp_ns + last_sync_offset_ns;
        buf_gpio->bitmask[cIDX]      = (uint16_t) gpio_status;

        if (cIDX >= BUFFER_GPIO_SIZE - 1u)
        {
            buf_gpio->idx_pru          = 0u;
            SHARED_MEM.buffer_gpio_idx = 0u;
        }
        else
        {
            buf_gpio->idx_pru          = cIDX + 1u;
            SHARED_MEM.buffer_gpio_idx = cIDX + 1u;
        }
    }
}


/* The firmware for synchronization/sample timing is based on a simple
 * event loop. There are three events:
 * 1) Interrupt from Linux kernel module
 * 2) Local IEP timer compare for sampling
 * 3) Local IEP timer wrapped
 *
 * Event 1:
 * The kernel module periodically timestamps its own clock and immediately
 * triggers an interrupt to PRU1. On reception of that interrupt we have
 * to timestamp our local IEP clock. We then send the local timestamp to the
 * kernel module. The kernel module runs a PI control loop
 * that minimizes the phase shift (and frequency deviation) by calculating a
 * correction factor that we apply to the base period of the IEP clock. This
 * resembles a Phase-Locked-Loop system. The kernel module sends the resulting
 * correction factor to PRU1. Ideally, Event 1 happens at the same
 * time as Event 3, i.e. our local clock should wrap at exactly the same time
 * as the Linux host clock. However, due to phase shifts and kernel timer
 * jitter, the two events typically happen with a small delay and in arbitrary
 * order.
 *
 * Event 2:
 * This is the main sample trigger that is used to trigger the actual sampling
 * on PRU0 by raising an interrupt. After every sample, we have to forward
 * the compare value, taking into account the current sampling period
 * (dynamically adapted by PLL). Also, we will only check for the controller
 * reply directly following this event in order to avoid sampling jitter.
 *
 * Event 3:
 *
 */

int32_t event_loop()
{
    /*
	 * Sync-Algorithm:
     * - the pru-clock is manipulated by max 1% to phase-lock with system
     * - sync-reply contains
     *    - value[0]: ticks-compensations for every step and
     *    - value[1]: remainder of compensation that is added per bresenham-algo
	 */
    uint32_t        compensation_counter   = 0u;
    uint32_t        compensation_increment = 0u;
    uint32_t        bresenham_counter      = 0u;
    uint32_t        bresenham_increment    = 0u;
    uint32_t        timer_ns               = 0u;
    bool_ft         host_int_early         = 0u;
    /* Tracks our local state, allowing to execute actions at the right time */
    struct ProtoMsg sync_repl;
    enum SyncState  sync_state          = IDLE;

    /* pru0 util monitor */
    uint32_t        pru0_tsample_ns_max = 0u;
    uint32_t        pru0_tsample_ns_sum = 0u;
    uint32_t        pru0_sample_count   = 0u;
    /* pru1 util monitor */
    uint32_t        pru1_tsample_ns_max = 0u;
    uint32_t        last_timer_ns       = 0u;
    bool_ft         transmit_util       = 0u;

    /* Configure timer */
    iep_set_cmp_val(IEP_CMP0, SYNC_INTERVAL_NS);   // 20 MTicks -> 100 ms
    iep_set_cmp_val(IEP_CMP1, SAMPLE_INTERVAL_NS); //  2 kTicks -> 10 us

    iep_enable_evt_cmp(IEP_CMP1); // sample-loop
    iep_clear_evt_cmp(IEP_CMP0);  // sync-loop

    /* Clear raw interrupt status from ARM host */
    INTC_CLEAR_EVENT(HOST_PRU_EVT_TIMESTAMP);

    /* sync with kernel module - wait for start-signal */
    DEBUG_STATE_1;
    msgsys_send(MSG_SYNC_RESET, 1u, 0u);
    struct ProtoMsg64 ts_repl;
    ts_repl.type = 0u;
    while (ts_repl.type != MSG_SYNC_RESET)
    {
        //if (msgsys_check_delivery()) msgsys_send(MSG_SYNC_RESET, 0u, 1u);
        __delay_cycles(1000u / TICK_INTERVAL_NS);
        receive_sync_reply((struct ProtoMsg *) &ts_repl);
    }
    // schedule hard-set of timestamp
    SHARED_MEM.next_sync_timestamp_ns = ts_repl.value;
    /* Wait for first timer interrupt from Linux host */
    DEBUG_STATE_2;
    while (!(read_r31() & HOST_INT_TIMESTAMP_MASK)) {}
    DEBUG_STATE_0;

    iep_start();

    while (1)
    {
#if DEBUG_LOOP_EN
        debug_loop_delays(SHARED_MEM.shp_pru_state);
#endif
        /* clock-skewing for sync */
        if (compensation_counter > 0)
        {
            iep_compensate();
            compensation_counter--;
        }

        timer_ns = iep_get_cnt_val();
        if (read_r31() & HOST_INT_TIMESTAMP_MASK) host_int_early = 1u;

        DEBUG_GPIO_STATE_1;
        check_gpio(timer_ns);
        DEBUG_GPIO_STATE_0;

        /* pru1 util monitoring */
        const uint32_t tsample_ns = timer_ns - last_timer_ns;
        if ((tsample_ns > pru1_tsample_ns_max) && (tsample_ns < (1u << 20u)))
        {
            pru1_tsample_ns_max = tsample_ns;
        }
        last_timer_ns = timer_ns;

        /* [Sync-Event 1] Check for interrupt from KernelModule to take counter snapshot */
        if (read_r31() & HOST_INT_TIMESTAMP_MASK)
        {
            if (!INTC_CHECK_EVENT(HOST_PRU_EVT_TIMESTAMP)) continue;

            /* Take timestamp of IEP if event just came up now */
            if (!host_int_early) timer_ns = iep_get_cnt_val();
            host_int_early = 0u;

            DEBUG_EVENT_STATE_3;
            /* Clear interrupt */
            INTC_CLEAR_EVENT(HOST_PRU_EVT_TIMESTAMP);

            if (sync_state == IDLE) sync_state = REPLY_PENDING;
            else
            {
                msgsys_send_status(MSG_ERR_SYNC_STATE_NOT_IDLE, sync_state, 0u);
                return 0;
            }
            msgsys_send(MSG_SYNC_ROUTINE, timer_ns, 0u);
            DEBUG_EVENT_STATE_0;
            continue; // for more regular gpio-sampling
        }

        /*  [Sync-Event 3] Timer compare 0 handle -> sync period is resetting */
        if (SHARED_MEM.cmp0_trigger_for_pru1)
        {
            DEBUG_EVENT_STATE_2;
            // reset trigger
            SHARED_MEM.cmp0_trigger_for_pru1 = 0;

            /* update clock compensation of sample-trigger */
            //iep_set_cmp_val(IEP_CMP1, 0); // TODO: is this correct?
            iep_enable_evt_cmp(IEP_CMP1);

            SHARED_MEM.next_sync_timestamp_ns += SYNC_INTERVAL_NS;

            if (sync_repl.value[0] >= SAMPLE_INTERVAL_TICKS)
            {
                // PRU is ahead, slow down
                compensation_increment = sync_repl.value[0] - SAMPLE_INTERVAL_TICKS;
                iep_set_compensation_inc(TICK_INTERVAL_NS - 1u);
            }
            else
            {
                // PRU is behind, speed up
                compensation_increment = SAMPLE_INTERVAL_TICKS - sync_repl.value[0];
                iep_set_compensation_inc(TICK_INTERVAL_NS + 1u);
            }
            bresenham_increment = sync_repl.value[1];
            bresenham_counter   = 0;

            /* trigger logging of util - separate for lower impact */
            transmit_util       = 1u;
            // TODO: add warning for when sync not idle?

            DEBUG_EVENT_STATE_0;
            continue; // for more regular gpio-sampling
        }

        /* [Sync-Event 2] Timer compare 1 handle -> analog sampling on pru0 */
        if (SHARED_MEM.cmp1_trigger_for_pru1)
        {
            /* prevent a race condition (cmp0_event has to happen before cmp1_event!) */
            if (SHARED_MEM.cmp0_trigger_for_pru1) continue;

            DEBUG_EVENT_STATE_1;
            // reset trigger
            SHARED_MEM.cmp1_trigger_for_pru1 = 0;

            // Update sample-trigger of timer
            uint32_t new_trigger             = iep_get_cmp_val(IEP_CMP1) + SAMPLE_INTERVAL_NS;
            if (new_trigger > SYNC_INTERVAL_NS) new_trigger -= SYNC_INTERVAL_NS;
            iep_set_cmp_val(IEP_CMP1, new_trigger);

            /* reactivate compensation with fixed point magic */
            compensation_counter += compensation_increment;
            bresenham_counter += bresenham_increment;
            /* If we are in compensation phase add one */
            if (bresenham_counter >= SAMPLES_PER_SYNC)
            {
                compensation_counter++;
                bresenham_counter -= SAMPLES_PER_SYNC;
            }

            /* If we are waiting for a reply from Linux kernel module */
            if (receive_sync_reply(&sync_repl) > 0)
            {
                sync_state = IDLE;
                //SHARED_MEM.next_sync_timestamp_ns = sync_repl.next_timestamp_ns;  // TODO
            }
            DEBUG_EVENT_STATE_0;
            continue; // for more regular gpio-sampling
        }

        /* remote gpio-triggering for pru0 */
        if (SHARED_MEM.vsource_batok_trigger_for_pru1)
        {
            if (SHARED_MEM.vsource_batok_pin_value)
            {
                GPIO_ON(GPIO_BATOK);
                DEBUG_PGOOD_STATE_1;
            }
            else
            {
                GPIO_OFF(GPIO_BATOK);
                DEBUG_PGOOD_STATE_0;
            }
            SHARED_MEM.vsource_batok_trigger_for_pru1 = false;
            continue;
        }

        /* transmit pru0-util, current design puts this in fresh/next buffer */
        if (transmit_util)
        {
            const uint32_t idx                                   = SHARED_MEM.buffer_util_idx;
            // TODO: add timestamp
            SHARED_MEM.buffer_util_ptr->pru0_tsample_ns_sum[idx] = pru0_tsample_ns_sum;
            SHARED_MEM.buffer_util_ptr->pru0_tsample_ns_max[idx] = pru0_tsample_ns_max;
            SHARED_MEM.buffer_util_ptr->pru0_sample_count[idx]   = pru0_sample_count;
            SHARED_MEM.buffer_util_ptr->pru1_tsample_ns_max[idx] = pru1_tsample_ns_max;
            SHARED_MEM.buffer_util_ptr->idx_pru                  = idx;
            transmit_util                                        = 0u;
            pru0_tsample_ns_sum                                  = 0u;
            pru0_tsample_ns_max                                  = 0u;
            pru0_sample_count                                    = 0u;
            pru1_tsample_ns_max                                  = 0u;
            if (idx < BUFFER_UTIL_SIZE - 1u) { SHARED_MEM.buffer_util_idx = idx + 1u; }
            else { SHARED_MEM.buffer_util_idx = 0u; }
            continue;
        }

        /* pru0 util monitoring */
        // TODO: move to PRU0?
        if (SHARED_MEM.pru0_ns_per_sample != IDX_OUT_OF_BOUND)
        {
            if (SHARED_MEM.pru0_ns_per_sample < (1u << 20u))
            {
                if (SHARED_MEM.pru0_ns_per_sample > pru0_tsample_ns_max)
                {
                    pru0_tsample_ns_max = SHARED_MEM.pru0_ns_per_sample;
                }
                pru0_tsample_ns_sum += SHARED_MEM.pru0_ns_per_sample;
                pru0_sample_count += 1;
            }
            SHARED_MEM.pru0_ns_per_sample = IDX_OUT_OF_BOUND;
            continue;
        }
    }
}

int main(void)
{
    /* Allow OCP primary port access by the PRU so the PRU can read external memories */
    CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;
    DEBUG_STATE_0;

    /* Enable 'timestamp' interrupt from ARM host */
    CT_INTC.EISR_bit.EN_SET_IDX = HOST_PRU_EVT_TIMESTAMP;

    /* wait until pru0 is ready */
    while (SHARED_MEM.cmp0_trigger_for_pru1 == 0u) __delay_cycles(10);
    SHARED_MEM.cmp0_trigger_for_pru1 = 0u;
    msgsys_init();

reset:
    msgsys_send_status(MSG_STATUS_RESTARTING_ROUTINE, 1u, 0u);

    DEBUG_STATE_0;
    iep_init();
    iep_set_increment(TICK_INTERVAL_NS);
    iep_reset();

    event_loop();
    goto reset;
}
