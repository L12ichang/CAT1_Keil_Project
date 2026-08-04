/*************************************************************
程序功能：BL0942 23 字节读取帧校验与解码
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_bl0942_frame.h"

static u32 sys_bl0942_frame_read_u24_le(const u8 *buffer)
{
    return (u32)buffer[0] | ((u32)buffer[1] << 8) | ((u32)buffer[2] << 16);
}

static s32 sys_bl0942_frame_read_s24_le(const u8 *buffer)
{
    u32 value = sys_bl0942_frame_read_u24_le(buffer);
    if ((value & 0x00800000UL) != 0U)
    {
        value |= 0xFF000000UL;
    }
    return (s32)value;
}

/************************************
功能描述：计算 BL0942 23 字节读取帧校验和
输入参数：frame 23 字节响应帧
输出返回：按协议计算的校验字节
************************************/
u8 sys_bl0942_frame_calculate_checksum(const u8 *frame)
{
    u16 sum;
    u8 index;

    if (frame == NULL)
    {
        return 0U;
    }
    sum = (u16)SYS_BL0942_READ_REQUEST_HEADER +
          (u16)SYS_BL0942_READ_RESPONSE_HEADER;
    for (index = 1U; index <= 21U; ++index)
    {
        sum += frame[index];
    }
    return (u8)(~sum);
}

/************************************
功能描述：验证 BL0942 23 字节读取帧头、保留字节和校验
输入参数：frame 响应帧；length 帧长度
输出返回：有效 BOOL_TRUE，无效 BOOL_FALSE
************************************/
boolean_en sys_bl0942_frame_validate(const u8 *frame, u16 length)
{
    if (frame == NULL || length != SYS_BL0942_READ_FRAME_LENGTH)
    {
        return BOOL_FALSE;
    }
    if (frame[0] != SYS_BL0942_READ_RESPONSE_HEADER ||
        frame[18] != 0U ||
        frame[20] != 0U ||
        frame[21] != 0U)
    {
        return BOOL_FALSE;
    }
    return (frame[22] == sys_bl0942_frame_calculate_checksum(frame)) ?
           BOOL_TRUE : BOOL_FALSE;
}

/************************************
功能描述：验证并解析 BL0942 23 字节读取帧
输入参数：frame 响应帧；length 帧长度；decoded 解码结果
输出返回：成功 BOOL_TRUE，失败 BOOL_FALSE
************************************/
boolean_en sys_bl0942_frame_decode(const u8 *frame,
                                    u16 length,
                                    sys_bl0942_frame_st *decoded)
{
    if (decoded == NULL || sys_bl0942_frame_validate(frame, length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    decoded->i_rms_raw = sys_bl0942_frame_read_u24_le(frame + 1);
    decoded->v_rms_raw = sys_bl0942_frame_read_u24_le(frame + 4);
    decoded->i_fast_rms_raw = sys_bl0942_frame_read_u24_le(frame + 7);
    decoded->watt_raw = sys_bl0942_frame_read_s24_le(frame + 10);
    decoded->cf_cnt_raw = sys_bl0942_frame_read_u24_le(frame + 13);
    decoded->freq_raw = (u16)frame[16] | ((u16)frame[17] << 8);
    decoded->status_raw = frame[19];
    return BOOL_TRUE;
}
