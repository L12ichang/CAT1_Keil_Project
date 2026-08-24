#ifndef SYS_CALIBRATION_SNAPSHOT_H
#define SYS_CALIBRATION_SNAPSHOT_H

#include "type.h"

#define SYS_CALIBRATION_METER_RAW_FRAME_LENGTH    23U
#define SYS_CALIBRATION_SNAPSHOT_READ_RETRIES       3U
#define SYS_CALIBRATION_SNAPSHOT_AGE_INVALID        0xFFFFFFFFUL

#define SYS_CALIBRATION_METER_FRAME_VALID           0x0001U
#define SYS_CALIBRATION_METER_HEAD_VALID            0x0002U
#define SYS_CALIBRATION_METER_CHECKSUM_VALID        0x0004U
#define SYS_CALIBRATION_METER_RESERVED_VALID        0x0008U

#define SYS_CALIBRATION_ADC_SAMPLE_VALID            0x0001U
#define SYS_CALIBRATION_PWM_SAMPLE_VALID            0x0001U

#define SYS_CALIBRATION_AGGREGATE_METER_PRESENT     0x0001U
#define SYS_CALIBRATION_AGGREGATE_ADC_PRESENT       0x0002U
#define SYS_CALIBRATION_AGGREGATE_PWM_PRESENT       0x0004U

typedef struct
{
    u32 seq;
    u32 tick_ms;
    u32 i_rms_raw;
    u32 v_rms_raw;
    u32 i_fast_rms_raw;
    s32 watt_raw;
    u32 cf_cnt_raw;
    u16 freq_raw;
    u8 status_raw;
    u8 raw_frame[SYS_CALIBRATION_METER_RAW_FRAME_LENGTH];
    u16 valid_flags;
    u32 frame_error_count;
} sys_calibration_meter_snapshot_st;

typedef struct
{
    u32 seq;
    u32 tick_ms;
    u16 ntc_raw;
    u16 vout_raw;
    u16 leak_raw;
    u16 iout_raw;
    u16 valid_flags;
} sys_calibration_adc_snapshot_st;

typedef struct
{
    u32 seq;
    u32 tick_ms;
    u16 requested_percent;
    u16 protected_percent;
    u16 logical_pwm;
    u16 ccr;
    u8 oco_on;
    u16 valid_flags;
} sys_calibration_pwm_snapshot_st;

typedef struct
{
    sys_calibration_meter_snapshot_st meter;
    sys_calibration_adc_snapshot_st adc;
    sys_calibration_pwm_snapshot_st pwm;
    u32 meter_age_ms;
    u32 adc_age_ms;
    u32 pwm_age_ms;
    u32 meter_adc_skew_ms;
    u32 meter_pwm_skew_ms;
    u16 valid_flags;
    u8 bl_fresh;
} sys_calibration_snapshot_aggregate_st;

/************************************
功能描述：初始化校准只读快照缓存
输入参数：无
输出返回：无
************************************/
extern void sys_calibration_snapshot_init(void);

/************************************
功能描述：发布一次校验通过的 BL0942 原始计量快照
输入参数：tick_ms 采样时间；原始寄存器值；valid_flags 帧有效位；frame_error_count 错误计数
输出返回：无
************************************/
extern void sys_calibration_snapshot_publish_meter(u32 tick_ms,
                                                    u32 i_rms_raw,
                                                    u32 v_rms_raw,
                                                    u32 i_fast_rms_raw,
                                                    s32 watt_raw,
                                                    u32 cf_cnt_raw,
                                                    u16 freq_raw,
                                                    u8 status_raw,
                                                    const u8 *raw_frame,
                                                    u16 valid_flags,
                                                    u32 frame_error_count);

/************************************
功能描述：发布一组完整 ADC DMA 扫描的原始快照
输入参数：tick_ms 采样时间；四路 ADC 原码，顺序为 NTC、Vout、漏电流、Iout
输出返回：无
************************************/
extern void sys_calibration_snapshot_publish_adc(u32 tick_ms,
                                                  u16 ntc_raw,
                                                  u16 vout_raw,
                                                  u16 leak_raw,
                                                  u16 iout_raw,
                                                  u16 valid_flags);

/************************************
功能描述：缓存下一次 PWM 硬件输出对应的请求和保护后百分比
输入参数：requested_percent 原始请求百分比；protected_percent 保护裁剪后百分比
输出返回：无
************************************/
extern void sys_calibration_snapshot_prepare_pwm(u16 requested_percent,
                                                  u16 protected_percent);

/************************************
功能描述：发布一次 PWM 硬件输出快照
输入参数：tick_ms 时间；logical_pwm 实际传给硬件的逻辑值；ccr 比较值；oco_on OCO 状态
输出返回：无
************************************/
extern void sys_calibration_snapshot_publish_pwm(u32 tick_ms,
                                                  u16 logical_pwm,
                                                  u16 ccr,
                                                  u8 oco_on,
                                                  u16 valid_flags);

/************************************
功能描述：无撕裂读取最近一次 BL0942 原始快照
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，缓存正在更新或无效 BOOL_FALSE
************************************/
extern boolean_en sys_calibration_snapshot_read_meter(sys_calibration_meter_snapshot_st *snapshot);

/************************************
功能描述：无撕裂读取最近一次 ADC 原始快照
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，缓存正在更新或无效 BOOL_FALSE
************************************/
extern boolean_en sys_calibration_snapshot_read_adc(sys_calibration_adc_snapshot_st *snapshot);

/************************************
功能描述：无撕裂读取最近一次 PWM 硬件快照
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，缓存正在更新或无效 BOOL_FALSE
************************************/
extern boolean_en sys_calibration_snapshot_read_pwm(sys_calibration_pwm_snapshot_st *snapshot);

/************************************
功能描述：复制三个独立来源并计算年龄与时间偏差
输入参数：now_tick_ms 当前毫秒节拍；snapshot 聚合输出缓存
输出返回：三份缓存均可无撕裂读取 BOOL_TRUE，否则 BOOL_FALSE
注意：返回的偏差只描述缓存时间，不代表 BL0942 与 ADC 物理同周期采样。
************************************/
extern boolean_en sys_calibration_snapshot_read_aggregate(
    u32 now_tick_ms,
    sys_calibration_snapshot_aggregate_st *snapshot);

#endif
