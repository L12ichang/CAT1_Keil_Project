/*************************************************************
程序功能：BL0942 23 字节读取帧校验与解码
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_bl0942_frame.h"

static void sys_bl0942_counter_increment(u32 *counter)
{
    if (counter != NULL && *counter < 0xFFFFFFFFUL)
    {
        ++(*counter);
    }
}

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

static u8 sys_bl0942_frame_calculate_checksum_to(const u8 *frame,
                                                  u8 last_data_index)
{
    u16 sum;
    u8 index;

    if (frame == NULL)
    {
        return 0U;
    }
    sum = (u16)SYS_BL0942_READ_REQUEST_HEADER +
          (u16)SYS_BL0942_READ_RESPONSE_HEADER;
    for (index = 1U; index <= last_data_index; ++index)
    {
        sum += frame[index];
    }
    return (u8)(~sum);
}

/************************************
功能描述：计算 BL0942 23 字节读取帧校验和
输入参数：frame 23 字节响应帧
输出返回：按协议计算的校验字节
************************************/
u8 sys_bl0942_frame_calculate_checksum(const u8 *frame)
{
    return sys_bl0942_frame_calculate_checksum_to(frame, 21U);
}

u8 sys_bl0942_frame_calculate_legacy_checksum(const u8 *frame)
{
    return sys_bl0942_frame_calculate_checksum_to(frame, 20U);
}

boolean_en sys_bl0942_frame_reserved_valid(const u8 *frame)
{
    if (frame == NULL)
    {
        return BOOL_FALSE;
    }
    return (frame[18] == 0U && frame[20] == 0U && frame[21] == 0U) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_bl0942_frame_uses_legacy_checksum(const u8 *frame)
{
    if (frame == NULL || frame[0] != SYS_BL0942_READ_RESPONSE_HEADER ||
        frame[22] == sys_bl0942_frame_calculate_checksum(frame))
    {
        return BOOL_FALSE;
    }
    return (frame[22] == sys_bl0942_frame_calculate_legacy_checksum(frame)) ?
           BOOL_TRUE : BOOL_FALSE;
}

/************************************
功能描述：验证 BL0942 23 字节读取帧头和官方/基线兼容校验
输入参数：frame 响应帧；length 帧长度
输出返回：有效 BOOL_TRUE，无效 BOOL_FALSE
************************************/
boolean_en sys_bl0942_frame_validate(const u8 *frame, u16 length)
{
    if (frame == NULL || length != SYS_BL0942_READ_FRAME_LENGTH)
    {
        return BOOL_FALSE;
    }
    if (frame[0] != SYS_BL0942_READ_RESPONSE_HEADER)
    {
        return BOOL_FALSE;
    }
    /*
     * 官方 V1.06 格式的 checksum 覆盖字节1..21。基线固件长期采用
     * 字节1..20的校验，字节21又不承载业务数据。两种校验都完整保护
     * I/V/WATT/CF/FREQ/STATUS，因此保留兼容分支不会放宽有效电参字段。
     */
    return (frame[22] == sys_bl0942_frame_calculate_checksum(frame) ||
            frame[22] == sys_bl0942_frame_calculate_legacy_checksum(frame)) ?
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

boolean_en sys_bl0942_voltage_apply_gain_q24(u32 raw_voltage,
                                             u32 gain_q24,
                                             u16 *corrected_voltage_01v)
{
    u64 corrected;

    if (corrected_voltage_01v == NULL || gain_q24 == 0U)
    {
        return BOOL_FALSE;
    }
    corrected = ((u64)raw_voltage * (u64)gain_q24 + (1ULL << 23)) >> 24;
    if (corrected > 0xFFFFULL)
    {
        return BOOL_FALSE;
    }
    *corrected_voltage_01v = (u16)corrected;
    return BOOL_TRUE;
}

void sys_bl0942_health_init(sys_bl0942_health_st *health)
{
    if (health == NULL)
    {
        return;
    }
    health->valid_frame_count = 0U;
    health->last_valid_frame_tick = 0U;
    health->recovery_count = 0U;
    health->recovery_fail_count = 0U;
    health->has_valid_frame = 0U;
    health->recovery_attempted_since_valid = 0U;
    health->recovery_waiting_frame = 0U;
    health->last_recovery_state = (u8)SYS_BL0942_RECOVERY_NONE;
}

void sys_bl0942_health_record_valid(sys_bl0942_health_st *health,
                                    u32 valid_tick_ms)
{
    if (health == NULL)
    {
        return;
    }
    sys_bl0942_counter_increment(&health->valid_frame_count);
    health->last_valid_frame_tick = valid_tick_ms;
    health->has_valid_frame = 1U;
    if (health->recovery_waiting_frame != 0U)
    {
        health->last_recovery_state = (u8)SYS_BL0942_RECOVERY_SUCCEEDED;
    }
    health->recovery_attempted_since_valid = 0U;
    health->recovery_waiting_frame = 0U;
}

boolean_en sys_bl0942_health_begin_recovery(sys_bl0942_health_st *health)
{
    if (health == NULL)
    {
        return BOOL_FALSE;
    }
    if (health->recovery_waiting_frame != 0U)
    {
        health->recovery_waiting_frame = 0U;
        health->last_recovery_state = (u8)SYS_BL0942_RECOVERY_FAILED;
        sys_bl0942_counter_increment(&health->recovery_fail_count);
        return BOOL_FALSE;
    }
    if (health->recovery_attempted_since_valid != 0U)
    {
        return BOOL_FALSE;
    }
    health->recovery_attempted_since_valid = 1U;
    health->recovery_waiting_frame = 1U;
    health->last_recovery_state = (u8)SYS_BL0942_RECOVERY_WAIT_VALID_FRAME;
    sys_bl0942_counter_increment(&health->recovery_count);
    return BOOL_TRUE;
}

void sys_bl0942_health_record_recovery_start(sys_bl0942_health_st *health,
                                             boolean_en started)
{
    if (health == NULL || started == BOOL_TRUE)
    {
        return;
    }
    if (health->recovery_waiting_frame != 0U)
    {
        health->recovery_waiting_frame = 0U;
        health->last_recovery_state = (u8)SYS_BL0942_RECOVERY_FAILED;
        sys_bl0942_counter_increment(&health->recovery_fail_count);
    }
}

u32 sys_bl0942_health_age_ms(const sys_bl0942_health_st *health,
                             u32 now_tick_ms)
{
    if (health == NULL || health->has_valid_frame == 0U)
    {
        return SYS_BL0942_DATA_AGE_INVALID;
    }
    return now_tick_ms - health->last_valid_frame_tick;
}

boolean_en sys_bl0942_health_is_fresh(const sys_bl0942_health_st *health,
                                      u32 now_tick_ms)
{
    u32 age_ms = sys_bl0942_health_age_ms(health, now_tick_ms);
    return (age_ms != SYS_BL0942_DATA_AGE_INVALID &&
            age_ms <= SYS_BL0942_FRESH_MAX_AGE_MS) ? BOOL_TRUE : BOOL_FALSE;
}
