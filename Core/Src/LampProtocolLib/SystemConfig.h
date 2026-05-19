#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H
#include "common.h" 
#include "Protocol.h"
#define VALID_FLAG 0x55555554
#define DEVICE_MODEL 0x01
#define FIRMWARE_VERSION 0x01
#define MAX_SERVER_ADDRESS_LENGTH 50

typedef struct 
{
    uint32 validFlag;
    uint32 deviceId;
    uint8 serverAddress[MAX_SERVER_ADDRESS_LENGTH];
    uint32 serverPort;
    RUNNING_PARAM runningParam;
    uint32 cumulativeWorkTime;//累计工作时长
    uint32 cumulativeEnergyConsumption;//累计能耗
    uint16 crc;
} SYSTEM_CONFIG;

/**
*@brief   加载默认配置
*@return  无
*/
void loadDefaultConfig(void);

/**
*@brief   获取系统配置
*@return  系统配置结构体变量指针
*/
SYSTEM_CONFIG *getSystemConfig(void);

/**
*@brief   设置设备id，需要调用saveSystemConfig，才会保存到flash
* @param  id：设备id
*@return  无
*/
void setDeviceId(uint32 id);

/**
*@brief   设置服务器地址信息，需要调用saveSystemConfig，才会保存到flash
* @param  address：服务器地址
* @param  port：服务器监听的tcp端口号
*@return  无
*/
void setServerInfo(uint8 *address, uint32 port);

/**
*@brief   设置系统运行参数，需要调用saveSystemConfig，才会保存到flash
* @param   param：系统运行参数结构体指针
*@return  无
*/
void setRunningParam(RUNNING_PARAM *param);

/**
*@brief   保存累计工作时长，调用完本函数后，需要调用saveSystemConfig，才会保存到flash
* @param  time：累计工作时长
*@return  无
*/
void setCumulativeWorkTime(uint32 time);

/**
*@brief   保存累计能耗，调用完本函数后，需要调用saveSystemConfig，才会保存到flash
* @param  consumption：累计能耗
*@return  无
*/
void setCumulativeEnergyConsumption(uint32 consumption);

/**
*@brief   保存系统配置到flash
*@return  0：保存成功；1：保存失败
*/
unsigned char saveSystemConfig(void);

/**
*@brief   从flash加载系统配置到内存
*@return  1：加载系统配置成功；0：加载系统配置失败
*/
uint8 loadSystemConfig(void);

#endif
