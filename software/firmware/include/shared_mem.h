#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include "commons.h"
#include "stdint_fast.h"

/* Format of memory structure shared between PRU0, PRU1 and kernel module (lives in shared RAM of PRUs) */
struct SharedMem
{
    /* NOTE: start of region read-accessed by kernel module, but controlled by PRUs. */
    /* safety */
    volatile uint32_t           canary1;
    /* Stores state & the mode, e.g. harvester or emulator */
    volatile uint32_t           shp_pru_state;
    volatile uint32_t           shp_pru0_mode;
    /**
    * Parameters of buffer structures in current far & slow RAM.
    * Only PRU0 knows about physical address of shared area in DDR RAM,
    * that is used to exchange data between user space and PRUs.
    */
    volatile struct IVTraceInp *buffer_iv_inp_ptr;
    volatile struct IVTraceOut *buffer_iv_out_ptr;
    volatile struct GPIOTrace  *buffer_gpio_ptr;
    volatile struct UtilTrace  *buffer_util_ptr;
    /* internal fast index to far-buffers */
    volatile uint32_t           buffer_iv_idx;   // write by pru0 only
    volatile uint32_t           buffer_gpio_idx; // write by pru1 only
    volatile uint32_t           buffer_util_idx; // write by pru1 only
    /* size of these buffers - allows cheap verification in userspace */
    volatile uint32_t           buffer_size;
    volatile uint32_t           buffer_iv_inp_size;
    volatile uint32_t           buffer_iv_out_size;
    volatile uint32_t           buffer_gpio_size;
    volatile uint32_t           buffer_util_size;
    /* NOTE: start of region controlled by kModule. */
    /* userspace buffer-states */
    volatile uint32_t           buffer_iv_inp_sys_idx; // write by kMod only, TODO: consider in PRU
    /* Cache System (for buffer_iv_inp) to avoid far/slow RAM-reads */
    volatile uint32_t           cache_flags[CACHE_FLAG_SIZE_U32_N]; // write by kMod only
    /* Allows setting a fixed voltage for the seconds DAC-Output (Channel A),
     * TODO: this has to be optimized, allow better control (off, link to ch-b, change NOW) */
    volatile uint32_t           dac_auxiliary_voltage_raw;
    /* safety */
    volatile uint32_t           canary2; // write by pru0 only
    /* ADC calibration settings */
    volatile struct CalibrationConfig calibration_settings; // write by kMod only
    /* This structure defines all settings of virtual converter emulation*/
    volatile struct ConverterConfig   converter_settings; // write by kMod only
    volatile struct HarvesterConfig   harvester_settings; // write by kMod only
    /* settings for programmer-subroutines */
    volatile struct ProgrammerCtrl    programmer_ctrl; // write by kMod only
    /* Msg-System-replacement for slow rpmsg (check 640ns, receive 2820 on pru0 and 4820ns on pru1) */
    volatile struct ProtoMsg          pru0_msg_inbox; // write by kMod only
    volatile struct ProtoMsg          pru0_msg_outbox;
    volatile struct ProtoMsg          pru0_msg_error;
    volatile struct ProtoMsg          pru1_msg_inbox; // write by kMod only
    volatile struct ProtoMsg          pru1_msg_outbox;
    volatile struct ProtoMsg          pru1_msg_error;
    /* safety */
    volatile uint32_t                 canary3; // write by pru0 only
    /* NOTE: End of region accessed by kernel module */

    /* Used to use/exchange timestamp of last sample taken & next buffer between PRU1 and PRU0 */
    volatile uint64_t                 last_sync_timestamp_ns;
    volatile uint64_t                 next_sync_timestamp_ns;
    /* internal gpio-register from PRU1 (for PRU1, debug), only updated when not running */
    volatile uint32_t                 gpio_pin_state;

    /* Token system to ensure both PRUs can share interrupts */
    volatile bool_ft                  cmp0_trigger_for_pru1;
    volatile bool_ft                  cmp1_trigger_for_pru1;
    /* BATOK Msg system -> PRU0 decides about state, but PRU1 has control over Pin */
    volatile bool_ft                  vsource_batok_trigger_for_pru1;
    volatile bool_ft                  vsource_batok_pin_value;
    /* Trigger to control sampling of gpios */
    volatile bool_ft                  vsource_skip_gpio_logging;
    /* active utilization-monitor for PRU0 */
    volatile uint32_t                 pru0_ns_per_sample;
} __attribute__((packed));

ASSERT(shared_mem_size, sizeof(struct SharedMem) < 10000u);
// NOTE: PRUs shared ram should be even 12kb

// NOTE: GCC-way preferred as cgt builds to 62204 bytes instead of 64244
#define SHARED_MEM (*((volatile struct SharedMem *) PRU_SHARED_MEM_OFFSET))
//volatile struct SharedMem SHARED_MEM __attribute__((cregister("PRU_SHAREDMEM", near), peripheral));

#endif //SHARED_MEM_H
