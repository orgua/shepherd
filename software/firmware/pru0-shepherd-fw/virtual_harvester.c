#include "virtual_harvester.h"
#include "calibration.h"
#include "hw_config.h"
#include "math64_safe.h"
#include "spi_transfer_pru.h"
#include <stddef.h>
#include <stdint.h>

#include "fw_config.h"
#include "msg_sys.h"
#include "shared_mem.h"

// internal variables
uint32_t        voltage_set_uV = 0u; // global
static bool_ft  is_rising      = 0u;

static uint32_t voltage_hold   = 0u;
static uint32_t current_hold   = 0u;

#ifdef EMU_SUPPORT
static uint32_t voltage_step_x4_uV = 0u;
static uint32_t age_max            = 0u;

static uint32_t voc_now            = 0u;
static uint32_t voc_nxt            = 0u;
static uint32_t voc_min            = 0u;

static bool_ft  lin_extrapolation  = 0u;
#endif // EMU_SUPPORT

static uint32_t settle_steps   = 0; // adc_ivcurve
static uint32_t interval_step  = 0u;

static uint32_t volt_step_uV   = 0u;
static uint32_t power_last_raw = 0u; // adc_mppt_po


#define HRV_CFG                                                                                    \
    (*((volatile struct HarvesterConfig *) (PRU_SHARED_MEM_OFFSET +                                \
                                            offsetof(struct SharedMem, harvester_settings))))
static volatile struct IVTraceOut *buffer;

#ifdef HRV_SUPPORT
/* ivcurve cutout
   - prevents power-spike during the non-linear reset-step of the ivcurve
   - slow analog filters show this behavior with cape 2.4
   TODO: make value configurable by frontend
*/
static const uint32_t STEP_IV_CUTOUT = 5u;

// to be used with harvester-frontend
static void           harvest_adc_2_ivcurve(const uint32_t sample_idx);
static void           harvest_adc_2_isc_voc(const uint32_t sample_idx);
static void           harvest_adc_2_cv(const uint32_t sample_idx);
static void           harvest_adc_2_mppt_voc(const uint32_t sample_idx);
static void           harvest_adc_2_mppt_po(const uint32_t sample_idx);
#endif // HRV_SUPPORT

#ifdef EMU_SUPPORT
// to be used in virtual harvester (part of emulator)
static void harvest_ivcurve_2_cv(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA);
static void harvest_ivcurve_2_mppt_voc(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA);
static void harvest_ivcurve_2_mppt_po(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA);
static void harvest_ivcurve_2_mppt_opt(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA);
#endif // EMU_SUPPORT

#ifdef __PYTHON__
/* next is a mock-impl to calm the compiler (does not get used)*/
static uint32_t hw_value = 0u;
uint32_t        adc_readwrite(uint32_t cs_pin, uint32_t val) { return cs_pin + val + hw_value; }
uint32_t        adc_fastread(uint32_t cs_pin) { return cs_pin + hw_value; }
void            dac_write(uint32_t cs_pin, uint32_t val) { hw_value = cs_pin + val; }
#endif

#define HRV_ISC_VOC  (1u << 3u)
#define HRV_IVCURVE  (1u << 4u)
#define HRV_CV       (1u << 8u)
#define HRV_MPPT_VOC (1u << 12u)
#define HRV_MPPT_PO  (1u << 13u)
#define HRV_MPPT_OPT (1u << 14u)


