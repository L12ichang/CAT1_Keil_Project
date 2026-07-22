#include "sys_pwm.h"
#include "sys_data.h"
#include "sys_temp_over_protect.h"
#include "hw_tim1_pwm2.h"
#include "sys_Vo_Io.h"
#include "factory_user_data.h"
#include "current_cal_storage.h"
#include "hw_flash.h"

#define TIMEOUT_MAX 200U

#define SYS_PWM_PROTECT_LOW_TEMP     0x0001U
#define SYS_PWM_PROTECT_INPUT_LOW    0x0002U
#define SYS_PWM_PROTECT_INPUT_HIGH   0x0004U
#define SYS_PWM_PROTECT_HIGH_TEMP    0x0008U
#define SYS_PWM_PROTECT_OVER_CURRENT 0x0010U

volatile u8 reload;
volatile u8 set_percent;
u8 net_entery_flag;
u8 has_entery_at_first;

static volatile u8 pwm_fade_timer = TIMEOUT_MAX;
static volatile boolean_en pwm_fade_active = BOOL_FALSE;
static volatile boolean_en force_off_latched = BOOL_FALSE;
static volatile u8 power_old;
static volatile u8 power_new;
static volatile u8 power_current;
static volatile u8 force_off_resume_percent;
static volatile sys_pwm_source_en force_off_resume_source = SYS_PWM_SOURCE_INTERNAL;
static volatile sys_pwm_source_en normal_source = SYS_PWM_SOURCE_INTERNAL;
static volatile boolean_en calibration_lock_active = BOOL_FALSE;
static sys_pwm_status_t pwm_status;

static boolean_en sys_pwm_flash_fail_safe_active(void)
{
    return hw_flash_update_fault_latched();
}

#if LEGACY_APP_PROCESS_ENABLE
extern u32 fac_en_timer;
extern u8 fa_test_EN;
#else
u32 fac_en_timer;
u8 fa_test_EN;
#endif

static u8 sys_pwm_apply_percent_protection(u8 percent, u16 *protect_code)
{
    u16 limited;
    u16 code;

    code = 0U;
    limited = percent;
    if (low_temp_detect_is_low(&limited, percent) == BOOL_TRUE)
    {
        code |= SYS_PWM_PROTECT_LOW_TEMP;
        percent = (limited < percent) ? (u8)limited : percent;
    }
    limited = percent;
    if (DC_low_voltage_detect_is_low(&limited, percent) == BOOL_TRUE)
    {
        code |= SYS_PWM_PROTECT_INPUT_LOW;
        percent = (limited < percent) ? (u8)limited : percent;
    }
    limited = percent;
    if (High_voltage_detect_is_high(&limited, percent) == BOOL_TRUE)
    {
        code |= SYS_PWM_PROTECT_INPUT_HIGH;
        percent = (limited < percent) ? (u8)limited : percent;
    }
    limited = percent;
    if (temp_detect_is_over(&limited, percent) == BOOL_TRUE)
    {
        code |= SYS_PWM_PROTECT_HIGH_TEMP;
        percent = (limited < percent) ? (u8)limited : percent;
    }
    if (Error_1_OL != 0U)
    {
        code |= SYS_PWM_PROTECT_OVER_CURRENT;
        percent = 0U;
    }
    if (protect_code != NULL)
    {
        *protect_code = code;
    }
    return percent;
}

static u8 sys_pwm_legacy_network_percent(u8 percent)
{
    if (MID == 4U && percent > 9U && percent < 27U)
    {
        percent = (u8)(percent - 3U);
    }
    if (percent < 5U)
    {
        percent = 0U;
    }
    return percent;
}

static u16 sys_pwm_legacy_percent_to_logical(u8 percent)
{
    u32 value;
    u16 logical_max;

    logical_max = hw_tim1_pwm2_get_logical_max();
    if (fa_test_EN != 0U)
    {
        value = ((u32)percent * logical_max) / 100U;
    }
    else if (HWMAX_OUTCUR == 0U)
    {
        value = 0U;
    }
    else
    {
        value = ((u32)percent * (u32)SET_OUTCUR * logical_max) /
                ((u32)HWMAX_OUTCUR * 100U);
    }
    if (value > logical_max)
    {
        value = logical_max;
    }
    return (u16)value;
}

