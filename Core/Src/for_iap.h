#ifndef FOR_IAP_H
#define FOR_IAP_H

#include "common.h"


#define MYID                202


/************************************
功能描述：IAP配置
输入参数：无
输出返回：无
*************************************/
extern void Reset_Control (void); //把这个函数加入你自己的项目。下面main中加入这个函数的调用。






/************************************
功能描述：程序从app区跳转到boot区
输入参数：无
输出返回：无
*************************************/
extern void iap_jump2boot(void);

extern void soft_reset(void);




#endif

