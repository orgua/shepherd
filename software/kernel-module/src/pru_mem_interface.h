#ifndef PRU_MEM_INTERFACE_H_
#define PRU_MEM_INTERFACE_H_
#include "_commons.h"

/**
 * Initializes communication between our kernel module and the PRUs.
 *
 * Maps the PRU's interrupt controller's memory to allow triggering system
 * events, causing 'interrupts' on the PRUs.
 * Maps the shared memory structure within the PRU's 'shared RAM' memory
 * region.
 */
void               mem_interface_init(void);
/**
 * Clean up communication between our kernel module and the PRUs.
 *
 * @see mem_interface_init()
 */
void               mem_interface_exit(void);

void               mem_interface_reset(void);

/* test the 11 canaries that are placed in shared-mem */
uint32_t           mem_interface_check_canaries(void);
/**
 * Trigger a system event on the PRUs
 *
 * The PRUs have an interrupt controller (INTC), which connects 64 so-called
 * 'system events' to the PRU's interrupt system. We use some of these system
 * events to communicate time-critical events from this Linux kernel module to
 * the PRUs
 */
void               mem_interface_trigger(unsigned int system_event);

/**
 * Schedule start of the actual sampling at a later point in time
 *
 * It is hard to execute a command simultaneously on a set of Linux hosts.
 * This is however necessary, especially for emulation, where all shepherd
 * nodes should start replaying samples at the same time. This function allows
 * to register a trigger at a defined time with respect to the CLOCK_REALTIME.
 *
 * @param start_time_second desired system time in seconds at which PRUs should start sampling/replaying
 */
int                mem_interface_schedule_delayed_start(unsigned int start_time_second);

/**
 * Cancel a previously scheduled 'delayed start'
 *
 * @see mem_interface_trigger()
 */
int                mem_interface_cancel_delayed_start(void);

/**
 * Read the 'shepherd state' from the PRUs
 *
 * This kernel module usually requests state changes from the PRU. By reading
 * the state from the shared memory structure, we can check in which state
 * the PRUs actually are.
 */
enum ShepherdState mem_interface_get_state(void);
/**
 * Set the 'shepherd state'
 *
 * When scheduling a delayed start, it is necessary that we directly change the
 * shepherd state from within the kernel module by directly writing the
 * corresponding value to the shared memory structure
 *
 * @param state new shepherd state
 * @see SharedMem
 */
void               mem_interface_set_state(enum ShepherdState state);

/**
 * Receives Sync-Messages from PRU1
 * @param msg
 * @return success = 1, error = 0
 */
unsigned char      pru1_comm_receive_sync_request(struct ProtoMsg *const msg);
/**
 * Sends Sync-Messages to PRU1, error occurs on send when previous msg was not yet received (will be overwritten)
 * @param msg
 * @return success = 1, error = 0
 */
unsigned char      pru1_comm_send_sync_reply(struct ProtoMsg *const msg);

/*
 * COM-System between kernel module and PRU0
 * ERROR occurs on send when previous msg was not yet received (will be overwritten)
 * @param msg_container is a ProtoMsg
 * @return success = 1, error = 0
 */
unsigned char      pru0_comm_receive_error(struct ProtoMsg *const msg);
unsigned char      pru1_comm_receive_error(struct ProtoMsg *const msg);

unsigned char      pru0_comm_receive_msg(struct ProtoMsg *const msg);
unsigned char      pru0_comm_send_msg(struct ProtoMsg *const msg);
/*
 * send_status -> returns 1 if last sent msg was received and buffer is free to fill, 0 otherwise
 */
unsigned char      pru0_comm_check_send_status(void);


#endif /* PRU_MEM_INTERFACE_H_ */