static void sys_pwm_commit_logical(u16 requested,
                                   u16 applied,
                                   u8 requested_percent,
                                   u8 effective_percent,
                                   u16 protect_code)
{
    u16 logical_max;

    /* A force-off may pre-empt a foreground calculation.  Re-check the latch
     * at the final hardware boundary so the stale calculation cannot win. */
    if (sys_pwm_flash_fail_safe_active() == BOOL_TRUE ||
        (force_off_latched == BOOL_TRUE && calibration_lock_active != BOOL_TRUE))
    {
        applied = 0U;
    }
    logical_max = hw_tim1_pwm2_get_logical_max();
    if (applied > logical_max)
    {
        applied = logical_max;
    }
    hw_tim1_pwm2_set_PWM_OUT(applied);
    pwm_status.requested_percent = requested_percent;
    pwm_status.effective_percent = effective_percent;
    pwm_status.requested_logical_pwm = requested;
    pwm_status.applied_logical_pwm = hw_tim1_pwm2_get_logical_pwm();
    pwm_status.compare_value = hw_tim1_pwm2_get_compare();
    pwm_status.protect_code = protect_code;
    pwm_status.output_enabled = hw_tim1_pwm2_output_enabled();
    pwm_status.calibration_locked = calibration_lock_active;
    pwm_status.limited = (requested != pwm_status.applied_logical_pwm) ? BOOL_TRUE : BOOL_FALSE;
}

static void sys_pwm_apply_normal(u8 requested_percent, sys_pwm_source_en source)
{
    const current_cal_curve_t *curve;
    u8 mapped_percent;
    u8 effective_percent;
    u16 protect_code;
    u16 logical;

    if (calibration_lock_active == BOOL_TRUE || force_off_latched == BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        return;
    }
    if (requested_percent > 100U)
    {
        requested_percent = 100U;
    }
    set_percent = requested_percent;
    normal_source = source;
    mapped_percent = requested_percent;
    curve = current_cal_storage_active_curve();
    if (curve == NULL && source == SYS_PWM_SOURCE_NETWORK)
    {
        mapped_percent = sys_pwm_legacy_network_percent(mapped_percent);
    }
    if (source == SYS_PWM_SOURCE_NETWORK)
    {
        dim_bak_to_low_acin = mapped_percent;
    }
    effective_percent = sys_pwm_apply_percent_protection(mapped_percent, &protect_code);
    if (curve != NULL && fa_test_EN == 0U)
    {
        /* SET_OUTCUR is a runtime derating setting; the curve is CAL_MAX based. */
        logical = current_cal_curve_interpolate_setpoint(curve,
                                                         effective_percent,
                                                         SET_OUTCUR);
    }
    else
    {
        logical = sys_pwm_legacy_percent_to_logical(effective_percent);
    }
    sys_pwm_commit_logical(logical, logical, requested_percent, effective_percent, protect_code);
}

void pwm_output(u8 percent)
{
    if (calibration_lock_active == BOOL_TRUE || force_off_latched == BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        return;
    }
    pwm_fade_active = BOOL_FALSE;
    power_old = percent;
    sys_pwm_apply_normal(percent, SYS_PWM_SOURCE_INTERNAL);
}

void sys_pwm_timer(void)
{
    u32 delta;

    if (pwm_fade_active == BOOL_TRUE && calibration_lock_active != BOOL_TRUE &&
        force_off_latched != BOOL_TRUE &&
        sys_pwm_flash_fail_safe_active() != BOOL_TRUE)
    {
        if (pwm_fade_timer < TIMEOUT_MAX)
        {
            ++pwm_fade_timer;
            if (power_new > power_old)
            {
                delta = power_new - power_old;
                power_current = (u8)(power_old + delta * pwm_fade_timer / TIMEOUT_MAX);
            }
            else
            {
                delta = power_old - power_new;
                power_current = (u8)(power_old - delta * pwm_fade_timer / TIMEOUT_MAX);
            }
            sys_pwm_apply_normal(power_current, normal_source);
        }
        else
        {
            pwm_fade_active = BOOL_FALSE;
            power_old = power_new;
            sys_pwm_apply_normal(power_new, normal_source);
        }
    }
    if (fac_en_timer > 0U)
    {
        --fac_en_timer;
    }
}

