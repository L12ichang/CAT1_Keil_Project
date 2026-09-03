/*************************************************************
程序功能：Calibration V3 244B Wire Payload显式Little Endian Codec
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_calibration_driver_protocol.h"

#include <string.h>

#define SYS_CALIBRATION_OCO_GAIN_ANCHOR_INDEX        9U
#define SYS_CALIBRATION_OCO_LINEAR_QUANTIZATION       1U

static void sys_calibration_put_u16_le(u8 *destination, u16 value)
{
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8U);
}

static void sys_calibration_put_u32_le(u8 *destination, u32 value)
{
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8U);
    destination[2] = (u8)(value >> 16U);
    destination[3] = (u8)(value >> 24U);
}

static u16 sys_calibration_get_u16_le(const u8 *source)
{
    return (u16)((u16)source[0] | ((u16)source[1] << 8U));
}

static u32 sys_calibration_get_u32_le(const u8 *source)
{
    return (u32)source[0] |
           ((u32)source[1] << 8U) |
           ((u32)source[2] << 16U) |
           ((u32)source[3] << 24U);
}

static u32 sys_calibration_crc32_byte(u32 crc, u8 value)
{
    u8 bit;

    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
    {
        crc = (crc & 1U) != 0U ?
              ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
    return crc;
}

static boolean_en sys_calibration_hex_nibble(char value, u8 *nibble)
{
    if (nibble == NULL)
    {
        return BOOL_FALSE;
    }
    if (value >= '0' && value <= '9')
    {
        *nibble = (u8)(value - '0');
        return BOOL_TRUE;
    }
    if (value >= 'A' && value <= 'F')
    {
        *nibble = (u8)(value - 'A' + 10);
        return BOOL_TRUE;
    }
    if (value >= 'a' && value <= 'f')
    {
        *nibble = (u8)(value - 'a' + 10);
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

/*
 * Payload V2 keeps the historical 11-pair OCO wire area for compatibility,
 * but the production sampling algorithm is frozen as two-point linear:
 *   level 0   -> Offset Raw
 *   level 180 -> Gain anchor (90%)
 *
 * Every OCO pair must stay on that same line and its current axis must equal
 * the Output TargetCurrent axis. One ADC count is tolerated for integer
 * quantization only; this does not permit an independent 11-point OCO curve.
 */
static boolean_en sys_calibration_payload_oco_two_point_valid(
    const sys_calibration_payload_st *payload)
{
    u16 offset_raw;
    u16 anchor_raw;
    u16 anchor_reference_ma;
    u32 delta_raw;
    u8 index;

    if (payload == NULL ||
        SYS_CALIBRATION_OCO_GAIN_ANCHOR_INDEX >=
            SYS_CALIBRATION_PAYLOAD_POINT_COUNT)
    {
        return BOOL_FALSE;
    }

    offset_raw = payload->oco[0].oco_adc_raw;
    anchor_raw = payload->oco[
        SYS_CALIBRATION_OCO_GAIN_ANCHOR_INDEX].oco_adc_raw;
    anchor_reference_ma = payload->oco[
        SYS_CALIBRATION_OCO_GAIN_ANCHOR_INDEX].reference_output_current_ma;

    if (payload->oco[0].reference_output_current_ma != 0U ||
        anchor_raw <= offset_raw || anchor_reference_ma == 0U)
    {
        return BOOL_FALSE;
    }
    delta_raw = (u32)anchor_raw - (u32)offset_raw;

    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        u16 reference_ma = payload->oco[index].reference_output_current_ma;
        u32 expected_raw;
        u32 actual_raw;
        u32 difference;

        if (reference_ma !=
            payload->output[index].reference_output_current_ma)
        {
            return BOOL_FALSE;
        }

        expected_raw = (u32)offset_raw +
            (((u32)reference_ma * delta_raw +
              (u32)anchor_reference_ma / 2U) /
             (u32)anchor_reference_ma);
        actual_raw = (u32)payload->oco[index].oco_adc_raw;
        difference = (actual_raw > expected_raw) ?
                     (actual_raw - expected_raw) :
                     (expected_raw - actual_raw);
        if (expected_raw > 65535UL ||
            difference > SYS_CALIBRATION_OCO_LINEAR_QUANTIZATION)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

u32 sys_calibration_payload_crc32_iso_hdlc(const u8 *data, u16 length)
{
    u32 crc = 0xFFFFFFFFUL;
    u16 index;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        crc = sys_calibration_crc32_byte(crc, data[index]);
    }
    return crc ^ 0xFFFFFFFFUL;
}

