#include "msg_sys.h"
#include "commons.h"
#include "shared_mem.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(PRU0)
  #define MSG_INBOX                                                                                \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru0_msg_inbox))))
  #define MSG_OUTBOX                                                                               \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru0_msg_outbox))))
  #define MSG_ERROR                                                                                \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru0_msg_error))))
#elif defined(PRU1)
  #define MSG_INBOX                                                                                \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru1_msg_inbox))))
  #define MSG_OUTBOX                                                                               \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru1_msg_outbox))))
  #define MSG_ERROR                                                                                \
      (*((volatile struct ProtoMsg *) (PRU_SHARED_MEM_OFFSET +                                     \
                                       offsetof(struct SharedMem, pru1_msg_error))))
#else
  #error "PRU number must be defined and either 1 or 0"
#endif

void msgsys_init()
{
    MSG_INBOX.unread    = 0u;

    MSG_OUTBOX.unread   = 0u;
    MSG_OUTBOX.type     = 0u;
    MSG_OUTBOX.value[0] = 0u;
    MSG_OUTBOX.id       = MSG_TO_KERNEL;
    MSG_OUTBOX.canary   = CANARY_VALUE_U32;

    MSG_ERROR.unread    = 0u;
    MSG_ERROR.type      = 0u;
    MSG_ERROR.value[0]  = 0u;
    MSG_ERROR.id        = MSG_TO_KERNEL;
    MSG_ERROR.canary    = CANARY_VALUE_U32;
}

// alternative message channel specially dedicated for errors
void msgsys_send_status(enum MsgType type, const uint32_t value1, const uint32_t value2)
{
    // do not care for sent-status -> the newest error wins IF different from previous
    if (!((MSG_ERROR.type == type) && (MSG_ERROR.value[0] == value1)))
    {
        // NOTE: id & canary are set during init (shouldn't change)
        MSG_ERROR.unread   = 0u;
        MSG_ERROR.type     = type;
        MSG_ERROR.value[0] = value1;
        MSG_ERROR.value[1] = value2;
        // NOTE: always make sure that the unread-flag is activated AFTER payload is copied
        MSG_ERROR.unread   = 1u;
    }
    // apply some rate limiting
    if (type >= 0xE0) __delay_cycles(200U / TICK_INTERVAL_NS); // 200 ns
}

// send returns a 1 on success
bool_ft msgsys_send(enum MsgType type, const uint32_t value1, const uint32_t value2)
{
    if (MSG_OUTBOX.unread == 0)
    {
        // NOTE: id & canary are set during init (shouldn't change)
        MSG_OUTBOX.type     = type;
        MSG_OUTBOX.value[0] = value1;
        MSG_OUTBOX.value[1] = value2;
        // NOTE: always make sure that the unread-flag is activated AFTER payload is copied
        MSG_OUTBOX.unread   = 1u;
        return 1u;
    }
    /* Error occurs if kernel was not able to handle previous message in time */
    msgsys_send_status(MSG_ERR_BACKPRESSURE, 0u, 0u);
    return 0u;
}

bool_ft msgsys_check_delivery(void)
{
    return MSG_OUTBOX.unread == 0u; // return 1 if sent
}

// only one central hub should receive, because a message is only handed out once
bool_ft msgsys_receive(struct ProtoMsg *const container)
{
    if (MSG_INBOX.unread >= 1u)
    {
        if (MSG_INBOX.id == MSG_TO_PRU)
        {
            *container       = MSG_INBOX;
            MSG_INBOX.unread = 0u;
            return 1u;
        }
        // send mem_corruption warning
        msgsys_send_status(MSG_ERR_MEM_CORRUPTION, 0u, 0u);
    }
    return 0u;
}
