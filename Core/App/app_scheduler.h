#ifndef __APP_SCHEDULER_H__
#define __APP_SCHEDULER_H__

#include "common.h"

typedef enum
{
    APP_SCHEDULER_TASK_LEGACY_10MS = 0,
    APP_SCHEDULER_TASK_UART3,
    APP_SCHEDULER_TASK_UART1,
    APP_SCHEDULER_TASK_AT_ENGINE,
    APP_SCHEDULER_TASK_METER_RUNTIME,
    APP_SCHEDULER_TASK_RUNTIME_COUNTER,
    APP_SCHEDULER_TASK_GATEWAY,
    APP_SCHEDULER_TASK_DIM_UART,
    APP_SCHEDULER_TASK_ADC,
    APP_SCHEDULER_TASK_TEMP_PROTECT,
    APP_SCHEDULER_TASK_MODEM_RESET,
    APP_SCHEDULER_TASK_CELLULAR,
    APP_SCHEDULER_TASK_NB_SEND,
    APP_SCHEDULER_TASK_AT_COMMAND,
    APP_SCHEDULER_TASK_TCP_CLIENT,
    APP_SCHEDULER_TASK_BL0942,
    APP_SCHEDULER_TASK_OTA,
    APP_SCHEDULER_TASK_COPY_FIRMWARE,
    APP_SCHEDULER_TASK_RTC,
    APP_SCHEDULER_TASK_LEGACY_LOG,
    APP_SCHEDULER_TASK_POWER_DROP,
    APP_SCHEDULER_TASK_DANGER_CURRENT,
    APP_SCHEDULER_TASK_ERROR_REPORT,
    APP_SCHEDULER_TASK_PWM,
    APP_SCHEDULER_TASK_TEMP_LOW,
    APP_SCHEDULER_TASK_WORK_PLAN,
    APP_SCHEDULER_TASK_JSON,
    APP_SCHEDULER_TASK_CALIBRATION,
    APP_SCHEDULER_TASK_EVENT,
    APP_SCHEDULER_TASK_COUNT
} app_scheduler_task_id_en;

typedef struct
{
    u32 period_ms;
    u32 next_run_ms;
    u32 run_count;
    u32 missed_count;
    u32 max_lag_ms;
    u32 last_cost_ticks;
    u32 max_cost_ticks;
} app_scheduler_task_stats_st;

/************************************
功能描述：初始化绝对毫秒调度器及任务统计
输入参数：无
输出返回：无
*************************************/
extern void app_scheduler_init(void);

/************************************
功能描述：按原有主循环顺序执行阶段 1 调度
输入参数：无
输出返回：无
*************************************/
extern void app_scheduler_process(void);

/************************************
功能描述：复制指定任务的调度和耗时统计
输入参数：task_id 任务编号，stats 接收统计的稳定存储地址
输出返回：参数有效返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
extern boolean_en app_scheduler_get_task_stats(
    app_scheduler_task_id_en task_id,
    app_scheduler_task_stats_st *stats);

/************************************
功能描述：按字符形式输出阶段 1 保留的字节日志
输入参数：buf 字节缓冲，length 字节数
输出返回：无
注意：阶段 8 删除该旧诊断入口。
*************************************/
extern void printf_buf_char(u8 *buf, u16 length);

#endif
