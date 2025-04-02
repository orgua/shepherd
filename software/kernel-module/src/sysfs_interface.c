#include <asm/io.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "_commons.h"
#include "_shared_mem.h"
#include "pru_firmware.h"
#include "pru_mem_interface.h"
#include "pru_msg_sys.h"
#include "pru_sync_control.h"

#include "sysfs_interface.h"


int             schedule_start(unsigned int start_time_second);

struct kobject *kobj_ref;
struct kobject *kobj_mem_ref;
struct kobject *kobj_sync_ref;
struct kobject *kobj_prog_ref;
struct kobject *kobj_firmware_ref;

static ssize_t  sysfs_sync_error_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t  sysfs_sync_error_sum_show(struct kobject *kobj, struct kobj_attribute *attr,
                                          char *buf);

static ssize_t  sysfs_sync_correction_show(struct kobject *kobj, struct kobj_attribute *attr,
                                           char *buf);

static ssize_t  sysfs_SharedMem_show(struct kobject *const kobj, struct kobj_attribute *attr,
                                     char *const buf);

static ssize_t  sysfs_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_state_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                                 size_t count);

static ssize_t sysfs_mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                                size_t count);

static ssize_t sysfs_auxiliary_voltage_store(struct kobject *kobj, struct kobj_attribute *attr,
                                             const char *buf, size_t count);

static ssize_t sysfs_calibration_settings_store(struct kobject *kobj, struct kobj_attribute *attr,
                                                const char *buf, size_t count);

static ssize_t sysfs_calibration_settings_show(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buf);

static ssize_t sysfs_virtual_converter_settings_store(struct kobject        *kobj,
                                                      struct kobj_attribute *attr, const char *buf,
                                                      size_t count);

static ssize_t sysfs_virtual_converter_settings_show(struct kobject        *kobj,
                                                     struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_virtual_harvester_settings_store(struct kobject        *kobj,
                                                      struct kobj_attribute *attr, const char *buf,
                                                      size_t count);

static ssize_t sysfs_virtual_harvester_settings_show(struct kobject        *kobj,
                                                     struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_pru_msg_system_store(struct kobject *kobj, struct kobj_attribute *attr,
                                          const char *buffer, size_t count);
static ssize_t sysfs_pru_msg_system_show(struct kobject *kobj, struct kobj_attribute *attr,
                                         char *buffer);

static ssize_t sysfs_prog_state_store(struct kobject *kobj, struct kobj_attribute *attr,
                                      const char *buffer, size_t count);
static ssize_t sysfs_prog_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_prog_target_store(struct kobject *kobj, struct kobj_attribute *attr,
                                       const char *buffer, size_t count);
static ssize_t sysfs_prog_target_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t sysfs_prog_datarate_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count);
static ssize_t sysfs_prog_datasize_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count);
static ssize_t sysfs_prog_pin_store(struct kobject *kobj, struct kobj_attribute *attr,
                                    const char *buffer, size_t count);

static ssize_t sysfs_pru0_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buf);
static ssize_t sysfs_pru1_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buf);
static ssize_t sysfs_pru0_firmware_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count);
static ssize_t sysfs_pru1_firmware_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count);

struct kobj_attr_struct_s
{
    struct kobj_attribute attr;
    unsigned int          val_offset;
};

struct kobj_attribute     attr_state = __ATTR(state, 0660, sysfs_state_show, sysfs_state_store);

struct kobj_attr_struct_s attr_buffer_iv_inp_ptr = {
        .attr       = __ATTR(iv_inp_address, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_iv_inp_ptr)};
struct kobj_attr_struct_s attr_buffer_iv_inp_size = {
        .attr       = __ATTR(iv_inp_size, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_iv_inp_size)};
struct kobj_attr_struct_s attr_buffer_iv_out_ptr = {
        .attr       = __ATTR(iv_out_address, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_iv_out_ptr)};
struct kobj_attr_struct_s attr_buffer_iv_out_size = {
        .attr       = __ATTR(iv_out_size, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_iv_out_size)};
struct kobj_attr_struct_s attr_buffer_gpio_ptr = {
        .attr       = __ATTR(gpio_address, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_gpio_ptr)};
struct kobj_attr_struct_s attr_buffer_gpio_size = {
        .attr       = __ATTR(gpio_size, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_gpio_size)};
struct kobj_attr_struct_s attr_buffer_util_ptr = {
        .attr       = __ATTR(util_address, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_util_ptr)};
