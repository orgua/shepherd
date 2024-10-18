#include <asm/io.h> /* gpio0-access */
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/slab.h> /* kmalloc */

#include "_shepherd_config.h"
#include "pru_mem_interface.h"
#include "pru_sync_control.h"

#define U32T_MAX (0xFFFFFFFFu)
static uint32_t             sys_ts_over_wrap_ns = U32T_MAX;
static uint64_t             ts_upcoming_ns      = 0;
static uint64_t             ts_previous_ns      = 0; /* for plausibility-check */

static enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart);
static enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart);

/* Timer to trigger fast sync_loop */
struct hrtimer              trigger_loop_timer;
struct hrtimer              sync_loop_timer;
static u8                   timers_active = 0u;

/* debug gpio - gpio0[22] - P8_19 - BUTTON_LED is suitable */
static void __iomem        *gpio0set      = NULL;
static void __iomem        *gpio0clear    = NULL;
#define GPIO_P819_SET writel(0b1u << 22u, gpio0set);
#define GPIO_P819_CLR writel(0b1u << 22u, gpio0clear);

/* series of halving sleep cycles, sleep less coming slowly near a total of 100ms of sleep */
static const unsigned int TIMER_STEPS_NS[] = {20000000u, 20000000u, 20000000u, 20000000u, 10000000u,
                                              5000000u,  2000000u,  1000000u,  500000u,   200000u,
                                              100000u,   50000u,    20000u};
/* TODO: sleep 100ms - 20 us
* TODO: above halvings sum up to 98870 us */
static const size_t       TIMER_STEPS_NS_SIZE = sizeof(TIMER_STEPS_NS) / sizeof(TIMER_STEPS_NS[0]);
//static unsigned int step_pos = 0;

static uint32_t           info_count          = 6666; /* >6k triggers explanation-message once */
struct sync_data_s        sync_state;
static u8                 init_done       = 0;

/* PI-Tuning
 * - float is hard to use in kernel 4.19, so i32 it is ...
 * - Ki already includes 0.1 s sample_interval
 * - values are inverted (inv) and shifted by 10 bit (n10)
 */
const int32_t             kp_inv_n10_init = 1024 / 1.1;
const int32_t             ki_inv_n10_init = 1024 / (0.9 * 0.1); //

/* Benchmark high-res busy-wait - RESULTS:
 * - ktime_get                  99.6us   215n   463ns/call
 * - ktime_get_real             100.3us  302n   332ns/call -> current best performer (4.19.94-ti-r73)
 * - ktime_get_ns               100.2us  257n   389ns/call
 * - ktime_get_real_ns          131.5us  247n   532ns
 * - ktime_get_raw              99.3us   273n   364ns
 * - ktime_get_real_fast_ns     90.0us   308n   292ns
 * - increment-loop             825us    100k   8.25ns/iteration
 */

void                      sync_exit(void)
{
    hrtimer_cancel(&trigger_loop_timer);
    hrtimer_cancel(&sync_loop_timer);
    if (gpio0clear != NULL)
    {
        iounmap(gpio0clear);
        gpio0clear = NULL;
    }
    if (gpio0set != NULL)
    {
        iounmap(gpio0set);
        gpio0set = NULL;
    }
    init_done = 0;
    printk(KERN_INFO "shprd.sync: pru-sync-system exited");
}

