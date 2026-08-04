#ifndef SYS_BL0942_FRAME_H
#define SYS_BL0942_FRAME_H

#include "type.h"

#define SYS_BL0942_READ_FRAME_LENGTH        23U
#define SYS_BL0942_READ_REQUEST_HEADER      0x58U
#define SYS_BL0942_READ_RESPONSE_HEADER     0x55U

typedef struct
{
    u32 i_rms_raw;
    u32 v_rms_raw;
    u32 i_fast_rms_raw;
    s32 watt_raw;
    u32 cf_cnt_raw;
    u16 freq_raw;
    u8 status_raw;
} sys_bl0942_frame_st;

/************************************
功能描述：计算 BL0942 23 字节读取帧校验和
输入参数：frame 23 字节响应帧
输出返回：按协议计算的校验字节
************************************/
extern u8 sys_bl0942_frame_calculate_checksum(const u8 *frame);

/************************************
功能描述：验证 BL0942 23 字节读取帧头、保留字节和校验
输入参数：frame 响应帧；length 帧长度
输出返回：有效 BOOL_TRUE，无效 BOOL_FALSE
************************************/
extern boolean_en sys_bl0942_frame_validate(const u8 *frame, u16 length);

/************************************
功能描述：验证并解析 BL0942 23 字节读取帧
输入参数：frame 响应帧；length 帧长度；decoded 解码结果
输出返回：成功 BOOL_TRUE，失败 BOOL_FALSE
************************************/
extern boolean_en sys_bl0942_frame_decode(const u8 *frame,
                                           u16 length,
                                           sys_bl0942_frame_st *decoded);

#endif
