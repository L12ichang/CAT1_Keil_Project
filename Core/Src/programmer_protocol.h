#ifndef __OLD_PROTOCOL_H__
#define __OLD_PROTOCOL_H__
#include "common.h"


#define DELI_CUSTOM_CODE            0xfb//deli2.0预留的地址字节，我们用来做自定义通信协议使用

extern boolean_en online_status;



/************************************
功能描述：接收到一字节数据
输入参数：dat 接收到的字节
输出返回：无
*************************************/
extern void programmer_rx(u8 dat);

/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/
extern void programmer_timer(void);


/************************************
功能描述：主循环调用
输入参数：无
输出返回：无
*************************************/
extern void programmer_process(void);


/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
extern void programmer_init(void);


/************************************
功能描述：收到下位机通过dali发来的数据
输入参数：buf 收到的数据指针， length 数据的字节数。
输出返回：无
*************************************/
extern void programmer_rx_from_dali(u8* buf, u16 length);


/************************************
功能描述：在线检查
输入参数：无
输出返回：无
*************************************/
extern void programmer_online_detect(void);

extern void programmer_rx_from_nfc(u8* buf, u16 length);

#endif
