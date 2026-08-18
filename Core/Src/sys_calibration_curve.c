/*************************************************************
程序功能：当前编译产品的11点曲线与上下文校验
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_curve.h"

boolean_en sys_calibration_curve_validate_level(u16 level)
{
    u16 index;

    for (index = 0U; index < SYS_CALIBRATION_CURVE_POINT_COUNT; ++index)
    {
        if (level == (u16)(index * SYS_CALIBRATION_CURVE_LEVEL_STEP))
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

boolean_en sys_calibration_curve_validate_pwm(const u16 *pwm, u32 count)
{
    u32 index;

    if (pwm == NULL || count != SYS_CALIBRATION_CURVE_POINT_COUNT ||
        pwm[0U] != 0U)
    {
        return BOOL_FALSE;
    }

    for (index = 0U; index < SYS_CALIBRATION_CURVE_POINT_COUNT; ++index)
    {
        if (pwm[index] > SYS_CALIBRATION_CURVE_PWM_MAX)
        {
            return BOOL_FALSE;
        }
        if (index > 0U && pwm[index] < pwm[index - 1U])
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_curve_interpolate(
    const u16 *pwm,
    u32 count,
    u16 level,
    u16 *value)
{
    u32 index;
    u32 remainder;
    u32 result;

    if (value == NULL || level > 200U ||
        sys_calibration_curve_validate_pwm(pwm, count) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    index = (u32)level / SYS_CALIBRATION_CURVE_LEVEL_STEP;
    remainder = (u32)level % SYS_CALIBRATION_CURVE_LEVEL_STEP;
    if (index >= (SYS_CALIBRATION_CURVE_POINT_COUNT - 1U))
    {
        *value = pwm[SYS_CALIBRATION_CURVE_POINT_COUNT - 1U];
        return BOOL_TRUE;
    }

    result = (u32)pwm[index] +
             (((u32)pwm[index + 1U] - (u32)pwm[index]) * remainder) /
             SYS_CALIBRATION_CURVE_LEVEL_STEP;
    *value = (u16)result;
    return BOOL_TRUE;
}

boolean_en sys_calibration_curve_get_iv_limit(
    u32 index,
    sys_calibration_iv_limit_st *limit)
{
    return sys_product_profile_get_iv_limit(sys_product_profile_current(),
                                            index, limit);
}
