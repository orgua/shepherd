#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "pru_mem_interface.h"
#include "pru_msg_sys.h"

/***************************************************************/
/***************************************************************/

struct RingBuffer msg_ringbuf_from_pru;
struct RingBuffer msg_ringbuf_to_pru;

// TODO: base msg-system on irqs (there are free ones from rpmsg)
// TODO: maybe replace by official kfifo, https://tuxthink.blogspot.com/2020/03/creating-fifo-in-linux-kernel.html
static void       ring_init(struct RingBuffer *const buf)
{
    buf->start  = 0u;
    buf->end    = 0u;
    buf->active = 0u;
}

static void ring_put(struct RingBuffer *const buf, const struct ProtoMsg *const element)
{
    //mutex_lock(&buf->mutex); // NOTE: module is single-threaded, so no lock needed
    buf->ring[buf->end] = *element;

    // special faster version of buf = (buf + 1) % SIZE
    if (++(buf->end) == MSG_FIFO_SIZE) buf->end = 0U;

    if (buf->active < MSG_FIFO_SIZE) buf->active++;
    else
    {
        if (++(buf->start) == MSG_FIFO_SIZE) buf->start = 0U; // fast modulo
        /* fire warning - maybe not the best place to do this - could start an avalanche */
        printk(KERN_ERR "shprd.k: FIFO of msg-system is full - lost oldest msg!");
    }
    //mutex_unlock(&buf->mutex);
}

static uint8_t ring_get(struct RingBuffer *const buf, struct ProtoMsg *const element)
{
    if (buf->active == 0) return 0;
    //mutex_lock(&buf->mutex);
    *element = buf->ring[buf->start];
    if (++(buf->start) == MSG_FIFO_SIZE) buf->start = 0U; // fast modulo
    buf->active--;
    //mutex_unlock(&buf->mutex);
    return 1;
}

void put_msg_to_pru(const struct ProtoMsg *const element)
{
    ring_put(&msg_ringbuf_to_pru, element);
}

uint8_t get_msg_from_pru(struct ProtoMsg *const element)
{
    return ring_get(&msg_ringbuf_from_pru, element);
}

/***************************************************************/
/***************************************************************/

struct hrtimer              coordinator_loop_timer;
static enum hrtimer_restart coordinator_callback(struct hrtimer *timer_for_restart);
static u8                   timers_active          = 0;
static u8                   init_done              = 0;
/* series of halving sleep cycles, sleep less coming slowly near a total of 100ms of sleep */
static const unsigned int   coord_timer_steps_ns[] = {500000u, 200000u, 100000u,
                                                      50000u,  20000u,  10000u};
static const size_t         coord_timer_steps_ns_size =
        sizeof(coord_timer_steps_ns) / sizeof(coord_timer_steps_ns[0]);


/***************************************************************/
/***************************************************************/

void msg_sys_exit(void)
{
    hrtimer_cancel(&coordinator_loop_timer);
    init_done     = 0;
    timers_active = 0;
    printk(KERN_INFO "shprd.k: msg-system exited");
}

void msg_sys_reset(void)
{
    ring_init(&msg_ringbuf_from_pru);
    ring_init(&msg_ringbuf_to_pru);
}

void msg_sys_test(void)
{
    struct ProtoMsg msg = {.id       = MSG_TO_PRU,
                           .unread   = 0u,
                           .type     = MSG_NONE,
                           .reserved = {0u},
                           .value    = {0u, 0u}};
    printk(KERN_INFO "shprd.k: test msg-pipelines between kM and PRUs -> triggering "
                     "roundtrip-messages for pipeline 1-3");
    msg.type     = MSG_TEST_ROUTINE;
    msg.value[0] = 1;
    put_msg_to_pru(&msg); // message-pipeline pru0
    msg.value[0] = 2;
    put_msg_to_pru(&msg); // error-pipeline pru0
    msg.value[0] = 3;
    pru1_comm_send_sync_reply(&msg); // error-pipeline pru1
}

