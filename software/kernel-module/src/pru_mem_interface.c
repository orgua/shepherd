#include <asm/io.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "_commons.h"
#include "_commons_inits.h"
#include "_shared_mem.h"
#include "pru_mem_interface.h"

#define PRU_BASE_ADDR        (0x4A300000ul)
#define PRU_INTC_OFFSET      (0x00020000ul)
#define PRU_INTC_SIZE        (0x400)
#define PRU_INTC_SISR_OFFSET (0x20)

static void __iomem        *pru_intc_io       = NULL;
void __iomem               *pru_shared_mem_io = NULL;

/* This timer is used to schedule a delayed start of the actual sampling on the PRU */
struct hrtimer              delayed_start_timer;
static u8                   init_done = 0;

static enum hrtimer_restart delayed_start_callback(struct hrtimer *timer_for_restart);

void                        mem_interface_init(void)
{
    if (init_done)
    {
        printk(KERN_ERR "shprd.k: mem-interface init requested -> can't init twice!");
        return;
    }
    /* Maps the control registers of the PRU's interrupt controller */
    pru_intc_io       = ioremap(PRU_BASE_ADDR + PRU_INTC_OFFSET, PRU_INTC_SIZE);
    /* Maps the shared memory in the shared DDR, used to exchange info/control between PRU cores and kernel */
    pru_shared_mem_io = ioremap(PRU_BASE_ADDR + PRU_SHARED_MEM_OFFSET, sizeof(struct SharedMem));

    hrtimer_init(&delayed_start_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
    delayed_start_timer.function = &delayed_start_callback;

    init_done                    = 1;
    printk(KERN_INFO "shprd.k: mem-interface initialized, shared mem @ 0x%p", pru_shared_mem_io);

    mem_interface_reset();
}

void mem_interface_exit(void)
{
    if (pru_intc_io != NULL)
    {
        iounmap(pru_intc_io);
        pru_intc_io = NULL;
    }
    if (pru_shared_mem_io != NULL)
    {
        iounmap(pru_shared_mem_io);
        pru_shared_mem_io = NULL;
    }
    hrtimer_cancel(&delayed_start_timer);
    init_done = 0;
    printk(KERN_INFO "shprd.k: mem-interface exited");
}

void mem_interface_reset(void)
{
    struct SharedMem *const shared_mem = (struct SharedMem *) pru_shared_mem_io;
    // TODO: why not use this as default interface?

    if (!init_done)
    {
        printk(KERN_ERR "shprd.k: mem-interface reset requested without prior init");
        return;
    }

    shared_mem->calibration_settings = CalibrationConfig_default;
    shared_mem->converter_settings   = ConverterConfig_default;
    shared_mem->harvester_settings   = HarvesterConfig_default;

    shared_mem->programmer_ctrl      = ProgrammerCtrl_default;

    shared_mem->pru0_msg_inbox       = ProtoMsg_default;
    //shared_mem->pru0_msg_outbox      = ProtoMsg_default;  // Owned by PRU
    //shared_mem->pru0_msg_error       = ProtoMsg_default;

    shared_mem->pru1_msg_inbox       = ProtoMsg_default;
    //shared_mem->pru1_msg_outbox      = ProtoMsg_default; // Owned by PRU
    //shared_mem->pru1_msg_error       = ProtoMsg_default;
    shared_mem->canary               = CANARY_VALUE_U32;
    printk(KERN_INFO "shprd.k: mem-interface reset to default");
}


static enum hrtimer_restart delayed_start_callback(struct hrtimer *timer_for_restart)
{
    /* Timestamp system clock */
    const uint64_t now_ns_system = ktime_get_real_ns();

    mem_interface_set_state(STATE_RUNNING);

    printk(KERN_INFO "shprd.k: Triggered delayed start  @ %llu (now)", now_ns_system);
    return HRTIMER_NORESTART;
}

int mem_interface_schedule_delayed_start(unsigned int start_time_second)
{
    ktime_t  kt_trigger;
    uint64_t ts_trigger_ns;

    kt_trigger    = ktime_set((const s64) start_time_second, 0);

    /**
     * The timer should fire in the middle of the interval before we want to
     * start. This allows the PRU enough time to receive the interrupt and
     * prepare itself to start at exactly the right time.
     */
    kt_trigger    = ktime_sub_ns(kt_trigger, 3 * SYNC_INTERVAL_NS / 4); // TODO: try 15/16 or larger

    ts_trigger_ns = ktime_to_ns(kt_trigger);

    printk(KERN_INFO "shprd.k: Delayed start timer set to %llu", ts_trigger_ns);

    hrtimer_start(&delayed_start_timer, kt_trigger, HRTIMER_MODE_ABS);

    return 0;
}

int  mem_interface_cancel_delayed_start(void) { return hrtimer_cancel(&delayed_start_timer); }

void mem_interface_trigger(unsigned int system_event)
{
    /* Raise Interrupt on PRU INTC*/
    writel(system_event, pru_intc_io + PRU_INTC_SISR_OFFSET);
}

enum ShepherdState mem_interface_get_state(void)
{
    return (enum ShepherdState) readl(pru_shared_mem_io +
                                      offsetof(struct SharedMem, shp_pru_state));
}

void mem_interface_set_state(enum ShepherdState state)
{
    writel(state, pru_shared_mem_io + offsetof(struct SharedMem, shp_pru_state));
}

// TODO: unify send/receive functions a lot of duplication
unsigned char pru1_comm_receive_sync_request(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru1_msg_outbox);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);

    /* testing for unread-msg-token */
    if (readb(pru_shared_mem_io + offset_unread) >= 1u)
    {
        /* if unread, then continue to copy request */
        memcpy_fromio(msg, pru_shared_mem_io + offset_msg, sizeof(struct ProtoMsg));
        /* mark as read */
        writeb(0u, pru_shared_mem_io + offset_unread);

        if (msg->id != MSG_TO_KERNEL) /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd.k: recv_sync_req from pru1 -> mem corruption? id=%u (!=%u)",
                   msg->id, MSG_TO_KERNEL);
        if (msg->canary != CANARY_VALUE_U32)
            printk(KERN_ERR "shprd.k: recv_sync_req from PRU1 -> canary was harmed");
        return 1;
    }
    return 0;
}


