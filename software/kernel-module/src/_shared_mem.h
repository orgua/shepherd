#ifndef _SHARED_MEM_H
#define _SHARED_MEM_H

#include "_commons.h"
#include <linux/types.h>

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
} __attribute__((packed));

/* This is external to expose some attributes through sysfs */
extern void __iomem *pru_shared_mem_io;

#endif //_SHARED_MEM_H
