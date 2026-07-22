#include "current_cal_curve.h"
#include "factory_user_data.h"

#define CURRENT_CAL_TIM1_PRESCALER      71U
#define CURRENT_CAL_TIM1_ARR             999U
#define CURRENT_CAL_TIM1_PWM_MODE        2U
#define CURRENT_CAL_TIM1_POLARITY_HIGH   1U
#define CURRENT_CAL_PROFILE_FORMAT_LEGACY 1U
#define CURRENT_CAL_CONTEXT_FORMAT        2U
#define CURRENT_CAL_MAPPING_ALGORITHM     2U

static void current_cal_put_u16_le(u8 *dst, u16 value)
{
    dst[0] = (u8)(value & 0xffU);
    dst[1] = (u8)((value >> 8) & 0xffU);
}

static void current_cal_put_u32_le(u8 *dst, u32 value)
{
    dst[0] = (u8)(value & 0xffUL);
    dst[1] = (u8)((value >> 8) & 0xffUL);
    dst[2] = (u8)((value >> 16) & 0xffUL);
    dst[3] = (u8)((value >> 24) & 0xffUL);
}

u32 current_cal_crc32(const u8 *data, u32 length)
{
    u32 crc;
    u32 i;
    u8 bit;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }

    crc = 0xffffffffUL;
    for (i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 1UL) != 0U)
            {
                crc = (crc >> 1) ^ 0xedb88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xffffffffUL;
}

u16 current_cal_pwm_logical_max(void)
{
    if (OP_PWM_OFFSET >= CURRENT_CAL_PWM_PERIOD_COUNTS)
    {
        return 0U;
    }
    return (u16)(CURRENT_CAL_PWM_PERIOD_COUNTS - OP_PWM_OFFSET);
}

u32 current_cal_legacy_profile_crc(void)
{
    u8 serialized[52];
    u8 *out;

    out = serialized;
    current_cal_put_u32_le(out, CURRENT_CAL_PROFILE_FORMAT_LEGACY); out += 4;
    current_cal_put_u32_le(out, SID); out += 4;
    current_cal_put_u32_le(out, MID); out += 4;
    current_cal_put_u32_le(out, DRV_VERSION); out += 4;
    current_cal_put_u32_le(out, SET_OUTCUR); out += 4;
    current_cal_put_u32_le(out, HWMAX_OUTCUR); out += 4;
    current_cal_put_u32_le(out, OUTPUT_CUR_SENSOR); out += 4;
    current_cal_put_u32_le(out, OP_PWM_OFFSET); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_PRESCALER); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_ARR); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_PWM_MODE); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_POLARITY_HIGH); out += 4;
    current_cal_put_u32_le(out, current_cal_pwm_logical_max());

    return current_cal_crc32(serialized, sizeof(serialized));
}

u32 current_cal_context_crc(void)
{
    u8 serialized[52];
    u8 *out;

    /*
     * SET_OUTCUR is deliberately excluded: it is a runtime derating setting.
     * Any item below changes the physical PWM/current transfer function.
     */
    out = serialized;
    current_cal_put_u32_le(out, CURRENT_CAL_CONTEXT_FORMAT); out += 4;
    current_cal_put_u32_le(out, SID); out += 4;
    current_cal_put_u32_le(out, MID); out += 4;
    current_cal_put_u32_le(out, DRV_VERSION); out += 4;
    current_cal_put_u32_le(out, HWMAX_OUTCUR); out += 4;
    current_cal_put_u32_le(out, OUTPUT_CUR_SENSOR); out += 4;
    current_cal_put_u32_le(out, OP_PWM_OFFSET); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_PRESCALER); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_ARR); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_PWM_MODE); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_TIM1_POLARITY_HIGH); out += 4;
    current_cal_put_u32_le(out, current_cal_pwm_logical_max()); out += 4;
    current_cal_put_u32_le(out, CURRENT_CAL_MAPPING_ALGORITHM);

    return current_cal_crc32(serialized, sizeof(serialized));
}

u32 current_cal_profile_crc(void)
{
    return current_cal_context_crc();
}

