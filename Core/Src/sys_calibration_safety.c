/*************************************************************
程序功能：50W校准运行时输出限幅与绝对过流门禁
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_safety.h"
#include "sys_calibration_curve.h"

static u16 sys_calibration_safety_table_current_ma(u16 voltage_01v)
{
    u32 index;
    sys_calibration_iv_limit_st limit;
    u16 current_ma = 0U;

    for (index = 0U; index < SYS_CALIBRATION_IV_POINT_COUNT; ++index)
    {
        if (sys_calibration_curve_get_iv_limit(index, &limit) != BOOL_TRUE)
        {
            break;
        }
        if (voltage_01v >= limit.voltage_01v)
        {
            current_ma = limit.current_ma;
        }
        else
        {
            break;
        }
    }
    return current_ma;
}

u16 sys_calibration_safety_limit_current_ma(u16 voltage_01v)
{
    u32 power_current_ma;
    u16 table_current_ma;

    if (voltage_01v < SYS_CALIBRATION_50W_MIN_VOLTAGE_01V ||
        voltage_01v > SYS_CALIBRATION_50W_MAX_VOLTAGE_01V)
    {
        return 0U;
    }

    table_current_ma = sys_calibration_safety_table_current_ma(voltage_01v);
    power_current_ma = ((u32)SYS_CALIBRATION_50W_POWER_LIMIT_01W * 1000U) /
                       voltage_01v;
    if (power_current_ma < table_current_ma)
    {
        table_current_ma = (u16)power_current_ma;
    }
    if (table_current_ma > SYS_CALIBRATION_50W_CURRENT_LIMIT_MA)
    {
        table_current_ma = SYS_CALIBRATION_50W_CURRENT_LIMIT_MA;
    }
    return table_current_ma;
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
    return (current_ma >= SYS_CALIBRATION_ABSOLUTE_FAIL_CURRENT_MA) ?
           BOOL_TRUE : BOOL_FALSE;
}

u16 sys_calibration_safety_arbitrate_pwm(
    u16 requested_pwm,
    sys_calibration_output_source_en source,
    boolean_en boot_inhibited,
    boolean_en emergency_stop,
    boolean_en feedback_fresh)
{
    if (requested_pwm == 0U || boot_inhibited == BOOL_TRUE ||
        emergency_stop == BOOL_TRUE)
    {
        return 0U;
    }

    if (source != SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL &&
        source != SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN &&
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

    return (requested_pwm > 1000U) ? 1000U : requested_pwm;
}
