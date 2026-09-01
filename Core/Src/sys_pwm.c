/*************************************************************
程序功能：Default/Calibrated/Calibration三条PWM路径与统一硬件保护出口
开发环境：Keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_pwm.h"

#include "factory_user_data.h"
#include "hw_tim1_pwm2.h"
#include "net_dim.h"
#include "sys_bl0942.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_safety.h"
#include "sys_calibration_service.h"
#include "sys_calibration_snapshot.h"
#include "sys_product_profile.h"
#include "sys_temp_over_protect.h"
#include "sys_Vo_Io.h"

#define TIMEOUT_MAX                200U
#define PWM_OUT_MAX               1000U
#define CALIBRATION_FEEDBACK_MS    500UL

u8 reload;
u16 set_percent;
static u8 _timer = TIMEOUT_MAX;
static boolean_en _fade = BOOL_FALSE;
static u8 power_old;
static u8 power_new;
static u8 power_current;

#if LEGACY_APP_PROCESS_ENABLE
extern u32 fac_en_timer;
extern u8 fa_test_EN;
#else
u32 fac_en_timer;
u8 fa_test_EN;
#endif

static boolean_en sys_pwm_config_valid(void)
{
    const sys_product_profile_st *profile = sys_product_profile_current();

    return (sys_product_profile_is_complete(profile) == BOOL_TRUE &&
            sys_product_profile_runtime_matches(
                MID, OUTPUT_CUR_SENSOR, HWMAX_OUTCUR) == BOOL_TRUE &&
            SET_OUTCUR > 0U && HWMAX_OUTCUR > 0U &&
            SET_OUTCUR <= HWMAX_OUTCUR &&
            HWMAX_OUTCUR <= profile->hw_max_current_ma) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_pwm_non_temperature_hardware_fault_active(void)
{
    return (Error_1_OL != 0U || Error_Out_LV != 0U ||
            Error_3_OV != 0U || Error_4_LV != 0U) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_pwm_normal_shutdown_active(void)
{
    return (sys_pwm_non_temperature_hardware_fault_active() == BOOL_TRUE ||
            sys_data.lamp_power == 0U) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_pwm_calibration_fault_active(void)
{
    return (sys_pwm_non_temperature_hardware_fault_active() == BOOL_TRUE ||
            sys_temp_over_protect_state != SYS_TEMP_OVER_PROTECT_STATE_IDLE) ?
           BOOL_TRUE : BOOL_FALSE;
}

static u8 sys_pwm_apply_dynamic_limits(u8 requested_percent)
{
    u16 limited;
    u16 percent = requested_percent;

    if (low_temp_detect_is_low(&limited, percent) == BOOL_TRUE &&
        limited < percent)
    {
        percent = limited;
    }
    if (DC_low_voltage_detect_is_low(&limited, percent) == BOOL_TRUE &&
        limited < percent)
    {
        percent = limited;
    }
    if (High_voltage_detect_is_high(&limited, percent) == BOOL_TRUE &&
        limited < percent)
    {
        percent = limited;
    }
    if (temp_detect_is_over(&limited, percent) == BOOL_TRUE &&
        limited < percent)
    {
        percent = limited;
    }
    return (percent > 100U) ? 100U : (u8)percent;
}

/* Historical uncalibrated transfer. HWMAX remains the Factory PWM full-scale
 * denominator; Product I-V limits are safety/calibration envelopes only. */
static boolean_en sys_pwm_default_for_percent(u8 percent, u16 *logical_pwm)
{
    u16 useful_range;
    u32 scaled;

    if (logical_pwm == NULL || percent > 100U || HWMAX_OUTCUR == 0U ||
        OP_PWM_OFFSET >= PWM_OUT_MAX || SET_OUTCUR == 0U ||
        SET_OUTCUR > HWMAX_OUTCUR)
    {
        return BOOL_FALSE;
    }
    useful_range = (u16)(PWM_OUT_MAX - OP_PWM_OFFSET);
    scaled = ((u32)percent * (u32)SET_OUTCUR * (u32)useful_range) /
             ((u32)HWMAX_OUTCUR * 100U);
    if (scaled > useful_range)
    {
        scaled = useful_range;
    }
    *logical_pwm = (u16)scaled;
    return BOOL_TRUE;
}