void harvester_initialize()
{
    // basic (shared) states for ADC- and IVCurve-Version
    buffer               = SHARED_MEM.buffer_iv_out_ptr;
    // TODO: replace with buffer_samples = SHARED_MEM.buffer_iv_ptr->samples
    voltage_set_uV       = HRV_CFG.voltage_uV + 1u; // deliberately off for cv-version
    settle_steps         = 0u;

    const bool_ft is_emu = (HRV_CFG.hrv_mode >> 0u) & 1u;
    if (is_emu && (HRV_CFG.interval_n > 2 * HRV_CFG.window_size))
        interval_step = HRV_CFG.interval_n - (2 * HRV_CFG.window_size);
    else interval_step = 1u << 30u;
    // ⤷ intake two ivcurves before overflow / reset if possible
    is_rising      = (HRV_CFG.hrv_mode >> 1u) & 1u;

    // MPPT-PO
    volt_step_uV   = HRV_CFG.voltage_step_uV;
    power_last_raw = 0u;

    // for IV-Curve-Version, mostly resets states
    voltage_hold   = 0u;
    current_hold   = 0u;

#ifdef EMU_SUPPORT
    voltage_step_x4_uV = 4u * HRV_CFG.voltage_step_uV;
    age_max            = 2u * HRV_CFG.window_size;

    voc_now            = HRV_CFG.voltage_max_uV;
    voc_nxt            = HRV_CFG.voltage_max_uV;
    voc_min            = HRV_CFG.voltage_min_uV > 1000u ? HRV_CFG.voltage_min_uV : 1000u;

    /* extrapolation */
    lin_extrapolation  = (HRV_CFG.hrv_mode >> 2u) & 1u;
#endif // EMU_SUPPORT

    // TODO: all static vars in sub-fns should be globals (they are anyway), saves space due to overlaps
    // TODO: check that ConfigParams are used in SubFns if applicable
    // TODO: divide lib into IVC and ADC Parts

    // TODO: embed HRV_CFG.current_limit_nA as a limiter if resources allow for it
}

#ifdef HRV_SUPPORT
void sample_adc_harvester(const uint32_t sample_idx)
{
    if (HRV_CFG.algorithm >= HRV_MPPT_PO) harvest_adc_2_mppt_po(sample_idx);
    else if (HRV_CFG.algorithm >= HRV_MPPT_VOC)
        harvest_adc_2_mppt_voc(sample_idx); // ~ 1300 ns without SPI
    else if (HRV_CFG.algorithm >= HRV_CV) harvest_adc_2_cv(sample_idx);
    else if (HRV_CFG.algorithm >= HRV_IVCURVE) harvest_adc_2_ivcurve(sample_idx);
    else if (HRV_CFG.algorithm >= HRV_ISC_VOC) harvest_adc_2_isc_voc(sample_idx);
    else msgsys_send_status(MSG_ERR_HRV_ALGO, HRV_CFG.algorithm, 0u);
}

static void harvest_adc_2_cv(const uint32_t sample_idx)
{
    /* 	Set constant voltage and log resulting current
 * 	- ADC and DAC voltage should match but can vary, depending on calibration and load (no closed loop)
 * 	- Note: could be self-adjusting (in loop with adc) if needed
 * 	- influencing parameters: voltage_uV,
 */

    /* ADC-Sample probably not ready -> Trigger at timer_cmp -> ads8691 needs 1us to acquire and convert */
    /* NOTE: it's in here so this timeslot can be used for calculations */
    __delay_cycles(800 / 5);
    const uint32_t current_adc = adc_fastread(SPI_CS_HRV_C_ADC_PIN);
    const uint32_t voltage_adc = adc_fastread(SPI_CS_HRV_V_ADC_PIN);

    if (voltage_set_uV != HRV_CFG.voltage_uV)
    {
        /* set new voltage if not already set */
        voltage_set_uV             = HRV_CFG.voltage_uV;
        const uint32_t voltage_raw = cal_conv_uV_to_dac_raw(voltage_set_uV);
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | voltage_raw);
    }
    buffer->current[sample_idx] = current_adc;
    buffer->voltage[sample_idx] = voltage_adc;
}

