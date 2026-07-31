/*************************************************************
程序功能：阶段 1 绝对毫秒调度与旧主循环顺序适配
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.30
*************************************************************/
#include "app_scheduler.h"
#include "adc.h"
#include "aip1302.h"
#include "current_calibration.h"
#include "danger_current_check.h"
#include "hw_gateway.h"
#include "hw_uart1.h"
#include "hw_uart3.h"
#include "Json_Protocol.h"
#include "meter_runtime.h"
#include "NbDriver.h"
#include "nb_at_legacy_adapter.h"
#include "net_dim.h"
#include "ota.h"
#include "Portable.h"
#include "sys_aip1302.h"
#include "sys_at_engine.h"
#include "sys_cellular.h"
#include "sys_connectivity.h"
#include "sys_bl0942.h"
#include "sys_event.h"
#include "sys_mqtt.h"
#include "sys_pow_drop_check.h"
#include "sys_pwm.h"
#include "sys_temp_over_protect.h"
#include "sys_tick.h"
#include "sys_time.h"
#include "sys_Vo_Io.h"
#include "TcpClient.h"
#include "u32_q.h"
#include "watchdog.h"
#include "zk_runtime_stats.h"
#include "zk_work_plan.h"

#define APP_SCHEDULER_LEGACY_PERIOD_MS          ((u32)10U)
#define APP_SCHEDULER_LEGACY_RTC_READY_MS       ((u32)1000U)
#define APP_SCHEDULER_U32_MAX                   ((u32)0xFFFFFFFFUL)

typedef void (*app_scheduler_task_fn)(void);

static app_scheduler_task_stats_st _task_stats[APP_SCHEDULER_TASK_COUNT];
static boolean_en _soft_start_pending;
static boolean_en _rtc_initialized;

/************************************
功能描述：对无符号调度统计执行饱和累加
输入参数：value 待更新统计地址，increment 增量
输出返回：无
*************************************/
static void app_scheduler_add_saturated(u32 *value, u32 increment)
{
    if (increment > (APP_SCHEDULER_U32_MAX - *value))
    {
        *value = APP_SCHEDULER_U32_MAX;
    }
    else
    {
        *value += increment;
    }
}


/************************************
功能描述：执行单个任务并记录高分辨率耗时
输入参数：task_id 任务编号，task 待执行函数
输出返回：无
*************************************/
static void app_scheduler_run_profiled(
    app_scheduler_task_id_en task_id,
    app_scheduler_task_fn task)
{
    u32 start_tick;
    u32 cost_ticks;
    app_scheduler_task_stats_st *stats;

    stats = &_task_stats[task_id];
    start_tick = sys_tick_get_tick();
    task();
    cost_ticks = sys_tick_get_tick() - start_tick;
    stats->last_cost_ticks = cost_ticks;
    if (cost_ticks > stats->max_cost_ticks)
    {
        stats->max_cost_ticks = cost_ticks;
    }
    app_scheduler_add_saturated(&stats->run_count, 1U);
}