int sync_init(void)
{
    uint32_t ret_value;

    if (init_done)
    {
        printk(KERN_ERR "shprd.sync: pru-sync-system init requested -> can't init twice!");
        return -1;
    }
    sync_reset();

    gpio0clear = ioremap(0x44E07000 + 0x190, 4); // BBB, GPIO0
    gpio0set   = ioremap(0x44E07000 + 0x194, 4);

    /* timer for trigger */
    hrtimer_init(&trigger_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    // TODO: HRTIMER_MODE_ABS_HARD wanted, but _HARD not defined in 4.19 (without -RT)
    trigger_loop_timer.function = &trigger_loop_callback;

    /* timer for Sync-Loop */
    hrtimer_init(&sync_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    sync_loop_timer.function = &sync_loop_callback;

    init_done                = 1;
    printk(KERN_INFO "shprd.sync: pru-sync-system initialized (wanted: hres, abs, hard)");

    ret_value = hrtimer_is_hres_active(&trigger_loop_timer);
    printk("%sshprd.sync: trigger_hrtimer.hres    = %d", ret_value == 1 ? KERN_INFO : KERN_ERR,
           ret_value);
    ret_value = trigger_loop_timer.is_rel;
    printk("%sshprd.sync: trigger_hrtimer.is_rel  = %d", ret_value == 0 ? KERN_INFO : KERN_ERR,
           ret_value);
    ret_value = trigger_loop_timer.is_soft;
    printk("%sshprd.sync: trigger_hrtimer.is_soft = %d", ret_value == 0 ? KERN_INFO : KERN_ERR,
           ret_value);
    //printk(KERN_INFO "shprd.sync: trigger_hrtimer.is_hard = %d", trigger_loop_timer.is_hard); // needs kernel 5.4+
    sync_start();
    return 0;
}

void sync_pause(void)
{
    if (!timers_active)
    {
        printk(KERN_ERR "shprd.sync: pru-sync-system pause requested -> but wasn't running!");
        return;
    }
    timers_active = 0;
    printk(KERN_INFO "shprd.sync: pru-sync-system paused");
}

void sync_start(void)
{
    uint32_t       ns_over_wrap;
    uint64_t       ns_to_next_trigger;
    /* Timestamp system clock */
    const uint64_t ts_now_ns = ktime_get_real_ns();

    if (!init_done)
    {
        printk(KERN_ERR "shprd.sync: pru-sync-system start requested without prior init!");
        return;
    }
    if (timers_active)
    {
        printk(KERN_ERR "shprd.sync: pru-sync-system start requested -> but already running!");
        return;
    }

    sync_reset();

    div_u64_rem(ts_now_ns, SYNC_INTERVAL_NS, &ns_over_wrap);
    if (ns_over_wrap > (SYNC_INTERVAL_NS / 2))
        ns_to_next_trigger = 2 * SYNC_INTERVAL_NS - ns_over_wrap;
    else ns_to_next_trigger = SYNC_INTERVAL_NS - ns_over_wrap;

    hrtimer_start(&sync_loop_timer, ns_to_ktime(ts_now_ns + 1000000u), HRTIMER_MODE_ABS);
    printk(KERN_INFO "shprd.sync: pru-sync-system started");
    timers_active = 1;
}

void sync_reset(void)
{
    sync_state.output_sum   = 0;
    sync_state.input_smooth = 20000000;
    sync_state.k_state      = 0;
    sync_state.kp_inv_n10   = kp_inv_n10_init;
    sync_state.ki_inv_n10   = ki_inv_n10_init;
}

void trigger_loop_start(void)
{
    struct ProtoMsg64 sync_reply64;
    const uint64_t    ts_now_ns = ktime_get_real_ns();

    // initial hard reset of timestamp on PRU on 0.1s - grid
    div_u64_rem(ts_now_ns, SYNC_INTERVAL_NS, &sys_ts_over_wrap_ns);
    ts_upcoming_ns     = ts_now_ns + SYNC_INTERVAL_NS - sys_ts_over_wrap_ns;

    sync_reply64.type  = MSG_SYNC_RESET;
    sync_reply64.value = ts_upcoming_ns;
    pru1_comm_send_sync_reply((struct ProtoMsg *) &sync_reply64);

    if (sys_ts_over_wrap_ns > 1000u) ndelay(sys_ts_over_wrap_ns - 1000u);
    mem_interface_trigger(HOST_PRU_EVT_TIMESTAMP);

    ts_previous_ns = 0;
    ts_upcoming_ns += SYNC_INTERVAL_NS;
    sys_ts_over_wrap_ns = 0;
    hrtimer_start(&trigger_loop_timer, ns_to_ktime(ts_upcoming_ns),
                  HRTIMER_MODE_ABS); // was: HRTIMER_MODE_ABS_HARD for -rt Kernel

    printk(KERN_WARNING "shprd.sync: pru1-init with reset of time to %llu - starting loop",
           ts_upcoming_ns);
}


enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart)
{
    uint64_t ns_to_next_trigger;
    uint64_t ts_now_ns;
    // TODO: best would be to get rid of this additional loop - is there a sys-interrupt we can use?
    GPIO_P819_SET;

    /* Raise Interrupt on PRU, telling it to timestamp IEP */
    mem_interface_trigger(HOST_PRU_EVT_TIMESTAMP);

    /* Timestamp system clock */
    ts_now_ns = ktime_get_real_ns();
    GPIO_P819_CLR;

    if (!timers_active) return HRTIMER_NORESTART;
    /*
     * Get distance of system clock from timer wrap.
     * Is negative, when interrupt happened before wrap, positive when after
     */
    div_u64_rem(ts_now_ns, SYNC_INTERVAL_NS, &sys_ts_over_wrap_ns);

    if (sys_ts_over_wrap_ns > (SYNC_INTERVAL_NS / 2))
        ns_to_next_trigger = 2 * SYNC_INTERVAL_NS - sys_ts_over_wrap_ns;
    else ns_to_next_trigger = SYNC_INTERVAL_NS - sys_ts_over_wrap_ns;
    ts_upcoming_ns = ts_now_ns + ns_to_next_trigger;

    // printk(KERN_INFO "shprd.sync: triggered @%llu, next ts = %llu", ts_now_ns, ts_upcoming_ns);
    // NOTE: without load this trigger is accurate < 4 us

    hrtimer_forward(timer_for_restart, ns_to_ktime(ts_now_ns), ns_to_ktime(ns_to_next_trigger));

    return HRTIMER_RESTART;
}

/* Handler for sync-requests from PRU1 */
enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart)
{
    struct ProtoMsg       sync_rqst;
    struct ProtoMsg       sync_reply;

    static uint64_t       ts_last_error_ns = 0;
    static const uint64_t quiet_time_ns    = 10000000000; // 10 s
    static unsigned int   step_pos         = 0;
    /* Timestamp system clock */
    const uint64_t        ts_now_ns        = ktime_get_real_ns();

    if (!timers_active) return HRTIMER_NORESTART;

    if (pru1_comm_receive_sync_request(&sync_rqst))
    {
        switch (sync_rqst.type)
        {
            case MSG_SYNC_ROUTINE:
                sync_PID_correction(&sync_reply, &sync_rqst);
                if (!pru1_comm_send_sync_reply(&sync_reply))
                {
                    /* Error occurs if PRU was not able to handle previous message in time */
                    printk(KERN_WARNING "shprd.sync: Send_SyncResponse -> back-pressure / did "
                                        "overwrite old msg");
                }
                /* resetting to the longest sleep period */
                step_pos = 0;
                break;

            case MSG_SYNC_RESET:
                trigger_loop_start();
                /* resetting to the longest sleep period */
                step_pos = 0;
                break;

            default:
                /* these are all handled in userspace and will be passed by sys-fs */
                printk(KERN_ERR "shprd.k: received invalid command / msg-type (0x%02X) from pru1",
                       sync_rqst.type);
        }
    }
    else if ((ts_last_error_ns + quiet_time_ns < ts_now_ns) &&
             (ts_previous_ns + 2 * SYNC_INTERVAL_NS < ts_now_ns) && (ts_previous_ns > 0))
    {
        // TODO: not working as expected, this routine should get notified when PRUs are offline
        ts_last_error_ns = ts_now_ns;
        printk(KERN_ERR "shprd.sync: Faulty behavior - PRU did not answer to "
                        "trigger-request in time!");
    }
    else if ((ts_previous_ns + 2 * SYNC_INTERVAL_NS < ts_now_ns) && (ts_previous_ns > 0))
    {
        // try to reduce CPU-Usage (ktimersoftd/0 has 50%) when PRUs are halting during operation
        step_pos = 0;
    }

    /* variable sleep cycle */
    hrtimer_forward(timer_for_restart, ns_to_ktime(ts_now_ns),
                    ns_to_ktime(TIMER_STEPS_NS[step_pos]));

    if (step_pos < TIMER_STEPS_NS_SIZE - 1u) step_pos++;

    return HRTIMER_RESTART;
}