struct kobj_attr_struct_s attr_buffer_util_size = {
        .attr       = __ATTR(util_size, 0660, sysfs_SharedMem_show, NULL),
        .val_offset = offsetof(struct SharedMem, buffer_util_size)};

struct kobj_attr_struct_s attr_mode = {
        .attr       = __ATTR(mode, 0660, sysfs_mode_show, sysfs_mode_store),
        .val_offset = offsetof(struct SharedMem, shp_pru0_mode)};
struct kobj_attr_struct_s attr_auxiliary_voltage = {
        .attr       = __ATTR(dac_auxiliary_voltage_raw, 0660, sysfs_SharedMem_show,
                             sysfs_auxiliary_voltage_store),
        .val_offset = offsetof(struct SharedMem, dac_auxiliary_voltage_raw)};
struct kobj_attr_struct_s attr_calibration_settings = {
        .attr       = __ATTR(calibration_settings, 0660, sysfs_calibration_settings_show,
                             sysfs_calibration_settings_store),
        .val_offset = offsetof(struct SharedMem, calibration_settings)};
struct kobj_attr_struct_s attr_virtual_converter_settings = {
        .attr = __ATTR(virtual_converter_settings, 0660, sysfs_virtual_converter_settings_show,
                       sysfs_virtual_converter_settings_store),
        .val_offset = offsetof(struct SharedMem, converter_settings)};
struct kobj_attr_struct_s attr_virtual_harvester_settings = {
        .attr = __ATTR(virtual_harvester_settings, 0660, sysfs_virtual_harvester_settings_show,
                       sysfs_virtual_harvester_settings_store),
        .val_offset = offsetof(struct SharedMem, harvester_settings)};
struct kobj_attr_struct_s attr_pru_msg_system_settings = {
        .attr = __ATTR(pru_msg_box, 0660, sysfs_pru_msg_system_show, sysfs_pru_msg_system_store),
        .val_offset = 0};


struct kobj_attr_struct_s attr_prog_state = {
        .attr       = __ATTR(state, 0660, sysfs_prog_state_show, sysfs_prog_state_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, state)};
struct kobj_attr_struct_s attr_prog_target = {
        .attr       = __ATTR(target, 0660, sysfs_prog_target_show, sysfs_prog_target_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, target)};
struct kobj_attr_struct_s attr_prog_datarate = {
        .attr       = __ATTR(datarate, 0660, sysfs_SharedMem_show, sysfs_prog_datarate_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, datarate)};
struct kobj_attr_struct_s attr_prog_datasize = {
        .attr       = __ATTR(datasize, 0660, sysfs_SharedMem_show, sysfs_prog_datasize_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, datasize)};
struct kobj_attr_struct_s attr_prog_pin_tck = {
        .attr       = __ATTR(pin_tck, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_tck)};
struct kobj_attr_struct_s attr_prog_pin_tdio = {
        .attr       = __ATTR(pin_tdio, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_tdio)};
struct kobj_attr_struct_s attr_prog_pin_dir_tdio = {
        .attr       = __ATTR(pin_dir_tdio, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_dir_tdio)};
struct kobj_attr_struct_s attr_prog_pin_tdo = {
        .attr       = __ATTR(pin_tdo, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_tdo)};
struct kobj_attr_struct_s attr_prog_pin_tms = {
        .attr       = __ATTR(pin_tms, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_tms)};
struct kobj_attr_struct_s attr_prog_pin_dir_tms = {
        .attr       = __ATTR(pin_dir_tms, 0660, sysfs_SharedMem_show, sysfs_prog_pin_store),
        .val_offset = offsetof(struct SharedMem, programmer_ctrl) +
                      offsetof(struct ProgrammerCtrl, pin_dir_tms)};

struct kobj_attr_struct_s attr_pru0_firmware = {
        .attr = __ATTR(pru0_firmware, 0660, sysfs_pru0_firmware_show, sysfs_pru0_firmware_store),
        .val_offset = 0};
struct kobj_attr_struct_s attr_pru1_firmware = {
        .attr = __ATTR(pru1_firmware, 0660, sysfs_pru1_firmware_show, sysfs_pru1_firmware_store),
        .val_offset = 0};

struct kobj_attribute attr_sync_error = __ATTR(error, 0660, sysfs_sync_error_show, NULL);

struct kobj_attribute attr_sync_correction =
        __ATTR(correction, 0660, sysfs_sync_correction_show, NULL);