static void harvest_adc_2_ivcurve(const uint32_t sample_idx)
{
    /* 	Record iv-curves
 * 	- by controlling voltage with sawtooth
 * 	- influencing parameters: window_size, voltage_min_uV, voltage_max_uV, voltage_step_uV, wait_cycles_n, hrv_mode (init)
 */

    /* ADC-Sample probably not ready -> Trigger at timer_cmp -> ads8691 needs 1us to acquire and convert */
    /* NOTE: it's in here so this timeslot can be used for calculations */
    __delay_cycles(800 / 5);
    uint32_t current_adc = adc_fastread(SPI_CS_HRV_C_ADC_PIN);
    uint32_t voltage_adc = adc_fastread(SPI_CS_HRV_V_ADC_PIN);

    /* discard initial readings during reset */
    if (interval_step < STEP_IV_CUTOUT)
    {
        // set lowest & highest 18 bit value of ADC
        if (is_rising) voltage_adc = 0u;
        else
        {
            voltage_adc = 0x3FFFFu;
            current_adc = 0u;
        }
    }

    if (settle_steps == 0u)
    {
        if (++interval_step >= HRV_CFG.window_size)
        {
            /* reset curve to start */
            voltage_set_uV = is_rising ? HRV_CFG.voltage_min_uV : HRV_CFG.voltage_max_uV;
            interval_step  = 0u;
        }
        else
        {
            /* stepping through */
            if (is_rising) voltage_set_uV = add32(voltage_set_uV, HRV_CFG.voltage_step_uV);
            else voltage_set_uV = sub32(voltage_set_uV, HRV_CFG.voltage_step_uV);
        }
        /* check boundaries */
        if (is_rising && (voltage_set_uV > HRV_CFG.voltage_max_uV))
            voltage_set_uV = HRV_CFG.voltage_max_uV;
        if ((!is_rising) && (voltage_set_uV < HRV_CFG.voltage_min_uV))
            voltage_set_uV = HRV_CFG.voltage_min_uV;

        /* write new step */
        const uint32_t voltage_raw = cal_conv_uV_to_dac_raw(voltage_set_uV);
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | voltage_raw);
        settle_steps = HRV_CFG.wait_cycles_n;
    }
    else settle_steps--;

    buffer->current[sample_idx] = current_adc;
    buffer->voltage[sample_idx] = voltage_adc;
}

static void harvest_adc_2_isc_voc(const uint32_t sample_idx)
{
    /* 	Record VOC & ISC
	 * 	- open the circuit -> voltage will settle when set to MAX
	 * 	- short circuit current -> current will rise when voltage is set to 0
	 * 	- influencing parameters: wait_cycles_n
 	*/

    /* ADC-Sample probably not ready -> Trigger at timer_cmp -> ads8691 needs 1us to acquire and convert */
    /* NOTE: it's in here so this timeslot can be used for calculations */
    __delay_cycles(800 / 5);
    const uint32_t current_adc = adc_fastread(SPI_CS_HRV_C_ADC_PIN);
    const uint32_t voltage_adc = adc_fastread(SPI_CS_HRV_V_ADC_PIN);

    if (settle_steps == 0u)
    {
        /* write new state (is_rising == VOC, else ISC)*/
        const uint32_t voltage_raw = is_rising ? DAC_MAX_VAL : 0u;
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | voltage_raw);

        /* sample and hold after settling-period */
        if (is_rising) current_hold = current_adc;
        else voltage_hold = voltage_adc;

        /* prepare next state-change */
        is_rising ^= 1u; // reverse direction
        settle_steps = HRV_CFG.wait_cycles_n;
    }
    else settle_steps--;

    buffer->voltage[sample_idx] = voltage_hold;
    buffer->current[sample_idx] = current_hold;
}