/************************************
功能描述：检查周期任务绝对到期时间并跳过积压周期
输入参数：task_id 任务编号，now_ms 当前毫秒时间
输出返回：本轮到期返回 BOOL_TRUE，否则返回 BOOL_FALSE
注意：按总文档 7.2，同一任务每轮最多执行一次；错过周期只统计并跳过，
      不进行补跑。主循环阻塞造成的调度延迟需要实机验证。
*************************************/
static boolean_en app_scheduler_periodic_is_due(
    app_scheduler_task_id_en task_id,
    u32 now_ms)
{
    app_scheduler_task_stats_st *stats;
    u32 lag_ms;
    u32 missed_periods;

    stats = &_task_stats[task_id];
    if (sys_time_is_due(now_ms, stats->next_run_ms) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    lag_ms = now_ms - stats->next_run_ms;
    if (lag_ms > stats->max_lag_ms)
    {
        stats->max_lag_ms = lag_ms;
    }
    missed_periods = lag_ms / stats->period_ms;
    app_scheduler_add_saturated(&stats->missed_count, missed_periods);
    stats->next_run_ms += (missed_periods + 1U) * stats->period_ms;
    return BOOL_TRUE;
}


/************************************
功能描述：保持阶段 1 原有上电首次循环的 100% 输出行为
输入参数：无
输出返回：无
注意：阶段 5 删除，届时由 sys_control 按安全上电策略仲裁。
*************************************/
static void app_scheduler_legacy_soft_start(void)
{
    if (_soft_start_pending == BOOL_TRUE)
    {
        _soft_start_pending = BOOL_FALSE;
        dim_level = 100U;
        sys_pwm_fade_output(0U, 100U);
    }
}


/************************************
功能描述：保持阶段 1 原有延迟 RTC 初始化与处理顺序
输入参数：无
输出返回：无
注意：阶段 5/8 删除，届时由 sys_rtc 和工作计划模块接管。
*************************************/
static void app_scheduler_legacy_rtc_process(void)
{
    u32 now_ms;

    if (OTA_ENABLE_state != 0U)
    {
        return;
    }

    now_ms = sys_time_get_ms();
    if ((_rtc_initialized == BOOL_FALSE) &&
        (sys_time_is_due(now_ms, APP_SCHEDULER_LEGACY_RTC_READY_MS) == BOOL_TRUE))
    {
        _rtc_initialized = BOOL_TRUE;
        Ds1302_init();
        sys_aip1302_init();
        Ds1302_set_charge();
    }
    if (_rtc_initialized == BOOL_TRUE)
    {
        sys_aip1302_process();
    }
}


/************************************
功能描述：保持阶段 1 原有调试字节日志出队行为
输入参数：无
输出返回：无
注意：阶段 8 删除，届时由统一诊断模块接管。
*************************************/
static void app_scheduler_legacy_log_process(void)
{
    log_type_st log;

    if (u32_queue_out(&log))
    {
        printf("%d:%d > %c-0x%x\r\n", log.sn, log.index, log.dat, log.dat);
    }
}


/************************************
功能描述：按原顺序执行网络、计量和 OTA 的阶段 8 临时适配
输入参数：无
输出返回：无
注意：阶段 8 删除，迁移期间不得调整调用顺序。
*************************************/
static void app_scheduler_legacy_network_process(void)
{
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_CELLULAR,
                               sys_cellular_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_NB_SEND,
                               sys_mqtt_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_GATEWAY,
                               sys_connectivity_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_DIM_UART,
                               uart_diam_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_ADC, adc_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_TEMP_PROTECT,
                               sys_temp_over_protect_process);
    /*
     * 正常模式完全切断旧网络状态机。OTA 独占期间，新三模块已暂停，
     * 旧 AT 适配器仅为 OTA 尚未迁移的逐行命令路径服务。
     */
    if (nb_at_legacy_adapter_has_exclusive() == BOOL_TRUE)
    {
        app_scheduler_run_profiled(APP_SCHEDULER_TASK_AT_COMMAND,
                                   send_AT_Command_machine);
    }
    if (OTA_ENABLE_IS_SET() == BOOL_FALSE)
    {
        app_scheduler_run_profiled(APP_SCHEDULER_TASK_TCP_CLIENT,
                                   tcpClientProcess);
    }
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_BL0942,
                               sys_bl0942_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_OTA, _4G_OTA_machine);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_COPY_FIRMWARE,
                               mcu_copy_firmware_machine);
}


/************************************
功能描述：按原顺序执行控制、保护和协议的阶段 5/8 临时适配
输入参数：无
输出返回：无
注意：阶段 5/8 删除，迁移期间不得调整调用顺序。
*************************************/
static void app_scheduler_legacy_control_process(void)
{
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_POWER_DROP,
                               sys_pow_drop_check_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_DANGER_CURRENT,
                               danger_current_check_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_ERROR_REPORT,
                               error_report_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_PWM, sys_pwm_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_TEMP_LOW,
                               sys_temp_low_protect_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_WORK_PLAN,
                               zk_work_plan_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_JSON, json_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_CALIBRATION,
                               current_calibration_process);
}


/************************************
功能描述：输出阶段 1 保留的十六进制字节日志
输入参数：buf 字节缓冲，length 字节数
输出返回：无
注意：阶段 8 删除，届时由统一诊断模块接管。
*************************************/
void printf_buf(u8 *buf, u16 length)
{
#if APP_HEX_LOG_ENABLE
    u16 index;

    printf("\n---------\n");
    for (index = 0U; index < length; index++)
    {
        printf("%02x,", buf[index]);
    }
    printf("\n---------\n");
#else
    (void)buf;
    (void)length;
#endif
}