u32 current_cal_curve_crc(const current_cal_curve_t *curve)
{
    u8 serialized[8U + CURRENT_CAL_POINT_COUNT * 2U];
    u8 *out;
    u8 i;

    if (curve == NULL)
    {
        return 0U;
    }

    out = serialized;
    current_cal_put_u16_le(out, curve->curve_version); out += 2;
    current_cal_put_u16_le(out, curve->point_count); out += 2;
    current_cal_put_u32_le(out, curve->calibration_max_current_ma); out += 4;
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        current_cal_put_u16_le(out, curve->logical_pwm[i]);
        out += 2;
    }
    return current_cal_crc32(serialized, sizeof(serialized));
}

current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *curve,
                                                       u32 expected_context_crc)
{
    u16 logical_max;
    u8 i;

    if (curve == NULL)
    {
        return CURRENT_CAL_CURVE_NULL;
    }
    if (curve->curve_version != CURRENT_CAL_CURVE_VERSION)
    {
        return CURRENT_CAL_CURVE_BAD_VERSION;
    }
    if (curve->point_count != CURRENT_CAL_POINT_COUNT)
    {
        return CURRENT_CAL_CURVE_BAD_COUNT;
    }
    if (curve->logical_pwm[0] != 0U)
    {
        return CURRENT_CAL_CURVE_BAD_ZERO;
    }
    if (curve->context_crc != expected_context_crc)
    {
        return CURRENT_CAL_CURVE_PROFILE_MISMATCH;
    }
    if (curve->calibration_max_current_ma == 0U ||
        SET_OUTCUR == 0U || HWMAX_OUTCUR == 0U ||
        (u32)SET_OUTCUR > curve->calibration_max_current_ma ||
        curve->calibration_max_current_ma > (u32)HWMAX_OUTCUR)
    {
        return CURRENT_CAL_CURVE_BAD_CURRENT_RANGE;
    }

    logical_max = current_cal_pwm_logical_max();
    for (i = 1U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        if (curve->logical_pwm[i] <= curve->logical_pwm[i - 1U])
        {
            return CURRENT_CAL_CURVE_NOT_MONOTONIC;
        }
        if (curve->logical_pwm[i] > logical_max)
        {
            return CURRENT_CAL_CURVE_OUT_OF_RANGE;
        }
    }
    if (curve->curve_crc != current_cal_curve_crc(curve))
    {
        return CURRENT_CAL_CURVE_CRC_MISMATCH;
    }
    return CURRENT_CAL_CURVE_OK;
}

static u16 current_cal_curve_interpolate_ratio(const current_cal_curve_t *curve,
                                               u64 target_numerator,
                                               u64 target_denominator)
{
    u32 index;
    u16 a;
    u16 b;
    u64 position_numerator;
    u64 delta_numerator;
    u64 rounded_delta;

    if (curve == NULL || target_numerator == 0ULL || target_denominator == 0ULL ||
        curve->calibration_max_current_ma == 0U)
    {
        return 0U;
    }
    if (target_numerator >=
        target_denominator * (u64)curve->calibration_max_current_ma)
    {
        return curve->logical_pwm[CURRENT_CAL_POINT_COUNT - 1U];
    }

    /* Position is target/CAL_MAX across twenty equal curve intervals. */
    position_numerator = target_numerator * (CURRENT_CAL_POINT_COUNT - 1U);
    target_denominator *= (u64)curve->calibration_max_current_ma;
    index = (u32)(position_numerator / target_denominator);
    a = curve->logical_pwm[index];
    b = curve->logical_pwm[index + 1U];
    delta_numerator = (position_numerator % target_denominator) * (u64)(b - a);
    rounded_delta = (delta_numerator + target_denominator / 2ULL) / target_denominator;
    return (u16)(a + (u16)rounded_delta);
}

u16 current_cal_curve_interpolate_current(const current_cal_curve_t *curve,
                                          u32 target_current_ma)
{
    return current_cal_curve_interpolate_ratio(curve, target_current_ma, 1ULL);
}

u16 current_cal_curve_interpolate_setpoint(const current_cal_curve_t *curve,
                                           u8 percent,
                                           u32 set_current_ma)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    return current_cal_curve_interpolate_ratio(curve,
                                               (u64)percent * set_current_ma,
                                               100ULL);
}

u16 current_cal_curve_interpolate(const current_cal_curve_t *curve, u8 percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    if (curve == NULL)
    {
        return 0U;
    }
    /* This API is the factory/calibration percentage path: percent is a
     * percentage of the curve's CAL_MAX current, not a current in mA. */
    return current_cal_curve_interpolate_ratio(
        curve,
        (u64)percent * (u64)curve->calibration_max_current_ma,
        100ULL);
}