static void harvest_adc_2_mppt_voc(const uint32_t sample_idx)
{
    /*	Determine VOC and harvest
 * 	- first part of interval is used for determining the open circuit voltage
 *	- Determine VOC: set DAC to max voltage -> hrv will settle at open voltage -> wait till end of measurement duration and sample valid voltage
 *	- influencing parameters: interval_n, duration_n, setpoint_n8, voltage_max_uV, voltage_min_uV, indirectly wait_cycles_n,
 */
    /* ADC-Sample probably not ready -> Trigger at timer_cmp -> ads8691 needs 1us to acquire and convert */
    /* NOTE: it's in here so this timeslot can be used for calculations later */
    __delay_cycles(800 / 5);
    const uint32_t current_adc = adc_fastread(SPI_CS_HRV_C_ADC_PIN);
    const uint32_t voltage_adc = adc_fastread(SPI_CS_HRV_V_ADC_PIN);

    /* keep track of time, do  step = mod(step + 1, n) */
    if (++interval_step >= HRV_CFG.interval_n) interval_step = 0u;

    if (interval_step == 0u)
    {
        /* open the circuit -> voltage will settle */
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | DAC_MAX_VAL);
    }

    if (interval_step == HRV_CFG.duration_n - 1u)
    {
        /* end of voc-measurement -> lock-in the value */
        const uint32_t voc_uV = cal_conv_adc_raw_to_uV(voltage_adc);
        voltage_set_uV        = mul32(voc_uV, HRV_CFG.setpoint_n8) >> 8u;

        /* check boundaries */
        if (voltage_set_uV > HRV_CFG.voltage_max_uV) voltage_set_uV = HRV_CFG.voltage_max_uV;
        if (voltage_set_uV < HRV_CFG.voltage_min_uV) voltage_set_uV = HRV_CFG.voltage_min_uV;

        /* write setpoint voltage */
        const uint32_t voltage_raw = cal_conv_uV_to_dac_raw(voltage_set_uV);
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | voltage_raw);
    }

    if (interval_step < HRV_CFG.duration_n)
    {
        /* output disconnected during voc-measurement */

        buffer->current[sample_idx] = 0u;
        buffer->voltage[sample_idx] = voltage_adc; // keep voltage for debug-purposes
    }
    else
    {
        /* converter-mode at pre-set VOC */
        buffer->current[sample_idx] = current_adc;
        buffer->voltage[sample_idx] = voltage_adc;
    }
}


static void harvest_adc_2_mppt_po(const uint32_t sample_idx)
{
    /*	perturb & observe
	 * 	- move a voltage step every interval and evaluate power-increase
	 * 		- if higher -> keep this step-direction and begin doubling step-size
	 * 		- if lower -> reverse direction and move the smallest step back
	 * 		- resulting steps if direction is kept: 1, 1, 2, 4, 8, ...
	 *	- influencing parameters: interval_n, voltage_set_uV, voltage_step_uV, voltage_min_uV, voltage_max_uV,
	 */
    /* ADC-Sample probably not ready -> Trigger at timer_cmp -> ads8691 needs 1us to acquire and convert */
    /* NOTE: it's in here so this timeslot can be used for calculations */
    __delay_cycles(800 / 5);
    const uint32_t current_adc = adc_fastread(SPI_CS_HRV_C_ADC_PIN);
    const uint32_t voltage_adc = adc_fastread(SPI_CS_HRV_V_ADC_PIN);

    /* keep track of time, do  step = mod(step + 1, n) */
    if (++interval_step >= HRV_CFG.interval_n) interval_step = 0u;

    if (interval_step == 0u)
    {
        const uint32_t power_raw = mul32(current_adc, voltage_adc);
        if (power_raw > power_last_raw)
        {
            /* got higher power -> keep direction, move further, speed up */
            if (is_rising) voltage_set_uV = add32(voltage_set_uV, volt_step_uV);
            else voltage_set_uV = sub32(voltage_set_uV, volt_step_uV);
            volt_step_uV = mul32(2u, volt_step_uV);
            if (volt_step_uV > 300000u) volt_step_uV = 300000u; // TODO: new, max step size
        }
        else
        {
            /* got less power -> reverse direction, restart step-size */
            is_rising ^= 1u;
            volt_step_uV = HRV_CFG.voltage_step_uV;
            if (is_rising) voltage_set_uV = add32(voltage_set_uV, volt_step_uV);
            else voltage_set_uV = sub32(voltage_set_uV, volt_step_uV);
        }
        power_last_raw         = power_raw;

        // TODO: experimental, to keep contact to solar-voltage when voltage is dropping
        const uint32_t adc_uV  = cal_conv_adc_raw_to_uV(voltage_adc);
        const uint32_t diff_uV = sub32(voltage_set_uV, adc_uV);
        if (is_rising && (diff_uV > (volt_step_uV << 1u)))
        {
            is_rising      = 0u;
            voltage_set_uV = sub32(adc_uV, volt_step_uV);
        }

        /* check boundaries */
        if (voltage_set_uV >= HRV_CFG.voltage_max_uV)
        {
            voltage_set_uV = HRV_CFG.voltage_max_uV;
            is_rising      = 0u;
            volt_step_uV   = HRV_CFG.voltage_step_uV;
        }
        if (voltage_set_uV <= HRV_CFG.voltage_min_uV)
        {
            voltage_set_uV = HRV_CFG.voltage_min_uV;
            is_rising      = 1u;
            volt_step_uV   = HRV_CFG.voltage_step_uV;
        }

        /* write setpoint voltage */
        const uint32_t voltage_raw = cal_conv_uV_to_dac_raw(voltage_set_uV);
        dac_write(SPI_CS_HRV_DAC_PIN, DAC_CH_B_ADDR | voltage_raw);
    }
    buffer->current[sample_idx] = current_adc;
    buffer->voltage[sample_idx] = voltage_adc;
}
#endif // HRV_SUPPORT

