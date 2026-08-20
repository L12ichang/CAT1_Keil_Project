/*************************************************************
程序功能：DC5200综合页面原始帧编解码
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_dc5200.h"

static u16 sys_calibration_dc5200_get_u16_be(const u8 *data)
{
    return (u16)(((u16)data[0] << 8U) | data[1]);
}

static u32 sys_calibration_dc5200_get_u32_be(const u8 *data)
{
    return ((u32)data[0] << 24U) |
           ((u32)data[1] << 16U) |
           ((u32)data[2] << 8U) |
           data[3];
}

u16 sys_calibration_dc5200_crc16_ccitt(const u8 *data, u16 length)
{
    u16 crc = 0U;
    u16 index;
    u8 bit;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        for (bit = 0x80U; bit != 0U; bit >>= 1U)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (u16)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (u16)(crc << 1U);
            }
            if ((data[index] & bit) != 0U)
            {
                crc ^= 0x1021U;
            }
        }
    }
    return crc;
}

boolean_en sys_calibration_dc5200_build_comprehensive_query(
    u8 *frame,
    u16 capacity,
    u16 *length)
{
    if (frame == NULL || length == NULL ||
        capacity < SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH)
    {
        return BOOL_FALSE;
    }
#if SYS_CALIBRATION_DC5200_QUERY_ENABLED == 0U
    /* 资料存在 00 3C D2 3A 与 00 3A B2 FC 两个候选，未经实机抓包不选边。 */
    *length = 0U;
    return BOOL_FALSE;
#else
    u16 crc;

    frame[0] = SYS_CALIBRATION_DC5200_REPLY_START_0;
    frame[1] = SYS_CALIBRATION_DC5200_REPLY_START_1;
    frame[2] = SYS_CALIBRATION_DC5200_REPLY_START_2;
    frame[3] = SYS_CALIBRATION_DC5200_REQUEST_START_3;
    frame[4] = SYS_CALIBRATION_DC5200_ADDRESS;
    frame[5] = SYS_CALIBRATION_DC5200_COMPREHENSIVE_PAGE;
    frame[6] = 0x00U;
    frame[7] = SYS_CALIBRATION_DC5200_QUERY_DATA_LENGTH;
    frame[8] = 0x00U;
    frame[9] = SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH;
    crc = sys_calibration_dc5200_crc16_ccitt(frame, 10U);
    frame[10] = (u8)(crc >> 8U);
    frame[11] = (u8)crc;
    frame[12] = SYS_CALIBRATION_DC5200_REPLY_START_0;
    frame[13] = SYS_CALIBRATION_DC5200_REPLY_START_1;
    frame[14] = SYS_CALIBRATION_DC5200_REPLY_START_2;
    frame[15] = SYS_CALIBRATION_DC5200_REQUEST_START_3;
    *length = SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH;
    return BOOL_TRUE;
#endif
}

boolean_en sys_calibration_dc5200_validate_comprehensive_reply(
    const u8 *frame,
    u16 length)
{
    u16 data_length;
    u16 crc_offset;
    u16 expected_crc;

    if (frame == NULL || length != SYS_CALIBRATION_DC5200_COMPREHENSIVE_FRAME_LENGTH ||
        frame[0] != SYS_CALIBRATION_DC5200_REPLY_START_0 ||
        frame[1] != SYS_CALIBRATION_DC5200_REPLY_START_1 ||
        frame[2] != SYS_CALIBRATION_DC5200_REPLY_START_2 ||
        frame[3] != SYS_CALIBRATION_DC5200_REPLY_START_3 ||
        frame[4] != SYS_CALIBRATION_DC5200_ADDRESS ||
        frame[5] != SYS_CALIBRATION_DC5200_COMPREHENSIVE_PAGE ||
        frame[10U + SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH] !=
            SYS_CALIBRATION_DC5200_REPLY_START_0 ||
        frame[11U + SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH] !=
            SYS_CALIBRATION_DC5200_REPLY_START_1 ||
        frame[12U + SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH] !=
            SYS_CALIBRATION_DC5200_REPLY_START_2 ||
        frame[13U + SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH] !=
            SYS_CALIBRATION_DC5200_REPLY_START_3)
    {
        return BOOL_FALSE;
    }
    data_length = sys_calibration_dc5200_get_u16_be(&frame[6]);
    if (data_length != SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH)
    {
        return BOOL_FALSE;
    }
    crc_offset = (u16)(8U + data_length);
    expected_crc = sys_calibration_dc5200_crc16_ccitt(frame, crc_offset);
    return (expected_crc == (u16)(((u16)frame[crc_offset] << 8U) |
                                  frame[crc_offset + 1U])) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_dc5200_decode_comprehensive_reply(
    const u8 *frame,
    u16 length,
    sys_calibration_dc5200_comprehensive_st *measurement)
{
    const u8 *data;

    if (measurement == NULL ||
        sys_calibration_dc5200_validate_comprehensive_reply(frame, length) !=
            BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    data = &frame[8];
    measurement->input_voltage_001v = sys_calibration_dc5200_get_u32_be(&data[0]);
    measurement->input_current_0001a = sys_calibration_dc5200_get_u32_be(&data[4]);
    measurement->input_power_001w = sys_calibration_dc5200_get_u32_be(&data[8]);
    measurement->power_factor_001 = sys_calibration_dc5200_get_u16_be(&data[12]);
    measurement->frequency = sys_calibration_dc5200_get_u16_be(&data[14]);
    measurement->output_rms_voltage_001v = sys_calibration_dc5200_get_u32_be(&data[16]);
    measurement->output_rms_current_0001a = sys_calibration_dc5200_get_u32_be(&data[20]);
    measurement->output_power_001w = sys_calibration_dc5200_get_u32_be(&data[24]);
    measurement->efficiency = sys_calibration_dc5200_get_u32_be(&data[28]);
    measurement->ripple_voltage = sys_calibration_dc5200_get_u16_be(&data[32]);
    measurement->ripple_voltage_percent = sys_calibration_dc5200_get_u16_be(&data[34]);
    measurement->ripple_current = sys_calibration_dc5200_get_u16_be(&data[36]);
    measurement->ripple_current_percent = sys_calibration_dc5200_get_u16_be(&data[38]);
    measurement->start_phase = sys_calibration_dc5200_get_u16_be(&data[40]);
    measurement->peak_phase = sys_calibration_dc5200_get_u16_be(&data[42]);
    measurement->end_phase = sys_calibration_dc5200_get_u16_be(&data[44]);
    measurement->input_voltage_crest = sys_calibration_dc5200_get_u16_be(&data[46]);
    measurement->input_current_crest = sys_calibration_dc5200_get_u16_be(&data[48]);
    measurement->output_average_voltage_001v =
        sys_calibration_dc5200_get_u32_be(&data[50]);
    measurement->output_average_current_0001a =
        sys_calibration_dc5200_get_u32_be(&data[54]);
    measurement->df_power_factor_001 = sys_calibration_dc5200_get_u16_be(&data[58]);
    return BOOL_TRUE;
}
