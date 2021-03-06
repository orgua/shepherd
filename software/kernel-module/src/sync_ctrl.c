#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/math64.h>

#include "sync_ctrl.h"
#include "pru_comm.h"

static int64_t ns_sys_to_wrap;
static uint64_t next_timestamp_ns;
static uint64_t prev_timestamp_ns = 0; 	/* for plausibility-check */

void reset_prev_timestamp(void) // TODO: not needed anymore^, // TODO: there was this reset when a string-message came in per rpmsg
{
    prev_timestamp_ns = 0;
}

static enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart);
static enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart);
static uint32_t trigger_loop_period_ns = 100000000; /* just initial value to avoid div0 */

/* Timer to trigger fast synch_loop */
struct hrtimer trigger_loop_timer;
struct hrtimer sync_loop_timer;

/* series of halving sleep cycles, sleep less coming slowly near a total of 100ms of sleep */
const static unsigned int timer_steps_ns[] = {
        20000000u, 20000000u,
        20000000u, 20000000u, 10000000u,
        5000000u,  2000000u,  1000000u,
        500000u,   200000u,   100000u,
        50000u,    20000u};
const static size_t timer_steps_ns_size = sizeof(timer_steps_ns) / sizeof(timer_steps_ns[0]);
//static unsigned int step_pos = 0;

// Sync-Routine - TODO: take these from pru-sharedmem
#define BUFFER_PERIOD_NS    	(100000000U) // TODO: there is already: trigger_loop_period_ns
#define ADC_SAMPLES_PER_BUFFER  (10000U)
#define TIMER_TICK_NS           (5U)
#define TIMER_BASE_PERIOD   	(BUFFER_PERIOD_NS / TIMER_TICK_NS)
#define SAMPLE_INTERVAL_NS  	(BUFFER_PERIOD_NS / ADC_SAMPLES_PER_BUFFER)
static uint32_t info_count = 0;
struct sync_data_s *sync_data;


int sync_exit(void)
{
	hrtimer_cancel(&trigger_loop_timer);
	hrtimer_cancel(&sync_loop_timer);
	kfree(sync_data);

	return 0;
}

