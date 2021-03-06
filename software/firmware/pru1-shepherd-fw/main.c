#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pru_cfg.h>
#include <pru_intc.h>
#include <pru_iep.h>
#include <gpio.h>

#include "iep.h"
#include "intc.h"

#include "resource_table.h"
#include "commons.h"
#include "shepherd_config.h"
#include "stdint_fast.h"
#include "debug_routines.h"

/* The Arm to Host interrupt for the timestamp event is mapped to Host interrupt 0 -> Bit 30 (see resource_table.h) */
#define HOST_INT_TIMESTAMP_MASK (1U << 30U)

// both pins have a LED
#define DEBUG_PIN0_MASK 	BIT_SHIFT(P8_28)
#define DEBUG_PIN1_MASK 	BIT_SHIFT(P8_30)

#define GPIO_MASK		(0x03FF)

/* overview for current pin-mirroring
#define TARGET_GPIO0            BIT_SHIFT(P8_45) // r31_00
#define TARGET_GPIO1            BIT_SHIFT(P8_46) // r31_01
#define TARGET_GPIO2            BIT_SHIFT(P8_43) // r31_02
#define TARGET_GPIO3            BIT_SHIFT(P8_44) // r31_03
#define TARGET_UART_TX          BIT_SHIFT(P8_41) // r31_04
#define TARGET_UART_RX          BIT_SHIFT(P8_42) // r31_05
#define TARGET_SWD_CLK          BIT_SHIFT(P8_39) // r31_06
#define TARGET_SWD_IO           BIT_SHIFT(P8_40) // r31_07
#define TARGET_BAT_OK           BIT_SHIFT(P8_27) // r31_08
#define TARGET_GPIO4            BIT_SHIFT(P8_29) // r31_09

TODO: new order for hw-rev2.1, also adapt device tree (gpio2/3 switches with swd_clk/io AND gpio4 switches with bat-ok)
#define TARGET_GPIO0            BIT_SHIFT(P8_45) // r31_00
#define TARGET_GPIO1            BIT_SHIFT(P8_46) // r31_01
#define TARGET_SWD_CLK          BIT_SHIFT(P8_43) // r31_02
#define TARGET_SWD_IO           BIT_SHIFT(P8_44) // r31_03
#define TARGET_UART_TX          BIT_SHIFT(P8_41) // r31_04
#define TARGET_UART_RX          BIT_SHIFT(P8_42) // r31_05
#define TARGET_GPIO2            BIT_SHIFT(P8_39) // r31_06
#define TARGET_GPIO3            BIT_SHIFT(P8_40) // r31_07
#define TARGET_GPIO4            BIT_SHIFT(P8_27) // r31_08
#define TARGET_BAT_OK           BIT_SHIFT(P8_29) // r31_09
*/


enum SyncState {
	IDLE,
	REPLY_PENDING
};

// alternative message channel specially dedicated for errors
static void emit_error(volatile struct SharedMem *const shared_mem, enum MsgType type, const uint32_t value)
{
	//if (shared_mem->pru0_msg_error.msg_unread == 0) // do not care, newest error wins
	{
		shared_mem->pru1_msg_error.msg_type = type;
		shared_mem->pru1_msg_error.value = value;
		shared_mem->pru1_msg_error.msg_id = MSG_TO_KERNEL;
		// NOTE: always make sure that the unread-flag is activated AFTER payload is copied
		shared_mem->pru1_msg_error.msg_unread = 1u;
	}
	if (type >= 0xE0)
		__delay_cycles(1000000U/TIMER_TICK_NS); // 1 ms
}

//static void fault_handler(const uint32_t shepherd_state, const char * err_msg) // TODO: use when pssp gets changed,
// TODO: replace by pru0-error-msg-system
static void fault_handler(volatile struct SharedMem *const shared_mem, enum MsgType err_msg)
{
	/* If shepherd is not running, we can recover from the fault */
	if (shared_mem->shepherd_state != STATE_RUNNING)
	{
		emit_error(shared_mem, err_msg, 0);
		return;
	}

	while (true)
	{
		emit_error(shared_mem, err_msg, 0);
		__delay_cycles(2000000000U);
	}
}


