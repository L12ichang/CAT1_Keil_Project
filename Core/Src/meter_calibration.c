#include "meter_calibration.h"
#if defined(METER_CALIBRATION_HOST_TEST)
u32 current_cal_crc32(const u8 *data, u32 length);
#else
#include "current_cal_curve.h"
#endif
#include <stddef.h>

#define METER_CAL_COEFFICIENT_CRC_SIZE       92U
#define METER_CAL_CHECKPOINT_CRC_SIZE         40U

/* Broad physical guards for all currently planned 50 W through 240 W SKUs. */
#define METER_CAL_INPUT_VOLTAGE_MAX_MV       1000000UL  /* 1000 V */
#define METER_CAL_INPUT_CURRENT_MAX_UA       10000000UL /* 10 A */
#define METER_CAL_INPUT_POWER_MAX_MW         2000000UL  /* 2 kW */
#define METER_CAL_FREQUENCY_MIN_MILLIHZ      20000UL    /* 20 Hz */
#define METER_CAL_FREQUENCY_MAX_MILLIHZ      120000UL   /* 120 Hz */
#define METER_CAL_OUTPUT_VOLTAGE_MAX_MV      200000UL   /* 200 V */
#define METER_CAL_OUTPUT_CURRENT_MAX_UA      25000000UL /* 25 A */

/* The BL0942 period register is about 20,000 at 50 Hz and 16,667 at 60 Hz. */
#define METER_CAL_FREQUENCY_RAW_MIN           10000UL
#define METER_CAL_FREQUENCY_RAW_MAX           40000UL

/*
 * A coefficient must produce at least these deliberately low values at the
 * channel's highest credible raw input.  This rejects erased/corrupt factors
 * such as 1 while leaving wide margin around every supported power class.
 */
#define METER_CAL_INPUT_VOLTAGE_VALIDATION_MIN_MV  10000UL
#define METER_CAL_INPUT_CURRENT_VALIDATION_MIN_UA  10000UL
#define METER_CAL_INPUT_POWER_VALIDATION_MIN_MW    1000UL
#define METER_CAL_OUTPUT_VOLTAGE_VALIDATION_MIN_MV 1000UL
#define METER_CAL_OUTPUT_CURRENT_VALIDATION_MIN_UA 10000UL

/*
 * Per-channel raw-zero and Q24 factor guards.  Ranges intentionally cover
 * the known BL0942 front ends and 15/30/50/100 mOhm output-shunt families,
 * while rejecting erased or dimensionally impossible values before use.
 */
#define METER_CAL_INPUT_ZERO_MAX_RAW          1000000L
#define METER_CAL_POWER_ZERO_MIN_RAW          (-262144L)
#define METER_CAL_POWER_ZERO_MAX_RAW          262144L
#define METER_CAL_OUTPUT_ZERO_MAX_RAW         1024L

#define METER_CAL_INPUT_VOLTAGE_FACTOR_MIN_Q24 16777ULL
#define METER_CAL_INPUT_VOLTAGE_FACTOR_MAX_Q24 3355443ULL
#define METER_CAL_INPUT_CURRENT_FACTOR_MIN_Q24 167772ULL
#define METER_CAL_INPUT_CURRENT_FACTOR_MAX_Q24 83886080ULL
#define METER_CAL_INPUT_POWER_FACTOR_MIN_Q24   16777ULL
#define METER_CAL_INPUT_POWER_FACTOR_MAX_Q24   33554432ULL
#define METER_CAL_FREQUENCY_FACTOR_MIN_Q24     8388608000000000ULL
#define METER_CAL_FREQUENCY_FACTOR_MAX_Q24     25165824000000000ULL
#define METER_CAL_OUTPUT_VOLTAGE_FACTOR_MIN_Q24 16777216ULL
#define METER_CAL_OUTPUT_VOLTAGE_FACTOR_MAX_Q24 1677721600ULL
#define METER_CAL_OUTPUT_CURRENT_FACTOR_MIN_Q24 16777216ULL
#define METER_CAL_OUTPUT_CURRENT_FACTOR_MAX_Q24 838860800000ULL

#define METER_CAL_INPUT_MIN_RAW_SPAN           16000000UL
#define METER_CAL_POWER_MIN_RAW_SPAN           8300000UL
#define METER_CAL_OUTPUT_MIN_RAW_SPAN          3500UL

/* Enabled energy calibration accepts 0.001 through 100,000 uWh per CF. */
#define METER_CAL_ENERGY_GAIN_Q24_MIN         16777ULL
#define METER_CAL_ENERGY_GAIN_Q24_MAX         1677721600000ULL

#define METER_CAL_ASSERT_JOIN_(a, b) a##b
#define METER_CAL_ASSERT_JOIN(a, b) METER_CAL_ASSERT_JOIN_(a, b)
#define METER_CAL_STATIC_ASSERT(condition) \
    typedef char METER_CAL_ASSERT_JOIN(meter_cal_static_assert_, __LINE__)[(condition) ? 1 : -1]

METER_CAL_STATIC_ASSERT(sizeof(meter_cal_coefficients_t) == 96U);
METER_CAL_STATIC_ASSERT(sizeof(meter_cal_coefficients_t) <= 128U);
METER_CAL_STATIC_ASSERT(offsetof(meter_cal_coefficients_t, data_crc) ==
                        METER_CAL_COEFFICIENT_CRC_SIZE);
METER_CAL_STATIC_ASSERT(METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE == 44U);

static void meter_cal_put_u16_le(u8 *destination, u16 value)
{
    destination[0] = (u8)(value & 0xffU);
    destination[1] = (u8)((value >> 8) & 0xffU);
}

static void meter_cal_put_u32_le(u8 *destination, u32 value)
{
    destination[0] = (u8)(value & 0xffUL);
    destination[1] = (u8)((value >> 8) & 0xffUL);
    destination[2] = (u8)((value >> 16) & 0xffUL);
    destination[3] = (u8)((value >> 24) & 0xffUL);
}

static void meter_cal_put_u64_le(u8 *destination, u64 value)
{
    u8 index;

    for (index = 0U; index < 8U; ++index)
    {
        destination[index] = (u8)(value & 0xffULL);
        value >>= 8;
    }
}

