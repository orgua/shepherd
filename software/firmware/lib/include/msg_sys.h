#ifndef MSG_SYS_H
#define MSG_SYS_H

#include "commons.h"
#include <stdint.h>

void    msgsys_init();

// alternative message channel specially dedicated for errors
void    msgsys_send_status(enum MsgType type, const uint32_t value1, const uint32_t value2);

// send returns a 1 on success
bool_ft msgsys_send(enum MsgType type, const uint32_t value1, const uint32_t value2);

// return 1 if received
bool_ft msgsys_check_delivery(void);

// only one central hub should receive, because a message is only handed out once
bool_ft msgsys_receive(struct ProtoMsg *const container);

#endif //MSG_SYS_H