static inline bool_ft receive_control_reply(volatile struct SharedMem *const shared_mem, struct CtrlRepMsg *const ctrl_rep)
{
	if (shared_mem->pru1_msg_ctrl_rep.msg_unread >= 1)
	{
		if (shared_mem->pru1_msg_ctrl_rep.identifier != MSG_TO_PRU)
		{
			/* Error occurs if something writes over boundaries */
			fault_handler(shared_mem, MSG_ERR_MEMCORRUPTION);
		}
		*ctrl_rep = shared_mem->pru1_msg_ctrl_rep; // TODO: faster to copy only the needed 2 uint32
		shared_mem->pru1_msg_ctrl_rep.msg_unread = 0;
		// TODO: move this to kernel
		if (ctrl_rep->buffer_block_period > TIMER_BASE_PERIOD + (TIMER_BASE_PERIOD>>3))
		{
			fault_handler(shared_mem, MSG_ERROR); //"Recv_CtrlReply -> buffer_block_period too high");
		}
		if (ctrl_rep->buffer_block_period < TIMER_BASE_PERIOD - (TIMER_BASE_PERIOD>>3))
		{
			fault_handler(shared_mem, MSG_ERROR); //"Recv_CtrlReply -> buffer_block_period too low");
		}
		if (ctrl_rep->analog_sample_period > 2100)
		{
			fault_handler(shared_mem, MSG_ERROR); //"Recv_CtrlReply -> analog_sample_period too high");
		}
		if (ctrl_rep->analog_sample_period < 1900)
		{
			fault_handler(shared_mem, MSG_ERROR); //"Recv_CtrlReply -> analog_sample_period too low");
		}
		if (ctrl_rep->compensation_steps > ADC_SAMPLES_PER_BUFFER)
		{
			fault_handler(shared_mem, MSG_ERROR); //"Recv_CtrlReply -> compensation_steps too high");
		}
		return 1;
	}
	return 0;
}

// send emits a 1 on success
// pru1_msg_ctrl_req: (future opt.) needs to have special config set: identifier=MSG_TO_KERNEL and msg_unread=1
static inline bool_ft send_control_request(volatile struct SharedMem *const shared_mem, const struct CtrlReqMsg *const ctrl_req)
{
	if (shared_mem->pru1_msg_ctrl_req.msg_unread == 0)
	{
		shared_mem->pru1_msg_ctrl_req = *ctrl_req;
		shared_mem->pru1_msg_ctrl_req.identifier = MSG_TO_KERNEL;
		// NOTE: always make sure that the unread-flag is activated AFTER payload is copied
		shared_mem->pru1_msg_ctrl_req.msg_unread = 1u;
		return 1;
	}
	/* Error occurs if PRU was not able to handle previous message in time */
	fault_handler(shared_mem, MSG_ERR_BACKPRESSURE);
	return 0;
}

/*
 * Here, we sample the the GPIO pins from a connected sensor node. We repeatedly
 * poll the state via the R31 register and keep the last state in a static
 * variable. Once we detect a change, the new value (V1=4bit, V2=10bit) is written to the
 * corresponding buffer (which is managed by PRU0). The tricky part is the
 * synchronization between the PRUs to avoid inconsistent state, while
 * minimizing sampling delay
 */
static inline void check_gpio(volatile struct SharedMem *const shared_mem,
        const uint64_t current_timestamp_ns,
        const uint32_t last_sample_ticks)
{
	static uint32_t prev_gpio_status = 0x00;

	/*
	* Only continue if shepherd is running and PRU0 actually provides a buffer
	* to write to.
	*/
	if ((shared_mem->shepherd_state != STATE_RUNNING) ||
	    (shared_mem->gpio_edges == NULL)) {
		prev_gpio_status = 0x00;
		shared_mem->gpio_pin_state = read_r31() & GPIO_MASK;
		return;
	}

	const uint32_t gpio_status = read_r31() & GPIO_MASK;
	const uint32_t gpio_diff = gpio_status ^ prev_gpio_status;

	prev_gpio_status = gpio_status;

	if (gpio_diff > 0)
	{
		DEBUG_GPIO_STATE_2;
		// local copy reduces reads to far-ram to current minimum
		const uint32_t cIDX = shared_mem->gpio_edges->idx;

		/* Each buffer can only store a limited number of events */
		if (cIDX >= MAX_GPIO_EVT_PER_BUFFER) return;

		/* Ticks since we've taken the last sample */
		const uint32_t ticks_since_last_sample = CT_IEP.TMR_CNT - last_sample_ticks;

		/* Nanoseconds from current buffer start to last sample */
		const uint32_t last_sample_ns = SAMPLE_INTERVAL_NS * (shared_mem->analog_sample_counter);

		/* Calculate final timestamp of gpio event */
		const uint64_t gpio_timestamp = current_timestamp_ns + last_sample_ns + TIMER_TICK_NS * ticks_since_last_sample;

		simple_mutex_enter(&shared_mem->gpio_edges_mutex);
		shared_mem->gpio_edges->timestamp_ns[cIDX] = gpio_timestamp;
		shared_mem->gpio_edges->bitmask[cIDX] = (uint16_t)gpio_status;
		shared_mem->gpio_edges->idx = cIDX + 1;
		simple_mutex_exit(&shared_mem->gpio_edges_mutex);
	}
}


