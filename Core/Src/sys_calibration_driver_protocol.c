/*************************************************************
程序功能：量产校准驱动器二进制协议编解码
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_driver_protocol.h"
#include <string.h>

static u16 sys_calibration_driver_get_u16_be(const u8 *data)
{
    return (u16)(((u16)data[0] << 8U) | data[1]);
}

static void sys_calibration_driver_put_u16_be(u8 *data, u16 value)
{
    data[0] = (u8)(value >> 8U);
    data[1] = (u8)value;
}

static boolean_en sys_calibration_driver_is_level(u8 level, u8 index)
{
    return (level == (u8)(index * SYS_CALIBRATION_DRIVER_LEVEL_STEP)) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_calibration_driver_is_command(u8 command)
{
    return (command == SYS_CALIBRATION_DRIVER_CMD_SET ||
            command == SYS_CALIBRATION_DRIVER_CMD_SET_ACK ||
            command == SYS_CALIBRATION_DRIVER_CMD_QUERY ||
            command == SYS_CALIBRATION_DRIVER_CMD_QUERY_REPLY) ?
           BOOL_TRUE : BOOL_FALSE;
}

u8 sys_calibration_driver_checksum(u8 command,
                                   u8 offset,
                                   u8 length,
                                   const u8 *data)
{
    u16 index;
    u8 checksum = (u8)(command + offset + length);

    for (index = 0U; index < length; ++index)
    {
        checksum = (u8)(checksum + data[index]);
    }
    return checksum;
}

boolean_en sys_calibration_driver_encode(u8 command,
                                         u8 offset,
                                         const u8 *data,
                                         u8 length,
                                         u8 *frame,
                                         u16 frame_capacity,
                                         u16 *frame_length)
{
    u16 total_length;

    if (frame_length == NULL ||
        frame == NULL ||
        sys_calibration_driver_is_command(command) != BOOL_TRUE ||
        length > SYS_CALIBRATION_DRIVER_MAX_PAYLOAD_LENGTH ||
        (length > 0U && data == NULL))
    {
        return BOOL_FALSE;
    }

    total_length = (u16)(SYS_CALIBRATION_DRIVER_FRAME_OVERHEAD + length);
    if (frame_capacity < total_length)
    {
        return BOOL_FALSE;
    }

    frame[0] = SYS_CALIBRATION_DRIVER_FRAME_HEADER;
    frame[1] = command;
    frame[2] = offset;
    frame[3] = length;
    if (length > 0U)
    {
        memcpy(&frame[4], data, length);
    }
    frame[4U + length] = sys_calibration_driver_checksum(command,
                                                         offset,
                                                         length,
                                                         data);
    frame[5U + length] = SYS_CALIBRATION_DRIVER_FRAME_TAIL_0;
    frame[6U + length] = SYS_CALIBRATION_DRIVER_FRAME_TAIL_1;
    *frame_length = total_length;
    return BOOL_TRUE;
}

boolean_en sys_calibration_driver_decode(
    const u8 *frame,
    u16 frame_length,
    sys_calibration_driver_message_st *message)
{
    u16 expected_length;
    u8 checksum;

    if (frame == NULL || message == NULL ||
        frame_length < SYS_CALIBRATION_DRIVER_FRAME_OVERHEAD ||
        frame[0] != SYS_CALIBRATION_DRIVER_FRAME_HEADER ||
        sys_calibration_driver_is_command(frame[1]) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    expected_length = (u16)(SYS_CALIBRATION_DRIVER_FRAME_OVERHEAD + frame[3]);
    if (frame[3] > SYS_CALIBRATION_DRIVER_MAX_PAYLOAD_LENGTH ||
        frame_length != expected_length ||
        frame[frame_length - 2U] != SYS_CALIBRATION_DRIVER_FRAME_TAIL_0 ||
        frame[frame_length - 1U] != SYS_CALIBRATION_DRIVER_FRAME_TAIL_1)
    {
        return BOOL_FALSE;
    }

    checksum = sys_calibration_driver_checksum(frame[1],
                                               frame[2],
                                               frame[3],
                                               &frame[4]);
    if (checksum != frame[4U + frame[3]])
    {
        return BOOL_FALSE;
    }

    memset(message, 0, sizeof(*message));
    message->command = frame[1];
    message->offset = frame[2];
    message->length = frame[3];
    if (message->length > 0U)
    {
        memcpy(message->data, &frame[4], message->length);
    }
    return sys_calibration_driver_validate_message(message);
}

boolean_en sys_calibration_driver_table_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_table_st *table)
{
    u8 index;
    u16 offset;
    sys_calibration_driver_point_st *point;

    if (payload == NULL || table == NULL ||
        payload_length != SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH)
    {
        return BOOL_FALSE;
    }

    memset(table, 0, sizeof(*table));
    for (index = 0U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        offset = (u16)index * 18U;
        point = &table->point[index];
        point->level = payload[offset];
        point->power_factor_percent = payload[offset + 1U];
        point->input_voltage_01v = sys_calibration_driver_get_u16_be(&payload[offset + 2U]);
        point->input_current_ma = sys_calibration_driver_get_u16_be(&payload[offset + 4U]);
        point->input_power_01w = sys_calibration_driver_get_u16_be(&payload[offset + 6U]);
        point->instrument_output_current_ma =
            sys_calibration_driver_get_u16_be(&payload[offset + 8U]);
        point->instrument_output_power_01w =
            sys_calibration_driver_get_u16_be(&payload[offset + 10U]);
        point->device_output_current_ma =
            sys_calibration_driver_get_u16_be(&payload[offset + 12U]);
        point->device_output_power_01w =
            sys_calibration_driver_get_u16_be(&payload[offset + 14U]);
        point->input_current_ad = sys_calibration_driver_get_u16_be(&payload[offset + 16U]);
        if (sys_calibration_driver_is_level(point->level, index) != BOOL_TRUE ||
            point->power_factor_percent > 100U)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_driver_table_encode(
    const sys_calibration_driver_table_st *table,
    u8 *payload,
    u16 payload_capacity)
{
    u8 index;
    u16 offset;
    const sys_calibration_driver_point_st *point;

    if (table == NULL || payload == NULL ||
        payload_capacity < SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH)
    {
        return BOOL_FALSE;
    }

    memset(payload, 0, SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH);
    for (index = 0U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        offset = (u16)index * 18U;
        point = &table->point[index];
        if (sys_calibration_driver_is_level(point->level, index) != BOOL_TRUE ||
            point->power_factor_percent > 100U)
        {
            return BOOL_FALSE;
        }
        payload[offset] = point->level;
        payload[offset + 1U] = point->power_factor_percent;
        sys_calibration_driver_put_u16_be(&payload[offset + 2U],
                                          point->input_voltage_01v);
        sys_calibration_driver_put_u16_be(&payload[offset + 4U],
                                          point->input_current_ma);
        sys_calibration_driver_put_u16_be(&payload[offset + 6U],
                                          point->input_power_01w);
        sys_calibration_driver_put_u16_be(&payload[offset + 8U],
                                          point->instrument_output_current_ma);
        sys_calibration_driver_put_u16_be(&payload[offset + 10U],
                                          point->instrument_output_power_01w);
        sys_calibration_driver_put_u16_be(&payload[offset + 12U],
                                          point->device_output_current_ma);
        sys_calibration_driver_put_u16_be(&payload[offset + 14U],
                                          point->device_output_power_01w);
        sys_calibration_driver_put_u16_be(&payload[offset + 16U],
                                          point->input_current_ad);
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_driver_max_context_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_max_context_st *context)
{
    if (payload == NULL || context == NULL || payload_length != 8U)
    {
        return BOOL_FALSE;
    }
    context->input_ac_voltage_float_bits =
        ((u32)payload[0] << 24U) |
        ((u32)payload[1] << 16U) |
        ((u32)payload[2] << 8U) |
        payload[3];
    context->maximum_output_voltage_01v =
        sys_calibration_driver_get_u16_be(&payload[4]);
    context->maximum_output_current_ma =
        sys_calibration_driver_get_u16_be(&payload[6]);
    return BOOL_TRUE;
}

boolean_en sys_calibration_driver_measurement_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_measurement_st *measurement)
{
    if (payload == NULL || measurement == NULL || payload_length != 6U)
    {
        return BOOL_FALSE;
    }
    measurement->device_output_current_ma =
        sys_calibration_driver_get_u16_be(&payload[0]);
    measurement->device_output_power_01w =
        sys_calibration_driver_get_u16_be(&payload[2]);
    measurement->input_current_ad =
        sys_calibration_driver_get_u16_be(&payload[4]);
    return BOOL_TRUE;
}

boolean_en sys_calibration_driver_validate_message(
    const sys_calibration_driver_message_st *message)
{
    sys_calibration_driver_table_st table;

    if (message == NULL || message->length > SYS_CALIBRATION_DRIVER_MAX_PAYLOAD_LENGTH)
    {
        return BOOL_FALSE;
    }

    if (message->command == SYS_CALIBRATION_DRIVER_CMD_SET)
    {
        switch (message->offset)
        {
            case SYS_CALIBRATION_DRIVER_OFFSET_MODE:
                return (message->length == 1U &&
                        (message->data[0] == 0U || message->data[0] == 1U)) ?
                       BOOL_TRUE : BOOL_FALSE;
            case SYS_CALIBRATION_DRIVER_OFFSET_TABLE:
                return sys_calibration_driver_table_decode(message->data,
                                                            message->length,
                                                            &table);
            case SYS_CALIBRATION_DRIVER_OFFSET_LEVEL:
                return (message->length == 1U && message->data[0] <= 200U) ?
                       BOOL_TRUE : BOOL_FALSE;
            case SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT:
                return (message->length == 8U) ? BOOL_TRUE : BOOL_FALSE;
            default:
                return BOOL_FALSE;
        }
    }

    if (message->command == SYS_CALIBRATION_DRIVER_CMD_SET_ACK)
    {
        return (message->length == 1U &&
                message->data[0] == SYS_CALIBRATION_DRIVER_ACK_VALUE &&
                (message->offset == SYS_CALIBRATION_DRIVER_OFFSET_MODE ||
                 message->offset == SYS_CALIBRATION_DRIVER_OFFSET_TABLE ||
                 message->offset == SYS_CALIBRATION_DRIVER_OFFSET_LEVEL ||
                 message->offset == SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT)) ?
               BOOL_TRUE : BOOL_FALSE;
    }

    if (message->command == SYS_CALIBRATION_DRIVER_CMD_QUERY)
    {
        return (message->offset == SYS_CALIBRATION_DRIVER_OFFSET_MEASURE &&
                message->length == 0U) ? BOOL_TRUE : BOOL_FALSE;
    }

    return (message->command == SYS_CALIBRATION_DRIVER_CMD_QUERY_REPLY &&
            message->offset == SYS_CALIBRATION_DRIVER_OFFSET_MEASURE &&
            message->length == 6U) ? BOOL_TRUE : BOOL_FALSE;
}
