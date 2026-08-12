/*************************************************************
程序功能：量产校准 BL0942、ADC、PWM 独立只读快照
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_snapshot.h"
#include <string.h>

static volatile u32 _meter_write_seq;
static volatile u32 _adc_write_seq;
static volatile u32 _pwm_write_seq;
static volatile u16 _pending_requested_percent;
static volatile u16 _pending_protected_percent;
static volatile sys_calibration_meter_snapshot_st _meter_snapshot;
static volatile sys_calibration_adc_snapshot_st _adc_snapshot;
static volatile sys_calibration_pwm_snapshot_st _pwm_snapshot;

static u32 sys_calibration_snapshot_abs_diff(u32 lhs, u32 rhs)
{
    return (lhs >= rhs) ? (lhs - rhs) : (rhs - lhs);
}

static u32 sys_calibration_snapshot_age(u32 now_tick_ms, u32 tick_ms, u16 valid_flags)
{
    if (valid_flags == 0U)
    {
        return SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;
    }
    return now_tick_ms - tick_ms;
}

/************************************
功能描述：初始化校准只读快照缓存
输入参数：无
输出返回：无
************************************/
void sys_calibration_snapshot_init(void)
{
    _meter_write_seq = 0U;
    _adc_write_seq = 0U;
    _pwm_write_seq = 0U;
    _pending_requested_percent = 0U;
    _pending_protected_percent = 0U;
    memset((void *)&_meter_snapshot, 0, sizeof(_meter_snapshot));
    memset((void *)&_adc_snapshot, 0, sizeof(_adc_snapshot));
    memset((void *)&_pwm_snapshot, 0, sizeof(_pwm_snapshot));
}

/************************************
功能描述：发布一次校验通过的 BL0942 原始计量快照
输入参数：tick_ms 采样时间；原始寄存器值；valid_flags 帧有效位；frame_error_count 错误计数
输出返回：无
************************************/
void sys_calibration_snapshot_publish_meter(u32 tick_ms,
                                            u32 i_rms_raw,
                                            u32 v_rms_raw,
                                            u32 i_fast_rms_raw,
                                            s32 watt_raw,
                                            u32 cf_cnt_raw,
                                            u16 freq_raw,
                                            u8 status_raw,
                                            const u8 *raw_frame,
                                            u16 valid_flags,
                                            u32 frame_error_count)
{
    u8 index;
    ++_meter_write_seq;
    _meter_snapshot.seq++;
    _meter_snapshot.tick_ms = tick_ms;
    _meter_snapshot.i_rms_raw = i_rms_raw;
    _meter_snapshot.v_rms_raw = v_rms_raw;
    _meter_snapshot.i_fast_rms_raw = i_fast_rms_raw;
    _meter_snapshot.watt_raw = watt_raw;
    _meter_snapshot.cf_cnt_raw = cf_cnt_raw;
    _meter_snapshot.freq_raw = freq_raw;
    _meter_snapshot.status_raw = status_raw;
    for (index = 0U; index < SYS_CALIBRATION_METER_RAW_FRAME_LENGTH; ++index)
    {
        _meter_snapshot.raw_frame[index] = (raw_frame == NULL) ? 0U : raw_frame[index];
    }
    _meter_snapshot.valid_flags = valid_flags;
    _meter_snapshot.frame_error_count = frame_error_count;
    ++_meter_write_seq;
}

/************************************
功能描述：发布一组完整 ADC DMA 扫描的原始快照
输入参数：tick_ms 采样时间；四路 ADC 原码；valid_flags 有效位
输出返回：无
************************************/
void sys_calibration_snapshot_publish_adc(u32 tick_ms,
                                          u16 ntc_raw,
                                          u16 vout_raw,
                                          u16 leak_raw,
                                          u16 iout_raw,
                                          u16 valid_flags)
{
    ++_adc_write_seq;
    _adc_snapshot.seq++;
    _adc_snapshot.tick_ms = tick_ms;
    _adc_snapshot.ntc_raw = ntc_raw;
    _adc_snapshot.vout_raw = vout_raw;
    _adc_snapshot.leak_raw = leak_raw;
    _adc_snapshot.iout_raw = iout_raw;
    _adc_snapshot.valid_flags = valid_flags;
    ++_adc_write_seq;
}