/* TODO: update comments, seem outdated
 * The firmware for synchronization/sample timing is based on a simple
 * event loop. There are three events: 1) Interrupt from Linux kernel module
 * 2) Local IEP timer wrapped 3) Local IEP timer compare for sampling
 *
 * Event 1:
 * The kernel module periodically timestamps its own clock and immediately
 * triggers an interrupt to PRU1. On reception of that interrupt we have
 * to timestamp our local IEP clock. We then send the local timestamp to the
 * kernel module as an RPMSG message. The kernel module runs a PI control loop
 * that minimizes the phase shift (and frequency deviation) by calculating a
 * correction factor that we apply to the base period of the IEP clock. This
 * resembles a Phase-Locked-Loop system. The kernel module sends the resulting
 * correction factor to us as an RPMSG. Ideally, Event 1 happens at the same
 * time as Event 2, i.e. our local clock should wrap at exactly the same time
 * as the Linux host clock. However, due to phase shifts and kernel timer
 * jitter, the two events typically happen with a small delay and in arbitrary
 * order. However, we would
 *
 * Event 2:
 *
 * Event 3:
 * This is the main sample trigger that is used to trigger the actual sampling
 * on PRU0 by raising an interrupt. After every sample, we have to forward
 * the compare value, taking into account the current sampling period
 * (dynamically adapted by PLL). Also, we will only check for the controller
 * reply directly following this event in order to avoid sampling jitter that
 * could result from being busy with RPMSG and delaying response to the next
 * Event 3
 */