void msg_sys_init(void)
{
    if (init_done)
    {
        printk(KERN_ERR "shprd.k: msg-system init requested -> can't init twice!");
        return;
    }

    hrtimer_init(&coordinator_loop_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    coordinator_loop_timer.function = &coordinator_callback;

    init_done                       = 1;
    printk(KERN_INFO "shprd.k: msg-system initialized");

    msg_sys_start();
    msg_sys_test();
}


void msg_sys_pause(void)
{
    if (!timers_active)
    {
        printk(KERN_ERR "shprd.k: msg-system pause requested -> sys not running!");
        return;
    }
    timers_active = 0;
    printk(KERN_INFO "shprd.k: msg-system paused");
}

void msg_sys_start(void)
{
    /* Timestamp system clock */
    const ktime_t ts_now_kt = ktime_get_real();

    if (!init_done)
    {
        printk(KERN_ERR "shprd.k: msg-system start requested without prior init");
        return;
    }
    if (timers_active)
    {
        printk(KERN_ERR "shprd.k: msg-system start requested -> but already running!");
        return;
    }

    msg_sys_reset();

    hrtimer_start(&coordinator_loop_timer, ts_now_kt + ns_to_ktime(coord_timer_steps_ns[0]),
                  HRTIMER_MODE_ABS);

    timers_active = 1;
    printk(KERN_INFO "shprd.k: msg-system started");
}

/***************************************************************/
/***************************************************************/

static enum hrtimer_restart coordinator_callback(struct hrtimer *timer_for_restart)
{
    struct ProtoMsg pru_msg;
    static uint32_t step_pos       = 0;
    static uint32_t canary_counter = 100000;
    uint8_t         had_work;
    uint32_t        iter;

    /* Timestamp system clock */
    const ktime_t   ts_now_kt = ktime_get_real();

    if (!timers_active) return HRTIMER_NORESTART;

    for (iter = 0; iter < 4; ++iter) /* 3 should be enough, 6 has safety-margin included */
    {
        if (pru0_comm_receive_msg(&pru_msg)) had_work = 2;
        else if (pru0_comm_receive_error(&pru_msg)) had_work = 4;
        else if (pru1_comm_receive_error(&pru_msg)) had_work = 5;
        else
        {
            had_work = 0;
            break;
        }

        if (pru_msg.type <= 0xF0u)
        {
            // relay everything below kernelspace to sheep and also RESTARTING_ROUTINE
            ring_put(&msg_ringbuf_from_pru, &pru_msg);
        }

        if (pru_msg.type >= 0xE0u)
        {
            switch (pru_msg.type)
            {
                // NOTE: all MSG_ERR also get handed to python
                case MSG_ERR_INVLD_CMD:
                    printk(KERN_ERR "shprd.pru%u: pru received invalid cmd, type = %u",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_MEM_CORRUPTION:
                    printk(KERN_ERR "shprd.pru%u: msg.id from kernel is faulty -> mem "
                                    "corruption? (val=%u)",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_BACKPRESSURE:
                    printk(KERN_ERR "shprd.pru%u: msg-buffer to kernel was still full "
                                    "-> backpressure (val=%u)",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_TIMESTAMP:
                    printk(KERN_ERR "shprd.pru%u: received timestamp is faulty (val=%u)",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_CANARY:
                    printk(KERN_ERR "shprd.pru%u: detected a dead canary (val=%u)", had_work & 1u,
                           pru_msg.value[0]);
                    break;
                case MSG_ERR_SYNC_STATE_NOT_IDLE:
                    printk(KERN_ERR "shprd.pru%u: Sync not idle at host interrupt (val=%u)",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_VALUE:
                    printk(KERN_ERR "shprd.pru%u: content of msg failed test (val=%u)",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                case MSG_ERR_ADC_NOT_FOUND:
                    printk(KERN_ERR "shprd.pru%u: failed to read back from ADC -> is cape powered? "
                                    "(pin=%u, value=%u)",
                           had_work & 1u, pru_msg.value[0], pru_msg.value[1]);
                    break;
                case MSG_ERR_SAMPLE_MODE: break;
                case MSG_ERR_HRV_ALGO: break;
                case MSG_STATUS_RESTARTING_ROUTINE:
                    printk(KERN_INFO "shprd.pru%u: (re)starting main-routine", had_work & 1u);
                    break;
                case MSG_TEST_ROUTINE:
                    printk(KERN_INFO "shprd.k: [test passed] received answer from "
                                     "pru%u / pipeline %u",
                           had_work & 1u, pru_msg.value[0]);
                    break;
                default:
                    /* these are all handled in userspace and will be passed by sys-fs */
                    printk(KERN_ERR "shprd.k: received invalid command / msg-type = 0x%02X "
                                    "from pru%u",
                           pru_msg.type, had_work & 1u);
            }
        }

        /* resetting to the shortest sleep period */
        step_pos = coord_timer_steps_ns_size - 1u;
    }

    if (pru0_comm_check_send_status() && ring_get(&msg_ringbuf_to_pru, &pru_msg))
    {
        pru0_comm_send_msg(&pru_msg);
        /* resetting to the shortest sleep period */
        step_pos = coord_timer_steps_ns_size - 1u;
    }

    if (canary_counter++ > 100000u)
    {
        canary_counter   = 0u;
        pru_msg.value[0] = mem_interface_check_canaries();
        if (pru_msg.value[0] > 0u)
        {
            pru_msg.id     = MSG_TO_USER;
            pru_msg.type   = MSG_ERR_CANARY;
            pru_msg.canary = CANARY_VALUE_U32;
            ring_put(&msg_ringbuf_from_pru, &pru_msg);
        }
        else printk(KERN_INFO "shprd.k: verified canaries");
    }

    /* variable sleep cycle */
    hrtimer_forward(timer_for_restart, ts_now_kt, ns_to_ktime(coord_timer_steps_ns[step_pos]));

    if (step_pos > 0) step_pos--;

    return HRTIMER_RESTART;
}
