/*************************************************************
程序功能：当前编译产品的运行时输出限幅与绝对过流门禁
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_safety.h"
#include "sys_calibration_curve.h"

static u16 sys_calibration_safety_table_current_ma(u16 voltage_01v)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_iv_limit_st lower;
    sys_calibration_iv_limit_st upper;
    u32 index;
    u32 span;
    u32 position;
    u32 delta;

    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        profile->iv_limit_count == 0U ||
        sys_calibration_curve_get_iv_limit(0U, &lower) != BOOL_TRUE ||
        voltage_01v < lower.voltage_01v)
    {
        return 0U;
    }
    if (voltage_01v == lower.voltage_01v)
    {
        return lower.current_ma;
    }

    for (index = 1U; index < profile->iv_limit_count; ++index)
    {
        if (sys_calibration_curve_get_iv_limit(index, &upper) != BOOL_TRUE)
        {
            return 0U;
        }
        if (voltage_01v == upper.voltage_01v)
        {
            return upper.current_ma;
        }
        if (voltage_01v < upper.voltage_01v)
        {
            span = (u32)upper.voltage_01v - lower.voltage_01v;
            position = (u32)voltage_01v - lower.voltage_01v;
            delta = (u32)lower.current_ma - upper.current_ma;
            return (u16)((u32)lower.current_ma -
                         (delta * position + span / 2U) / span);
        }
        lower = upper;
    }
    return (voltage_01v == lower.voltage_01v) ? lower.current_ma : 0U;
}

boolean_en sys_calibration_safety_is_supported_calibration_voltage(
    u16 voltage_01v)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_iv_limit_st limit;
    u32 index;

    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        voltage_01v == profile->special_test_voltage_01v)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (sys_calibration_curve_get_iv_limit(index, &limit) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        if (limit.voltage_01v == voltage_01v)
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

u16 sys_calibration_safety_limit_current_ma(u16 voltage_01v)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u64 power_numerator;
    u32 power_current_ma;
    u16 table_current_ma;

    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        voltage_01v < profile->minimum_voltage_01v ||
        voltage_01v > profile->maximum_voltage_01v ||
        voltage_01v == profile->special_test_voltage_01v)
    {
        return 0U;
    }

    table_current_ma = sys_calibration_safety_table_current_ma(voltage_01v);
    if (table_current_ma == 0U)
    {
        return 0U;
    }

    /* The published I-V table is authoritative. The rated-power guard keeps
     * the profile tolerance instead of cutting valid table points such as
     * 50W/36V/1400mA down to an exact zero-tolerance P/V value. */
    power_numerator = (u64)profile->rated_power_w * 10000ULL *
                      (1000ULL + profile->power_limit_tolerance_permille);
    power_current_ma = (u32)(power_numerator /
                             ((u64)voltage_01v * 1000ULL));
    if (power_current_ma < table_current_ma)
    {
        table_current_ma = (u16)power_current_ma;
    }
    if (table_current_ma > profile->hw_max_current_ma)
    {
        table_current_ma = profile->hw_max_current_ma;
    }
    return table_current_ma;
}

u16 sys_calibration_safety_calibration_span_ma(
    u16 voltage_01v,
    u16 configured_hwmax_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 span_ma;

    if (configured_hwmax_ma == 0U ||
        sys_calibration_safety_is_supported_calibration_voltage(voltage_01v) !=
            BOOL_TRUE ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        configured_hwmax_ma > profile->hw_max_current_ma)
    {
        return 0U;
    }
    span_ma = sys_calibration_safety_limit_current_ma(voltage_01v);
    if (span_ma == 0U)
    {
        return 0U;
    }
    if (span_ma > configured_hwmax_ma)
    {
        span_ma = configured_hwmax_ma;
    }
    return span_ma;
}

boolean_en sys_calibration_safety_limit_percent(
    u8 requested_percent,
    u16 voltage_01v,
    u16 requested_current_ma,
    u8 *safe_percent)
{
    u16 current_limit_ma;
    u32 percent;

    if (safe_percent == NULL || requested_percent > 100U ||
        requested_current_ma == 0U)
    {
        return BOOL_FALSE;
    }

    current_limit_ma = sys_calibration_safety_limit_current_ma(voltage_01v);
    if (current_limit_ma == 0U)
    {
        *safe_percent = 0U;
        return BOOL_TRUE;
    }

    percent = ((u32)current_limit_ma * 100U) / requested_current_ma;
    if (percent > 100U)
    {
        percent = 100U;
    }
    if ((u32)requested_percent < percent)
    {
        percent = requested_percent;
    }
    *safe_percent = (u8)percent;
    return BOOL_TRUE;
}

boolean_en sys_calibration_safety_is_absolute_overcurrent(u32 current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    return (current_ma >= profile->absolute_fail_current_ma) ?
           BOOL_TRUE : BOOL_FALSE;
}

u16 sys_calibration_safety_arbitrate_pwm(
    u16 requested_pwm,
    sys_calibration_output_source_en source,
    boolean_en boot_inhibited,
    boolean_en emergency_stop,
    boolean_en feedback_fresh)
{
    if (requested_pwm == 0U || emergency_stop == BOOL_TRUE ||
        (boot_inhibited == BOOL_TRUE &&
         source != SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION))
    {
        return 0U;
    }

    if (source != SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL &&
        source != SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN &&
        source != SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION &&
        source != SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT)
    {
        return 0U;
    }

    if (source == SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT &&
        (SYS_CALIBRATION_FACTORY_DIRECT_PWM_ENABLED == 0U ||
         feedback_fresh != BOOL_TRUE))
    {
        return 0U;
    }

    if (source == SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION &&
        feedback_fresh != BOOL_TRUE)
    {
        return 0U;
    }

    return (requested_pwm > 1000U) ? 1000U : requested_pwm;
}