int sync_init(uint32_t timer_period_ns)
{
	struct timespec ts_now;
	uint64_t now_ns_system;
	uint32_t ns_over_wrap;
	uint64_t ns_now_until_trigger;

	sync_data = kmalloc(sizeof(struct sync_data_s), GFP_KERNEL);
	if (!sync_data)
		return -1;
	sync_reset();

    /* Timestamp system clock */
    getnstimeofday(&ts_now);
    now_ns_system = (uint64_t)timespec_to_ns(&ts_now);

	/* timer for trigger, TODO: this needs better naming, make clear what it does */
	trigger_loop_period_ns = timer_period_ns; /* 100 ms */
    //printk(KERN_INFO "shprd.k: new timer_period_ns = %u\n", trigger_loop_period_ns);

    hrtimer_init(&trigger_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    trigger_loop_timer.function = &trigger_loop_callback;

    /* timer for Synch-Loop */
    hrtimer_init(&sync_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    sync_loop_timer.function = &sync_loop_callback;

	div_u64_rem(now_ns_system, timer_period_ns, &ns_over_wrap);
	if (ns_over_wrap > (timer_period_ns / 2))
		ns_now_until_trigger = 2 * timer_period_ns - ns_over_wrap;
	else
		ns_now_until_trigger = timer_period_ns - ns_over_wrap;

	hrtimer_start(&trigger_loop_timer,
		      ns_to_ktime(now_ns_system + ns_now_until_trigger),
		      HRTIMER_MODE_ABS);

    hrtimer_start(&sync_loop_timer,
            ns_to_ktime(now_ns_system + 1000000),
            HRTIMER_MODE_ABS);

	return 0;
}

int sync_reset(void)
{
    sync_data->error_now = 0;
    sync_data->error_pre = 0;
    sync_data->error_dif = 0;
	sync_data->error_sum = 0;
	sync_data->clock_corr = 0;
    sync_data->previous_period = TIMER_BASE_PERIOD;
	return 0;
}

enum hrtimer_restart trigger_loop_callback(struct hrtimer *timer_for_restart)
{
	struct timespec ts_now;
	uint64_t now_ns_system;
	uint32_t ns_over_wrap;
	uint64_t ns_now_until_trigger;
	/*
	* add pretrigger, because design aimed directly for busy pru_timer_wrap
	* (50% chance that pru takes a less meaningful counter-reading after wrap)
    * 1 ms + 5 us, this should be enough time for the ping-pong to complete before timer_wrap
    */
	static const uint32_t ns_pre_trigger = 1005000;

	/* Raise Interrupt on PRU, telling it to timestamp IEP */
	pru_comm_trigger(HOST_PRU_EVT_TIMESTAMP);

	/* Timestamp system clock */
	getnstimeofday(&ts_now);
	now_ns_system = (uint64_t)timespec_to_ns(&ts_now);

	/*
     * Get distance of system clock from timer wrap.
     * Is negative, when interrupt happened before wrap, positive when after
     */
	div_u64_rem(now_ns_system, trigger_loop_period_ns, &ns_over_wrap);
	if (ns_over_wrap > (trigger_loop_period_ns / 2))
	{
		/* normal use case (from now on) - marks beginning of next buffer*/
	    ns_sys_to_wrap = ((int64_t)ns_over_wrap - trigger_loop_period_ns);
		next_timestamp_ns = now_ns_system + 1 * trigger_loop_period_ns - ns_over_wrap;
		ns_now_until_trigger = 2 * trigger_loop_period_ns - ns_over_wrap - ns_pre_trigger;
	} else
	    {
		ns_sys_to_wrap = ((int64_t)ns_over_wrap);
		next_timestamp_ns = now_ns_system + trigger_loop_period_ns - ns_over_wrap;
		ns_now_until_trigger = trigger_loop_period_ns - ns_over_wrap - ns_pre_trigger;
	}

	hrtimer_forward(timer_for_restart, timespec_to_ktime(ts_now),
			ns_to_ktime(ns_now_until_trigger));

	return HRTIMER_RESTART;
}

/* Handler for ctrl-requests from PRU1 */
enum hrtimer_restart sync_loop_callback(struct hrtimer *timer_for_restart)
{
    struct CtrlReqMsg ctrl_req;
    struct CtrlRepMsg ctrl_rep;
    struct timespec ts_now;
    static unsigned int step_pos = 0;
    /* Timestamp system clock */
    getnstimeofday(&ts_now);

    if (pru_comm_get_ctrl_request(&ctrl_req))
    {
        if (ctrl_req.identifier != MSG_TO_KERNEL)
        {
            /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd.k: Recv_CtrlRequest -> mem corruption?\n");
        }

        sync_loop(&ctrl_rep, &ctrl_req);

        if (!pru_comm_send_ctrl_reply(&ctrl_rep))
        {
            /* Error occurs if PRU was not able to handle previous message in time */
            printk(KERN_WARNING "shprd.k: Send_CtrlResponse -> back-pressure\n");
        }

        /* resetting to longest sleep period */
        step_pos = 0;
    }

    hrtimer_forward(timer_for_restart, timespec_to_ktime(ts_now),
            ns_to_ktime(timer_steps_ns[step_pos])); /* variable sleep cycle */

    if (step_pos < timer_steps_ns_size - 1) step_pos++;

    return HRTIMER_RESTART;
}


int sync_loop(struct CtrlRepMsg *const ctrl_rep, const struct CtrlReqMsg *const ctrl_req)
{
	int64_t ns_iep_to_wrap;
	uint64_t ns_per_tick;

	/*
     * Based on the previous IEP timer period and the nominal timer period
     * we can estimate the real nanoseconds passing per tick
     * We operate on fixed point arithmetics by shifting by 30 bit
     */
	ns_per_tick = div_u64(((uint64_t)trigger_loop_period_ns << 30u),
            sync_data->previous_period);

	/*
     * Get distance of IEP clock at interrupt from timer wrap
     * negative, if interrupt happened before wrap, positive after
     */
	ns_iep_to_wrap = ((int64_t)ctrl_req->ticks_iep) * ns_per_tick;
    /* 29 in next line is correct, if ns_iep is over the half it is shorter to go the other direction */
	if (ns_iep_to_wrap > ((uint64_t)trigger_loop_period_ns << 29u))
	{
		ns_iep_to_wrap -= ((uint64_t)trigger_loop_period_ns << 30u);
	}

	/* Difference between system clock and IEP clock phase */
	sync_data->error_pre = sync_data->error_now; // TODO: new D (of PID) is not in sysfs yet
	sync_data->error_now = div_s64(ns_iep_to_wrap, 1ul<<30u) - ns_sys_to_wrap; // TODO: could save some divs
    sync_data->error_dif = sync_data->error_now - sync_data->error_pre;
    sync_data->error_sum += sync_data->error_now; // integral should be behind controller, because current P-value is twice in calculation

    /* This is the actual PI controller equation,
     * NOTE1: unit of clock_corr in pru is ticks, but input is based on nanosec
     * NOTE2: traces show, that quantization noise could be a problem. example: K-value of 127, divided by 128 will still be 0, ringing is around ~ +-150
     * previous parameters were:    P=1/32, I=1/128, correction settled at ~1340 with values from 1321 to 1359
     * current parameters:          P=1/100,I=1/300, correction settled at ~1332 with values from 1330 to 1335
     * */
    sync_data->clock_corr = (int32_t)(div_s64(sync_data->error_now, 128) + div_s64(sync_data->error_sum, 256));
    if (sync_data->clock_corr > +80000) sync_data->clock_corr = +80000;
    if (sync_data->clock_corr < -80000) sync_data->clock_corr = -80000;

    if (0)
    {
        printk(KERN_ERR "shprd.k: error=%lld, ns_iep=%lld, ns_sys=%lld, errsum=%lld, prev_period=%u, corr=%d\n",
                sync_data->error_now,
                div_s64(ns_iep_to_wrap, 1ul<<30u),
                ns_sys_to_wrap,
                sync_data->error_sum,
                sync_data->previous_period,
                sync_data->clock_corr);
    }

    /* determine corrected loop_ticks for next buffer_block */
    ctrl_rep->buffer_block_period = TIMER_BASE_PERIOD + sync_data->clock_corr;
    sync_data->previous_period = ctrl_rep->buffer_block_period;
    ctrl_rep->analog_sample_period = (ctrl_rep->buffer_block_period / ADC_SAMPLES_PER_BUFFER);
    ctrl_rep->compensation_steps = ctrl_rep->buffer_block_period - (ADC_SAMPLES_PER_BUFFER * ctrl_rep->analog_sample_period);

    if ((1) && ++info_count >= 200) /* val = 200 prints every 20s when enabled */
    {
        printk(KERN_INFO
        "shprd.k: p_buf=%u, p_as=%u, n_comp=%u, p_prev=%u, e_pid=%lld/%lld/%lld\n",
                ctrl_rep->buffer_block_period,
                ctrl_rep->analog_sample_period,
                ctrl_rep->compensation_steps,
                sync_data->previous_period,
                sync_data->error_now,
                sync_data->error_sum,
                sync_data->error_dif);
        info_count = 0;
    }

	/* for plausibility-check, in case the sync-algo produces jumps */
	if (prev_timestamp_ns > 0)
    {
        int64_t diff_timestamp = div_s64(next_timestamp_ns - prev_timestamp_ns, 1000000u);
        if (diff_timestamp < 0)
            printk(KERN_ERR "shprd.k: backwards timestamp-jump detected \n");
        else if (diff_timestamp < 95)
            printk(KERN_ERR "shprd.k: too small timestamp-jump detected\n");
        else if (diff_timestamp > 105)
            printk(KERN_ERR "shprd.k: forwards timestamp-jump detected\n");
    }
    prev_timestamp_ns = next_timestamp_ns;

    ctrl_rep->next_timestamp_ns = next_timestamp_ns;

	return 0;
}