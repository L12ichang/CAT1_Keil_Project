
#ifndef HW_4G_IO_H
#define HW_4G_IO_H

#include "common.h"


/************************************
功能描述：oco打开
输入参数：无
输出返回：无
*************************************/
extern void pwr_on(void);


/************************************
功能描述：oco关闭
输入参数：无
输出返回：无
*************************************/
extern void pwr_off(void);





extern void  reset_on(void);
extern void  reset_off(void);

/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
extern void hw_4g_io_init(void);

#endif