static u16 meter_cal_get_u16_le(const u8 *source)
{
    return (u16)((u16)source[0] | ((u16)source[1] << 8));
}

static u32 meter_cal_get_u32_le(const u8 *source)
{
    return (u32)source[0] |
           ((u32)source[1] << 8) |
           ((u32)source[2] << 16) |
           ((u32)source[3] << 24);
}

static s32 meter_cal_get_s32_le(const u8 *source)
{
    u32 value;

    value = meter_cal_get_u32_le(source);
    if (value <= 0x7fffffffUL)
    {
        return (s32)value;
    }
    return -1L - (s32)(~value);
}

static u64 meter_cal_get_u64_le(const u8 *source)
{
    u64 value;
    u8 index;

    value = 0ULL;
    for (index = 0U; index < 8U; ++index)
    {
        value |= (u64)source[index] << (index * 8U);
    }
    return value;
}

static void meter_cal_zero_bytes(void *destination, u32 length)
{
    u8 *output;

    if (destination == NULL)
    {
        return;
    }
    output = (u8 *)destination;
    while (length > 0U)
    {
        *output++ = 0U;
        --length;
    }
}

static u32 meter_cal_saturate_u32(u64 value)
{
    return (value > 0xffffffffULL) ? 0xffffffffUL : (u32)value;
}

/*
 * Applies a non-negative Q24 factor without ever overflowing u64.  Saturation
 * is reported to callers so a persisted coefficient cannot silently turn an
 * impossible value into a plausible maximum.
 */
static meter_cal_result_en meter_cal_apply_linear_q24(u32 delta_raw,
                                                       u64 factor_q24,
                                                       u32 *value)
{
    u64 maximum_rounded_product;
    u64 scaled;

    if (value == NULL)
    {
        return METER_CAL_NULL;
    }
    if (factor_q24 == 0ULL)
    {
        *value = 0U;
        return METER_CAL_FACTOR_ZERO;
    }
    if (delta_raw == 0U)
    {
        *value = 0U;
        return METER_CAL_OK;
    }

    maximum_rounded_product =
        ((u64)0xffffffffUL << METER_CAL_Q24_SHIFT) +
        ((u64)METER_CAL_Q24_HALF - 1ULL);
    if (factor_q24 > maximum_rounded_product / (u64)delta_raw)
    {
        *value = 0xffffffffUL;
        return METER_CAL_CONVERSION_SATURATED;
    }

    scaled = (u64)delta_raw * factor_q24;
    *value = (u32)((scaled + (u64)METER_CAL_Q24_HALF) >>
                   METER_CAL_Q24_SHIFT);
    return METER_CAL_OK;
}

static meter_cal_result_en meter_cal_apply_reciprocal_q24(
    u32 period_raw,
    u64 reciprocal_numerator_q24,
    u32 *value)
{
    u64 denominator;
    u64 quotient;
    u64 remainder;

    if (value == NULL)
    {
        return METER_CAL_NULL;
    }
    if (period_raw == 0U)
    {
        *value = 0U;
        return METER_CAL_RAW_OUT_OF_RANGE;
    }
    if (reciprocal_numerator_q24 == 0ULL)
    {
        *value = 0U;
        return METER_CAL_FACTOR_ZERO;
    }

    denominator = (u64)period_raw * (u64)METER_CAL_Q24_ONE;
    quotient = reciprocal_numerator_q24 / denominator;
    remainder = reciprocal_numerator_q24 % denominator;
    if (remainder >= (denominator / 2ULL + denominator % 2ULL))
    {
        ++quotient;
    }
    if (quotient > 0xffffffffULL)
    {
        *value = 0xffffffffUL;
        return METER_CAL_CONVERSION_SATURATED;
    }
    *value = (u32)quotient;
    return METER_CAL_OK;
}

static u32 meter_cal_validation_raw(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return 0x00ffffffUL;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return 0x007fffffUL;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return 0x00000fffUL;
        default:
            return 0U;
    }
}

static s32 meter_cal_zero_minimum(meter_cal_channel_en channel)
{
    if (channel == METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW)
    {
        return METER_CAL_POWER_ZERO_MIN_RAW;
    }
    return 0L;
}

static s32 meter_cal_zero_maximum(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_ZERO_MAX_RAW;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_POWER_ZERO_MAX_RAW;
        case METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ:
            return 0L;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_ZERO_MAX_RAW;
        default:
            return -1L;
    }
}

static u64 meter_cal_factor_minimum(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
            return METER_CAL_INPUT_VOLTAGE_FACTOR_MIN_Q24;
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_CURRENT_FACTOR_MIN_Q24;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_INPUT_POWER_FACTOR_MIN_Q24;
        case METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ:
            return METER_CAL_FREQUENCY_FACTOR_MIN_Q24;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
            return METER_CAL_OUTPUT_VOLTAGE_FACTOR_MIN_Q24;
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_CURRENT_FACTOR_MIN_Q24;
        default:
            return 0ULL;
    }
}

static u64 meter_cal_factor_maximum(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
            return METER_CAL_INPUT_VOLTAGE_FACTOR_MAX_Q24;
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_CURRENT_FACTOR_MAX_Q24;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_INPUT_POWER_FACTOR_MAX_Q24;
        case METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ:
            return METER_CAL_FREQUENCY_FACTOR_MAX_Q24;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
            return METER_CAL_OUTPUT_VOLTAGE_FACTOR_MAX_Q24;
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_CURRENT_FACTOR_MAX_Q24;
        default:
            return 0ULL;
    }
}

static u32 meter_cal_minimum_raw_span(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_MIN_RAW_SPAN;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_POWER_MIN_RAW_SPAN;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_MIN_RAW_SPAN;
        default:
            return 0U;
    }
}

static u32 meter_cal_validation_minimum(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
            return METER_CAL_INPUT_VOLTAGE_VALIDATION_MIN_MV;
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_CURRENT_VALIDATION_MIN_UA;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_INPUT_POWER_VALIDATION_MIN_MW;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
            return METER_CAL_OUTPUT_VOLTAGE_VALIDATION_MIN_MV;
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_CURRENT_VALIDATION_MIN_UA;
        default:
            return 0U;
    }
}