void sys_pwm_fade_output(u8 oldpower, u8 newpower)
{
    if (calibration_lock_active == BOOL_TRUE || force_off_latched == BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        return;
    }
    power_current = oldpower;
    power_old = oldpower;
    power_new = newpower;
    sys_pwm_apply_normal(oldpower, normal_source);
    if (power_old != power_new && calibration_lock_active != BOOL_TRUE)
    {
        pwm_fade_timer = 0U;
        pwm_fade_active = BOOL_TRUE;
    }
}

static void sys_pwm_output_from(u8 percent, sys_pwm_source_en source)
{
    if (calibration_lock_active == BOOL_TRUE)
    {
        return;
    }
    if (sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        sys_pwm_force_off();
        return;
    }
    /* A fresh control request is authoritative; only stale timer/process work
     * is blocked by the force-off latch. */
    force_off_latched = BOOL_FALSE;
    reload = 0U;
    normal_source = source;
    if (percent == 0U)
    {
        pwm_fade_active = BOOL_FALSE;
        power_old = 0U;
        sys_pwm_apply_normal(0U, source);
        return;
    }
    if (pwm_fade_active == BOOL_TRUE)
    {
        sys_pwm_fade_output(power_current, percent);
    }
    else if (source == SYS_PWM_SOURCE_NETWORK &&
             net_entery_flag != 0U && has_entery_at_first == 0U)
    {
        has_entery_at_first = 1U;
        sys_pwm_fade_output(power_current, percent);
    }
    else
    {
        power_old = percent;
        sys_pwm_apply_normal(percent, source);
    }
}

void sys_pwm_output(u8 percent)
{
    sys_pwm_output_from(percent, SYS_PWM_SOURCE_INTERNAL);
}

void sys_pwm_output_network(u8 percent)
{
    sys_pwm_output_from(percent, SYS_PWM_SOURCE_NETWORK);
}

void sys_pwm_output_offline(u8 percent)
{
    sys_pwm_output_from(percent, SYS_PWM_SOURCE_OFFLINE);
}

void sys_pwm_output_for_temp_protect(u8 percent)
{
    pwm_output(percent);
}

void sys_pwm_output_on_fade(u8 percent)
{
    pwm_output(percent);
}

void sys_pwm_reload(void)
{
    if (force_off_latched != BOOL_TRUE &&
        sys_pwm_flash_fail_safe_active() != BOOL_TRUE)
    {
        reload = 1U;
    }
}

void sys_pwm_release_and_reload(void)
{
    if (sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        sys_pwm_force_off();
        return;
    }
    if (force_off_latched == BOOL_TRUE)
    {
        set_percent = force_off_resume_percent;
        normal_source = force_off_resume_source;
    }
    pwm_fade_active = BOOL_FALSE;
    pwm_fade_timer = TIMEOUT_MAX;
    power_old = set_percent;
    power_new = set_percent;
    power_current = set_percent;
    reload = 0U;
    force_off_latched = BOOL_FALSE;
    reload = 1U;
}

void sys_pwm_process(void)
{
    if (force_off_latched == BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        reload = 0U;
        return;
    }
    if (reload != 0U)
    {
        reload = 0U;
        if (calibration_lock_active != BOOL_TRUE)
        {
            sys_pwm_apply_normal((u8)set_percent, normal_source);
        }
    }
}

void sys_pwm_calibration_lock(void)
{
    calibration_lock_active = BOOL_TRUE;
    pwm_fade_active = BOOL_FALSE;
    reload = 0U;
    sys_pwm_force_off();
}

void sys_pwm_calibration_unlock(void)
{
    calibration_lock_active = BOOL_FALSE;
    pwm_fade_active = BOOL_FALSE;
    reload = 0U;
    sys_pwm_force_off();
}

boolean_en sys_pwm_calibration_is_locked(void)
{
    return calibration_lock_active;
}

