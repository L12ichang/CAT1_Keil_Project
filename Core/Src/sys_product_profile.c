/*************************************************************
程序功能：旧11点校准六功率产品配置、I-V限值和校准上下文绑定
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_product_profile.h"

static const sys_product_profile_iv_limit_st _50w_iv_limits[] =
{
    {250U, 1400U}, {290U, 1400U}, {320U, 1400U},
    {360U, 1400U}, {400U, 1250U}, {440U, 1140U},
    {480U, 1040U}, {520U,  960U}, {560U,  900U}
};

static const sys_product_profile_iv_limit_st _75w_iv_limits[] =
{
    {250U, 2100U}, {290U, 2100U}, {320U, 2100U},
    {360U, 2100U}, {400U, 1870U}, {440U, 1700U},
    {480U, 1560U}, {520U, 1440U}, {560U, 1340U}
};

static const sys_product_profile_iv_limit_st _100w_iv_limits[] =
{
    {250U, 2800U}, {290U, 2800U}, {320U, 2800U},
    {360U, 2800U}, {400U, 2500U}, {440U, 2270U},
    {480U, 2080U}, {520U, 1920U}, {560U, 1780U}
};

static const sys_product_profile_iv_limit_st _150w_iv_limits[] =
{
    {250U, 4200U}, {290U, 4200U}, {320U, 4200U},
    {360U, 4200U}, {400U, 3750U}, {440U, 3400U},
    {480U, 3130U}, {520U, 2880U}, {560U, 2680U}
};

static const sys_product_profile_iv_limit_st _200w_iv_limits[] =
{
    {250U, 5600U}, {290U, 5600U}, {320U, 5600U},
    {360U, 5600U}, {400U, 5000U}, {440U, 4550U},
    {480U, 4170U}, {520U, 3850U}, {560U, 3570U}
};

static const sys_product_profile_iv_limit_st _240w_iv_limits[] =
{
    {250U, 6700U}, {290U, 6700U}, {320U, 6700U},
    {360U, 6700U}, {400U, 6000U}, {440U, 5450U},
    {480U, 5000U}, {520U, 4620U}, {560U, 4300U}
};

#define LEGACY_PROFILE_ENTRY(ID, MODEL, FP, MID, POWER, PTOL, CTOL, DEFAULT_MA, RS3, HWMAX, ABSFAIL, IV) \
    { (ID), (MODEL), SYS_PRODUCT_PROFILE_VERSION, (FP), (MID), (POWER), \
      (PTOL), (CTOL), (DEFAULT_MA), (RS3), (HWMAX), (ABSFAIL), \
      SYS_PRODUCT_PROFILE_MIN_VOLTAGE_01V, SYS_PRODUCT_PROFILE_CP_MIN_VOLTAGE_01V, \
      SYS_PRODUCT_PROFILE_MAX_VOLTAGE_01V, SYS_PRODUCT_PROFILE_SPECIAL_TEST_VOLTAGE_01V, \
      (IV), SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX, BOOL_TRUE, BOOL_TRUE, "", "OK" }

static const sys_product_profile_st _profiles[] =
{
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_50W, SYS_PRODUCT_PROFILE_50W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_50W_MID,
        SYS_PRODUCT_PROFILE_50W_RATED_POWER_W, SYS_PRODUCT_PROFILE_50W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_50W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_50W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_50W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_50W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_50W_ABSOLUTE_FAIL_MA,
        _50w_iv_limits),
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_75W, SYS_PRODUCT_PROFILE_75W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_75W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_75W_MID,
        SYS_PRODUCT_PROFILE_75W_RATED_POWER_W, SYS_PRODUCT_PROFILE_75W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_75W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_75W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_75W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_75W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_75W_ABSOLUTE_FAIL_MA,
        _75w_iv_limits),
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_100W, SYS_PRODUCT_PROFILE_100W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_100W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_100W_MID,
        SYS_PRODUCT_PROFILE_100W_RATED_POWER_W, SYS_PRODUCT_PROFILE_100W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_100W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_100W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_100W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_100W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_100W_ABSOLUTE_FAIL_MA,
        _100w_iv_limits),
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_150W, SYS_PRODUCT_PROFILE_150W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_150W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_150W_MID,
        SYS_PRODUCT_PROFILE_150W_RATED_POWER_W, SYS_PRODUCT_PROFILE_150W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_150W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_150W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_150W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_150W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_150W_ABSOLUTE_FAIL_MA,
        _150w_iv_limits),
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_200W, SYS_PRODUCT_PROFILE_200W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_200W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_200W_MID,
        SYS_PRODUCT_PROFILE_200W_RATED_POWER_W, SYS_PRODUCT_PROFILE_200W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_200W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_200W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_200W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_200W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_200W_ABSOLUTE_FAIL_MA,
        _200w_iv_limits),
    LEGACY_PROFILE_ENTRY(
        SYS_PRODUCT_PROFILE_ID_240W, SYS_PRODUCT_PROFILE_240W_MODEL_CODE,
        SYS_PRODUCT_PROFILE_240W_FINGERPRINT_CRC32, SYS_PRODUCT_PROFILE_240W_MID,
        SYS_PRODUCT_PROFILE_240W_RATED_POWER_W, SYS_PRODUCT_PROFILE_240W_POWER_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_240W_CAL_SPAN_TOLERANCE_PM,
        SYS_PRODUCT_PROFILE_240W_DEFAULT_CURRENT_MA, SYS_PRODUCT_PROFILE_240W_RS3_MOHM,
        SYS_PRODUCT_PROFILE_240W_HW_MAX_CURRENT_MA, SYS_PRODUCT_PROFILE_240W_ABSOLUTE_FAIL_MA,
        _240w_iv_limits)
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

static u32 sys_product_profile_crc32_u16(u32 crc, u16 value)
{
    crc = sys_product_profile_crc32_byte(crc, (u8)(value >> 8U));
    return sys_product_profile_crc32_byte(crc, (u8)value);
}

static u32 sys_product_profile_crc32_u32(u32 crc, u32 value)
{
    crc = sys_product_profile_crc32_byte(crc, (u8)(value >> 24U));
    crc = sys_product_profile_crc32_byte(crc, (u8)(value >> 16U));
    crc = sys_product_profile_crc32_byte(crc, (u8)(value >> 8U));
    return sys_product_profile_crc32_byte(crc, (u8)value);
}

const sys_product_profile_st *sys_product_profile_find(u16 profile_id)
{
    u32 index;
    for (index = 0U; index < (u32)(sizeof(_profiles) / sizeof(_profiles[0])); ++index)
    {
        if (_profiles[index].profile_id == profile_id)
        {
            return &_profiles[index];
        }
    }
    return NULL;
}

const sys_product_profile_st *sys_product_profile_current(void)
{
    return sys_product_profile_find((u16)SYS_PRODUCT_PROFILE_SELECT);
}

u32 sys_product_profile_calculate_fingerprint(
    const sys_product_profile_st *profile)
{
    u32 crc = 0xFFFFFFFFUL;
    u32 index;

    if (profile == NULL)
    {
        return 0U;
    }
    crc = sys_product_profile_crc32_u16(crc, profile->profile_id);
    crc = sys_product_profile_crc32_u16(crc, profile->profile_version);
    crc = sys_product_profile_crc32_byte(crc, profile->mid);
    crc = sys_product_profile_crc32_u16(crc, profile->rated_power_w);
    crc = sys_product_profile_crc32_u16(crc, profile->power_limit_tolerance_permille);
    crc = sys_product_profile_crc32_u16(crc, profile->calibration_span_tolerance_permille);
    crc = sys_product_profile_crc32_u16(crc, profile->rs3_mohm);
    crc = sys_product_profile_crc32_u16(crc, profile->hw_max_current_ma);
    crc = sys_product_profile_crc32_u16(crc, profile->absolute_fail_current_ma);
    crc = sys_product_profile_crc32_u16(crc, profile->minimum_voltage_01v);
    crc = sys_product_profile_crc32_u16(crc, profile->constant_power_min_voltage_01v);
    crc = sys_product_profile_crc32_u16(crc, profile->maximum_voltage_01v);
    crc = sys_product_profile_crc32_u16(crc, profile->special_test_voltage_01v);
    crc = sys_product_profile_crc32_byte(crc, profile->iv_limit_count);
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        crc = sys_product_profile_crc32_u16(crc, profile->iv_limits[index].voltage_01v);
        crc = sys_product_profile_crc32_u16(crc, profile->iv_limits[index].current_ma);
    }
    return crc ^ 0xFFFFFFFFUL;
}

boolean_en sys_product_profile_is_complete(
    const sys_product_profile_st *profile)
{
    u32 maximum_power_product;
    u32 index;

    if (profile == NULL || profile->build_enabled != BOOL_TRUE ||
        profile->nonzero_calibration_enabled != BOOL_TRUE ||
        profile->model_code == NULL || profile->fingerprint_crc32 == 0U ||
        profile->mid == 0U || profile->rated_power_w == 0U ||
        profile->default_runtime_current_ma == 0U ||
        profile->rs3_mohm == 0U || profile->hw_max_current_ma == 0U ||
        profile->absolute_fail_current_ma == 0U ||
        profile->iv_limits == NULL ||
        profile->iv_limit_count != SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX ||
        profile->minimum_voltage_01v > profile->constant_power_min_voltage_01v ||
        profile->constant_power_min_voltage_01v > profile->maximum_voltage_01v ||
        profile->default_runtime_current_ma >= profile->absolute_fail_current_ma ||
        profile->default_runtime_current_ma > profile->hw_max_current_ma ||
        sys_product_profile_calculate_fingerprint(profile) != profile->fingerprint_crc32)
    {
        return BOOL_FALSE;
    }

    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (profile->iv_limits[index].current_ma == 0U ||
            profile->iv_limits[index].current_ma > profile->hw_max_current_ma ||
            (index > 0U && profile->iv_limits[index].voltage_01v <=
                             profile->iv_limits[index - 1U].voltage_01v) ||
            (index > 0U && profile->iv_limits[index].current_ma >
                             profile->iv_limits[index - 1U].current_ma))
        {
            return BOOL_FALSE;
        }
    }

    /* Profile completeness must not assume the default current is valid at 56V.
     * Runtime validation applies the I-V cap at the actual bound output voltage. */
    maximum_power_product =
        ((u32)profile->rated_power_w * 10000UL *
         (1000UL + profile->power_limit_tolerance_permille)) / 1000UL;
    if ((u32)profile->default_runtime_current_ma *
            profile->maximum_voltage_01v > maximum_power_product)
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
            hw_max_current_ma == profile->hw_max_current_ma) ? BOOL_TRUE : BOOL_FALSE;
}