/* // TODO: do we need a constant-current-version?
const uint32_t current_nA = cal_conv_adc_raw_to_nA(current_adc); // TODO: could be simplified by providing raw-value in cfg
if (current_nA > HRV_CFG.current_limit_nA)
*/

#ifdef EMU_SUPPORT
void sample_ivcurve_harvester(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA)
{
    // check for IVCurve-Input Indicator and use selected algo
    if (HRV_CFG.window_size <= 1) return;
    else if (HRV_CFG.algorithm >= HRV_MPPT_OPT)
        harvest_ivcurve_2_mppt_opt(p_voltage_uV, p_current_nA);
    else if (HRV_CFG.algorithm >= HRV_MPPT_PO)
        harvest_ivcurve_2_mppt_po(p_voltage_uV, p_current_nA);
    else if (HRV_CFG.algorithm >= HRV_MPPT_VOC)
        harvest_ivcurve_2_mppt_voc(p_voltage_uV, p_current_nA);
    else if (HRV_CFG.algorithm >= HRV_CV) harvest_ivcurve_2_cv(p_voltage_uV, p_current_nA);
    else msgsys_send_status(MSG_ERR_HRV_ALGO, HRV_CFG.algorithm, 0u);
}


static void harvest_ivcurve_2_cv(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA)
{
    /* look for wanted constant voltage in an iv-curve-stream (constantly moving up or down in voltage, jumping back when limit reached)
	 * - influencing parameters: voltage_uV (in init)
	 * - no min/max usage here, the main FNs do that, or python if cv() is used directly
	 * */
    static uint32_t voltage_last = 0u, current_last = 0u;
    static int32_t  voltage_delta = 0u, current_delta = 0u;
    static bool_ft  compare_last  = 0u;

    /* find matching voltage with threshold-crossing-detection -> direction of curve is irrelevant */
    const bool_ft   compare_now   = *p_voltage_uV < voltage_set_uV;
    /* abs(step_size) -> for detecting reset of sawtooth */
    const uint32_t  step_size_now = (*p_voltage_uV > voltage_last) ? (*p_voltage_uV - voltage_last)
                                                                   : (voltage_last - *p_voltage_uV);
    /* voltage_set_uV can change outside of loop, so algo has to keep track */
    const uint32_t  distance_now  = (*p_voltage_uV > voltage_set_uV)
                                            ? (*p_voltage_uV - voltage_set_uV)
                                            : (voltage_set_uV - *p_voltage_uV);
    const uint32_t  distance_last = (voltage_last > voltage_set_uV)
                                            ? (voltage_last - voltage_set_uV)
                                            : (voltage_set_uV - voltage_last);

    if ((compare_now != compare_last) && (step_size_now < voltage_step_x4_uV))
    {
        /* a fresh ConstVoltage was found in stream, choose the closer value
		 * TODO: could also be interpolated if sampling-routine has time to spare */
        if ((distance_now < distance_last) && (distance_now < voltage_step_x4_uV))
        {
            voltage_hold  = *p_voltage_uV;
            current_hold  = *p_current_nA;
            voltage_delta = (int32_t) *p_voltage_uV - voltage_last;
            current_delta = (int32_t) *p_current_nA - current_last;
        }
        else if ((distance_last < distance_now) && (distance_last < voltage_step_x4_uV))
        {
            voltage_hold  = voltage_last;
            current_hold  = current_last;
            voltage_delta = (int32_t) *p_voltage_uV - voltage_last;
            current_delta = (int32_t) *p_current_nA - current_last;
        }
    }
    else if (lin_extrapolation)
    {
        /* apply the proper delta if needed */
        if ((voltage_hold < voltage_set_uV) == (voltage_delta > 0))
        {
            voltage_hold += voltage_delta;
            current_hold += current_delta;
        }
        else
        {
            const uint32_t uvd = voltage_delta >= 0 ? (uint32_t) voltage_delta : 0u;
            const uint32_t ucd = current_delta >= 0 ? (uint32_t) current_delta : 0u;
            if (voltage_hold > uvd) voltage_hold -= voltage_delta;
            else voltage_hold = 0u;
            if (current_hold > ucd) current_hold -= current_delta;
            else current_hold = 0u;
        }
    }
    voltage_last  = *p_voltage_uV;
    current_last  = *p_current_nA;
    compare_last  = compare_now;

    /* manipulate the values of the parameter-pointers ("return values") */
    *p_voltage_uV = voltage_hold;
    *p_current_nA = current_hold;
}