u32 meter_calibration_raw_max(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return 0x00ffffffUL;
        case METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ:
            return 0x0000ffffUL;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return 0x00000fffUL;
        default:
            return 0U;
    }
}

u32 meter_calibration_engineering_max(meter_cal_channel_en channel)
{
    switch (channel)
    {
        case METER_CAL_CHANNEL_INPUT_VOLTAGE_MV:
            return METER_CAL_INPUT_VOLTAGE_MAX_MV;
        case METER_CAL_CHANNEL_INPUT_CURRENT_UA:
            return METER_CAL_INPUT_CURRENT_MAX_UA;
        case METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW:
            return METER_CAL_INPUT_POWER_MAX_MW;
        case METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ:
            return METER_CAL_FREQUENCY_MAX_MILLIHZ;
        case METER_CAL_CHANNEL_OUTPUT_VOLTAGE_MV:
            return METER_CAL_OUTPUT_VOLTAGE_MAX_MV;
        case METER_CAL_CHANNEL_OUTPUT_CURRENT_UA:
            return METER_CAL_OUTPUT_CURRENT_MAX_UA;
        default:
            return 0U;
    }
}

static void meter_cal_coefficients_encode_prefix(
    const meter_cal_coefficients_t *coefficients,
    u8 *serialized)
{
    u8 *output;
    u8 index;

    output = serialized;
    meter_cal_put_u16_le(output, coefficients->version); output += 2;
    meter_cal_put_u16_le(output, coefficients->channel_count); output += 2;
    meter_cal_put_u32_le(output, coefficients->context_crc); output += 4;
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        meter_cal_put_u32_le(output, (u32)coefficients->zero_raw[index]);
        output += 4;
    }
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        meter_cal_put_u64_le(output, coefficients->factor_q24[index]);
        output += 8;
    }
    meter_cal_put_u64_le(output, coefficients->energy_gain_q24); output += 8;
    meter_cal_put_u32_le(output, coefficients->flags);
}

u32 meter_calibration_coefficients_crc(const meter_cal_coefficients_t *coefficients)
{
    u8 serialized[METER_CAL_COEFFICIENT_CRC_SIZE];

    if (coefficients == NULL)
    {
        return 0U;
    }

    meter_cal_coefficients_encode_prefix(coefficients, serialized);
    return current_cal_crc32(serialized, sizeof(serialized));
}

s32 meter_calibration_sign_extend_s24(u32 raw24)
{
    raw24 &= METER_CAL_CF_COUNTER_MASK;
    if ((raw24 & 0x00800000UL) != 0U)
    {
        return (s32)(raw24 & 0x007fffffUL) - 8388608L;
    }
    return (s32)raw24;
}

static meter_cal_result_en meter_cal_convert_unchecked(
    const meter_cal_coefficients_t *coefficients,
    meter_cal_channel_en channel,
    u32 raw,
    u32 *value)
{
    s32 signed_raw;
    s64 signed_delta;

    if (channel == METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ)
    {
        return meter_cal_apply_reciprocal_q24(
            raw,
            coefficients->factor_q24[(u32)channel],
            value);
    }

    if (channel == METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW)
    {
        signed_raw = meter_calibration_sign_extend_s24(raw);
        signed_delta = (s64)signed_raw -
                       (s64)coefficients->zero_raw[(u32)channel];
        /* Reverse/negative WATT is never exposed as a huge unsigned power. */
        if (signed_raw <= 0 || signed_delta <= 0LL)
        {
            *value = 0U;
            return METER_CAL_OK;
        }
        return meter_cal_apply_linear_q24(
            (u32)signed_delta,
            coefficients->factor_q24[(u32)channel],
            value);
    }

    if ((s64)raw <= (s64)coefficients->zero_raw[(u32)channel])
    {
        *value = 0U;
        return METER_CAL_OK;
    }
    signed_delta = (s64)raw -
                   (s64)coefficients->zero_raw[(u32)channel];
    return meter_cal_apply_linear_q24(
        (u32)signed_delta,
        coefficients->factor_q24[(u32)channel],
        value);
}

static meter_cal_result_en meter_cal_validate_linear_anchors(
    const meter_cal_coefficients_t *coefficients,
    meter_cal_channel_en channel)
{
    static const u8 anchor_percent[3] = {25U, 50U, 100U};
    meter_cal_result_en result;
    s64 signed_span;
    u64 scaled_span;
    u32 delta_raw;
    u32 engineering_value;
    u32 minimum_value;
    u32 maximum_value;
    u8 index;

    signed_span = (s64)meter_cal_validation_raw(channel) -
                  (s64)coefficients->zero_raw[(u32)channel];
    if (signed_span < (s64)meter_cal_minimum_raw_span(channel))
    {
        return METER_CAL_RAW_SPAN_TOO_SMALL;
    }
    for (index = 0U; index < 3U; ++index)
    {
        scaled_span = (u64)signed_span * (u64)anchor_percent[index];
        delta_raw = (u32)((scaled_span + 50ULL) / 100ULL);
        result = meter_cal_apply_linear_q24(
            delta_raw,
            coefficients->factor_q24[(u32)channel],
            &engineering_value);
        if (result != METER_CAL_OK)
        {
            return METER_CAL_FACTOR_OUT_OF_RANGE;
        }
        minimum_value =
            (u32)(((u64)meter_cal_validation_minimum(channel) *
                   (u64)anchor_percent[index] + 99ULL) / 100ULL);
        maximum_value =
            (u32)(((u64)meter_calibration_engineering_max(channel) *
                   (u64)anchor_percent[index] + 99ULL) / 100ULL);
        if (engineering_value < minimum_value ||
            engineering_value > maximum_value)
        {
            return METER_CAL_FACTOR_OUT_OF_RANGE;
        }
    }
    return METER_CAL_OK;
}