static boolean_en sys_pwm_default_for_target(u16 target_current_ma,
                                              u16 *logical_pwm)
{
    u16 useful_range;
    u32 scaled;

    if (logical_pwm == NULL || HWMAX_OUTCUR == 0U ||
        OP_PWM_OFFSET >= PWM_OUT_MAX || target_current_ma > HWMAX_OUTCUR)
    {
        return BOOL_FALSE;
    }
    useful_range = (u16)(PWM_OUT_MAX - OP_PWM_OFFSET);
    scaled = ((u32)target_current_ma * (u32)useful_range) /
             (u32)HWMAX_OUTCUR;
    if (scaled > useful_range)
    {
        scaled = useful_range;
    }
    *logical_pwm = (u16)scaled;
    return BOOL_TRUE;
}

static void sys_pwm_publish(u16 requested_percent,
                            u16 protected_percent)
{
    sys_calibration_snapshot_prepare_pwm(requested_percent,
                                         protected_percent);
    sys_calibration_snapshot_publish_pwm(
        HAL_GetTick(),
        hw_tim1_pwm2_get_logical_pwm(),
        hw_tim1_pwm2_get_ccr(),
        hw_tim1_pwm2_get_oco_on(),
        SYS_CALIBRATION_PWM_SAMPLE_VALID);
}