boolean_en sys_calibration_payload_validate(
    const sys_calibration_payload_st *payload)
{
    u8 index;

    if (payload == NULL ||
        payload->point_count != SYS_CALIBRATION_PAYLOAD_POINT_COUNT ||
        payload->level_step != SYS_CALIBRATION_PAYLOAD_LEVEL_STEP ||
        payload->valid_flags != SYS_CALIBRATION_PAYLOAD_VALID_FLAGS ||
        payload->voltage_gain_q24 == 0U)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        if (payload->output[index].logical_pwm > 1000U ||
            (index == 0U && payload->output[index].logical_pwm != 0U))
        {
            return BOOL_FALSE;
        }
        if (index != 0U &&
            (payload->output[index].logical_pwm <=
                 payload->output[index - 1U].logical_pwm ||
             payload->output[index].reference_output_current_ma <=
                 payload->output[index - 1U].reference_output_current_ma ||
             payload->oco[index].oco_adc_raw <=
                 payload->oco[index - 1U].oco_adc_raw ||
             payload->oco[index].reference_output_current_ma <
                 payload->oco[index - 1U].reference_output_current_ma ||
             payload->bl_current[index].bl_current_raw <=
                 payload->bl_current[index - 1U].bl_current_raw ||
             payload->bl_current[index].reference_input_current_ma <
                 payload->bl_current[index - 1U].reference_input_current_ma ||
             payload->bl_power[index].bl_power_raw <=
                 payload->bl_power[index - 1U].bl_power_raw ||
             payload->bl_power[index].reference_input_power_01w <
                 payload->bl_power[index - 1U].reference_input_power_01w))
        {
            return BOOL_FALSE;
        }
    }
    if (payload->output[10].reference_output_current_ma <=
            payload->output[0].reference_output_current_ma ||
        payload->oco[10].oco_adc_raw <= payload->oco[0].oco_adc_raw ||
        payload->oco[10].reference_output_current_ma <=
            payload->oco[0].reference_output_current_ma ||
        payload->bl_current[10].bl_current_raw <=
            payload->bl_current[0].bl_current_raw ||
        payload->bl_current[10].reference_input_current_ma <=
            payload->bl_current[0].reference_input_current_ma ||
        payload->bl_power[10].bl_power_raw <=
            payload->bl_power[0].bl_power_raw ||
        payload->bl_power[10].reference_input_power_01w <=
            payload->bl_power[0].reference_input_power_01w ||
        sys_calibration_payload_oco_two_point_valid(payload) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_matches_product(
    const sys_calibration_payload_st *payload,
    const sys_product_profile_st *profile)
{
    if (sys_calibration_payload_validate(payload) != BOOL_TRUE ||
        profile == NULL ||
        payload->profile_id != profile->profile_id ||
        payload->profile_version != profile->profile_version ||
        payload->profile_fingerprint != profile->fingerprint_crc32)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_within_product_limits(
    const sys_calibration_payload_st *payload,
    const sys_product_profile_st *profile)
{
    u8 index;

    if (sys_calibration_payload_validate(payload) != BOOL_TRUE ||
        profile == NULL)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        if (payload->output[index].reference_output_current_ma >
                profile->hw_max_current_ma ||
            payload->oco[index].reference_output_current_ma >
                profile->hw_max_current_ma)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_encode(
    const sys_calibration_payload_st *payload,
    u8 *encoded,
    u16 encoded_capacity)
{
    u16 offset;
    u8 index;

    if (encoded == NULL || encoded_capacity < SYS_CALIBRATION_PAYLOAD_LENGTH ||
        sys_calibration_payload_validate(payload) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memset(encoded, 0, SYS_CALIBRATION_PAYLOAD_LENGTH);
    encoded[0x00] = SYS_CALIBRATION_PAYLOAD_MAGIC_0;
    encoded[0x01] = SYS_CALIBRATION_PAYLOAD_MAGIC_1;
    encoded[0x02] = SYS_CALIBRATION_PAYLOAD_MAGIC_2;
    encoded[0x03] = SYS_CALIBRATION_PAYLOAD_MAGIC_3;
    sys_calibration_put_u16_le(&encoded[0x04],
                               SYS_CALIBRATION_PAYLOAD_VERSION);
    sys_calibration_put_u16_le(&encoded[0x06],
                               SYS_CALIBRATION_PAYLOAD_LENGTH);
    sys_calibration_put_u16_le(&encoded[0x08], payload->profile_id);
    sys_calibration_put_u16_le(&encoded[0x0A], payload->profile_version);
    sys_calibration_put_u32_le(&encoded[0x0C], payload->profile_fingerprint);
    encoded[0x10] = payload->point_count;
    encoded[0x11] = payload->level_step;
    sys_calibration_put_u16_le(&encoded[0x12], payload->valid_flags);

    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        offset = (u16)(SYS_CALIBRATION_PAYLOAD_OUTPUT_OFFSET +
                       (u16)index * 4U);
        sys_calibration_put_u16_le(&encoded[offset],
                                   payload->output[index].logical_pwm);
        sys_calibration_put_u16_le(
            &encoded[offset + 2U],
            payload->output[index].reference_output_current_ma);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_OCO_OFFSET +
                       (u16)index * 4U);
        sys_calibration_put_u16_le(&encoded[offset],
                                   payload->oco[index].oco_adc_raw);
        sys_calibration_put_u16_le(
            &encoded[offset + 2U],
            payload->oco[index].reference_output_current_ma);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_BL_CURRENT_OFFSET +
                       (u16)index * 6U);
        sys_calibration_put_u32_le(&encoded[offset],
                                   payload->bl_current[index].bl_current_raw);
        sys_calibration_put_u16_le(
            &encoded[offset + 4U],
            payload->bl_current[index].reference_input_current_ma);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET +
                       (u16)index * 6U);
        sys_calibration_put_u32_le(
            &encoded[offset], (u32)payload->bl_power[index].bl_power_raw);
        sys_calibration_put_u16_le(
            &encoded[offset + 4U],
            payload->bl_power[index].reference_input_power_01w);
    }
    sys_calibration_put_u32_le(&encoded[SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET],
                               payload->voltage_gain_q24);
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_decode(
    const u8 *encoded,
    u16 encoded_length,
    sys_calibration_payload_st *payload)
{
    u16 offset;
    u8 index;

    if (encoded == NULL || payload == NULL ||
        encoded_length != SYS_CALIBRATION_PAYLOAD_LENGTH ||
        encoded[0x00] != SYS_CALIBRATION_PAYLOAD_MAGIC_0 ||
        encoded[0x01] != SYS_CALIBRATION_PAYLOAD_MAGIC_1 ||
        encoded[0x02] != SYS_CALIBRATION_PAYLOAD_MAGIC_2 ||
        encoded[0x03] != SYS_CALIBRATION_PAYLOAD_MAGIC_3 ||
        sys_calibration_get_u16_le(&encoded[0x04]) !=
            SYS_CALIBRATION_PAYLOAD_VERSION ||
        sys_calibration_get_u16_le(&encoded[0x06]) !=
            SYS_CALIBRATION_PAYLOAD_LENGTH)
    {
        return BOOL_FALSE;
    }
    memset(payload, 0, sizeof(*payload));
    payload->profile_id = sys_calibration_get_u16_le(&encoded[0x08]);
    payload->profile_version = sys_calibration_get_u16_le(&encoded[0x0A]);
    payload->profile_fingerprint = sys_calibration_get_u32_le(&encoded[0x0C]);
    payload->point_count = encoded[0x10];
    payload->level_step = encoded[0x11];
    payload->valid_flags = sys_calibration_get_u16_le(&encoded[0x12]);

    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        offset = (u16)(SYS_CALIBRATION_PAYLOAD_OUTPUT_OFFSET +
                       (u16)index * 4U);
        payload->output[index].logical_pwm =
            sys_calibration_get_u16_le(&encoded[offset]);
        payload->output[index].reference_output_current_ma =
            sys_calibration_get_u16_le(&encoded[offset + 2U]);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_OCO_OFFSET +
                       (u16)index * 4U);
        payload->oco[index].oco_adc_raw =
            sys_calibration_get_u16_le(&encoded[offset]);
        payload->oco[index].reference_output_current_ma =
            sys_calibration_get_u16_le(&encoded[offset + 2U]);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_BL_CURRENT_OFFSET +
                       (u16)index * 6U);
        payload->bl_current[index].bl_current_raw =
            sys_calibration_get_u32_le(&encoded[offset]);
        payload->bl_current[index].reference_input_current_ma =
            sys_calibration_get_u16_le(&encoded[offset + 4U]);

        offset = (u16)(SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET +
                       (u16)index * 6U);
        payload->bl_power[index].bl_power_raw =
            (s32)sys_calibration_get_u32_le(&encoded[offset]);
        payload->bl_power[index].reference_input_power_01w =
            sys_calibration_get_u16_le(&encoded[offset + 4U]);
    }
    payload->voltage_gain_q24 = sys_calibration_get_u32_le(
        &encoded[SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET]);
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_hex_encode(
    const u8 *payload,
    u16 payload_length,
    char *hex,
    u16 hex_capacity)
{
    static const char digit[] = "0123456789ABCDEF";
    u16 index;

    if (payload == NULL || hex == NULL ||
        payload_length != SYS_CALIBRATION_PAYLOAD_LENGTH ||
        hex_capacity < SYS_CALIBRATION_PAYLOAD_HEX_LENGTH + 1U)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < payload_length; ++index)
    {
        hex[index * 2U] = digit[payload[index] >> 4U];
        hex[index * 2U + 1U] = digit[payload[index] & 0x0FU];
    }
    hex[SYS_CALIBRATION_PAYLOAD_HEX_LENGTH] = '\0';
    return BOOL_TRUE;
}

boolean_en sys_calibration_payload_hex_decode(
    const char *hex,
    u8 *payload,
    u16 payload_capacity)
{
    u16 index;
    u8 high;
    u8 low;

    if (hex == NULL || payload == NULL ||
        payload_capacity < SYS_CALIBRATION_PAYLOAD_LENGTH ||
        strlen(hex) != SYS_CALIBRATION_PAYLOAD_HEX_LENGTH)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_LENGTH; ++index)
    {
        if (sys_calibration_hex_nibble(hex[index * 2U], &high) != BOOL_TRUE ||
            sys_calibration_hex_nibble(hex[index * 2U + 1U], &low) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        payload[index] = (u8)((high << 4U) | low);
    }
    return BOOL_TRUE;
}