struct kobj_attribute attr_sync_error_sum =
        __ATTR(error_sum, 0660, sysfs_sync_error_sum_show, NULL);

static struct attribute *pru_attrs[] = {
        &attr_mode.attr.attr,
        &attr_auxiliary_voltage.attr.attr,
        &attr_calibration_settings.attr.attr,
        &attr_virtual_converter_settings.attr.attr,
        &attr_virtual_harvester_settings.attr.attr,
        &attr_pru_msg_system_settings.attr.attr,
        NULL,
};

static struct attribute_group attr_group = {
        .attrs = pru_attrs,
};


static struct attribute *pru_mem_attrs[] = {
        &attr_buffer_iv_inp_ptr.attr.attr,
        &attr_buffer_iv_inp_size.attr.attr,
        &attr_buffer_iv_out_ptr.attr.attr,
        &attr_buffer_iv_out_size.attr.attr,
        &attr_buffer_gpio_ptr.attr.attr,
        &attr_buffer_gpio_size.attr.attr,
        &attr_buffer_util_ptr.attr.attr,
        &attr_buffer_util_size.attr.attr,
        NULL,
};
static struct attribute_group attr_mem_group = {
        .attrs = pru_mem_attrs,
};


static struct attribute *pru_prog_attrs[] = {
        &attr_prog_state.attr.attr,
        &attr_prog_target.attr.attr,
        &attr_prog_datarate.attr.attr,
        &attr_prog_datasize.attr.attr,
        &attr_prog_pin_tck.attr.attr,
        &attr_prog_pin_tdio.attr.attr,
        &attr_prog_pin_dir_tdio.attr.attr,
        &attr_prog_pin_tdo.attr.attr,
        &attr_prog_pin_tms.attr.attr,
        &attr_prog_pin_dir_tms.attr.attr,
        NULL,
};
static struct attribute_group attr_prog_group = {
        .attrs = pru_prog_attrs,
};


static struct attribute *pru_sync_attrs[] = {
        &attr_sync_error.attr,
        &attr_sync_error_sum.attr,
        &attr_sync_correction.attr,
        NULL,
};
static struct attribute_group attr_sync_group = {
        .attrs = pru_sync_attrs,
};


static struct attribute *pru_firmware_attrs[] = {
        &attr_pru0_firmware.attr.attr,
        &attr_pru1_firmware.attr.attr,
        NULL,
};
static struct attribute_group attr_firmware_group = {
        .attrs = pru_firmware_attrs,
};


static ssize_t sysfs_SharedMem_show(struct kobject *const kobj, struct kobj_attribute *attr,
                                    char *const buf)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    return sprintf(buf, "%u", ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset));
}

static ssize_t sysfs_sync_error_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u", 0u);
}

static ssize_t sysfs_sync_error_sum_show(struct kobject *kobj, struct kobj_attribute *attr,
                                         char *buf)
{
    return sprintf(buf, "%u", 0u); // TODO: remove
}

static ssize_t sysfs_sync_correction_show(struct kobject *kobj, struct kobj_attribute *attr,
                                          char *buf)
{
    return sprintf(buf, "%u", 0u); // TODO: remove
}

static ssize_t sysfs_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    switch (mem_interface_get_state())
    {
        case STATE_IDLE: return sprintf(buf, "idle");
        case STATE_ARMED: return sprintf(buf, "armed");
        case STATE_RUNNING: return sprintf(buf, "running");
        case STATE_RESET: return sprintf(buf, "reset");
        case STATE_FAULT: return sprintf(buf, "fault");
        default: return sprintf(buf, "unknown");
    }
}

static ssize_t sysfs_state_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                                 size_t count)
{
    time64_t kt_sec;
    int      tmp;

    if (strncmp(buf, "start", 5) == 0)
    {
        if ((count < 5) || (count > 6)) return -EINVAL;

        if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

        mem_interface_set_state(STATE_RUNNING);
        return count;
    }

    else if (strncmp(buf, "stop", 4) == 0)
    {
        if ((count < 4) || (count > 5)) return -EINVAL;

        mem_interface_cancel_delayed_start();
        mem_interface_set_state(STATE_RESET);
        return count;
    }

    else if (sscanf(buf, "%d", &tmp) == 1)
    {
        if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;
        /* Timestamp system clock */
        kt_sec = ktime_get_real_seconds();

        if (tmp < kt_sec + 1) return -EINVAL;
        printk(KERN_INFO "shprd.k: Setting start-timestamp to %d", tmp);
        mem_interface_set_state(STATE_ARMED);
        mem_interface_schedule_delayed_start(tmp);
        return count;
    }
    else return -EINVAL;
}

