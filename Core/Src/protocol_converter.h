
#ifndef PROTOCOL_CONVERTER_H
#define PROTOCOL_CONVERTER_H

#include "common.h"

#define OFFSET_ID    0
#define OFFSET_CMD   1  
#define OFFSET_SN    2
#define OFFSET_DATA  4

#define		CC_CMD_SET_DALI_CYCLE					    0x9e
#define		CC_CMD_POLL					                0x96
#define		CC_CMD_HEADER					            0x94
#define     CMD_APP_VERSION                             0xEF
#define     CMD_APP_BUILD_DATE                          0xEE
#define     CMD_APP_DEBUG                               0xED
#define     CMD_APP_BASE_TIME                           0xEC


/************************************
功能描述：串口与dali协议转换
输入参数：无
输出返回：无
*************************************/
extern void protocol_converter_process(void);

/************************************
功能描述：从串口收到数据
输入参数：buf 数据指针， length 数据的长度
输出返回：无
*************************************/
extern void rx_packet_from_serial(u8* buf, u16 length);



/************************************
功能描述：从dali收到数据
输入参数：buf 数据指针， length 数据的长度
输出返回：无
*************************************/
extern void rx_packet_from_dali(u8* buf, u16 length);


/************************************
功能描述：dali发送完成回调函数
输入参数：无
输出返回：无
*************************************/
extern void dali_tx_complete(void);


#endif