static meter_cal_result_en meter_cal_validate_frequency_anchors(
    const meter_cal_coefficients_t *coefficients)
{
    static const u8 anchor_percent[5] = {0U, 25U, 50U, 75U, 100U};
    meter_cal_result_en result;
    u32 raw_span;
    u32 period_raw;
    u32 engineering_value;
    u8 index;

    raw_span = METER_CAL_FREQUENCY_RAW_MAX -
               METER_CAL_FREQUENCY_RAW_MIN;
    for (index = 0U; index < 5U; ++index)
    {
        period_raw = METER_CAL_FREQUENCY_RAW_MIN +
            (u32)(((u64)raw_span * (u64)anchor_percent[index] + 50ULL) /
                  100ULL);
        result = meter_cal_apply_reciprocal_q24(
            period_raw,
            coefficients->factor_q24[
                METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ],
            &engineering_value);
        if (result != METER_CAL_OK ||
            engineering_value < METER_CAL_FREQUENCY_MIN_MILLIHZ ||
            engineering_value > METER_CAL_FREQUENCY_MAX_MILLIHZ)
        {
            return METER_CAL_FACTOR_OUT_OF_RANGE;
        }
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_coefficients_validate(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc)
{
    meter_cal_result_en result;
    meter_cal_channel_en channel;
    u8 index;

    if (coefficients == NULL)
    {
        return METER_CAL_NULL;
    }
    if (coefficients->version != METER_CAL_COEFFICIENT_VERSION)
    {
        return METER_CAL_BAD_VERSION;
    }
    if (coefficients->channel_count != METER_CAL_CHANNEL_COUNT)
    {
        return METER_CAL_BAD_CHANNEL_COUNT;
    }
    if (coefficients->context_crc != expected_context_crc)
    {
        return METER_CAL_CONTEXT_MISMATCH;
    }
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        channel = (meter_cal_channel_en)index;
        if (coefficients->zero_raw[index] < meter_cal_zero_minimum(channel) ||
            coefficients->zero_raw[index] > meter_cal_zero_maximum(channel))
        {
            return METER_CAL_ZERO_OUT_OF_RANGE;
        }
        if (coefficients->factor_q24[index] == 0ULL)
        {
            return METER_CAL_FACTOR_ZERO;
        }
        if (coefficients->factor_q24[index] <
                meter_cal_factor_minimum(channel) ||
            coefficients->factor_q24[index] >
                meter_cal_factor_maximum(channel))
        {
            return METER_CAL_FACTOR_OUT_OF_RANGE;
        }
    }

    result = meter_cal_validate_frequency_anchors(coefficients);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        channel = (meter_cal_channel_en)index;
        if (channel == METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ)
        {
            continue;
        }
        result = meter_cal_validate_linear_anchors(coefficients, channel);
        if (result != METER_CAL_OK)
        {
            return result;
        }
    }

    if ((coefficients->flags & ~METER_CAL_FLAGS_ALLOWED) != 0U)
    {
        return METER_CAL_FLAGS_INVALID;
    }
    if (coefficients->flags == 0U)
    {
        if (coefficients->energy_gain_q24 != 0ULL)
        {
            return METER_CAL_ENERGY_CONFIGURATION_INVALID;
        }
    }
    else if (coefficients->flags == METER_CAL_FLAGS_ENERGY_CF24)
    {
        if (coefficients->energy_gain_q24 < METER_CAL_ENERGY_GAIN_Q24_MIN ||
            coefficients->energy_gain_q24 > METER_CAL_ENERGY_GAIN_Q24_MAX)
        {
            return METER_CAL_ENERGY_CONFIGURATION_INVALID;
        }
    }
    else
    {
        return METER_CAL_ENERGY_CONFIGURATION_INVALID;
    }
    if (coefficients->data_crc !=
        meter_calibration_coefficients_crc(coefficients))
    {
        return METER_CAL_DATA_CRC_MISMATCH;
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_coefficients_encode(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u8 *serialized,
    u32 capacity)
{
    meter_cal_result_en result;
    u8 encoded[METER_CAL_COEFFICIENT_SERIALIZED_SIZE];
    u8 index;

    if (serialized != NULL)
    {
        meter_cal_zero_bytes(serialized,
            (capacity < METER_CAL_COEFFICIENT_SERIALIZED_SIZE) ?
            capacity : METER_CAL_COEFFICIENT_SERIALIZED_SIZE);
    }
    if (coefficients == NULL || serialized == NULL)
    {
        return METER_CAL_NULL;
    }
    if (capacity < METER_CAL_COEFFICIENT_SERIALIZED_SIZE)
    {
        return METER_CAL_BUFFER_TOO_SMALL;
    }
    result = meter_calibration_coefficients_validate(coefficients,
                                                     expected_context_crc);
    if (result != METER_CAL_OK)
    {
        return result;
    }

    meter_cal_coefficients_encode_prefix(coefficients, encoded);
    meter_cal_put_u32_le(&encoded[METER_CAL_COEFFICIENT_CRC_SIZE],
                         coefficients->data_crc);
    for (index = 0U; index < METER_CAL_COEFFICIENT_SERIALIZED_SIZE; ++index)
    {
        serialized[index] = encoded[index];
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_coefficients_decode(
    const u8 *serialized,
    u32 serialized_size,
    u32 expected_context_crc,
    meter_cal_coefficients_t *coefficients)
{
    meter_cal_coefficients_t candidate;
    meter_cal_result_en result;
    const u8 *input;
    u8 index;

    if (coefficients == NULL)
    {
        return METER_CAL_NULL;
    }
    meter_cal_zero_bytes(coefficients, sizeof(*coefficients));
    if (serialized == NULL)
    {
        return METER_CAL_NULL;
    }
    if (serialized_size != METER_CAL_COEFFICIENT_SERIALIZED_SIZE)
    {
        return METER_CAL_SERIALIZED_SIZE_INVALID;
    }

    meter_cal_zero_bytes(&candidate, sizeof(candidate));
    input = serialized;
    candidate.version = meter_cal_get_u16_le(input); input += 2;
    candidate.channel_count = meter_cal_get_u16_le(input); input += 2;
    candidate.context_crc = meter_cal_get_u32_le(input); input += 4;
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        candidate.zero_raw[index] = meter_cal_get_s32_le(input);
        input += 4;
    }
    for (index = 0U; index < METER_CAL_CHANNEL_COUNT; ++index)
    {
        candidate.factor_q24[index] = meter_cal_get_u64_le(input);
        input += 8;
    }
    candidate.energy_gain_q24 = meter_cal_get_u64_le(input); input += 8;
    candidate.flags = meter_cal_get_u32_le(input); input += 4;
    candidate.data_crc = meter_cal_get_u32_le(input);

    result = meter_calibration_coefficients_validate(&candidate,
                                                     expected_context_crc);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    *coefficients = candidate;
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_convert(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    meter_cal_channel_en channel,
    u32 raw,
    u32 *value)
{
    meter_cal_result_en result;

    if (value == NULL)
    {
        return METER_CAL_NULL;
    }
    *value = 0U;
    if ((u32)channel >= METER_CAL_CHANNEL_COUNT)
    {
        return METER_CAL_BAD_CHANNEL;
    }
    result = meter_calibration_coefficients_validate(coefficients,
                                                     expected_context_crc);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    if (raw > meter_calibration_raw_max(channel))
    {
        return METER_CAL_RAW_OUT_OF_RANGE;
    }
    if (channel == METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ &&
        (raw < METER_CAL_FREQUENCY_RAW_MIN ||
         raw > METER_CAL_FREQUENCY_RAW_MAX))
    {
        return METER_CAL_RAW_OUT_OF_RANGE;
    }

    result = meter_cal_convert_unchecked(coefficients, channel, raw, value);
    if (result == METER_CAL_CONVERSION_SATURATED ||
        (result == METER_CAL_OK &&
         *value > meter_calibration_engineering_max(channel)))
    {
        *value = 0U;
        return METER_CAL_ENGINEERING_OUT_OF_RANGE;
    }
    if (channel == METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ &&
        result == METER_CAL_OK && *value < METER_CAL_FREQUENCY_MIN_MILLIHZ)
    {
        *value = 0U;
        return METER_CAL_ENGINEERING_OUT_OF_RANGE;
    }
    return result;
}

u32 meter_calibration_output_power_mw(u32 output_voltage_mv,
                                      u32 output_current_ua)
{
    u64 product;
    u64 quotient;
    u64 remainder;

    product = (u64)output_voltage_mv * (u64)output_current_ua;
    quotient = product / 1000000ULL;
    remainder = product % 1000000ULL;
    if (remainder >= 500000ULL)
    {
        ++quotient;
    }
    return meter_cal_saturate_u32(quotient);
}

typedef struct
{
    u32 high;
    u64 low;
} meter_cal_u96_t;

static meter_cal_u96_t meter_cal_multiply_u64_u32(u64 value, u32 multiplier)
{
    meter_cal_u96_t product;
    u64 low_product;
    u64 high_product;
    u64 shifted_high;

    low_product = (u64)(u32)value * (u64)multiplier;
    high_product = (value >> 32) * (u64)multiplier;
    shifted_high = high_product << 32;
    product.low = low_product + shifted_high;
    product.high = (u32)(high_product >> 32);
    if (product.low < low_product)
    {
        ++product.high;
    }
    return product;
}

static s32 meter_cal_compare_u96(meter_cal_u96_t left,
                                 meter_cal_u96_t right)
{
    if (left.high < right.high)
    {
        return -1;
    }
    if (left.high > right.high)
    {
        return 1;
    }
    if (left.low < right.low)
    {
        return -1;
    }
    if (left.low > right.low)
    {
        return 1;
    }
    return 0;
}

static meter_cal_u96_t meter_cal_subtract_u96(meter_cal_u96_t left,
                                               meter_cal_u96_t right)
{
    meter_cal_u96_t difference;
    u32 borrow;

    borrow = (left.low < right.low) ? 1U : 0U;
    difference.low = left.low - right.low;
    difference.high = left.high - right.high - borrow;
    return difference;
}

/* Rounded value * multiplier / divisor; caller guarantees value < divisor. */
static u32 meter_cal_fraction_multiply(u64 value,
                                       u32 multiplier,
                                       u64 divisor)
{
    meter_cal_u96_t product;
    meter_cal_u96_t candidate;
    meter_cal_u96_t remainder;
    u32 lower;
    u32 upper;
    u32 middle;
    u64 half_divisor;

    product = meter_cal_multiply_u64_u32(value, multiplier);
    lower = 0U;
    upper = multiplier;
    while (lower < upper)
    {
        middle = lower + (upper - lower + 1U) / 2U;
        candidate = meter_cal_multiply_u64_u32(divisor, middle);
        if (meter_cal_compare_u96(candidate, product) <= 0)
        {
            lower = middle;
        }
        else
        {
            upper = middle - 1U;
        }
    }

    candidate = meter_cal_multiply_u64_u32(divisor, lower);
    remainder = meter_cal_subtract_u96(product, candidate);
    half_divisor = divisor / 2ULL + divisor % 2ULL;
    if (remainder.high == 0U && remainder.low >= half_divisor &&
        lower < multiplier)
    {
        ++lower;
    }
    return lower;
}

u32 meter_calibration_input_pf_ppm(u32 input_active_power_mw,
                                   u32 input_voltage_mv,
                                   u32 input_current_ua)
{
    u64 denominator;
    u64 scaled_power;

    denominator = (u64)input_voltage_mv * (u64)input_current_ua;
    if (denominator == 0ULL || input_active_power_mw == 0U)
    {
        return 0U;
    }

    /* mW / (mV * uA) contributes 1,000,000 before the ppm scale. */
    scaled_power = (u64)input_active_power_mw * 1000000ULL;
    if (scaled_power >= denominator)
    {
        return METER_CAL_PF_PPM_MAX;
    }
    return meter_cal_fraction_multiply(scaled_power,
                                       METER_CAL_PF_PPM_MAX,
                                       denominator);
}

u32 meter_calibration_cf24_delta(u32 previous_cf24, u32 current_cf24)
{
    return (current_cf24 - previous_cf24) & METER_CAL_CF_COUNTER_MASK;
}

static meter_cal_result_en meter_cal_energy_apply_q24(
    u32 cf_delta,
    u64 gain_q24,
    u32 previous_remainder_q24,
    u64 *energy_uwh,
    u32 *new_remainder_q24)
{
    u64 whole_gain_uwh;
    u64 fractional_gain_q24;
    u64 whole_energy_uwh;
    u64 fractional_energy_q24;
    u64 maximum_u64;

    if (energy_uwh == NULL || new_remainder_q24 == NULL)
    {
        return METER_CAL_NULL;
    }
    if (cf_delta == 0U)
    {
        *energy_uwh = 0ULL;
        *new_remainder_q24 = previous_remainder_q24;
        return METER_CAL_OK;
    }
    if (previous_remainder_q24 >= METER_CAL_Q24_ONE)
    {
        *energy_uwh = 0ULL;
        *new_remainder_q24 = 0U;
        return METER_CAL_CONVERSION_SATURATED;
    }

    /*
     * Keep the integer and fractional Q24 products separate.  A legal
     * 24-bit CF delta at the maximum 100,000 uWh/CF gain exceeds u64 before
     * the Q24 shift, even though the resulting uWh increment fits in u64.
     */
    whole_gain_uwh = gain_q24 >> METER_CAL_Q24_SHIFT;
    fractional_gain_q24 = gain_q24 & ((u64)METER_CAL_Q24_ONE - 1ULL);
    maximum_u64 = ~(u64)0;
    if (whole_gain_uwh > maximum_u64 / (u64)cf_delta)
    {
        *energy_uwh = 0ULL;
        *new_remainder_q24 = 0U;
        return METER_CAL_CONVERSION_SATURATED;
    }
    whole_energy_uwh = (u64)cf_delta * whole_gain_uwh;
    fractional_energy_q24 = (u64)cf_delta * fractional_gain_q24 +
                            (u64)previous_remainder_q24;
    if ((fractional_energy_q24 >> METER_CAL_Q24_SHIFT) >
        maximum_u64 - whole_energy_uwh)
    {
        *energy_uwh = 0ULL;
        *new_remainder_q24 = 0U;
        return METER_CAL_CONVERSION_SATURATED;
    }
    *energy_uwh = whole_energy_uwh +
                   (fractional_energy_q24 >> METER_CAL_Q24_SHIFT);
    *new_remainder_q24 = (u32)(fractional_energy_q24 &
                               ((u64)METER_CAL_Q24_ONE - 1ULL));
    return METER_CAL_OK;
}

void meter_calibration_energy_accumulator_init(
    meter_cal_energy_accumulator_t *accumulator,
    u64 initial_energy_uwh,
    u32 continuity_epoch)
{
    if (accumulator == NULL)
    {
        return;
    }
    accumulator->previous_cf24 = 0U;
    accumulator->remainder_q24 = 0U;
    accumulator->total_energy_uwh = initial_energy_uwh;
    accumulator->continuity_epoch = continuity_epoch;
    accumulator->initialized = BOOL_FALSE;
}

static meter_cal_result_en meter_cal_energy_state_validate(
    const meter_cal_energy_accumulator_t *accumulator)
{
    if (accumulator == NULL)
    {
        return METER_CAL_NULL;
    }
    if (accumulator->initialized != BOOL_FALSE &&
        accumulator->initialized != BOOL_TRUE)
    {
        return METER_CAL_CHECKPOINT_INVALID;
    }
    if (accumulator->remainder_q24 >= METER_CAL_Q24_ONE ||
        accumulator->previous_cf24 > METER_CAL_CF_COUNTER_MASK)
    {
        return METER_CAL_CHECKPOINT_INVALID;
    }
    if (accumulator->initialized == BOOL_FALSE &&
        (accumulator->previous_cf24 != 0U ||
         accumulator->remainder_q24 != 0U))
    {
        return METER_CAL_CHECKPOINT_INVALID;
    }
    return METER_CAL_OK;
}

static void meter_cal_checkpoint_encode_prefix(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u8 *serialized)
{
    u8 *output;

    output = serialized;
    meter_cal_put_u16_le(output, checkpoint->version); output += 2;
    meter_cal_put_u16_le(output, checkpoint->serialized_size); output += 2;
    meter_cal_put_u32_le(output, checkpoint->domain); output += 4;
    meter_cal_put_u32_le(output, checkpoint->context_crc); output += 4;
    meter_cal_put_u32_le(output, checkpoint->coefficient_data_crc); output += 4;
    meter_cal_put_u32_le(output, checkpoint->continuity_epoch); output += 4;
    meter_cal_put_u32_le(output, checkpoint->previous_cf24); output += 4;
    meter_cal_put_u32_le(output, checkpoint->remainder_q24); output += 4;
    meter_cal_put_u64_le(output, checkpoint->total_energy_uwh); output += 8;
    meter_cal_put_u32_le(output, checkpoint->initialized);
}

u32 meter_calibration_energy_checkpoint_crc(
    const meter_cal_energy_checkpoint_t *checkpoint)
{
    u8 serialized[METER_CAL_CHECKPOINT_CRC_SIZE];

    if (checkpoint == NULL)
    {
        return 0U;
    }
    meter_cal_checkpoint_encode_prefix(checkpoint, serialized);
    return current_cal_crc32(serialized, sizeof(serialized));
}

meter_cal_result_en meter_calibration_energy_checkpoint_validate(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch)
{
    if (checkpoint == NULL)
    {
        return METER_CAL_NULL;
    }
    if (checkpoint->version != METER_CAL_ENERGY_CHECKPOINT_VERSION)
    {
        return METER_CAL_CHECKPOINT_BAD_VERSION;
    }
    if (checkpoint->serialized_size !=
        METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE)
    {
        return METER_CAL_CHECKPOINT_SIZE_INVALID;
    }
    if (checkpoint->domain != METER_CAL_ENERGY_CHECKPOINT_DOMAIN)
    {
        return METER_CAL_CHECKPOINT_DOMAIN_MISMATCH;
    }
    if (checkpoint->data_crc !=
        meter_calibration_energy_checkpoint_crc(checkpoint))
    {
        return METER_CAL_CHECKPOINT_CRC_MISMATCH;
    }
    if (checkpoint->context_crc != expected_context_crc)
    {
        return METER_CAL_CHECKPOINT_CONTEXT_MISMATCH;
    }
    if (checkpoint->coefficient_data_crc != expected_coefficient_data_crc)
    {
        return METER_CAL_CHECKPOINT_COEFFICIENT_MISMATCH;
    }
    if (checkpoint->continuity_epoch != expected_continuity_epoch)
    {
        return METER_CAL_CHECKPOINT_EPOCH_MISMATCH;
    }
    if (checkpoint->initialized > 1U ||
        checkpoint->previous_cf24 > METER_CAL_CF_COUNTER_MASK ||
        checkpoint->remainder_q24 >= METER_CAL_Q24_ONE)
    {
        return METER_CAL_CHECKPOINT_INVALID;
    }
    if (checkpoint->initialized == 0U &&
        (checkpoint->previous_cf24 != 0U ||
         checkpoint->remainder_q24 != 0U))
    {
        return METER_CAL_CHECKPOINT_INVALID;
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_checkpoint_capture(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    const meter_cal_energy_accumulator_t *accumulator,
    meter_cal_energy_checkpoint_t *checkpoint)
{
    meter_cal_result_en result;

    if (checkpoint == NULL)
    {
        return METER_CAL_NULL;
    }
    meter_cal_zero_bytes(checkpoint, sizeof(*checkpoint));
    if (accumulator == NULL)
    {
        return METER_CAL_NULL;
    }
    result = meter_calibration_coefficients_validate(coefficients,
                                                     expected_context_crc);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    if (coefficients->flags != METER_CAL_FLAGS_ENERGY_CF24)
    {
        return METER_CAL_ENERGY_DISABLED;
    }
    result = meter_cal_energy_state_validate(accumulator);
    if (result != METER_CAL_OK)
    {
        return result;
    }

    checkpoint->version = METER_CAL_ENERGY_CHECKPOINT_VERSION;
    checkpoint->serialized_size = METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE;
    checkpoint->domain = METER_CAL_ENERGY_CHECKPOINT_DOMAIN;
    checkpoint->context_crc = expected_context_crc;
    checkpoint->coefficient_data_crc = coefficients->data_crc;
    checkpoint->continuity_epoch = accumulator->continuity_epoch;
    checkpoint->previous_cf24 = accumulator->previous_cf24;
    checkpoint->remainder_q24 = accumulator->remainder_q24;
    checkpoint->total_energy_uwh = accumulator->total_energy_uwh;
    checkpoint->initialized =
        (accumulator->initialized == BOOL_TRUE) ? 1U : 0U;
    checkpoint->data_crc = meter_calibration_energy_checkpoint_crc(checkpoint);
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_checkpoint_encode(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    u8 *serialized,
    u32 capacity)
{
    meter_cal_result_en result;
    u8 encoded[METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE];
    u8 index;

    if (serialized != NULL)
    {
        meter_cal_zero_bytes(serialized,
            (capacity < METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE) ?
            capacity : METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE);
    }
    if (checkpoint == NULL || serialized == NULL)
    {
        return METER_CAL_NULL;
    }
    if (capacity < METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE)
    {
        return METER_CAL_BUFFER_TOO_SMALL;
    }
    result = meter_calibration_energy_checkpoint_validate(
        checkpoint,
        expected_context_crc,
        expected_coefficient_data_crc,
        expected_continuity_epoch);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    meter_cal_checkpoint_encode_prefix(checkpoint, encoded);
    meter_cal_put_u32_le(&encoded[METER_CAL_CHECKPOINT_CRC_SIZE],
                         checkpoint->data_crc);
    for (index = 0U; index < METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE;
         ++index)
    {
        serialized[index] = encoded[index];
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_checkpoint_decode(
    const u8 *serialized,
    u32 serialized_size,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    meter_cal_energy_checkpoint_t *checkpoint)
{
    meter_cal_energy_checkpoint_t candidate;
    meter_cal_result_en result;
    const u8 *input;

    if (checkpoint == NULL)
    {
        return METER_CAL_NULL;
    }
    meter_cal_zero_bytes(checkpoint, sizeof(*checkpoint));
    if (serialized == NULL)
    {
        return METER_CAL_NULL;
    }
    if (serialized_size != METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE)
    {
        return METER_CAL_SERIALIZED_SIZE_INVALID;
    }
    meter_cal_zero_bytes(&candidate, sizeof(candidate));
    input = serialized;
    candidate.version = meter_cal_get_u16_le(input); input += 2;
    candidate.serialized_size = meter_cal_get_u16_le(input); input += 2;
    candidate.domain = meter_cal_get_u32_le(input); input += 4;
    candidate.context_crc = meter_cal_get_u32_le(input); input += 4;
    candidate.coefficient_data_crc = meter_cal_get_u32_le(input); input += 4;
    candidate.continuity_epoch = meter_cal_get_u32_le(input); input += 4;
    candidate.previous_cf24 = meter_cal_get_u32_le(input); input += 4;
    candidate.remainder_q24 = meter_cal_get_u32_le(input); input += 4;
    candidate.total_energy_uwh = meter_cal_get_u64_le(input); input += 8;
    candidate.initialized = meter_cal_get_u32_le(input); input += 4;
    candidate.data_crc = meter_cal_get_u32_le(input);

    result = meter_calibration_energy_checkpoint_validate(
        &candidate,
        expected_context_crc,
        expected_coefficient_data_crc,
        expected_continuity_epoch);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    *checkpoint = candidate;
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_accumulator_restore(
    meter_cal_energy_accumulator_t *accumulator,
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch)
{
    meter_cal_result_en result;

    if (accumulator == NULL)
    {
        return METER_CAL_NULL;
    }
    meter_cal_zero_bytes(accumulator, sizeof(*accumulator));
    result = meter_calibration_energy_checkpoint_validate(
        checkpoint,
        expected_context_crc,
        expected_coefficient_data_crc,
        expected_continuity_epoch);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    accumulator->previous_cf24 = checkpoint->previous_cf24;
    accumulator->remainder_q24 = checkpoint->remainder_q24;
    accumulator->total_energy_uwh = checkpoint->total_energy_uwh;
    accumulator->continuity_epoch = checkpoint->continuity_epoch;
    accumulator->initialized =
        (checkpoint->initialized != 0U) ? BOOL_TRUE : BOOL_FALSE;
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_checkpoint_rounded_uwh(
    const meter_cal_energy_checkpoint_t *checkpoint,
    u32 expected_context_crc,
    u32 expected_coefficient_data_crc,
    u32 expected_continuity_epoch,
    u64 *rounded_energy_uwh)
{
    meter_cal_result_en result;
    u64 maximum_u64;

    if (rounded_energy_uwh == NULL)
    {
        return METER_CAL_NULL;
    }
    *rounded_energy_uwh = 0ULL;
    result = meter_calibration_energy_checkpoint_validate(
        checkpoint,
        expected_context_crc,
        expected_coefficient_data_crc,
        expected_continuity_epoch);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    *rounded_energy_uwh = checkpoint->total_energy_uwh;
    if (checkpoint->remainder_q24 < METER_CAL_Q24_HALF)
    {
        return METER_CAL_OK;
    }
    maximum_u64 = ~(u64)0;
    if (*rounded_energy_uwh != maximum_u64)
    {
        ++*rounded_energy_uwh;
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_accumulate_cf24(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u32 current_cf24,
    u32 maximum_trusted_cf_delta,
    u32 continuity_epoch,
    boolean_en counter_continuous,
    meter_cal_energy_accumulator_t *accumulator,
    u32 *delta_energy_uwh)
{
    meter_cal_result_en result;
    u32 cf_delta;
    u32 new_remainder;
    u64 full_delta_energy_uwh;
    u64 accepted_delta_energy_uwh;
    u64 maximum_u64;

    if (accumulator == NULL || delta_energy_uwh == NULL)
    {
        return METER_CAL_NULL;
    }
    *delta_energy_uwh = 0U;
    if (counter_continuous != BOOL_FALSE &&
        counter_continuous != BOOL_TRUE)
    {
        return METER_CAL_CONTINUITY_INVALID;
    }
    result = meter_calibration_coefficients_validate(coefficients,
                                                     expected_context_crc);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    if (coefficients->flags != METER_CAL_FLAGS_ENERGY_CF24)
    {
        return METER_CAL_ENERGY_DISABLED;
    }
    if (current_cf24 > METER_CAL_CF_COUNTER_MASK ||
        maximum_trusted_cf_delta > METER_CAL_CF_COUNTER_MASK)
    {
        return METER_CAL_RAW_OUT_OF_RANGE;
    }
    result = meter_cal_energy_state_validate(accumulator);
    if (result != METER_CAL_OK)
    {
        return result;
    }
    if (counter_continuous == BOOL_FALSE)
    {
        accumulator->previous_cf24 = current_cf24;
        accumulator->continuity_epoch = continuity_epoch;
        accumulator->initialized = BOOL_TRUE;
        return METER_CAL_COUNTER_DISCONTINUITY;
    }
    if (accumulator->continuity_epoch != continuity_epoch)
    {
        return METER_CAL_CHECKPOINT_EPOCH_MISMATCH;
    }
    if (accumulator->initialized != BOOL_TRUE)
    {
        accumulator->previous_cf24 = current_cf24;
        accumulator->initialized = BOOL_TRUE;
        return METER_CAL_OK;
    }

    cf_delta = meter_calibration_cf24_delta(accumulator->previous_cf24,
                                            current_cf24);
    if (cf_delta > maximum_trusted_cf_delta)
    {
        /* A rejected trusted delta deliberately establishes a safe baseline. */
        accumulator->previous_cf24 = current_cf24;
        return METER_CAL_COUNTER_DISCONTINUITY;
    }

    result = meter_cal_energy_apply_q24(cf_delta,
                                        coefficients->energy_gain_q24,
                                        accumulator->remainder_q24,
                                        &full_delta_energy_uwh,
                                        &new_remainder);
    if (result != METER_CAL_OK)
    {
        return result;
    }

    maximum_u64 = ~(u64)0;
    accepted_delta_energy_uwh = full_delta_energy_uwh;
    if (accumulator->total_energy_uwh >
        maximum_u64 - full_delta_energy_uwh)
    {
        accepted_delta_energy_uwh = maximum_u64 -
                                    accumulator->total_energy_uwh;
        accumulator->total_energy_uwh = maximum_u64;
    }
    else
    {
        accumulator->total_energy_uwh += full_delta_energy_uwh;
    }

    /* Commit the trusted counter state only after all conversion succeeds. */
    accumulator->previous_cf24 = current_cf24;
    accumulator->remainder_q24 = new_remainder;
    if (accepted_delta_energy_uwh > (u64)0xffffffffUL)
    {
        *delta_energy_uwh = 0xffffffffUL;
    }
    else
    {
        *delta_energy_uwh = (u32)accepted_delta_energy_uwh;
    }
    if (accepted_delta_energy_uwh != full_delta_energy_uwh)
    {
        return METER_CAL_ACCUMULATOR_OVERFLOW;
    }
    if (full_delta_energy_uwh > (u64)0xffffffffUL)
    {
        return METER_CAL_CONVERSION_SATURATED;
    }
    return METER_CAL_OK;
}

meter_cal_result_en meter_calibration_energy_resume_cf24(
    const meter_cal_coefficients_t *coefficients,
    u32 expected_context_crc,
    u32 current_cf24,
    u32 maximum_trusted_cf_delta,
    u32 continuity_epoch,
    boolean_en counter_continuous,
    meter_cal_energy_accumulator_t *accumulator,
    u32 *delta_energy_uwh)
{
    return meter_calibration_energy_accumulate_cf24(
        coefficients,
        expected_context_crc,
        current_cf24,
        maximum_trusted_cf_delta,
        continuity_epoch,
        counter_continuous,
        accumulator,
        delta_energy_uwh);
}