static ssize_t sysfs_mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    const unsigned int mode = ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset);

    switch (mode)
    {
        case MODE_HARVESTER: return sprintf(buf, "harvester");
        case MODE_HRV_ADC_READ: return sprintf(buf, "hrv_adc_read");
        case MODE_EMULATOR: return sprintf(buf, "emulator");
        case MODE_EMU_ADC_READ: return sprintf(buf, "emu_adc_read");
        case MODE_EMU_LOOPBACK: return sprintf(buf, "emu_loopback");
        case MODE_DEBUG: return sprintf(buf, "debug");
        case MODE_NONE: return sprintf(buf, "none");
        default: return -EINVAL;
    }
}

static ssize_t sysfs_mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                                size_t count)
{
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    unsigned int                     mode;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    // note: longer string must come first in case of similar strings (emulation_cal, emulation)
    if (strncmp(buf, "harvester", 9) == 0)
    {
        if ((count < 9) || (count > 10)) return -EINVAL;
        mode = MODE_HARVESTER;
    }
    else if (strncmp(buf, "hrv_adc_read", 12) == 0)
    {
        if ((count < 12) || (count > 13)) return -EINVAL;
        mode = MODE_HRV_ADC_READ;
    }
    else if (strncmp(buf, "emulator", 8) == 0)
    {
        if ((count < 8) || (count > 9)) return -EINVAL;
        mode = MODE_EMULATOR;
    }
    else if (strncmp(buf, "emu_adc_read", 12) == 0)
    {
        if ((count < 12) || (count > 13)) return -EINVAL;
        mode = MODE_EMU_ADC_READ;
    }
    else if (strncmp(buf, "emu_loopback", 12) == 0)
    {
        if ((count < 12) || (count > 13)) return -EINVAL;
        mode = MODE_EMU_LOOPBACK;
    }
    else if (strncmp(buf, "debug", 5) == 0)
    {
        if ((count < 5) || (count > 6)) return -EINVAL;
        mode = MODE_DEBUG;
    }
    else if (strncmp(buf, "none", 4) == 0)
    {
        if ((count < 4) || (count > 5)) return -EINVAL;
        mode = MODE_NONE;
    }
    else return -EINVAL;

    iowrite32(mode, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    printk(KERN_INFO "shprd.k: new mode = %d (%s)", mode, buf);
    mem_interface_set_state(STATE_RESET);
    return count;
}

static ssize_t sysfs_auxiliary_voltage_store(struct kobject *kobj, struct kobj_attribute *attr,
                                             const char *buf, size_t count)
{
    unsigned int                     tmp;
    const struct kobj_attr_struct_s *kobj_attr_wrapped;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (sscanf(buf, "%u", &tmp) == 1)
    {
        printk(KERN_INFO "shprd.k: Setting auxiliary DAC-voltage to raw %u", tmp);
        iowrite32(tmp, pru_shared_mem_io + kobj_attr_wrapped->val_offset);

        mem_interface_set_state(STATE_RESET); // TODO: really needed?
        return count;
    }

    return -EINVAL;
}

static ssize_t sysfs_calibration_settings_store(struct kobject *kobj, struct kobj_attribute *attr,
                                                const char *buf, size_t count)
{
    struct CalibrationConfig         tmp;
    const struct kobj_attr_struct_s *kobj_attr_wrapped;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (sscanf(buf, "%u %d %u %d %u %d", &tmp.adc_current_factor_nA_n8, &tmp.adc_current_offset_nA,
               &tmp.adc_voltage_factor_uV_n8, &tmp.adc_voltage_offset_uV,
               &tmp.dac_voltage_inv_factor_uV_n20, &tmp.dac_voltage_offset_uV) == 6)
    {
        printk(KERN_INFO "shprd: Setting ADC-Current calibration config. gain: %d, offset: %d",
               tmp.adc_current_factor_nA_n8, tmp.adc_current_offset_nA);

        printk(KERN_INFO "shprd: Setting ADC-Voltage calibration config. gain: %d, offset: %d",
               tmp.adc_voltage_factor_uV_n8, tmp.adc_voltage_offset_uV);

        printk(KERN_INFO "shprd: Setting DAC-Voltage calibration config. gain: %d, offset: %d",
               tmp.dac_voltage_inv_factor_uV_n20, tmp.dac_voltage_offset_uV);

        iowrite32(tmp.adc_current_factor_nA_n8,
                  pru_shared_mem_io + kobj_attr_wrapped->val_offset + 0);
        iowrite32(tmp.adc_current_offset_nA, pru_shared_mem_io + kobj_attr_wrapped->val_offset + 4);
        iowrite32(tmp.adc_voltage_factor_uV_n8,
                  pru_shared_mem_io + kobj_attr_wrapped->val_offset + 8);
        iowrite32(tmp.adc_voltage_offset_uV,
                  pru_shared_mem_io + kobj_attr_wrapped->val_offset + 12);
        iowrite32(tmp.dac_voltage_inv_factor_uV_n20,
                  pru_shared_mem_io + kobj_attr_wrapped->val_offset + 16);
        iowrite32(tmp.dac_voltage_offset_uV,
                  pru_shared_mem_io + kobj_attr_wrapped->val_offset + 20);
        /* TODO: this should copy the struct in one go */

        return count;
    }

    return -EINVAL;
}

static ssize_t sysfs_calibration_settings_show(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buf)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);

    return sprintf(buf, "%u %d \n%u %d \n%u %d \n",
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 0),
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 4),
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 8),
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 12),
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 16),
                   ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset + 20));
}