/************************************
功能描述：按字符形式输出阶段 1 保留的字节日志
输入参数：buf 字节缓冲，length 字节数
输出返回：无
注意：阶段 8 删除，届时由统一诊断模块接管。
*************************************/
void printf_buf_char(u8 *buf, u16 length)
{
#if APP_HEX_LOG_ENABLE
    u16 index;

    printf("\n---------\n");
    for (index = 0U; index < length; index++)
    {
        printf("%c,", buf[index]);
    }
    printf("\n---------\n");
#else
    (void)buf;
    (void)length;
#endif
}


/************************************
功能描述：输出带标签的阶段 1 十六进制字节日志
输入参数：str 日志标签，buf 字节缓冲，length 字节数
输出返回：无
注意：阶段 8 删除，届时由统一诊断模块接管。
*************************************/
void printf_buf2(const char *str, u8 *buf, u16 length)
{
#if APP_HEX_LOG_ENABLE
    u16 index;

    printf("%s: ", str);
    for (index = 0U; index < length; index++)
    {
        printf("%02x,", buf[index]);
    }
    printf("\n");
#else
    (void)str;
    (void)buf;
    (void)length;
#endif
}


/************************************
功能描述：初始化绝对毫秒调度器及任务统计
输入参数：无
输出返回：无
*************************************/
void app_scheduler_init(void)
{
    u32 task_index;
    u32 now_ms;

    memset(_task_stats, 0, sizeof(_task_stats));
    now_ms = sys_time_get_ms();
    _task_stats[APP_SCHEDULER_TASK_LEGACY_10MS].period_ms =
        APP_SCHEDULER_LEGACY_PERIOD_MS;
    _task_stats[APP_SCHEDULER_TASK_LEGACY_10MS].next_run_ms =
        now_ms + APP_SCHEDULER_LEGACY_PERIOD_MS;
    for (task_index = 0U; task_index < APP_SCHEDULER_TASK_COUNT; task_index++)
    {
        if (_task_stats[task_index].period_ms == 0U)
        {
            _task_stats[task_index].next_run_ms = now_ms;
        }
    }
    _soft_start_pending = BOOL_TRUE;
    _rtc_initialized = BOOL_FALSE;
}


/************************************
功能描述：按原有主循环顺序执行阶段 1 调度
输入参数：无
输出返回：无
注意：阶段 5/8 删除旧模块直调适配，保留绝对时间调度内核。
*************************************/
void app_scheduler_process(void)
{
    u32 now_ms;

    watchdog_loop_begin();
    app_scheduler_legacy_soft_start();
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_UART3,
                               hw_uart3_process);
#endif

    now_ms = sys_time_get_ms();
    if (app_scheduler_periodic_is_due(
            APP_SCHEDULER_TASK_LEGACY_10MS, now_ms) == BOOL_TRUE)
    {
        app_scheduler_run_profiled(APP_SCHEDULER_TASK_LEGACY_10MS,
                                   sys_tick_process);
        sys_tick_scheduler_stats_update(
            _task_stats[APP_SCHEDULER_TASK_LEGACY_10MS].missed_count,
            _task_stats[APP_SCHEDULER_TASK_LEGACY_10MS].max_lag_ms);
    }

    app_scheduler_run_profiled(APP_SCHEDULER_TASK_UART1,
                               hw_uart1_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_AT_ENGINE,
                               sys_at_engine_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_METER_RUNTIME,
                               meter_runtime_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_RUNTIME_COUNTER,
                               zk_runtime_counter_process);
    app_scheduler_legacy_network_process();
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_RTC,
                               app_scheduler_legacy_rtc_process);
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_LEGACY_LOG,
                               app_scheduler_legacy_log_process);
    app_scheduler_legacy_control_process();
    app_scheduler_run_profiled(APP_SCHEDULER_TASK_EVENT,
                               sys_event_process);
    watchdog_loop_end();
}


/************************************
功能描述：复制指定任务的调度和耗时统计
输入参数：task_id 任务编号，stats 接收统计的稳定存储地址
输出返回：参数有效返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
boolean_en app_scheduler_get_task_stats(
    app_scheduler_task_id_en task_id,
    app_scheduler_task_stats_st *stats)
{
    if (((u32)task_id >= (u32)APP_SCHEDULER_TASK_COUNT) ||
        (stats == NULL))
    {
        return BOOL_FALSE;
    }

    *stats = _task_stats[task_id];
    return BOOL_TRUE;
}
