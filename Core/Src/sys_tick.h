#ifndef __SYS_TICK_H__
#define __SYS_TICK_H__

#include "common.h"

#define SYS_TICK_CYCLE           ((u32)72000U)
#define SYS_TOTAL_TICK_PER_US    ((u32)72U)
#define MODULE_TIMER_INTERVAL    \
    ((u32)10000U * SYS_TOTAL_TICK_PER_US)
#define DOWN_COUNT_TICK_24BIT    (SysTick->VAL)
#define COUNT_TICK_24BIT         ((SYS_TICK_CYCLE - 1U) - SysTick->VAL)

/************************************
功能描述：读取系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_MHZ us
输入参数：无
输出返回：系统节拍值
*************************************/
extern u32 sys_tick_get_tick(void);

/************************************
功能描述：读取第二路兼容高分辨率系统节拍
输入参数：无
输出返回：32位系统节拍值
*************************************/
extern u32 sys_tick_get_tick2(void);

/************************************
功能描述：执行一次到期的旧 10ms 计时任务
输入参数：无
输出返回：无
*************************************/
extern void sys_tick_process(void);

/************************************
功能描述：读取 10ms 调度累计错过周期数
输入参数：无
输出返回：累计错过周期数
*************************************/
extern u32 sys_tick_get_lag_count(void);

/************************************
功能描述：读取 10ms 调度最大延迟的兼容节拍值
输入参数：无
输出返回：最大延迟系统节拍数
*************************************/
extern u32 sys_tick_get_max_lag_ticks(void);

/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
extern void sys_tick_init(void);

/************************************
功能描述：按原顺序执行阶段 1 保留的 10ms 业务计时调用
输入参数：无
输出返回：无
*************************************/
extern void sys_tick_cycle_handle(void);

/************************************
功能描述：精准延时，注意延时的时间不应该超过看门狗的超时值。
输入参数：time 需要延时的时间值， 单位 us
输出返回：无
*************************************/
extern void sys_tick_delay(u32 time);

/************************************
功能描述：在 SysTick 中断中维持旧 Portable 毫秒时间基准
输入参数：无
输出返回：无
*************************************/
extern void sys_tick_legacy_timebase_isr(void);

/************************************
功能描述：同步新调度器的 10ms 任务延迟统计
输入参数：missed_count 累计错过周期数，max_lag_ms 最大延迟毫秒数
输出返回：无
*************************************/
extern void sys_tick_scheduler_stats_update(
    u32 missed_count,
    u32 max_lag_ms);

#endif