static ssize_t sysfs_virtual_converter_settings_store(struct kobject        *kobj,
                                                      struct kobj_attribute *attr,
                                                      const char *buffer, size_t count)
{
    const uint32_t inp_lut_size = LUT_SIZE * LUT_SIZE * 1u;
    const uint32_t out_lut_size = LUT_SIZE * 4u;
    const uint32_t non_lut_size = sizeof(struct ConverterConfig) - inp_lut_size - out_lut_size - 4u;
    // ignore canary
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         mem_offset = 0u;
    int32_t                          buf_pos    = 0;
    uint32_t                         i          = 0u;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    /* u32 beginning of struct */
    mem_offset        = kobj_attr_wrapped->val_offset;
    for (i = 0; i < non_lut_size; i += 4)
    {
        uint32_t value_retrieved;
        int32_t  value_length;
        int32_t  ret = sscanf(&buffer[buf_pos], "%u%n ", &value_retrieved, &value_length);
        buf_pos += value_length;
        if (ret != 1) return -EINVAL;
        iowrite32(value_retrieved, pru_shared_mem_io + mem_offset + i);
    }

    /* u8 input LUT */
    mem_offset = kobj_attr_wrapped->val_offset + non_lut_size;
    for (i = 0; i < inp_lut_size; i += 1)
    {
        uint32_t value_retrieved;
        int32_t  value_length;
        int32_t  ret = sscanf(&buffer[buf_pos], "%u%n ", &value_retrieved, &value_length);
        buf_pos += value_length;
        if (ret != 1) return -EINVAL;
        if (value_retrieved > 255)
            printk(KERN_WARNING "shprd.k: virtual Converter parsing got a u8-value out of bound");
        iowrite8((uint8_t) value_retrieved, pru_shared_mem_io + mem_offset + i);
    }

    /* u32 output LUT */
    mem_offset = kobj_attr_wrapped->val_offset + non_lut_size + inp_lut_size;
    for (i = 0; i < out_lut_size; i += 4)
    {
        uint32_t value_retrieved;
        int32_t  value_length;
        int32_t  ret = sscanf(&buffer[buf_pos], "%u%n ", &value_retrieved, &value_length);
        buf_pos += value_length;
        if (ret != 1) return -EINVAL;
        iowrite32(value_retrieved, pru_shared_mem_io + mem_offset + i);
    }

    printk(KERN_INFO "shprd.k: Setting Virtual Converter Config");

    return count;
}

static ssize_t sysfs_virtual_converter_settings_show(struct kobject        *kobj,
                                                     struct kobj_attribute *attr, char *buf)
{
    const uint32_t inp_lut_size = LUT_SIZE * LUT_SIZE * 1u;
    const uint32_t out_lut_size = LUT_SIZE * 4u;
    const uint32_t non_lut_size = sizeof(struct ConverterConfig) - inp_lut_size - out_lut_size - 4u;
    // ignore canary
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    uint32_t mem_offset = 0u;
    uint32_t i          = 0u;
    int      count      = 0;

