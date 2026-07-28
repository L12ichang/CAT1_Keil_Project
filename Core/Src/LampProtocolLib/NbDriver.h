#ifndef NB_DRIVER_H_
#define NB_DRIVER_H_
#define NB_ERROR_NONE 0x00
#define NB_ERROR_NOT_CONNECTED 0x01
#define NB_ERROR_SEND_FAIL 0x02
#include "common.h"
#include "Utils.h"

#define NB_ICCID_DEFAULT "00000000000000000000"
#define NB_ICCID_MAX_ATTEMPTS 3

typedef enum
{
    NB_EVENT_INIT_FAIL = 0,
    NB_EVENT_CONNECT_TIME_OUT,
    NB_EVENT_OPERATION_TIME_OUT,
    NB_EVENT_LOST_CONNECTION,
    NB_EVENT_DATA,
    NB_EVENT_UPLOAD_COMPLETE,
    NB_EVENT_UPLOAD_FAIL,
    NB_EVENT_CONNECTED
} NB_SUB_EVENT;

typedef enum 
{
    NB_STATE_POWER_DOWN = 0,
    NB_STATE_NOT_CONNECT,
    NB_STATE_CONNECTING,
    NB_STATE_CONNECTED,
    NB_STATE_IDLE,
} NB_STATE;


typedef enum
{ 
    SEND_COMMAND_STATE_IDLE,
    SEND_COMMAND_STATE_RESETING,
    SEND_COMMAND_STATE_READY,
    SEND_COMMAND_STATE_TXING,
    SEND_COMMAND_STATE_TXING_COMPLETE,
    SEND_COMMAND_STATE_RXING,
    SEND_COMMAND_STATE_RXING_PACKET,
    SEND_COMMAND_STATE_RXING_COMPLETE,
    SEND_COMMAND_STATE_SEND_AGAIN,  //自动重发
    SEND_COMMAND_STATE_NO_REPLY
} SEND_COMMAND_state_en;

typedef enum 
{ 
    CONNECT_CONFIG_STATE_IDLE,
    CONNECT_CONFIG_PROBE_AT,
    CONNECT_CONFIG_PWRKEY_START,
    CONNECT_CONFIG_WAIT_AT,
    CONNECT_CONFIG_HARD_RESET_START,
    CONNECT_CONFIG_WAIT_HARD_RESET_AT,
    CONNECT_CONFIG_AT_CFUN0,
    CONNECT_CONFIG_AT_CFUN1,
    CONNECT_CONFIG_AT_CPIN,
    CONNECT_CONFIG_AT_CEREG_ENABLE,
    CONNECT_CONFIG_AT_CEREG_QUERY,
    CONNECT_CONFIG_WAIT_CEREG_QUERY,
    CONNECT_CONFIG_AT_RECVMODE,
    CONNECT_CONFIG_AT_VERSION,
    CONNECT_CONFIG_AT_keepalive,
    CONNECT_CONFIG_AT_SESSION,
    CONNECT_CONFIG_AT_TIMEOUT,
    CONNECT_CONFIG_AT_IEMI,
    CONNECT_CONFIG_AT_QCCID,
    CONNECT_CONFIG_AT_WILL_PROMPT,
    CONNECT_CONFIG_AT_WILL_RESULT,
    CONNECT_CONFIG_WAITING_QMTCLOSE,
    CONNECT_CONFIG_RECOVERY_WAIT,
    CONNECT_CONFIG_AT_IPPORT,
    CONNECT_CONFIG_AT_QMTCONN,
    CONNECT_CONFIG_AT_QMTSUB,
    CONNECT_CONFIG_AT_LAST,
    CONNECT_CONFIG__COMPLETE,

} CONNECT_CONFIG_state_en;