static boolean_en sys_pwm_calibration_feedback_ready(void)
{
    sys_calibration_adc_snapshot_st adc;
    u32 now_ms = HAL_GetTick();

    return (sys_pwm_config_valid() == BOOL_TRUE &&
            sys_pwm_calibration_fault_active() != BOOL_TRUE &&
            sys_bl0942_is_fresh(now_ms) == BOOL_TRUE &&
            sys_calibration_snapshot_read_adc(&adc) == BOOL_TRUE &&
            adc.valid_flags != 0U &&
            (now_ms - adc.tick_ms) <= CALIBRATION_FEEDBACK_MS) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_pwm_calibration_prepare_percent(
    u8 percent,
    u16 *target_current_ma)
{
    u8 protected_percent;
    u16 calibration_span_ma;

    if (target_current_ma == NULL || percent > 100U)
    {
        return BOOL_FALSE;
    }
    if (sys_pwm_calibration_feedback_ready() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    protected_percent = sys_pwm_apply_dynamic_limits(percent);
    if (protected_percent != percent)
    {
        return BOOL_FALSE;
    }
    calibration_span_ma = sys_calibration_service_calibration_span_ma();
    if (calibration_span_ma == 0U || calibration_span_ma > HWMAX_OUTCUR)
    {
        return BOOL_FALSE;
    }
    *target_current_ma = (u16)(((u32)calibration_span_ma * percent + 50U) /
                               100U);
    return BOOL_TRUE;
}

void pwm_output(u8 percent)
{
    u8 requested_percent = percent;
    u8 protected_percent;
    u16 target_current_ma = 0U;
    u16 logical_pwm = 0U;
    boolean_en calibrated_path = BOOL_FALSE;
    boolean_en emergency_stop;

    if (percent > 100U)
    {
        percent = 100U;
    }
    set_percent = requested_percent;
    protected_percent = sys_pwm_apply_dynamic_limits(percent);
    emergency_stop = sys_pwm_normal_shutdown_active();

    if (protected_percent > 0U && emergency_stop != BOOL_TRUE &&
        fa_test_EN == 0U && sys_pwm_config_valid() == BOOL_TRUE)
    {
        target_current_ma = (u16)(((u32)SET_OUTCUR * protected_percent + 50U) /
                                  100U);
        if (sys_calibration_service_output_pwm_for_current(
                target_current_ma, &logical_pwm) == BOOL_TRUE)
        {
            calibrated_path = BOOL_TRUE;
        }
        else if (sys_pwm_default_for_percent(protected_percent,
                                              &logical_pwm) != BOOL_TRUE)
        {
            logical_pwm = 0U;
        }
    }

    logical_pwm = sys_calibration_safety_arbitrate_pwm(
        logical_pwm,
        (fa_test_EN != 0U) ? SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT :
                             SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
        sys_calibration_service_is_boot_inhibited(),
        emergency_stop,
        BOOL_FALSE);

    if (logical_pwm == 0U)
    {
        hw_tim1_pwm2_set_PWM_OUT(0U);
    }
    else if (calibrated_path == BOOL_TRUE)
    {
        hw_tim1_pwm2_set_calibrated_PWM_OUT(logical_pwm);
    }
    else
    {
        hw_tim1_pwm2_set_PWM_OUT(logical_pwm);
    }
    sys_pwm_publish(requested_percent, protected_percent);
}

void sys_pwm_timer(void)
{
    u32 delta;

    if (_fade == BOOL_TRUE)
    {
        if (_timer < TIMEOUT_MAX)
        {
            ++_timer;
            if (power_new > power_old)
            {
                delta = power_new - power_old;
                power_current = (u8)(power_old +
                                     delta * _timer / TIMEOUT_MAX);
            }
            else
            {
                delta = power_old - power_new;
                power_current = (u8)(power_old -
                                     delta * _timer / TIMEOUT_MAX);
            }
            pwm_output(power_current);
        }
        else
        {
            _fade = BOOL_FALSE;
            power_old = power_new;
            pwm_output(power_new);
        }
    }
    if (fac_en_timer > 0U)
    {
        --fac_en_timer;
    }
}

void sys_pwm_fade_output(u8 oldpower, u8 newpower)
{
    power_current = oldpower;
    power_old = oldpower;
    power_new = newpower;
    pwm_output(oldpower);
    if (power_old != power_new)
    {
        _timer = 0U;
        _fade = BOOL_TRUE;
    }
}

u8 net_entery_flag;
u8 has_entery_at_first;

void sys_pwm_output(u8 percent)
{
    if (percent == 0U)
    {
        _fade = BOOL_FALSE;
        power_old = 0U;
        pwm_output(0U);
    }
    else if (_fade == BOOL_TRUE)
    {
        sys_pwm_fade_output(power_current, percent);
    }
    else if (net_entery_flag != 0U && has_entery_at_first == 0U)
    {
        has_entery_at_first = 1U;
        sys_pwm_fade_output(power_current, percent);
    }
    else
    {
        power_old = percent;
        pwm_output(percent);
    }
}

void sys_pwm_normal_output(u8 percent)
{
    if (sys_calibration_service_is_boot_inhibited() == BOOL_TRUE)
    {
        return;
    }
    sys_pwm_output(percent);
}

void sys_pwm_output_for_temp_protect(u8 percent)
{
    power_old = percent;
    _fade = BOOL_FALSE;
    pwm_output(percent);
}

void sys_pwm_output_on_fade(u8 percent)
{
    power_old = percent;
    _fade = BOOL_FALSE;
    pwm_output(percent);
}

void sys_pwm_reload(void)
{
    reload = 1U;
}

void sys_pwm_process(void)
{
    if (reload == 1U)
    {
        reload = 0U;
        sys_pwm_output((u8)set_percent);
    }
}

void sys_pwm_force_safe_off(void)
{
    net_dim_clear_pending();
    _fade = BOOL_FALSE;
    power_old = 0U;
    power_new = 0U;
    power_current = 0U;
    set_percent = 0U;
    hw_tim1_pwm2_set_PWM_OUT(0U);
    sys_pwm_publish(0U, 0U);
}

/* BUILD path: characterize the mature, uncalibrated SET/HWMAX transfer. */
boolean_en sys_pwm_calibration_set_level(u16 level, u16 *actual_pwm)
{
    u8 percent;
    u16 target_current_ma;
    u16 logical_pwm;
    u16 protected_pwm;

    if (actual_pwm == NULL ||
        sys_calibration_curve_validate_level(level) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    percent = (u8)(level / 2U);
    if (percent == 0U)
    {
        sys_pwm_force_safe_off();
        *actual_pwm = 0U;
        return BOOL_TRUE;
    }
    if (sys_pwm_calibration_prepare_percent(percent, &target_current_ma) !=
        BOOL_TRUE ||
        sys_pwm_default_for_target(target_current_ma, &logical_pwm) != BOOL_TRUE)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    protected_pwm = sys_calibration_safety_arbitrate_pwm(
        logical_pwm,
        SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION,
        sys_calibration_service_is_boot_inhibited(),
        sys_pwm_calibration_fault_active(),
        BOOL_TRUE);
    if (protected_pwm != logical_pwm)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    hw_tim1_pwm2_set_calibration_default_PWM_OUT(logical_pwm);
    sys_pwm_publish(percent, percent);
    *actual_pwm = hw_tim1_pwm2_get_logical_pwm();
    return (*actual_pwm == logical_pwm) ? BOOL_TRUE : BOOL_FALSE;
}

/* APPLY/VERIFY path: use the staged TargetCurrent->CorrectPWM curve. Never
 * fall back to the default transfer here, otherwise VERIFY would validate a
 * different algorithm from the one later used after COMMIT. */
boolean_en sys_pwm_calibration_set_output(u8 percent, u16 *actual_pwm)
{
    u16 target_current_ma;
    u16 logical_pwm;
    u16 protected_pwm;

    if (actual_pwm == NULL || percent > 100U)
    {
        return BOOL_FALSE;
    }
    if (percent == 0U)
    {
        sys_pwm_force_safe_off();
        *actual_pwm = 0U;
        return BOOL_TRUE;
    }
    if (sys_pwm_calibration_prepare_percent(percent, &target_current_ma) !=
        BOOL_TRUE ||
        sys_calibration_service_output_pwm_for_current(
            target_current_ma, &logical_pwm) != BOOL_TRUE)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    protected_pwm = sys_calibration_safety_arbitrate_pwm(
        logical_pwm,
        SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION,
        sys_calibration_service_is_boot_inhibited(),
        sys_pwm_calibration_fault_active(),
        BOOL_TRUE);
    if (protected_pwm != logical_pwm)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    hw_tim1_pwm2_set_calibration_PWM_OUT(logical_pwm);
    sys_pwm_publish(percent, percent);
    *actual_pwm = hw_tim1_pwm2_get_logical_pwm();
    return (*actual_pwm == logical_pwm) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_pwm_calibration_set_direct_pwm(
    u16 level,
    u16 logical_pwm,
    u16 *actual_pwm)
{
    u8 percent;
    u16 protected_pwm;

    if (actual_pwm == NULL || logical_pwm > PWM_OUT_MAX ||
        sys_calibration_curve_validate_level(level) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    percent = (u8)(level / 2U);
    if (level == 0U)
    {
        if (logical_pwm != 0U)
        {
            return BOOL_FALSE;
        }
        sys_pwm_force_safe_off();
        *actual_pwm = 0U;
        return BOOL_TRUE;
    }
    if (logical_pwm == 0U ||
        sys_pwm_calibration_feedback_ready() != BOOL_TRUE ||
        sys_pwm_apply_dynamic_limits(100U) != 100U)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }

    protected_pwm = sys_calibration_safety_arbitrate_pwm(
        logical_pwm,
        SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION,
        sys_calibration_service_is_boot_inhibited(),
        sys_pwm_calibration_fault_active(),
        BOOL_TRUE);
    if (protected_pwm != logical_pwm)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }

    hw_tim1_pwm2_set_calibration_PWM_OUT(logical_pwm);
    sys_pwm_publish(percent, percent);
    *actual_pwm = hw_tim1_pwm2_get_logical_pwm();
    return (*actual_pwm == logical_pwm) ? BOOL_TRUE : BOOL_FALSE;
}