/************************************
功能描述：缓存下一次 PWM 硬件输出对应的请求和保护后百分比
输入参数：requested_percent 原始请求；protected_percent 保护裁剪后请求
输出返回：无
************************************/
void sys_calibration_snapshot_prepare_pwm(u16 requested_percent,
                                          u16 protected_percent)
{
    _pending_requested_percent = requested_percent;
    _pending_protected_percent = protected_percent;
}

/************************************
功能描述：发布一次 PWM 硬件输出快照
输入参数：tick_ms 时间；logical_pwm 逻辑值；ccr 比较值；oco_on OCO 状态；valid_flags 有效位
输出返回：无
************************************/
void sys_calibration_snapshot_publish_pwm(u32 tick_ms,
                                          u16 logical_pwm,
                                          u16 ccr,
                                          u8 oco_on,
                                          u16 valid_flags)
{
    ++_pwm_write_seq;
    _pwm_snapshot.seq++;
    _pwm_snapshot.tick_ms = tick_ms;
    _pwm_snapshot.requested_percent = _pending_requested_percent;
    _pwm_snapshot.protected_percent = _pending_protected_percent;
    _pwm_snapshot.logical_pwm = logical_pwm;
    _pwm_snapshot.ccr = ccr;
    _pwm_snapshot.oco_on = oco_on;
    _pwm_snapshot.valid_flags = valid_flags;
    ++_pwm_write_seq;
}

/************************************
功能描述：读取最近一次 BL0942 原始快照并校验序列稳定性
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_snapshot_read_meter(sys_calibration_meter_snapshot_st *snapshot)
{
    u32 before;
    u32 after;
    u8 retry;
    u8 index;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    for (retry = 0U; retry < SYS_CALIBRATION_SNAPSHOT_READ_RETRIES; ++retry)
    {
        before = _meter_write_seq;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        snapshot->seq = _meter_snapshot.seq;
        snapshot->tick_ms = _meter_snapshot.tick_ms;
        snapshot->i_rms_raw = _meter_snapshot.i_rms_raw;
        snapshot->v_rms_raw = _meter_snapshot.v_rms_raw;
        snapshot->i_fast_rms_raw = _meter_snapshot.i_fast_rms_raw;
        snapshot->watt_raw = _meter_snapshot.watt_raw;
        snapshot->cf_cnt_raw = _meter_snapshot.cf_cnt_raw;
        snapshot->freq_raw = _meter_snapshot.freq_raw;
        snapshot->status_raw = _meter_snapshot.status_raw;
        for (index = 0U; index < SYS_CALIBRATION_METER_RAW_FRAME_LENGTH; ++index)
        {
            snapshot->raw_frame[index] = _meter_snapshot.raw_frame[index];
        }
        snapshot->valid_flags = _meter_snapshot.valid_flags;
        snapshot->frame_error_count = _meter_snapshot.frame_error_count;
        after = _meter_write_seq;
        if (before == after)
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

/************************************
功能描述：读取最近一次 ADC 原始快照并校验序列稳定性
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_snapshot_read_adc(sys_calibration_adc_snapshot_st *snapshot)
{
    u32 before;
    u32 after;
    u8 retry;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    for (retry = 0U; retry < SYS_CALIBRATION_SNAPSHOT_READ_RETRIES; ++retry)
    {
        before = _adc_write_seq;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        snapshot->seq = _adc_snapshot.seq;
        snapshot->tick_ms = _adc_snapshot.tick_ms;
        snapshot->ntc_raw = _adc_snapshot.ntc_raw;
        snapshot->vout_raw = _adc_snapshot.vout_raw;
        snapshot->leak_raw = _adc_snapshot.leak_raw;
        snapshot->iout_raw = _adc_snapshot.iout_raw;
        snapshot->valid_flags = _adc_snapshot.valid_flags;
        after = _adc_write_seq;
        if (before == after)
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

/************************************
功能描述：读取最近一次 PWM 硬件快照并校验序列稳定性
输入参数：snapshot 输出缓存
输出返回：读取成功 BOOL_TRUE，否则 BOOL_FALSE
************************************/
boolean_en sys_calibration_snapshot_read_pwm(sys_calibration_pwm_snapshot_st *snapshot)
{
    u32 before;
    u32 after;
    u8 retry;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    for (retry = 0U; retry < SYS_CALIBRATION_SNAPSHOT_READ_RETRIES; ++retry)
    {
        before = _pwm_write_seq;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        snapshot->seq = _pwm_snapshot.seq;
        snapshot->tick_ms = _pwm_snapshot.tick_ms;
        snapshot->requested_percent = _pwm_snapshot.requested_percent;
        snapshot->protected_percent = _pwm_snapshot.protected_percent;
        snapshot->logical_pwm = _pwm_snapshot.logical_pwm;
        snapshot->ccr = _pwm_snapshot.ccr;
        snapshot->oco_on = _pwm_snapshot.oco_on;
        snapshot->valid_flags = _pwm_snapshot.valid_flags;
        after = _pwm_write_seq;
        if (before == after)
        {
            return BOOL_TRUE;
        }
    }
    return BOOL_FALSE;
}