static void harvest_ivcurve_2_mppt_voc(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA)
{
    /* VOC - working on an iv-curve-stream, without complete curve-memory
	 * NOTE with no memory, there is a time-gap before CV gets picked up by harvest_ivcurve_2_cv()
	 *  - influencing parameters: interval_n, duration_n, current_limit_nA, voltage_min_uV, voltage_max_uV, setpoint_n8, window_size
	 * 		   from init: (wait_cycles_n), voltage_uV (for cv())
	 */
    static uint32_t age_now = 0u;
    static uint32_t age_nxt = 0u;

    /* keep track of time, do  step = mod(step + 1, n) */
    if (++interval_step >= HRV_CFG.interval_n) interval_step = 0u;
    age_nxt++;
    age_now++;

    /* lookout for new VOC */
    if ((*p_current_nA < HRV_CFG.current_limit_nA) && (*p_voltage_uV <= voc_nxt) &&
        (*p_voltage_uV >= voc_min) && (*p_voltage_uV <= HRV_CFG.voltage_max_uV))
    {
        voc_nxt = *p_voltage_uV;
        age_nxt = 0u;
    }

    /* current "best VOC" (the lowest voltage with zero-current) can not get too old, or be NOT the best */
    if ((age_now > age_max) || (voc_nxt <= voc_now))
    {
        age_now = age_nxt;
        voc_now = voc_nxt;
        age_nxt = 0u;
        voc_nxt = HRV_CFG.voltage_max_uV;
    }

    /* underlying cv-algo is doing the rest */
    harvest_ivcurve_2_cv(p_voltage_uV, p_current_nA);

    /* emulate VOC Search @ beginning of interval duration */
    if (interval_step < HRV_CFG.duration_n)
    {
        /* No Output here, also update wanted const voltage */
        voltage_set_uV = mul32(voc_now, HRV_CFG.setpoint_n8) >> 8u;
        *p_current_nA  = 0u;
    }
}

