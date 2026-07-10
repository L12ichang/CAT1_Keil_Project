/*************************************************************
程序功能：系统节拍 从开机开始计数，0-ffffffff循环计数，每个节拍是 1/72 us。 循环一周是 约59.65秒
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_tick.h"
#include "hw_tim2.h"
#include "buzzer.h"
#include "hw_dali.h"
#include "sys_serial_port.h"
#include "programmer_protocol.h"
#include "hw_led.h"
#include "charge.h"
#include "app_led.h"
#include "app_state.h"
#include "standby_mode.h"
#include "hw_uart2.h"
#include "hw_uart1.h"
#include "Portable.h"
#include "hw_gateway.h"

#include "sys_bl0942.h"
#include "adc.h"
#include "sys_temp_over_protect.h"
#include "sys_pwm.h"
#include "sys_aip1302.h"
#include "sys_pow_drop_check.h"
#include "danger_current_check.h"
#include "sys_Vo_Io.h"
#include "offline_Time_controlled_dimming.h"
#include "app_active.h"

#define SYS_TICK_MAX_CATCH_UP 3U

static u16 volatile _tick_h = 0; //让定时器揍够32位
static u32 _tick = 0;
static volatile u32 sys_tick_lag_count = 0;
static volatile u32 sys_tick_max_lag_ticks = 0;

extern void main_timer(void);
extern void hw_tim4_cap1_timer(void);
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
extern void hw_uart3_timer(void);
#endif

/************************************
功能描述：M0自带的24位定时器循环一周（233016us 72M主频）中断一次, 
          32位跑一圈是59.65秒 72M主频
输入参数：无
输出返回：无
*************************************/


/************************************
功能描述：读取系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_MHZ us
输入参数：无
输出返回：系统节拍值
*************************************/
//u32 sys_tick_get_value(void)
//{
//      u32 k = 0xffffff - SysTick->VAL;
//      u32hh(k) = _tick_h;
//      return(k);
//}


/************************************
功能描述：读取32位系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_MHZ us
输入参数：无
输出返回：32位系统节拍值
注意：调用时不能关闭中断，不能厅中断里执行。需要在中断执行或者关闭中断执行的请使用 sys_tick_get_tick_24bit 。
*************************************/

u32 sys_tick_get_tick(void)
{
    u32 volatile h1,h2;
    u32 volatile k;
    u64 tmp;
    h1 = HAL_GetTick();
    k = COUNT_TICK_24BIT;
    h2 = HAL_GetTick();
    if (h1 != h2)
    {
        k = COUNT_TICK_24BIT;
    }
    tmp = ((u64)h2*SYS_TICK_CYCLE)+k;
    k = u64l(tmp);
    return(k);        
}

u32 sys_tick_get_tick2(void)
{
    u32 volatile h1,h2;
    u32 volatile k;
    u64 tmp;
    h1 = HAL_GetTick();
    k = COUNT_TICK_24BIT;
    h2 = HAL_GetTick();
    if (h1 != h2)
    {
        k = COUNT_TICK_24BIT;
    }
    tmp = ((u64)h2*SYS_TICK_CYCLE)+k;
    k = u64l(tmp);
    return(k);        
}

/************************************
功能描述：精准延时，注意延时的时间不应该超过看门狗的超时值。
输入参数：time 需要延时的时间值， 单位 us
输出返回：无
*************************************/
void sys_tick_delay(u32 time)
{
    u32 volatile t;
    t = sys_tick_get_tick();
    while((sys_tick_get_tick() - t) < time*SYS_TOTAL_TICK_PER_US);
}


void sys_timer_1ms(void)
{
    static u8 _timer=0;
    ++_timer;
    if(_timer == 10)
    {
        _timer = 0;
        buzzer_timer();
         main_timer();
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
        hw_uart3_timer();
#endif
        updateTimeTick(10);
    }
    
}

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
功能描述：主程序调用
输入参数：无
输出返回：无
*************************************/
void sys_tick_process(void)
{
    u32 system_tick;
    u32 lag_ticks;
    u8 catch_count;

    system_tick = sys_tick_get_tick();
    catch_count = 0;

    while ((system_tick - _tick) >= MODULE_TIMER_INTERVAL && catch_count < SYS_TICK_MAX_CATCH_UP)
    {
        _tick += MODULE_TIMER_INTERVAL;
        sys_tick_cycle_handle();
        catch_count++;
    }

    if ((system_tick - _tick) >= MODULE_TIMER_INTERVAL)
    {
        lag_ticks = system_tick - _tick;
        if (lag_ticks > sys_tick_max_lag_ticks)
        {
            sys_tick_max_lag_ticks = lag_ticks;
        }
        sys_tick_lag_count++;
        _tick = system_tick;
    }
}

u32 sys_tick_get_lag_count(void)
{
    return sys_tick_lag_count;
}

u32 sys_tick_get_max_lag_ticks(void)
{
    return sys_tick_max_lag_ticks;
}



/************************************
功能描述：初始化
输入参数：     无
输出返回：无
*************************************/
void sys_tick_init(void)
{
	/*
	(1)设置Systick 定时时间
	*/
    //SysTick_Config(0xffffff);		
    _tick = sys_tick_get_tick();

    
}


