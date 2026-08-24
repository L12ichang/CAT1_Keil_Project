#ifndef SYS_BL0942_FRAME_H
#define SYS_BL0942_FRAME_H

#include "type.h"

#define SYS_BL0942_READ_FRAME_LENGTH        23U
#define SYS_BL0942_READ_REQUEST_HEADER      0x58U
#define SYS_BL0942_READ_RESPONSE_HEADER     0x55U
#define SYS_BL0942_FRESH_MAX_AGE_MS         500UL
#define SYS_BL0942_DATA_AGE_INVALID         0xFFFFFFFFUL

typedef enum
{
    SYS_BL0942_RECOVERY_NONE = 0,
    SYS_BL0942_RECOVERY_WAIT_VALID_FRAME,
    SYS_BL0942_RECOVERY_SUCCEEDED,
    SYS_BL0942_RECOVERY_FAILED
} sys_bl0942_recovery_state_en;

typedef struct
{
    u32 valid_frame_count;
    u32 last_valid_frame_tick;
    u32 recovery_count;
    u32 recovery_fail_count;
    u8 has_valid_frame;
    u8 recovery_attempted_since_valid;
    u8 recovery_waiting_frame;
    u8 last_recovery_state;
} sys_bl0942_health_st;

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
功能描述：计算基线固件使用的兼容校验和（不计保留字节21）
输入参数：frame 23 字节响应帧
输出返回：兼容校验字节
************************************/
extern u8 sys_bl0942_frame_calculate_legacy_checksum(const u8 *frame);

/************************************
功能描述：检查数据手册定义的三个填充字节是否均为0
输入参数：frame 23 字节响应帧
输出返回：符合官方格式 BOOL_TRUE，否则 BOOL_FALSE
************************************/
extern boolean_en sys_bl0942_frame_reserved_valid(const u8 *frame);

/************************************
功能描述：判断帧是否仅通过基线固件兼容校验
输入参数：frame 23 字节响应帧
输出返回：仅兼容校验通过 BOOL_TRUE，否则 BOOL_FALSE
************************************/
extern boolean_en sys_bl0942_frame_uses_legacy_checksum(const u8 *frame);

/************************************
功能描述：验证 BL0942 23 字节读取帧头和官方/基线兼容校验
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

/************************************
功能描述：应用V3 PayloadVersion=1的BL电压Gain-only Q24
输入参数：raw_voltage BL原码；gain_q24 增益；corrected_voltage_01v 输出0.1V
输出返回：成功BOOL_TRUE；空指针、零增益或u16溢出BOOL_FALSE
************************************/
extern boolean_en sys_bl0942_voltage_apply_gain_q24(u32 raw_voltage,
                                                     u32 gain_q24,
                                                     u16 *corrected_voltage_01v);

/************************************
功能描述：初始化/更新BL0942 freshness与有限恢复状态
说明：同一段“尚未重新得到有效帧”的故障窗口最多允许一次恢复。
************************************/
extern void sys_bl0942_health_init(sys_bl0942_health_st *health);
extern void sys_bl0942_health_record_valid(sys_bl0942_health_st *health,
                                           u32 valid_tick_ms);
extern boolean_en sys_bl0942_health_begin_recovery(sys_bl0942_health_st *health);
extern void sys_bl0942_health_record_recovery_start(
    sys_bl0942_health_st *health,
    boolean_en started);
extern u32 sys_bl0942_health_age_ms(const sys_bl0942_health_st *health,
                                    u32 now_tick_ms);
extern boolean_en sys_bl0942_health_is_fresh(const sys_bl0942_health_st *health,
                                             u32 now_tick_ms);

#endif
