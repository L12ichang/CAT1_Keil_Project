#ifndef __BUZZER_H__
#define __BUZZER_H__
#include "common.h"



//#define  BEEP      GPIO_Pin_7
//#define	 BEEP_0	 	GPIO_ResetBits(GPIOB,GPIO_Pin_7)
//#define	 BEEP_1	 	GPIO_SetBits(GPIOB,GPIO_Pin_7)
/************************************
功能描述：蜂鸣器PWM发生器
输入参数：无
输出返回：无
*************************************/
extern void pwm_generator(void);


/************************************
功能描述：每定时 10ms 执行一次
输入参数：无
输出返回：无
*************************************/
extern void buzzer_timer(void);



/************************************
功能描述：蜂鸣器发声
输入参数：ontime 响的时间， offtime 关的时间，cnt重复的次数 单位是10ms
输出返回：无
*************************************/
extern void  buzzer_sound(u8 ontime, u8 offtime, u8 cnt);



/************************************
功能描述：模块的初始化
输入参数：无
输出返回：无
*************************************/
extern void buzzer_init(void);

extern boolean_en buzzer_is_finish(void);



#endif



