#ifndef SYS_TICK_H
#define SYS_TICK_H

#include "common.h"
#define SYS_TICK_CYCLE           72000 //hal库用了1ms中断一次 (72000-1)往下减

#define SYS_TOTAL_TICK_PER_US    72    //1us需要经历72个系统节拍

#define MODULE_TIMER_INTERVAL    ((u32)10000*SYS_TOTAL_TICK_PER_US)  //给各模块用的定时器 10ms调用一次

#define DOWN_COUNT_TICK_24BIT       (SysTick->VAL)     // cortex m0 24位 倒计时 定时器         

#define COUNT_TICK_24BIT       ((SYS_TICK_CYCLE-1)-SysTick->VAL)     // cortex m0 24位 倒计时 定时器         

/************************************
功能描述：读取系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_MHZ us
输入参数：无
输出返回：系统节拍值
*************************************/
extern u32 sys_tick_get_tick(void);



/************************************
功能描述：主程序调用
输入参数：     无
输出返回：无
*************************************/
extern void sys_tick_process(void);

extern u32 sys_tick_get_lag_count(void);
extern u32 sys_tick_get_max_lag_ticks(void);



/************************************
功能描述：初始化
输入参数：     无
输出返回：无
*************************************/
extern void sys_tick_init(void);



/************************************
功能描述：M0自带的24位定时器循环一周（0.699秒24M主频，0.35秒24M主频）中断一次, 
          32位跑一圈是178.95697067秒（24M主频），89.478秒（48M主频）
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


#endif