    /* u32 beginning of struct */
    mem_offset          = kobj_attr_wrapped->val_offset;
    for (i = 0; i < non_lut_size; i += 4)
    {
        count += sprintf(buf + strlen(buf), "%u \n", ioread32(pru_shared_mem_io + mem_offset + i));
    }

    /* u8 input LUT */
    mem_offset = kobj_attr_wrapped->val_offset + non_lut_size;
    for (i = 0; i < inp_lut_size; i += 1)
    {
        count += sprintf(buf + strlen(buf), "%u ", ioread8(pru_shared_mem_io + mem_offset + i));
    }
    count += sprintf(buf + strlen(buf), "\n");

    /* u32 output LUT */
    mem_offset = kobj_attr_wrapped->val_offset + non_lut_size + inp_lut_size;
    for (i = 0; i < out_lut_size; i += 4)
    {
        count += sprintf(buf + strlen(buf), "%u ", ioread32(pru_shared_mem_io + mem_offset + i));
    }
    count += sprintf(buf + strlen(buf), "\n");
    printk(KERN_INFO "shprd.k: reading struct ConverterConfig");
    return count;
}

static ssize_t sysfs_virtual_harvester_settings_store(struct kobject        *kobj,
                                                      struct kobj_attribute *attr,
                                                      const char *buffer, size_t count)
{
    static const uint32_t            struct_size = sizeof(struct HarvesterConfig) - 4u;
    // ignore canary
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         mem_offset = 0u;
    int32_t                          buf_pos    = 0;
    uint32_t                         i          = 0u;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);
    mem_offset        = kobj_attr_wrapped->val_offset;
    for (i = 0; i < struct_size; i += 4)
    {
        uint32_t value_retrieved;
        int32_t  value_length;
        int32_t  ret = sscanf(&buffer[buf_pos], "%u%n ", &value_retrieved, &value_length);
        buf_pos += value_length;
        if (ret != 1) return -EINVAL;
        iowrite32(value_retrieved, pru_shared_mem_io + mem_offset + i);
    }
    printk(KERN_INFO "shprd.k: writing struct HarvesterConfig");
    return count;
}

static ssize_t sysfs_virtual_harvester_settings_show(struct kobject        *kobj,
                                                     struct kobj_attribute *attr, char *buf)
{
    static const uint32_t                  struct_size = sizeof(struct HarvesterConfig) - 4u;
    // ignore canary
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    uint32_t mem_offset = 0u;
    uint32_t i          = 0u;
    int      count      = 0;

    mem_offset          = kobj_attr_wrapped->val_offset;
    for (i = 0; i < struct_size; i += 4)
    {
        count += sprintf(buf + strlen(buf), "%u \n", ioread32(pru_shared_mem_io + mem_offset + i));
    }
    printk(KERN_INFO "shprd.k: reading struct HarvesterConfig");
    return count;
}

static ssize_t sysfs_pru_msg_system_store(struct kobject *kobj, struct kobj_attribute *attr,
                                          const char *buffer, size_t count)
{
    struct ProtoMsg pru_msg = {.id       = MSG_TO_PRU,
                               .unread   = 0u,
                               .type     = MSG_NONE,
                               .reserved = {0u},
                               .value    = {0u, 0u}};

    if (sscanf(buffer, "%hhu %u %u", &pru_msg.type, &pru_msg.value[0], &pru_msg.value[1]) != 0)
    {
        put_msg_to_pru(&pru_msg);
        return count;
    }

    return -EINVAL;
}

static ssize_t sysfs_pru_msg_system_show(struct kobject *kobj, struct kobj_attribute *attr,
                                         char *buf)
{
    int             count = 0;
    struct ProtoMsg pru_msg;

    if (get_msg_from_pru(&pru_msg))
    {
        count += sprintf(buf + strlen(buf), "%hhu %u %u", pru_msg.type, pru_msg.value[0],
                         pru_msg.value[1]);
    }
    else { count += sprintf(buf + strlen(buf), "%u ", 0x00u); }
    return count;
}


static ssize_t sysfs_prog_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    const int32_t value = ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset);

    if (value == PRG_STATE_IDLE) return sprintf(buf, "idle");
    else if (value == PRG_STATE_STARTING) return sprintf(buf, "starting");
    else if (value == PRG_STATE_INITIALIZING) return sprintf(buf, "initializing");
    else if (value < 0) return sprintf(buf, "error (%d)", value);
    else return sprintf(buf, "running - %d B written", value);
}

