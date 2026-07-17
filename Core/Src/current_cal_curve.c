#include "current_cal_curve.h"
#include "factory_user_data.h"

#define CURRENT_CAL_TIM1_PRESCALER      71U
#define CURRENT_CAL_TIM1_ARR             999U
#define CURRENT_CAL_TIM1_PWM_MODE        2U
#define CURRENT_CAL_TIM1_POLARITY_HIGH   1U
#define CURRENT_CAL_PROFILE_FORMAT       1U

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

u32 current_cal_profile_crc(void)
{
    u8 serialized[52];
    u8 *out;

    out = serialized;
    current_cal_put_u32_le(out, CURRENT_CAL_PROFILE_FORMAT); out += 4;
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

u32 current_cal_curve_crc(const current_cal_curve_t *curve)
{
    u8 serialized[4U + CURRENT_CAL_POINT_COUNT * 2U];
    u8 *out;
    u8 i;

    if (curve == NULL)
    {
        return 0U;
    }

    out = serialized;
    current_cal_put_u16_le(out, curve->curve_version); out += 2;
    current_cal_put_u16_le(out, curve->point_count); out += 2;
    for (i = 0U; i < CURRENT_CAL_POINT_COUNT; ++i)
    {
        current_cal_put_u16_le(out, curve->logical_pwm[i]);
        out += 2;
    }
    return current_cal_crc32(serialized, sizeof(serialized));
}

current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *curve,
                                                       u32 expected_profile_crc)
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
    if (curve->profile_crc != expected_profile_crc)
    {
        return CURRENT_CAL_CURVE_PROFILE_MISMATCH;
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

u16 current_cal_curve_interpolate(const current_cal_curve_t *curve, u8 percent)
{
    u8 index;
    u8 remainder;
    u16 a;
    u16 b;
    u32 delta;

    if (curve == NULL || percent == 0U)
    {
        return 0U;
    }
    if (percent >= 100U)
    {
        return curve->logical_pwm[CURRENT_CAL_POINT_COUNT - 1U];
    }

    index = (u8)(percent / 5U);
    remainder = (u8)(percent % 5U);
    a = curve->logical_pwm[index];
    b = curve->logical_pwm[index + 1U];
    delta = (u32)(b - a) * remainder;
    return (u16)(a + (delta + 2U) / 5U);
}