int sync_PID_correction(struct ProtoMsg *const sync_reply, const struct ProtoMsg *const sync_rqst)
{
    const int32_t sum_limit     = 10000000;
    const int32_t out_limit     = 200000; // max 1ms correction per 0.1s
    const int32_t smooth_factor = 5;
    /* temporary vars */
    uint32_t      pru_ts_over_wrap_ns;
    int32_t       input_now;
    int64_t       error, output;
    int32_t       correction_sample, correction_floor;

    /* Get distance of IEP clock at interrupt from last timer wrap
     * if sys_wrap is invalid -> (also) invalidate this measurement */
    pru_ts_over_wrap_ns = sync_rqst->value[0];
    if (sys_ts_over_wrap_ns == U32T_MAX) pru_ts_over_wrap_ns = U32T_MAX;

    /* calculate PID-Input */
    input_now = (int32_t) pru_ts_over_wrap_ns - (int32_t) sys_ts_over_wrap_ns;

    /* center values around Zero */
    if (input_now < -(int32_t) (SYNC_INTERVAL_NS / 2)) input_now += SYNC_INTERVAL_NS;
    else if (input_now > (int32_t) (SYNC_INTERVAL_NS / 2)) input_now -= SYNC_INTERVAL_NS;

    /* generate smoothed & absolute version of input */
    if (input_now >= 0)
        sync_state.input_smooth =
                ((smooth_factor - 1) * sync_state.input_smooth + input_now) / smooth_factor;
    else
        sync_state.input_smooth =
                ((smooth_factor - 1) * sync_state.input_smooth - input_now) / smooth_factor;

    /* adjust PI-Tuning once stable */
    // TODO: slow down state-changes, also try holding ki - phase-changes stay in recent versions
    if ((sync_state.input_smooth < 1000000) && (sync_state.k_state == 0))
    {
        sync_state.kp_inv_n10 *= 2;
        sync_state.ki_inv_n10 *= 2;
        sync_state.k_state++;
        printk(KERN_INFO "shprd.sync: error BELOW 1ms -> relax PI-tuning");
    }
    else if ((sync_state.input_smooth < 200000) && (sync_state.k_state == 1))
    {
        sync_state.kp_inv_n10 *= 2;
        sync_state.ki_inv_n10 *= 2;
        sync_state.k_state++;
        printk(KERN_INFO "shprd.sync: error BELOW 200us -> relax PI-tuning");
    }
    else if ((sync_state.input_smooth > 2000000) && (sync_state.k_state > 0))
    {
        sync_state.kp_inv_n10 = kp_inv_n10_init;
        sync_state.ki_inv_n10 = ki_inv_n10_init;
        sync_state.k_state    = 0;
        printk(KERN_ERR "shprd.sync: error ABOVE 2ms -> more responsive PI-tuning");
    }

    error = -input_now;

    sync_state.output_sum += div_s64(1024 * error, sync_state.ki_inv_n10);
    if (sync_state.output_sum > +sum_limit) sync_state.output_sum = +sum_limit;
    if (sync_state.output_sum < -sum_limit) sync_state.output_sum = -sum_limit;

    output = div_s64(1024 * error, sync_state.kp_inv_n10) + sync_state.output_sum;
    if (output > +out_limit) output = +out_limit;
    if (output < -out_limit) output = -out_limit;

    /* determine corrected slow-down / speed-up for next sync-interval
    * NOTE: positive value[0] means PRU is AHEAD and needs to slow
    * */
    sync_reply->type     = MSG_SYNC_ROUTINE;
    correction_sample    = div_s64(output, ((int32_t) SAMPLES_PER_SYNC));
    sync_reply->value[0] = (uint32_t) (SAMPLE_INTERVAL_TICKS - correction_sample);
    correction_floor     = ((int32_t) SAMPLES_PER_SYNC) * correction_sample;

    if (output >= correction_floor) sync_reply->value[1] = (uint32_t) (output - correction_floor);
    else sync_reply->value[1] = (uint32_t) (correction_floor - output);

    if ((sync_state.input_smooth > 300) && (++info_count >= 20))
    {
        if (info_count > 6600)
        {
            printk(KERN_INFO "shprd.sync: NOTE - next message is shown every 2 s when "
                             "sync-error exceeds 300 ns (normal during startup)");
            //printk(KERN_INFO "shprd.sync: init PI-Tuning is: %d, %d (inverted k-values x1024)",
            //       kp_inv_n10_init, ki_inv_n10_init);
        }
        info_count = 0;
        /* val = 200 prints every 20s when enabled */
        printk(KERN_INFO "shprd.sync: err_PI = %lld (%d), %lld -> fback = %lld ns/.1s [%u; %u]",
               error, sync_state.input_smooth, sync_state.output_sum, output, sync_reply->value[0],
               sync_reply->value[1]);
    }

    /* test for validity */
    if ((input_now > (int32_t) (SYNC_INTERVAL_NS / 2)) ||
        (input_now < -(int32_t) (SYNC_INTERVAL_NS / 2)))
        printk(KERN_ERR "shprd.sync: input (P) is out of bound (%d ns) -> BUG", input_now);

    /* plausibility-check, in case the sync-algo produces jumps */
    if (ts_previous_ns > 0)
    {
        int64_t diff_timestamp_ms = div_s64((int64_t) ts_upcoming_ns - ts_previous_ns, 1000000u);
        if (diff_timestamp_ms < 0)
            printk(KERN_ERR "shprd.sync: backwards timestamp-jump detected (sync-loop, %lld ms)",
                   diff_timestamp_ms);
        else if (diff_timestamp_ms < 95)
            printk(KERN_ERR "shprd.sync: too small timestamp-jump detected (sync-loop, %lld ms)",
                   diff_timestamp_ms);
        else if (diff_timestamp_ms > 105)
            printk(KERN_ERR "shprd.sync: forwards timestamp-jump detected (sync-loop, %lld ms)",
                   diff_timestamp_ms);
        else if (ts_upcoming_ns == 0)
            printk(KERN_ERR "shprd.sync: zero timestamp detected (sync-loop)");
    }
    ts_previous_ns = ts_upcoming_ns;
    return 0;
}
