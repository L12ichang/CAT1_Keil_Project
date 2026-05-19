#include "SystemConfig.h"
#include "stdio.h"
#include "string.h"
#include "Portable.h"
#include "Protocol.h"
#include "Utils.h"


#define SECTOR_SIZE 512
#define SECTOR0 0
#define SECTOR1 1

 SYSTEM_CONFIG systemConfig = {0};
 uint32 currentConfigSector = SECTOR1;

/**
*@brief   获取系统配置
*@return  系统配置结构体变量指针
*/
SYSTEM_CONFIG *getSystemConfig(void) {
    return &systemConfig;
}

/**
*@brief   加载默认配置
*@return  无
*/
void loadDefaultConfig(void)
{

#ifdef DEBUG_PRINT
    printf("load default config\n");
#endif

    systemConfig.validFlag = VALID_FLAG;
    systemConfig.deviceId = 252;
    memset(systemConfig.serverAddress, 0, 50);
    memcpy(systemConfig.serverAddress, "106.55.105.27", strlen("106.55.105.27"));
    //memcpy(systemConfig.serverAddress, "8.134.67.99", strlen("8.134.67.99"));
    systemConfig.serverPort = 7008;
    systemConfig.runningParam.outputVoltageThreshold = 0;
    systemConfig.runningParam.outputCurrentThreshold = 0;
    systemConfig.runningParam.lightCompensationParam.enable = 0;
    systemConfig.runningParam.lightCompensationParam.count = 0;
    systemConfig.runningParam.tempProtectedParam.enable = 0;
    systemConfig.runningParam.timingDimmingParam.enable = 0;
    systemConfig.runningParam.timingDimmingParam.count = 0;
    systemConfig.runningParam.uploadPeriod = 10 * 60;
}

/**
*@brief   设置系统运行参数，需要调用saveSystemConfig，才会保存到flash
* @param   param：系统运行参数结构体指针
*@return  无
*/
void setRunningParam(RUNNING_PARAM *param)
{
    memcpy((uint8 *) &systemConfig.runningParam, (uint8 *) param, sizeof(RUNNING_PARAM));
}

/**
*@brief   设置设备id，需要调用saveSystemConfig，才会保存到flash
* @param  id：设备id
*@return  无
*/
void setDeviceId(uint32 id) 
{
    systemConfig.deviceId = id;
}

/**
*@brief   设置服务器地址信息，需要调用saveSystemConfig，才会保存到flash
* @param  address：服务器地址
* @param  port：服务器监听的tcp端口号
*@return  无
*/
void setServerInfo(uint8 *address, uint32 port) 
{
    /*     //移植临时关闭20230922
    memset(&systemConfig.serverAddress, 0, MAX_SERVER_ADDRESS_LENGTH);
    memcpy(&systemConfig.serverAddress, address, strlen(address));
    systemConfig.serverPort = port;*/
}

/**
*@brief   保存累计工作时长，调用完本函数后，需要调用saveSystemConfig，才会保存到flash
* @param  time：累计工作时长
*@return  无
*/
void setCumulativeWorkTime(uint32 time) 
{
    systemConfig.cumulativeWorkTime = time;
}

/**
*@brief   保存累计能耗，调用完本函数后，需要调用saveSystemConfig，才会保存到flash
* @param  consumption：累计能耗
*@return  无
*/
void setCumulativeEnergyConsumption(uint32 consumption)
{
    systemConfig.cumulativeEnergyConsumption = consumption;
}

static void readSystemConfigFromFlash(uint32 sector)
{   
}

/**
*@brief   保存系统配置到flash
*@return  0：保存成功；1：保存失败
*/
uint8 saveSystemConfig(void) 
{
    return 0;
}

/**
*@brief   从flash加载系统配置到内存
*@return  1：加载系统配置成功；0：加载系统配置失败
*/
uint8 loadSystemConfig(void)
{
    readSystemConfigFromFlash(SECTOR0);
    if (VALID_FLAG == systemConfig.validFlag &&
        systemConfig.crc == getCRC16((uint8 *) &systemConfig, sizeof(systemConfig) - 2))
       {
        currentConfigSector = SECTOR0;
#ifdef DEBUG_PRINT
        printf("config in sector 0\n");
#endif
        return 1;
    }

    readSystemConfigFromFlash(SECTOR1);
    if (VALID_FLAG == systemConfig.validFlag &&
        systemConfig.crc == getCRC16((uint8 *) &systemConfig, sizeof(systemConfig) - 2)) 
     {
        currentConfigSector = SECTOR1;
#ifdef DEBUG_PRINT
        printf("config in sector 1\n");
#endif
        return 1;
    }

#ifdef DEBUG_PRINT
    printf("load config error\n");
#endif

    currentConfigSector = SECTOR1;
    return 0;
}