/************************************
功能描述：复制三个独立来源并计算年龄与时间偏差
输入参数：now_tick_ms 当前毫秒节拍；snapshot 聚合输出缓存
输出返回：三份缓存均可无撕裂读取 BOOL_TRUE，否则 BOOL_FALSE
注意：时间偏差不表示物理同周期采样。
************************************/
boolean_en sys_calibration_snapshot_read_aggregate(
    u32 now_tick_ms,
    sys_calibration_snapshot_aggregate_st *snapshot)
{
    boolean_en result = BOOL_TRUE;
    u32 meter_tick;
    u32 adc_tick;
    u32 pwm_tick;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->meter_age_ms = SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;
    snapshot->adc_age_ms = SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;
    snapshot->pwm_age_ms = SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;
    snapshot->meter_adc_skew_ms = SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;
    snapshot->meter_pwm_skew_ms = SYS_CALIBRATION_SNAPSHOT_AGE_INVALID;

    if (sys_calibration_snapshot_read_meter(&snapshot->meter) == BOOL_TRUE)
    {
        snapshot->valid_flags |= SYS_CALIBRATION_AGGREGATE_METER_PRESENT;
    }
    else
    {
        result = BOOL_FALSE;
    }
    if (sys_calibration_snapshot_read_adc(&snapshot->adc) == BOOL_TRUE)
    {
        snapshot->valid_flags |= SYS_CALIBRATION_AGGREGATE_ADC_PRESENT;
    }
    else
    {
        result = BOOL_FALSE;
    }
    if (sys_calibration_snapshot_read_pwm(&snapshot->pwm) == BOOL_TRUE)
    {
        snapshot->valid_flags |= SYS_CALIBRATION_AGGREGATE_PWM_PRESENT;
    }
    else
    {
        result = BOOL_FALSE;
    }

    snapshot->meter_age_ms = sys_calibration_snapshot_age(now_tick_ms,
                                                           snapshot->meter.tick_ms,
                                                           snapshot->meter.valid_flags);
    snapshot->adc_age_ms = sys_calibration_snapshot_age(now_tick_ms,
                                                         snapshot->adc.tick_ms,
                                                         snapshot->adc.valid_flags);
    snapshot->pwm_age_ms = sys_calibration_snapshot_age(now_tick_ms,
                                                         snapshot->pwm.tick_ms,
                                                         snapshot->pwm.valid_flags);
    meter_tick = snapshot->meter.tick_ms;
    adc_tick = snapshot->adc.tick_ms;
    pwm_tick = snapshot->pwm.tick_ms;
    if ((snapshot->meter.valid_flags != 0U) && (snapshot->adc.valid_flags != 0U))
    {
        snapshot->meter_adc_skew_ms = sys_calibration_snapshot_abs_diff(meter_tick, adc_tick);
    }
    if ((snapshot->meter.valid_flags != 0U) && (snapshot->pwm.valid_flags != 0U))
    {
        snapshot->meter_pwm_skew_ms = sys_calibration_snapshot_abs_diff(meter_tick, pwm_tick);
    }
    return result;
}