typedef enum 
{
    PUBSEDN_STATE_IDLE,
    PUBSEDN_STATE_SEND_HEADER,
    PUBSEDN_STATE_WAIT_PROMPT,
    PUBSEDN_STATE_SEND_PAYLOAD,
    PUBSEDN_STATE_WAIT_ACK,
    PUBSEDN_STATE_SENDFINISH,
    PUBSEDN_STATE_FAIL,
} PUBSEDN_STATE_EN;
extern u8   OTA_ENABLE_state;
extern PUBSEDN_STATE_EN pubsend_state ;
extern void changea_to_MQTT_modle(void);
extern u8   OTA_ENABLE;
boolean_en pubsend_state_finish(void);
void pubsend_state_set_idle(void);
boolean_en pubsend_state_idle(void);
uint32 nb_mqtt_get_publish_success_count(void);
uint32 nb_mqtt_get_publish_fail_count(void);
uint32 nb_mqtt_get_publish_timeout_count(void);
void  _4G_configModule_machine_star(void) ;
boolean_en  _4G_configModule_machine_finish(void) ;
void _4G_configModule_machine(void) ;
void nb_mark_boot_start(void);
void nb_trace_milestone(const char *stage);
void nb_mark_business_online(void);
void nb_request_reconnect(const char *reason);
void send_AT_Command_machine(void);
boolean_en  send_AT_Command_machine_finish(void);
void  send_AT_Command_machine_idle(void);
void  send_AT_Command_machine_star(char *command,uint8 length, char *response, uint32 waitCount, uint8 throwAwayTail) ;
boolean_en nb_get_rsrp_dbm10(s32 *rsrp_dbm10);
boolean_en OTA_ENABLE_IS_SET(void);
void nb_modem_lock_for_ota(void);
void nb_modem_unlock_for_ota(void);
boolean_en nb_modem_locked_by_ota(void);
void set_OTA_ENABLE(void);
void nbSendTcpData_sm(void);
 uint16 readLine(uint8 *buf, uint16 *len, uint8 syncMode) ;
 uint16 readLine_get_firmware(uint8 *buf, uint16 *len, uint16 *firmwarelenth);

extern u8 IMEI[18];

uint8 g4Send_MQTT_Data(char *topic,char *pData);


/**
*@brief   配置nb模块运行参数。
 * 注意：第一次启动时配置一次，以后不需要再配置
*@return  1：配置成功；0：配置失败
*/
u8 configNbModule(void);
uint8 config4GModule(void) ;
uint8 nb_modem_send_command_ota(void *command,uint16 length);
void sendCommand(void *command,uint16 length) ;//调试4G
/**
*@brief   设置连接的服务器信息
*@param	  address：服务器地址，注意：本驱动只保存服务器地址数组指针，不拷贝内容
*@param	  length：服务器地址长度
*@param	  port：服务器端口号
*@return  无
*/
void nbDriverInit(uint8 *address, uint8 length, uint32 port);
/**
*@brief   进入空闲模式
*@return  无
*/
void nbEnterIDLE(void);
/**
*@brief   发送tcp数据
*@param	  pData：要发送的数据缓存
*@param	  length：发送的数据长度
*@return  BC26_ERROR_NONE：发送成功；其它：发送失败
*/
uint8 nbSendTcpData(uint8 *pData, uint16 length);

/**
*@brief   获取sim卡IMSI
*@param	  simCardIMSILength：IMSI缓存
*@param	  simCardIMSI：IMSI长度
*@return  1：获取成功；0：发送失败
*/
uint8 getSimCardIMSI(uint8 *simCardIMSILength, uint8 *simCardIMSI);


/**
*@brief   获取信号强度
*@return  0：信号弱；1：信号中；2：信号强
*/
uint8 getSignalQuality(void);
/**
*@brief   获取当前日期时间
*@param	  sTime：时间
*@param	  sDate：日期
*@param	  timeZone：时区，以 1/4 小时为单位显示本地时间和 GMT 之间的时区区别。
*         范围：-47 ~ +48，例如，2019/05/06,22:10:00+8表示2019年5月6号，22:10:00GMT+2小时。
*@return  1：获取成功；0：获取失败
*/
uint8 getCurrentTime(TimeType *sTime,DateType *sDate, signed char *timeZone);

/**
*@brief   nb模块状态机，实现自动连接服务器和回调事件
*@return  无
*/
void nbModuleProcess(void);

#endif
