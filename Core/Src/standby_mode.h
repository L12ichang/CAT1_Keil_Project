#ifndef HIBERNATE_MODE_H
#define HIBERNATE_MODE_H

#include "common.h"


extern u16 standby_timer;

/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/
extern void standby_mode_timer(void);


/************************************
功能描述：计时复位
输入参数：无
输出返回：无
*************************************/
extern void standby_mode_recount(void);

extern void standby_mode_init(void);

#endif