int32_t event_loop(volatile struct SharedMem *const shared_mem)
{
	uint64_t current_timestamp_ns = 0;
	uint32_t last_analog_sample_ticks = 0;

	/* Prepare message that will be received and sent to Linux kernel module */
	struct CtrlReqMsg ctrl_req = { .identifier = MSG_TO_KERNEL, .msg_unread = 1 };
	struct CtrlRepMsg ctrl_rep = {
		.buffer_block_period = TIMER_BASE_PERIOD,
		.analog_sample_period = TIMER_BASE_PERIOD / ADC_SAMPLES_PER_BUFFER,
		.compensation_steps = 0u,
	};

	/* This tracks our local state, allowing to execute actions at the right time */
	enum SyncState sync_state = IDLE;

	/*
	* This holds the number of 'compensation' periods, where the sampling
	* period is increased by 1 in order to compensate for the remainder of the
	* integer division used to calculate the sampling period.
	*/
	uint32_t compensation_steps = ctrl_rep.compensation_steps;
	/*
	 * holds distribution of the compensation periods (every x samples the period is increased by 1)
	 */
	uint32_t compensation_counter = 0u;
	uint32_t compensation_increment = 0u;

	/* Our initial guess of the sampling period based on nominal timer period */
	uint32_t analog_sample_period = ctrl_rep.analog_sample_period;
	uint32_t buffer_block_period = ctrl_rep.buffer_block_period;

	/* These are our initial guesses for buffer sample period */
	iep_set_cmp_val(IEP_CMP0, buffer_block_period);  // 20 MTicks -> 100 ms
	iep_set_cmp_val(IEP_CMP1, analog_sample_period); // 20 kTicks -> 10 us

	iep_enable_evt_cmp(IEP_CMP1);
	iep_clear_evt_cmp(IEP_CMP0);

	/* Clear raw interrupt status from ARM host */
	INTC_CLEAR_EVENT(HOST_PRU_EVT_TIMESTAMP);
	/* Wait for first timer interrupt from Linux host */
	while (!(read_r31() & HOST_INT_TIMESTAMP_MASK)) {};

	if (INTC_CHECK_EVENT(HOST_PRU_EVT_TIMESTAMP)) INTC_CLEAR_EVENT(HOST_PRU_EVT_TIMESTAMP);

	iep_start();

	while (1)
	{
		#if DEBUG_LOOP_EN
		debug_loop_delays(shared_mem->shepherd_state);
		#endif

		DEBUG_GPIO_STATE_1;
		check_gpio(shared_mem, current_timestamp_ns, last_analog_sample_ticks);
		DEBUG_GPIO_STATE_0;

		/* [Event1] Check for timer interrupt from Linux host */
		if (read_r31() & HOST_INT_TIMESTAMP_MASK) {
			if (!INTC_CHECK_EVENT(HOST_PRU_EVT_TIMESTAMP)) continue;

			/* Take timestamp of IEP */
			ctrl_req.ticks_iep = iep_get_cnt_val();
			DEBUG_EVENT_STATE_2;
			/* Clear interrupt */
			INTC_CLEAR_EVENT(HOST_PRU_EVT_TIMESTAMP);

			if (sync_state == IDLE)    sync_state = REPLY_PENDING;
			else {
				fault_handler(shared_mem, MSG_ERR_SYNC_STATE_NOT_IDLE);
				return 0;
			}
			send_control_request(shared_mem, &ctrl_req);
			DEBUG_EVENT_STATE_0;
			continue;  // for more regular gpio-sampling
		}

		/*  [Event 2] Timer compare 0 handle -> trigger for buffer swap on pru0 */
		if (shared_mem->cmp0_trigger_for_pru1)
		{
			DEBUG_EVENT_STATE_2;
			// hand-back of cmp-token
			shared_mem->cmp0_trigger_for_pru1 = 0;

			/* update clock compensation of sample-trigger */
			iep_set_cmp_val(IEP_CMP1, 0);
			iep_enable_evt_cmp(IEP_CMP1);
			analog_sample_period = ctrl_rep.analog_sample_period;
			compensation_steps = ctrl_rep.compensation_steps;
			compensation_increment = ctrl_rep.compensation_steps;
			compensation_counter = 0;

			/* update main-loop */
			buffer_block_period = ctrl_rep.buffer_block_period;
			iep_set_cmp_val(IEP_CMP0, buffer_block_period);

			/* more maintenance */
			last_analog_sample_ticks = 0;

			/* With wrap, we'll use next timestamp as base for GPIO timestamps */
			current_timestamp_ns = shared_mem->next_timestamp_ns;
			// TODO: this is definitely wrong for edge case: buffer already exchanged, timer0 not yet wrapped

			DEBUG_EVENT_STATE_0;
		}

		/* [Event 3] Timer compare 1 handle -> trigger for analog sample on pru0 */
		if (shared_mem->cmp1_trigger_for_pru1)
		{
			DEBUG_EVENT_STATE_1;
			// hand-back of cmp-token
			shared_mem->cmp1_trigger_for_pru1 = 0;

			// Update Timer-Values
			last_analog_sample_ticks = iep_get_cmp_val(IEP_CMP1);
			/* Forward sample timer based on current analog_sample_period*/
			uint32_t next_cmp_val = last_analog_sample_ticks + analog_sample_period;
			compensation_counter += compensation_increment; // fixed point magic
			/* If we are in compensation phase add one */
			if ((compensation_counter >= ADC_SAMPLES_PER_BUFFER) && (compensation_steps > 0)) {
				next_cmp_val += 1;
				compensation_steps--;
				compensation_counter -= ADC_SAMPLES_PER_BUFFER;
			}
			iep_set_cmp_val(IEP_CMP1, next_cmp_val);

			/* If we are waiting for a reply from Linux kernel module */
			if (receive_control_reply(shared_mem, &ctrl_rep) > 0)
			{
				sync_state = IDLE;
				shared_mem->next_timestamp_ns = ctrl_rep.next_timestamp_ns;
			}
			DEBUG_EVENT_STATE_0;
			continue; // for more regular gpio-sampling
		}
	}
}

void main(void)
{
	volatile struct SharedMem *const shared_mememory = (volatile struct SharedMem *)PRU_SHARED_MEM_STRUCT_OFFSET;

    	/* Allow OCP master port access by the PRU so the PRU can read external memories */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;
	DEBUG_STATE_0;

	/* Enable 'timestamp' interrupt from ARM host */
	CT_INTC.EISR_bit.EN_SET_IDX = HOST_PRU_EVT_TIMESTAMP;

reset:
	emit_error(shared_mememory, MSG_STATUS_RESTARTING_SYNC_ROUTINE, 0); // TODO: rename
	/* Make sure the mutex is clear */
	simple_mutex_exit(&shared_mememory->gpio_edges_mutex);

	iep_init();
	iep_reset();

	event_loop(shared_mememory);
	goto reset;
}
