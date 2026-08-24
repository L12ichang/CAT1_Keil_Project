/*************************************************************
程序功能：编译期单产品配置、固定保护边界和Product Fingerprint
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_product_profile.h"

static const sys_product_profile_iv_limit_st _50w_iv_limits[] =
{
    {250U, 1400U},
    {290U, 1400U},
    {320U, 1400U},
    {360U, 1400U},
    {400U, 1250U},
    {440U, 1140U},
    {480U, 1040U},
    {520U,  960U},
    {560U,  900U}
};

static const sys_product_profile_st _profile =
{
    SYS_PRODUCT_PROFILE_ID_50W, "DL-50Z-56T-MXG",
    SYS_PRODUCT_PROFILE_VERSION, SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32,
    SYS_PRODUCT_PROFILE_50W_MID,
    SYS_PRODUCT_PROFILE_50W_HARDWARE_REVISION,
    SYS_PRODUCT_PROFILE_50W_RATED_POWER_W,
    SYS_PRODUCT_PROFILE_50W_RS3_MOHM,
    SYS_PRODUCT_PROFILE_50W_HARDWARE_MAX_MA,
    SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA,
    SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA,
    SYS_PRODUCT_PROFILE_50W_PWM_FULL_SCALE,
    SYS_PRODUCT_PROFILE_50W_PWM_POLARITY,
    SYS_PRODUCT_PROFILE_50W_OCO_HARDWARE_REVISION,
    SYS_PRODUCT_PROFILE_50W_POWER_TOLERANCE_PM,
    SYS_PRODUCT_PROFILE_50W_ABSOLUTE_FAIL_MA,
    SYS_PRODUCT_PROFILE_50W_MIN_VOLTAGE_01V,
    SYS_PRODUCT_PROFILE_50W_CP_MIN_VOLTAGE_01V,
    SYS_PRODUCT_PROFILE_50W_MAX_VOLTAGE_01V,
    SYS_PRODUCT_PROFILE_SPECIAL_TEST_VOLTAGE_01V,
    _50w_iv_limits, 9U
};

static u32 sys_product_profile_crc32_byte(u32 crc, u8 value)
{
    u8 bit;
    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
    {
        crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
    return crc;
}

static void sys_product_profile_put_u16_le(u8 *destination, u16 value)
{
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8U);
}

const sys_product_profile_st *sys_product_profile_current(void)
{
    return &_profile;
}

boolean_en sys_product_profile_encode_fingerprint(
    const sys_product_profile_st *profile,
    u8 *encoded,
    u16 encoded_size)
{
    if (profile == NULL || encoded == NULL ||
        encoded_size < SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH)
    {
        return BOOL_FALSE;
    }
    sys_product_profile_put_u16_le(&encoded[0x00], profile->profile_version);
    sys_product_profile_put_u16_le(&encoded[0x02], profile->profile_id);
    encoded[0x04] = profile->mid;
    sys_product_profile_put_u16_le(&encoded[0x05], profile->hardware_revision);
    sys_product_profile_put_u16_le(&encoded[0x07], profile->rated_power_w);
    sys_product_profile_put_u16_le(&encoded[0x09], profile->rs3_mohm);
    sys_product_profile_put_u16_le(&encoded[0x0B], profile->hw_max_current_ma);
    sys_product_profile_put_u16_le(&encoded[0x0D], profile->pwm_full_scale);
    encoded[0x0F] = profile->pwm_polarity;
    sys_product_profile_put_u16_le(
        &encoded[0x10], profile->oco_hardware_revision);
    return BOOL_TRUE;
}

u32 sys_product_profile_crc32_iso_hdlc(const u8 *data, u16 length)
{
    u32 crc = 0xFFFFFFFFUL;
    u16 index;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        crc = sys_product_profile_crc32_byte(crc, data[index]);
    }
    return crc ^ 0xFFFFFFFFUL;
}

u32 sys_product_profile_calculate_fingerprint(
    const sys_product_profile_st *profile)
{
    u8 encoded[SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH];

    if (sys_product_profile_encode_fingerprint(
            profile, encoded, (u16)sizeof(encoded)) != BOOL_TRUE)
    {
        return 0U;
    }
    return sys_product_profile_crc32_iso_hdlc(
        encoded, (u16)sizeof(encoded));
}

boolean_en sys_product_profile_is_complete(
    const sys_product_profile_st *profile)
{
    if (profile == NULL || profile->model_code == NULL ||
        profile->fingerprint_crc32 == 0U ||
        profile->mid == 0U || profile->hardware_revision == 0U ||
        profile->rated_power_w == 0U ||
        profile->pwm_full_scale == 0U || profile->pwm_polarity > 1U ||
        profile->oco_hardware_revision == 0U ||
        profile->default_runtime_current_ma == 0U ||
        profile->default_hwmax_current_ma == 0U ||
        profile->rs3_mohm == 0U ||
        profile->hw_max_current_ma == 0U || profile->absolute_fail_current_ma == 0U ||
        profile->iv_limits == NULL || profile->iv_limit_count == 0U ||
        profile->iv_limit_count > SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX ||
        profile->minimum_voltage_01v > profile->constant_power_min_voltage_01v ||
        profile->constant_power_min_voltage_01v > profile->maximum_voltage_01v ||
        profile->iv_limits[0].voltage_01v > profile->minimum_voltage_01v ||
        profile->iv_limits[profile->iv_limit_count - 1U].voltage_01v <
            profile->maximum_voltage_01v ||
        profile->default_runtime_current_ma >=
            profile->absolute_fail_current_ma ||
        profile->default_runtime_current_ma >
            profile->default_hwmax_current_ma ||
        profile->default_hwmax_current_ma > profile->hw_max_current_ma ||
        profile->default_runtime_current_ma >
            profile->iv_limits[profile->iv_limit_count - 1U].current_ma ||
        sys_product_profile_calculate_fingerprint(profile) !=
            profile->fingerprint_crc32)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_product_profile_runtime_matches(
    u8 mid,
    u16 rs3_mohm,
    u16 hw_max_current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    return (sys_product_profile_is_complete(profile) == BOOL_TRUE &&
            mid == profile->mid && rs3_mohm == profile->rs3_mohm &&
            hw_max_current_ma > 0U &&
            hw_max_current_ma <= profile->hw_max_current_ma) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_product_profile_get_iv_limit(
    const sys_product_profile_st *profile,
    u32 index,
    sys_product_profile_iv_limit_st *limit)
{
    if (profile == NULL || limit == NULL || profile->iv_limits == NULL ||
        index >= profile->iv_limit_count)
    {
        return BOOL_FALSE;
    }
    *limit = profile->iv_limits[index];
    return BOOL_TRUE;
}