static ssize_t sysfs_prog_state_store(struct kobject *kobj, struct kobj_attribute *attr,
                                      const char *buffer, size_t count)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);
    int32_t value = 0u;

    if (strncmp(buffer, "start", 5) == 0) value = PRG_STATE_STARTING;
    else if (strncmp(buffer, "stop", 4) == 0) value = PRG_STATE_IDLE;
    else return -EINVAL;

    if ((value == PRG_STATE_STARTING) && (mem_interface_get_state() != STATE_IDLE)) return -EBUSY;
    // TODO: kernel should test validity of struct (instead of pru) -> best place is here

    iowrite32(value, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    return count;
}

static ssize_t sysfs_prog_target_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    const struct kobj_attr_struct_s *const kobj_attr_wrapped =
            container_of(attr, struct kobj_attr_struct_s, attr);

    switch (ioread32(pru_shared_mem_io + kobj_attr_wrapped->val_offset))
    {
        case PRG_TARGET_NRF52: return sprintf(buf, "nrf52");
        case PRG_TARGET_MSP430: return sprintf(buf, "msp430");
        default: return sprintf(buf, "unknown");
    }
}

static ssize_t sysfs_prog_target_store(struct kobject *kobj, struct kobj_attribute *attr,
                                       const char *buffer, size_t count)
{
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         value = 0u;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (strncmp(buffer, "nrf52", 5) == 0) value = PRG_TARGET_NRF52;
    else if (strncmp(buffer, "msp430", 6) == 0) value = PRG_TARGET_MSP430;
    else if (strncmp(buffer, "dummy", 5) == 0) value = PRG_TARGET_DUMMY;
    else
    {
        printk(KERN_INFO "shprd.k: setting programmer-target failed -> unknown value");
        return -EINVAL;
    }

    iowrite32(value, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    return count;
}

static ssize_t sysfs_prog_datarate_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count)
{
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         value;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (sscanf(buffer, "%u", &value) != 1) return -EINVAL;
    if ((value < 1) || (value > 10000000)) // TODO: replace with valid boundaries
        return -EINVAL;
    iowrite32(value, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    return count;
}

static ssize_t sysfs_prog_datasize_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count)
{
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         value;
    uint32_t                         value_max;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);
    value_max         = ioread32(pru_shared_mem_io + offsetof(struct SharedMem, buffer_size));

    if (sscanf(buffer, "%u", &value) != 1) return -EINVAL;
    if ((value < 1) || (value > value_max)) return -EINVAL;
    iowrite32(value, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    return count;
}

static ssize_t sysfs_prog_pin_store(struct kobject *kobj, struct kobj_attribute *attr,
                                    const char *buffer, size_t count)
{
    const struct kobj_attr_struct_s *kobj_attr_wrapped;
    uint32_t                         value;

    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    kobj_attr_wrapped = container_of(attr, struct kobj_attr_struct_s, attr);

    if (sscanf(buffer, "%u", &value) != 1) return -EINVAL;
    if (value > 10000) // TODO: replace with proper range-test for valid pin-def
        return -EINVAL;
    iowrite32(value, pru_shared_mem_io + kobj_attr_wrapped->val_offset);
    return count;
}


static ssize_t sysfs_pru0_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buf)
{
    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;
    read_pru_firmware(0, buf);
    return strlen(buf);
}

static ssize_t sysfs_pru1_firmware_show(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buf)
{
    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;
    read_pru_firmware(1, buf);
    return strlen(buf);
}

static ssize_t sysfs_pru0_firmware_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count)
{
    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    /* FAIL with no file-name or not matching start-string */
    if (strlen(buffer) == 0) return -EINVAL;

    /*
    if (strncmp(buffer, "am335x-pru0-", 12)) return -EINVAL;
    swap_pru_firmware(buffer, "");
     */
    /* NOTE: this does not work as expected as buffer contains xtra chars at the end
     * ignore for now and hardcode
     */
    if ((strncmp(buffer, "emu", 3) == 0) || (strncmp(buffer, PRU0_FW_EMU, 27) == 0))
    {
        swap_pru_firmware(PRU0_FW_EMU, "");
    }
    else if ((strncmp(buffer, "hrv", 3) == 0) || (strncmp(buffer, PRU0_FW_HRV, 27) == 0))
    {
        swap_pru_firmware(PRU0_FW_HRV, "");
    }
    else if ((strncmp(buffer, "swd", 3) == 0) || (strncmp(buffer, PRU0_FW_PRG_SWD, 29) == 0))
    {
        swap_pru_firmware(PRU0_FW_PRG_SWD, "");
    }
    else if ((strncmp(buffer, "sbw", 3) == 0) || (strncmp(buffer, PRU0_FW_PRG_SBW, 29) == 0))
    {
        swap_pru_firmware(PRU0_FW_PRG_SBW, "");
    }
    else { swap_pru_firmware(PRU0_FW_DEFAULT, ""); }

    return count;
}


