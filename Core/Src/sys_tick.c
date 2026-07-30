/*************************************************************
程序功能：系统节拍 从开机开始计数，0-ffffffff循环计数，每个节拍是 1/72 us。 循环一周是 约59.65秒
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_tick.h"
#include "adc.h"
#include "app_active.h"
#include "buzzer.h"
#include "charge.h"
#include "danger_current_check.h"
#include "hw_gateway.h"
#include "hw_tim4_cap1.h"
#include "hw_uart3.h"
#include "offline_Time_controlled_dimming.h"
#include "Portable.h"
#include "sys_aip1302.h"
#include "sys_bl0942.h"
#include "sys_pow_drop_check.h"
#include "sys_pwm.h"
#include "sys_serial_port.h"
#include "sys_temp_over_protect.h"
#include "sys_time.h"
#include "sys_Vo_Io.h"

#define SYS_TICK_LEGACY_TIMEBASE_DIVIDER    ((u8)10U)
#define SYS_TICK_TICKS_PER_MS               \
    ((u32)(SYS_TOTAL_TICK_PER_US * 1000UL))
#define SYS_TICK_U32_MAX                    ((u32)0xFFFFFFFFUL)

/* 阶段 5/8 删除：旧头文件尚未公开以下两个 10ms 计时入口。 */
extern void hw_tim4_cap1_timer(void);
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
extern void hw_uart3_timer(void);
#endif

static u8 _legacy_timebase_divider;
static volatile u32 _scheduler_lag_count;
static volatile u32 _scheduler_max_lag_ticks;

/* 阶段 8 删除：旧 App.c 仍声明该兼容计时变量。 */
u16 upload_timer;

/************************************
功能描述：读取32位系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_MHZ us
输入参数：无
输出返回：32位系统节拍值
注意：调用时不能关闭中断，不能厅中断里执行。需要在中断执行或者关闭中断执行的请使用 sys_tick_get_tick_24bit 。
*************************************/

u32 sys_tick_get_tick(void)
{
    volatile u32 tick_ms_before;
    volatile u32 tick_ms_after;
    volatile u32 sub_tick;
    u64 tmp;

    tick_ms_before = HAL_GetTick();
    sub_tick = COUNT_TICK_24BIT;
    tick_ms_after = HAL_GetTick();
    if (tick_ms_before != tick_ms_after)
    {
        sub_tick = COUNT_TICK_24BIT;
    }
    tmp = ((u64)tick_ms_after * SYS_TICK_CYCLE) + sub_tick;
    return u64l(tmp);
}


/************************************
功能描述：读取第二路兼容高分辨率系统节拍
输入参数：无
输出返回：32位系统节拍值
注意：阶段 8 删除重复入口。
*************************************/
u32 sys_tick_get_tick2(void)
{
    return sys_tick_get_tick();
}


/************************************
功能描述：精准延时，注意延时的时间不应该超过看门狗的超时值。
输入参数：time 需要延时的时间值， 单位 us
输出返回：无
*************************************/
void sys_tick_delay(u32 time)
{
    volatile u32 start_tick;

    start_tick = sys_tick_get_tick();
    while ((sys_tick_get_tick() - start_tick) <
           (time * SYS_TOTAL_TICK_PER_US))
    {
        /* 仅保留规范允许的微秒级精确延时。 */
    }
}


/************************************
功能描述：在 SysTick 中断中维持旧 Portable 毫秒时间基准
输入参数：无
输出返回：无
注意：阶段 8 删除；不得在此增加 ADC、PWM、网络、RTC 或 Flash 业务。
*************************************/
void sys_tick_legacy_timebase_isr(void)
{
    _legacy_timebase_divider++;
    if (_legacy_timebase_divider >= SYS_TICK_LEGACY_TIMEBASE_DIVIDER)
    {
        _legacy_timebase_divider = 0U;
        updateTimeTick(10);
    }
}


/************************************
功能描述：更新旧模块使用的 10ms 上传计时变量
输入参数：无
输出返回：无
注意：阶段 8 删除该旧公共变量。
*************************************/
void main_timer(void)
{
    upload_timer = (u16)(sys_time_get_ms() / 10U);
}


/************************************
功能描述：按原顺序执行阶段 1 保留的 10ms 业务计时调用
输入参数：无
输出返回：无
注意：阶段 5/8 删除各旧模块直调适配。
*************************************/
void sys_tick_cycle_handle(void)
{
    adc_process_timer();
    sys_temp_over_protect_timer();
    sys_pwm_timer();
    sys_serial_port_timer();
    hw_tim4_cap1_timer();
    charge_timer();
    sys_bl0942_timer();
    hw_gateway_timer();
    sys_aip1302_timer();
    sys_pow_drop_check_timer();
    danger_current_timer();
    voio_timer();
    offline_timer();
    app_activate_timer();
}


/************************************
功能描述：执行一次到期的旧 10ms 计时任务
输入参数：无
输出返回：无
注意：绝对到期判定由 app_scheduler 完成，本函数不得自行追赶。
*************************************/
void sys_tick_process(void)
{
    buzzer_timer();
    main_timer();
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
    hw_uart3_timer();
#endif
    sys_tick_cycle_handle();
}


/************************************
功能描述：同步新调度器的 10ms 任务延迟统计
输入参数：missed_count 累计错过周期数，max_lag_ms 最大延迟毫秒数
输出返回：无
*************************************/
void sys_tick_scheduler_stats_update(u32 missed_count, u32 max_lag_ms)
{
    _scheduler_lag_count = missed_count;
    if (max_lag_ms > (SYS_TICK_U32_MAX / SYS_TICK_TICKS_PER_MS))
    {
        _scheduler_max_lag_ticks = SYS_TICK_U32_MAX;
    }
    else
    {
        _scheduler_max_lag_ticks = max_lag_ms * SYS_TICK_TICKS_PER_MS;
    }
}


/************************************
功能描述：读取 10ms 调度累计错过周期数
输入参数：无
输出返回：累计错过周期数
*************************************/
u32 sys_tick_get_lag_count(void)
{
    return _scheduler_lag_count;
}


/************************************
功能描述：读取 10ms 调度最大延迟的兼容节拍值
输入参数：无
输出返回：最大延迟系统节拍数
*************************************/
u32 sys_tick_get_max_lag_ticks(void)
{
    return _scheduler_max_lag_ticks;
}


/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
void sys_tick_init(void)
{
    upload_timer = (u16)(sys_time_get_ms() / 10U);
    _scheduler_lag_count = 0U;
    _scheduler_max_lag_ticks = 0U;
}