sys_product_current_validation_en sys_product_profile_validate_runtime_current(
    const sys_product_profile_st *profile,
    u16 bound_voltage_01v,
    u32 configured_current_ma)
{
    u32 index;
    u16 iv_limit_ma = 0U;
    u32 maximum_power_product;

    if (sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    if (bound_voltage_01v < profile->minimum_voltage_01v ||
        bound_voltage_01v > profile->maximum_voltage_01v ||
        bound_voltage_01v == profile->special_test_voltage_01v)
    {
        return SYS_PRODUCT_CURRENT_VOLTAGE_UNBOUND;
    }
    if (configured_current_ma == 0U)
    {
        return SYS_PRODUCT_CURRENT_ZERO;
    }
    if (configured_current_ma >= profile->absolute_fail_current_ma)
    {
        return SYS_PRODUCT_CURRENT_ABSOLUTE_FAIL;
    }
    if (configured_current_ma > profile->hw_max_current_ma)
    {
        return SYS_PRODUCT_CURRENT_HW_MAX;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (bound_voltage_01v >= profile->iv_limits[index].voltage_01v)
        {
            iv_limit_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (iv_limit_ma == 0U || configured_current_ma > iv_limit_ma)
    {
        return SYS_PRODUCT_CURRENT_IV_LIMIT;
    }
    maximum_power_product =
        ((u32)profile->rated_power_w * 10000UL *
         (1000UL + profile->power_limit_tolerance_permille)) / 1000UL;
    if (configured_current_ma * (u32)bound_voltage_01v > maximum_power_product)
    {
        return SYS_PRODUCT_CURRENT_POWER_LIMIT;
    }
    return SYS_PRODUCT_CURRENT_VALID;
}

sys_product_current_validation_en sys_product_profile_validate_calibrated_current(
    u32 configured_current_ma,
    boolean_en calibrated_max_available,
    u16 calibrated_max_current_ma)
{
    if (calibrated_max_available != BOOL_TRUE || calibrated_max_current_ma == 0U)
    {
        return SYS_PRODUCT_CURRENT_CALIBRATION_MAX_UNAVAILABLE;
    }
    if (configured_current_ma > calibrated_max_current_ma)
    {
        return SYS_PRODUCT_CURRENT_EXCEEDS_CALIBRATED_MAX;
    }
    return SYS_PRODUCT_CURRENT_VALID;
}

const char *sys_product_profile_current_validation_reason(
    sys_product_current_validation_en result)
{
    static const char *const reason[] =
    {
        "OK",
        "PROFILE_INCOMPLETE",
        "BOUND_VOLTAGE_REQUIRED_25_TO_56V",
        "CONFIGURED_CURRENT_MUST_BE_POSITIVE",
        "CONFIGURED_CURRENT_EXCEEDS_IV_LIMIT",
        "CONFIGURED_CURRENT_EXCEEDS_RATED_POWER",
        "CONFIGURED_CURRENT_EXCEEDS_HWMAX",
        "CONFIGURED_CURRENT_REACHES_ABSOLUTE_FAIL",
        "CALIBRATION_SESSION_ACTIVE",
        "CALIBRATED_MAX_CURRENT_UNAVAILABLE",
        "CONFIGURED_CURRENT_EXCEEDS_CALIBRATED_MAX"
    };
    if ((u32)result >= (u32)(sizeof(reason) / sizeof(reason[0])))
    {
        return "UNKNOWN_CURRENT_VALIDATION_ERROR";
    }
    return reason[(u32)result];
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

boolean_en sys_product_profile_compute_i100_ma(
    const sys_product_profile_st *profile,
    u16 calibration_voltage_01v,
    u16 *current_ma)
{
    u32 power_current_ma;
    u16 iv_current_ma = 0U;
    u16 result;
    u32 index;

    if (current_ma == NULL || sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        calibration_voltage_01v < profile->minimum_voltage_01v ||
        calibration_voltage_01v > profile->maximum_voltage_01v ||
        calibration_voltage_01v == profile->special_test_voltage_01v)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (calibration_voltage_01v >= profile->iv_limits[index].voltage_01v)
        {
            iv_current_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (iv_current_ma == 0U)
    {
        return BOOL_FALSE;
    }
    power_current_ma = ((u32)profile->rated_power_w * 10000UL) /
                       calibration_voltage_01v;
    result = (power_current_ma < iv_current_ma) ? (u16)power_current_ma :
                                                  iv_current_ma;
    if (result > profile->hw_max_current_ma)
    {
        result = profile->hw_max_current_ma;
    }
    if (result == 0U)
    {
        return BOOL_FALSE;
    }
    *current_ma = result;
    return BOOL_TRUE;
}

boolean_en sys_product_profile_scale_percent_to_pwm(
    const sys_product_profile_st *profile,
    u16 voltage_01v,
    u8 percent,
    u16 pwm_range,
    u16 *pwm_value)
{
    u16 reference_current_ma;
    u32 scaled;

    if (pwm_value == NULL || percent > 100U || pwm_range == 0U ||
        sys_product_profile_compute_i100_ma(
            profile, voltage_01v, &reference_current_ma) != BOOL_TRUE ||
        profile->hw_max_current_ma == 0U)
    {
        return BOOL_FALSE;
    }
    scaled = ((u32)percent * reference_current_ma * pwm_range) /
             ((u32)profile->hw_max_current_ma * 100U);
    if (scaled > pwm_range)
    {
        scaled = pwm_range;
    }
    *pwm_value = (u16)scaled;
    return BOOL_TRUE;
}

static boolean_en sys_product_profile_validate_legacy_i_max(
    const sys_product_profile_st *profile,
    u16 calibration_voltage_01v,
    u16 characterized_i_max_ma)
{
    u32 index;
    u16 iv_limit_ma = 0U;

    if (characterized_i_max_ma == 0U)
    {
        return BOOL_TRUE;
    }
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        calibration_voltage_01v < profile->minimum_voltage_01v ||
        calibration_voltage_01v > profile->maximum_voltage_01v ||
        calibration_voltage_01v == profile->special_test_voltage_01v)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (calibration_voltage_01v >= profile->iv_limits[index].voltage_01v)
        {
            iv_limit_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (iv_limit_ma == 0U || characterized_i_max_ma > iv_limit_ma ||
        characterized_i_max_ma > profile->hw_max_current_ma ||
        characterized_i_max_ma >= profile->absolute_fail_current_ma)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_product_profile_context_build(
    u16 calibration_voltage_01v,
    u16 configured_rated_current_ma,
    u16 calibrated_max_current_ma,
    u32 table_crc32,
    sys_calibration_context_st *context)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 theoretical_i100_ma;

    /* Keep the frozen wire field name, but bind it to the product algorithm:
       calibration target I100 = rated power / selected CV, limited by the
       existing I-V table and Hardware Max. It is not the 56V default SET. */
    if (context == NULL ||
        sys_product_profile_compute_i100_ma(
            profile, calibration_voltage_01v,
            &theoretical_i100_ma) != BOOL_TRUE ||
        configured_rated_current_ma != theoretical_i100_ma ||
        sys_product_profile_validate_runtime_current(
            profile, calibration_voltage_01v,
            configured_rated_current_ma) != SYS_PRODUCT_CURRENT_VALID ||
        sys_product_profile_validate_legacy_i_max(
            profile, calibration_voltage_01v,
            calibrated_max_current_ma) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    context->profile_id = profile->profile_id;
    context->profile_version = profile->profile_version;
    context->profile_fingerprint_crc32 = profile->fingerprint_crc32;
    context->calibration_voltage_01v = calibration_voltage_01v;
    /* Frozen field name; calibration semantics are theoretical I100 at
       calibration_voltage_01v, not the runtime/default SET_OUTCUR value. */
    context->configured_rated_current_ma = configured_rated_current_ma;
    /* Legacy protocol meaning: after 0x24/0x07 this is the measured Imax.
       It is not the theoretical rated-power I100 and not SET_OUTCUR. */
    context->calibrated_max_current_ma = calibrated_max_current_ma;
    context->table_crc32 = table_crc32;
    return BOOL_TRUE;
}

boolean_en sys_product_profile_context_validate(
    const sys_calibration_context_st *context,
    boolean_en require_table_crc)
{
    sys_calibration_context_st expected;
    if (context == NULL ||
        sys_product_profile_context_build(context->calibration_voltage_01v,
                                          context->configured_rated_current_ma,
                                          context->calibrated_max_current_ma,
                                          context->table_crc32,
                                          &expected) != BOOL_TRUE ||
        context->profile_id != expected.profile_id ||
        context->profile_version != expected.profile_version ||
        context->profile_fingerprint_crc32 != expected.profile_fingerprint_crc32 ||
        context->configured_rated_current_ma != expected.configured_rated_current_ma ||
        context->calibrated_max_current_ma != expected.calibrated_max_current_ma ||
        (require_table_crc == BOOL_TRUE &&
         (context->table_crc32 == 0U || context->calibrated_max_current_ma == 0U)) ||
        (require_table_crc != BOOL_TRUE && context->table_crc32 != 0U))
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

u32 sys_product_profile_context_binding_crc32(
    const sys_calibration_context_st *context)
{
    u32 crc = 0xFFFFFFFFUL;

    if (context == NULL)
    {
        return 0U;
    }
    crc = sys_product_profile_crc32_byte(crc, (u8)SYS_CALIBRATION_CONTEXT_VERSION);
    crc = sys_product_profile_crc32_u16(crc, context->profile_id);
    crc = sys_product_profile_crc32_u16(crc, context->profile_version);
    crc = sys_product_profile_crc32_u32(crc, context->profile_fingerprint_crc32);
    crc = sys_product_profile_crc32_u16(crc, context->calibration_voltage_01v);
    crc = sys_product_profile_crc32_u16(crc, context->configured_rated_current_ma);
    crc = sys_product_profile_crc32_u16(crc, context->calibrated_max_current_ma);
    crc = sys_product_profile_crc32_u32(crc, context->table_crc32);
    return crc ^ 0xFFFFFFFFUL;
}

boolean_en sys_product_profile_context_equal(
    const sys_calibration_context_st *first,
    const sys_calibration_context_st *second,
    boolean_en compare_table_crc)
{
    if (first == NULL || second == NULL ||
        first->profile_id != second->profile_id ||
        first->profile_version != second->profile_version ||
        first->profile_fingerprint_crc32 != second->profile_fingerprint_crc32 ||
        first->calibration_voltage_01v != second->calibration_voltage_01v ||
        first->configured_rated_current_ma != second->configured_rated_current_ma ||
        (compare_table_crc == BOOL_TRUE &&
         (first->calibrated_max_current_ma != second->calibrated_max_current_ma ||
          first->table_crc32 != second->table_crc32)))
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