static void harvest_ivcurve_2_mppt_po(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA)
{
    /* Perturb & Observe
	 * NOTE with no memory, there is a time-gap before CV gets picked up by harvest_ivcurve_2_cv()
	 * - influencing parameters: interval_n, voltage_step_uV, voltage_max_uV, voltage_min_uV
	 */
    static uint64_t power_last = 0u;

    /* keep track of time, do  step = mod(step + 1, n) */
    if (++interval_step >= HRV_CFG.interval_n) interval_step = 0u;

    /* underlying cv-algo is updating the current harvest-power */
    harvest_ivcurve_2_cv(p_voltage_uV, p_current_nA);
    /* p_voltage_uV and p_current_nA are changed now! */

    if (interval_step == 0u)
    {
        const uint64_t power_now = (uint64_t) (*p_voltage_uV) * (uint64_t) (*p_current_nA);
        if (power_now > power_last)
        {
            /* got higher power -> keep direction, move further, speed up */
            if (is_rising) voltage_set_uV = add32(voltage_set_uV, volt_step_uV);
            else voltage_set_uV = sub32(voltage_set_uV, volt_step_uV);
            volt_step_uV = mul32(2u, volt_step_uV);
        }
        else
        {
            if ((power_now == 0u) && (voltage_set_uV > 0u))
            {
                /* lost tracking - or started with bad init */
                is_rising      = 1u;
                volt_step_uV   = HRV_CFG.voltage_step_uV;
                voltage_set_uV = sub32(voltage_set_uV, voltage_step_x4_uV);
            }
            else
            {
                /* got less power -> reverse direction */
                is_rising ^= 1u;
                volt_step_uV = HRV_CFG.voltage_step_uV;
                if (is_rising) voltage_set_uV = add32(voltage_set_uV, volt_step_uV);
                else voltage_set_uV = sub32(voltage_set_uV, volt_step_uV);
            }
        }
        power_last = power_now;

        /* check boundaries */
        if (voltage_set_uV >= HRV_CFG.voltage_max_uV)
        {
            voltage_set_uV = HRV_CFG.voltage_max_uV;
            is_rising      = 0u;
            volt_step_uV   = HRV_CFG.voltage_step_uV;
        }
        if (voltage_set_uV <= HRV_CFG.voltage_min_uV)
        {
            voltage_set_uV = HRV_CFG.voltage_min_uV;
            is_rising      = 1u;
            volt_step_uV   = HRV_CFG.voltage_step_uV;
        }
        if (voltage_set_uV < HRV_CFG.voltage_step_uV)
        {
            voltage_set_uV = HRV_CFG.voltage_step_uV;
            is_rising      = 1u;
            volt_step_uV   = HRV_CFG.voltage_step_uV;
        }
    }
}

static void harvest_ivcurve_2_mppt_opt(uint32_t *const p_voltage_uV, uint32_t *const p_current_nA)
{
    /* Derivate of VOC -> selects the highest power directly
	 * - influencing parameters: window_size, voltage_min_uV, voltage_max_uV,
	 */
    static uint32_t age_now = 0u, voltage_now = 0u, current_now = 0u;
    static uint32_t age_nxt = 0u, voltage_nxt = 0u, current_nxt = 0u;
    static uint64_t power_now = 0ull, power_nxt = 0ull;

    /* keep track of time */
    age_nxt++;
    age_now++;

    /* search for new max */
    const uint64_t power_fW = (uint64_t) (*p_voltage_uV) * (uint64_t) (*p_current_nA);
    if ((power_fW >= power_nxt) && (*p_voltage_uV >= HRV_CFG.voltage_min_uV) &&
        (*p_voltage_uV <= HRV_CFG.voltage_max_uV))
    {
        age_nxt     = 0u;
        power_nxt   = power_fW;
        voltage_nxt = *p_voltage_uV;
        current_nxt = *p_current_nA;
    }

    /* current "best VOC" (the lowest voltage with zero-current) can not get too old, or NOT be the best */
    if ((age_now > age_max) || (power_nxt >= power_now))
    {
        age_now     = age_nxt;
        power_now   = power_nxt;
        voltage_now = voltage_nxt;
        current_now = current_nxt;

        age_nxt     = 0u;
        power_nxt   = 0u;
        voltage_nxt = 0u;
        current_nxt = 0u;
    }

    /* return current max */
    *p_voltage_uV = voltage_now;
    *p_current_nA = current_now;
}
#endif // EMU_SUPPORT