unsigned char pru1_comm_send_sync_reply(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru1_msg_inbox);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);
    const unsigned char   status        = readb(pru_shared_mem_io + offset_unread) == 0u;

    /* first update payload in memory */
    msg->id                             = MSG_TO_PRU;
    msg->unread                         = 0u;
    msg->canary                         = CANARY_VALUE_U32;
    memcpy_toio(pru_shared_mem_io + offset_msg, msg, sizeof(struct ProtoMsg));

    /* activate message with unread-token */
    writeb(1u, pru_shared_mem_io + offset_unread);
    return status;
}


unsigned char pru0_comm_receive_error(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru0_msg_error);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);

    /* testing for unread-msg-token */
    if (readb(pru_shared_mem_io + offset_unread) >= 1u)
    {
        /* if unread, then continue to copy request */
        memcpy_fromio(msg, pru_shared_mem_io + offset_msg, sizeof(struct ProtoMsg));
        /* mark as read */
        writeb(0u, pru_shared_mem_io + offset_unread);

        if (msg->id != MSG_TO_KERNEL) /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd.k: recv_status from pru0 -> mem corruption? id=%u (!=%u)",
                   msg->id, MSG_TO_KERNEL);
        if (msg->canary != CANARY_VALUE_U32)
            printk(KERN_ERR "shprd.k: recv_error from PRU0 -> canary was harmed");
        return 1;
    }
    return 0;
}


unsigned char pru1_comm_receive_error(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru1_msg_error);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);

    /* testing for unread-msg-token */
    if (readb(pru_shared_mem_io + offset_unread) >= 1u)
    {
        /* if unread, then continue to copy request */
        memcpy_fromio(msg, pru_shared_mem_io + offset_msg, sizeof(struct ProtoMsg));
        /* mark as read */
        writeb(0u, pru_shared_mem_io + offset_unread);

        if (msg->id != MSG_TO_KERNEL) /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd.k: recv_status from pru1 -> mem corruption? id=%u (!=%u)",
                   msg->id, MSG_TO_KERNEL);
        if (msg->canary != CANARY_VALUE_U32)
            printk(KERN_ERR "shprd.k: recv_error from PRU1 -> canary was harmed");
        return 1;
    }
    return 0;
}


unsigned char pru0_comm_receive_msg(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru0_msg_outbox);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);

    /* testing for unread-msg-token */
    if (readb(pru_shared_mem_io + offset_unread) >= 1u)
    {
        /* if unread, then continue to copy request */
        memcpy_fromio(msg, pru_shared_mem_io + offset_msg, sizeof(struct ProtoMsg));
        /* mark as read */
        writeb(0u, pru_shared_mem_io + offset_unread);

        if (msg->id != MSG_TO_KERNEL) /* Error occurs if something writes over boundaries */
            printk(KERN_ERR "shprd.k: recv_msg from pru0 -> mem corruption? id=%u (!=%u)", msg->id,
                   MSG_TO_KERNEL);
        if (msg->canary != CANARY_VALUE_U32)
            printk(KERN_ERR "shprd.k: recv_msg from PRU1 -> canary was harmed");
        return 1;
    }
    return 0;
}


unsigned char pru0_comm_send_msg(struct ProtoMsg *const msg)
{
    static const uint32_t offset_msg    = offsetof(struct SharedMem, pru0_msg_inbox);
    static const uint32_t offset_unread = offset_msg + offsetof(struct ProtoMsg, unread);
    const unsigned char   status        = readb(pru_shared_mem_io + offset_unread) == 0u;

    /* first update payload in memory */
    msg->id                             = MSG_TO_PRU;
    msg->unread                         = 0u;
    msg->canary                         = CANARY_VALUE_U32;
    memcpy_toio(pru_shared_mem_io + offset_msg, msg, sizeof(struct ProtoMsg));

    /* activate message with unread-token */
    writeb(1u, pru_shared_mem_io + offset_unread);
    return status;
}

unsigned char pru0_comm_check_send_status(void)
{
    static const uint32_t offset_unread =
            offsetof(struct SharedMem, pru0_msg_inbox) + offsetof(struct ProtoMsg, unread);
    return readb(pru_shared_mem_io + offset_unread) == 0u;
}