boolean_en sys_pwm_calibration_set_direct(u16 logical_pwm)
{
    sys_pwm_status_t safety_status;
    u16 protect_code;
    u16 logical_max;
    u8 safety_percent;

    if (calibration_lock_active != BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        sys_pwm_force_off();
        return BOOL_FALSE;
    }
    logical_max = hw_tim1_pwm2_get_logical_max();
    if (logical_pwm > logical_max)
    {
        return BOOL_FALSE;
    }
    if (logical_pwm == 0U)
    {
        sys_pwm_commit_logical(0U, 0U, 0U, 0U, 0U);
        return BOOL_TRUE;
    }
    if (sys_pwm_calibration_safety_ready() != BOOL_TRUE)
    {
        sys_pwm_get_status(&safety_status);
        if (safety_status.protect_code != 0U)
        {
            sys_pwm_commit_logical(logical_pwm, 0U, 0U, 0U,
                                   safety_status.protect_code);
        }
        return BOOL_FALSE;
    }
    safety_percent = sys_pwm_apply_percent_protection(100U, &protect_code);
    if (protect_code != 0U || safety_percent != 100U)
    {
        sys_pwm_commit_logical(logical_pwm, 0U, 0U, safety_percent, protect_code);
        return BOOL_FALSE;
    }
    sys_pwm_commit_logical(logical_pwm, logical_pwm, 0U, 100U, 0U);
    return BOOL_TRUE;
}

boolean_en sys_pwm_calibration_set_percent(const current_cal_curve_t *curve, u8 percent)
{
    u16 logical;

    if (calibration_lock_active != BOOL_TRUE || curve == NULL || percent > 100U)
    {
        return BOOL_FALSE;
    }
    logical = current_cal_curve_interpolate(curve, percent);
    return sys_pwm_calibration_set_direct(logical);
}

boolean_en sys_pwm_calibration_safety_ready(void)
{
    sys_vo_io_snapshot_t snapshot;
    u16 protect_code;
    u8 safety_percent;

    if (calibration_lock_active != BOOL_TRUE ||
        sys_pwm_flash_fail_safe_active() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (sys_vo_io_get_snapshot(&snapshot) != BOOL_TRUE)
    {
        sys_pwm_force_off();
        return BOOL_FALSE;
    }
    safety_percent = sys_pwm_apply_percent_protection(100U, &protect_code);
    if (protect_code != 0U || safety_percent != 100U)
    {
        sys_pwm_commit_logical(pwm_status.requested_logical_pwm,
                               0U,
                               pwm_status.requested_percent,
                               0U,
                               protect_code);
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

void sys_pwm_force_off(void)
{
    boolean_en was_latched;

    /* Publish the latch before touching any other state.  sys_pwm_timer() may
     * run from the tick interrupt, and sys_pwm_commit_logical() checks this
     * flag again immediately before writing the PWM peripheral. */
    was_latched = force_off_latched;
    force_off_latched = BOOL_TRUE;
    if (was_latched != BOOL_TRUE)
    {
        force_off_resume_percent = set_percent;
        force_off_resume_source = normal_source;
    }
    reload = 0U;
    pwm_fade_active = BOOL_FALSE;
    pwm_fade_timer = TIMEOUT_MAX;
    power_old = 0U;
    power_new = 0U;
    power_current = 0U;
    set_percent = 0U;
    hw_tim1_pwm2_set_PWM_OUT(0U);
    memset(&pwm_status, 0, sizeof(pwm_status));
    pwm_status.applied_logical_pwm = hw_tim1_pwm2_get_logical_pwm();
    pwm_status.compare_value = hw_tim1_pwm2_get_compare();
    pwm_status.calibration_locked = calibration_lock_active;
}

void sys_pwm_get_status(sys_pwm_status_t *status)
{
    if (status != NULL)
    {
        *status = pwm_status;
        status->applied_logical_pwm = hw_tim1_pwm2_get_logical_pwm();
        status->compare_value = hw_tim1_pwm2_get_compare();
        status->output_enabled = hw_tim1_pwm2_output_enabled();
        status->calibration_locked = calibration_lock_active;
    }
}