static ssize_t sysfs_pru1_firmware_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buffer, size_t count)
{
    if (mem_interface_get_state() != STATE_IDLE) return -EBUSY;

    /* FAIL with no file-name or not matching start-string */
    if (strlen(buffer) == 0) return -EINVAL;

    if (strncmp(buffer, "sync", 4) == 0)
    {
        printk(KERN_ERR "shprd.k: sync-fw was removed");
        // NOTE: this could be removed, but that makes the whole FN useless
    }
    else { swap_pru_firmware("", PRU1_FW_DEFAULT); }

    return count;
}

/*
 * TODO: implement a way to react on pru-firmware (hide programmer or shepherd-interface)
 *  -> hiding programmer should be easy
 */
int sysfs_interface_init(void)
{
    int retval = 0;

    kobj_ref   = kobject_create_and_add("shepherd", NULL);

    if ((retval = sysfs_create_file(kobj_ref, &attr_state.attr)))
    {
        printk(KERN_ERR "shprd.k: Cannot create sysfs state attrib");
        goto f_shp_state;
    }

    if ((retval = sysfs_create_group(kobj_ref, &attr_group)))
    {
        printk(KERN_ERR "shprd.k: cannot create sysfs shp-control attrib group");
        goto f_shp_group;
    };

    if ((retval = sysfs_create_group(kobj_ref, &attr_firmware_group)))
    {
        printk(KERN_ERR "shprd.k: cannot create sysfs firmware attrib group");
        goto f_firmware;
    };

    kobj_mem_ref = kobject_create_and_add("memory", kobj_ref);

    if ((retval = sysfs_create_group(kobj_mem_ref, &attr_mem_group)))
    {
        printk(KERN_ERR "shprd.k: cannot create sysfs memory attrib group");
        goto f_mem;
    };

    kobj_sync_ref = kobject_create_and_add("sync", kobj_ref);

    if ((retval = sysfs_create_group(kobj_sync_ref, &attr_sync_group)))
    {
        printk(KERN_ERR "shprd.k: cannot create sysfs sync attrib group");
        goto f_sync;
    };

    kobj_prog_ref = kobject_create_and_add("programmer", kobj_ref);

    if ((retval = sysfs_create_group(kobj_prog_ref, &attr_prog_group)))
    {
        printk(KERN_ERR "shprd.k: cannot create sysfs programmer attrib group");
        goto f_prog;
    };

    return 0;

    // last item stays: attr_prog_group
f_prog:
    sysfs_remove_group(kobj_ref, &attr_sync_group);
    kobject_put(kobj_prog_ref);
f_sync:
    sysfs_remove_group(kobj_ref, &attr_mem_group);
    kobject_put(kobj_sync_ref);
f_mem:
    sysfs_remove_group(kobj_ref, &attr_firmware_group);
    kobject_put(kobj_mem_ref);
f_firmware:
    sysfs_remove_group(kobj_ref, &attr_group);
f_shp_group:
    sysfs_remove_file(kobj_ref, &attr_state.attr);
f_shp_state:
    kobject_put(kobj_ref);

    return retval;
}

void sysfs_interface_exit(void)
{
    sysfs_remove_group(kobj_ref, &attr_prog_group);
    sysfs_remove_group(kobj_ref, &attr_sync_group);
    sysfs_remove_group(kobj_ref, &attr_mem_group);
    sysfs_remove_group(kobj_ref, &attr_firmware_group);
    sysfs_remove_group(kobj_ref, &attr_group);
    sysfs_remove_file(kobj_ref, &attr_state.attr);
    kobject_put(kobj_prog_ref);
    kobject_put(kobj_sync_ref);
    kobject_put(kobj_mem_ref);
    kobject_put(kobj_ref);
    printk(KERN_INFO "shprd.k: sysfs exited");
}
