#ifndef HW_GATEWAY_H
#define HW_GATEWAY_H

#include "common.h"
#include "sys_data.h"
extern uint32_t IMEI_DEC;
extern char * IMEI_CHAR;
extern uint8 IMEI[18];
extern u8 IMEI10[13];
#define DEVICE_ID          IMEI_DEC     //数字
#define PubTopic           DEVICE_ID       
#define SubTopic           DEVICE_ID       
#define CLIENT_ID          DEVICE_ID     
typedef unsigned char     uint8;
typedef unsigned short    uint16;
typedef unsigned long     uint32;
typedef   signed char     int8;
typedef   signed short    int16;
typedef   signed long     int32;
#define GATEWAY_RX_SIZE      64
#define RECV_BUF_LENGTH      600   //nbdriver.c中有定义


typedef enum 
{
    GATEWAY_STATE_POWER_DOWN = 0,
    GATEWAY_STATE_NOT_CONNECT,
    GATEWAY_STATE_CONNECTING,
    GATEWAY_STATE_CONNECTED,
    GATEWAY_STATE_LOGIN_WITH_ONE_ID,
    GATEWAY_STATE_SCAN_ALL_DRIVER,
    GATEWAY_STATE_LOGIN_ALL_DRIVER,
    GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA,
    GATEWAY_STATE_IDLE,
} GATEWAY_STATE;
typedef enum
{
    GATEWAY_RX_STATE_NONE,
    GATEWAY_RX_STATE_NET,
}gateway_rx_state_en;

extern u8 online;
extern  gateway_rx_state_en  gateway_rx_state;
extern uint32 DeviceId;
extern void longin_sucess(void);
void longin_sucess_flag_clear(void);
extern GATEWAY_STATE xdata gateway_state;
extern  uint8 stringBuf[RECV_BUF_LENGTH];//数据接收缓存 //nbdriver.c中有定义
extern  void gateway_rx(u8 dat);
extern void hw_gateway_timer(void);
extern  void hw_gateway_process(void);
extern void recive_flag_MQTT(void);
#endif
