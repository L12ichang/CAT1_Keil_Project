#ifndef CODE_PROTOCOL_H
#define CODE_PROTOCOL_H
#include "common.h"
#define MSG_TYPE_INVALID            0x00
#define MSG_TYPE_LOGIN_RESPONSE     0x01
#define MSG_TYPE_PONG               0x02
#define MSG_TYPE_DOWNLOAD_DATA      0x03
#define MSG_TYPE_DOWNLOAD_FIRMWARE  0x04
#define MSG_TYPE_READ_DATA_POINT    0x05
#define MSG_TYPE_READ_FACDATA_POINT 0x06
#define MSG_TYPE_LOGIN_REQUEST      0x81
#define MSG_TYPE_PING               0x82
#define MSG_TYPE_DOWNLOAD_RESPONSE  0x83
#define MSG_TYPE_UPLOAD_DATA        0x84
#define MSG_TYPE_REQUEST_CONFIG     0x85
#define MSG_TYPE_READ_RESPONSE      0x84  //改用84回复    #define MSG_TYPE_READ_RESPONSE 0x86
#define MSG_TYPE_READ_FAC_PARA_RESPONSE 0x86
//数据类型
#define DATA_POINT_TYPE_INT                0x01
#define DATA_POINT_TYPE_BOOL               0x02
#define DATA_POINT_TYPE_LIGHT_COMPENSATION 0x03
#define DATA_POINT_TYPE_TEMP_PROTECT_PARAM 0x04
#define DATA_POINT_TYPE_SOFT_LIGHT_UP      0x05
#define DATA_POINT_TYPE_TIMING_DIMMING     0x06
#define DATA_POINT_TYPE_LIFE_WARNING       0x07
#define DATA_POINT_TYPE_FAULT_LIST         0x08
#define DATA_POINT_TYPE_FACTORY_PARAM      0x09
#define DATA_POINT_TYPE_RTC                0x0A
#define DATA_POINT_TYPE_APP_DATE           0x0B   // AP固件编译日期类型
#define DATA_POINT_TYPE_BOOT_DATE          0x0C   // BOOT固件编译日期类型
#define DATA_POINT_TYPE_IMEI               0x0D   // IMEI
#define DATA_POINT_TYPE_ICCID              0x0E   // ICCID
#define LOGIN_FAIL    0x00
#define LOGIN_SUCCESS 0x01
//服务器下发数据点id
#define DATA_POINT_ID_OUTPUT_VOLTAGE_THRESHOLD 0x01//输出电压阈值
#define DATA_POINT_ID_OUTPUT_CURRENT_THRESHOLD 0x02//输出电流阈值
#define DATA_POINT_ID_LIGHT_COMPENSATION 0x03//光衰补偿
#define DATA_POINT_ID_TEMP_PROTECT_PARAM 0x04//温度保护
#define DATA_POINT_ID_SOFT_LIGHT_UP 0x05//软起动
#define DATA_POINT_ID_TIMING_DIMMING 0x06//定时调光
#define DATA_POINT_ID_UPLOAD_PERIOD 0x07//数据上报周期
#define DATA_POINT_ID_LIFE_WARNING_THRESHOLD 0x08//寿命预警
#define DATA_POINT_ID_SET_BRIGHTNESS 0x09//实时调光
#define DATA_POINT_ID_ON_OFF_CONTROL 0x0A//开关
#define DATA_POINT_ID_FACTORY_PARAM 0x0B//工厂参数
#define DATA_POINT_ID_NFC_SWITCH_ENABLE 0x0C//NFC开关
#define DATA_POINT_ID_DEVICE_ADREESS 0x0D//设备地址
#define DATA_POINT_ID_SET_NEW_DEVICE_ADREESS 0x0E//更新的设备地址
#define DATA_POINT_ID_SET_CCO_ON 0x0F//集控开关0关1开
#define DATA_POINT_ID_SET_TEMP_PROTECT_VALUE  0x11//设定温度保护值
#define DATA_POINT_ID_SET_RTC_VALUE  0x12//设定RTC
#define DATA_POINT_ID_SET_DIM_STRATG_VALUE  0x13//设定驱动器离线调光策略
#define DATA_POINT_ID_SET_DIM_TIMING_ENABLE  0x14//设定定时调光使能
#define DATA_POINT_ID_SET_SYSTEM_RESET  0x15  //系统重启
#define DATA_POINT_ID_SET_HWMAX_OUT_CUR  0x16  //设定硬件输出最大电流
#define DATA_POINT_ID_SET_SETCUR         0x17 //设定硬件输额定电流
#define DATA_POINT_ID_SET_TEMP_PROTECT   0x18 //设定过温保护使能开关（数据0失能1使能   BOOL类型）
#define DATA_POINT_ID_SET_LOWTEMP_PROTECT   0x19 //设定低温保护使能开关（数据0失能1使能 BOOL类型）
#define DATA_POINT_ID_SET_FA_TEST           0x1A //产测测试开启通知1，关闭通知0
//终端上报数据点id
#define DATA_POINT_ID_SIGNAL 0x81//信号强度
#define DATA_POINT_ID_BRIGHTNESS 0x82//调光亮度
#define DATA_POINT_ID_INPUT_VOLTAGE 0x83//输入电压
#define DATA_POINT_ID_OUTPUT_VOLTAGE 0x84//输出电压
#define DATA_POINT_ID_INPUT_CURRENT 0x85//输入电流
#define DATA_POINT_ID_OUTPUT_CURRENT 0x86//输出电流
#define DATA_POINT_ID_INPUT_POWER 0x87//输入功率
#define DATA_POINT_ID_OUTPUT_POWER 0x88//输出功率
#define DATA_POINT_ID_WORK_STATE 0x89//当前工作状态
#define DATA_POINT_ID_CURRENT_WORK_TIME 0x8A//当前工作时长
#define DATA_POINT_ID_CUMULATIVE_WORK_TIME 0x8B//累计工作时长
#define DATA_POINT_ID_POWER_TEMP 0x8C//电源温度
#define DATA_POINT_ID_LAMP_TEMP 0x8D//灯具温度
#define DATA_POINT_ID_CURRENT_ENERGY_CONSUMPTION 0x8E     //当前能耗
#define DATA_POINT_ID_CUMULATIVE_ENERGY_CONSUMPTION 0x8F  //累计能耗
#define DATA_POINT_ID_FAULT_LIST 0x90//故障列表
#define DATA_POINT_ID_DRIVER_ADDRESS 0x91//上报驱动器设备地址？？？也肯能不用上报
#define DATA_POINT_ID_PROGRESS_BAR 0x92 //上报固件下载进度
#define DATA_POINT_ID_FIRMWARE_DATE 0x93//APP固件编译日期
#define DATA_POINT_ID_BOOT_DATE 0x94//BOOT固件编译日期
#define DATA_POINT_ID_ICCID 0x95//eSIM卡号
#define DATA_POINT_ID_OFFLIEN_REPORT 0x96//离线通知
#define DATA_POINT_ID_IMEI 0x97//IMEI
#define DATA_POINT_ID_RTC 0x99 //读驱动器RTC
#define DATA_POINT_ID_DANGER_CURRENT 0x9A//漏电报警
#define DATA_POINT_ID_PF 0x9B//PF   
#define DATA_POINT_ID_DANGE_CURRENT 0x9C
#define DATA_POINT_ID_SET_OT 0x9D //读设定过的温值  
#define DATA_POINT_ID_ALL_POINT 0xFF//所有数据点
#define MAX_LIGHT_COMPENSATION_PARAM_ITEM_COUNT 20
#define MAX_TIMING_DIMMING_PARAM_ITEM_COUNT 10
#define DOWNLOAD_SUCCESS 0x01
#define DOWNLOAD_PARAM_ERROR 0x02
#define DOWNLOAD_SAVE_FAIL 0x03
#define DOWNLOAD_FIRMWARE_VERSION_ERROR 0x04
#define DOWNLOAD_FIRMWARE_CHECK_ERROR 0x05
#define DOWNLOAD_FIRMWARE_SUCCESS 0x06
#define DOWNLOAD_DATA_TOOL_LONG 0x07
#define DOWNLOAD_OTHER_ERROR 0xFF

typedef struct 
{
    uint8 power;//输出功率
    uint32 runningTime;//运行时间
} LIGHT_COMPENSATION_PARAM_ITEM;

typedef struct
{
    uint8 enable;
    uint8 count;
    LIGHT_COMPENSATION_PARAM_ITEM items[MAX_LIGHT_COMPENSATION_PARAM_ITEM_COUNT];
} LIGHT_COMPENSATION_PARAM;

typedef struct
{
    uint8 power;//输出功率
    uint32 outputTime;//输出时间
    uint32 switchTime;//切换时间
} TIMING_DIMMING_PARAM_ITEM;

typedef struct 
{
    uint8 enable;
    uint8 count;
    TIMING_DIMMING_PARAM_ITEM items[MAX_TIMING_DIMMING_PARAM_ITEM_COUNT];
} TIMING_DIMMING_PARAM;

typedef struct
{
    uint8 enable;
    uint8 ntcType;
    uint32 recoveryPoint;
    uint32 protectPoint;
    uint8 current;
} TEMP_PROTECTED_PARAM;

typedef struct 
{
    uint8 enable;
    uint32 switchTime;
    uint8 startPower;
} SOFT_LIGHT_UP_PARAM;

typedef struct
{
    uint8 enable;
    uint32 time;
} LIFE_WARNING_PARAM;

typedef struct 
{
    uint32 outputVoltageThreshold;//输出电压阈值
    uint32 outputCurrentThreshold;//输出电流阈值
    LIGHT_COMPENSATION_PARAM lightCompensationParam;//曝光补偿
    TEMP_PROTECTED_PARAM tempProtectedParam;//温度保护
    SOFT_LIGHT_UP_PARAM softLightUpParam;//软起动
    TIMING_DIMMING_PARAM timingDimmingParam;//定时调光
    uint32 uploadPeriod;//数据上报周期
    LIFE_WARNING_PARAM lifeWarningParam;
} RUNNING_PARAM;

typedef struct
{
    uint8 count;
    uint8 list[20];
} FAULT_LIST;

typedef struct 
{
    uint32 driveradress;//调光ID
    uint32 newdriveradress;//新调光ID
    uint32 signal;//信号强度
    uint32 brightness;//调光亮度
    uint32 inputVoltage;//输入电压
    uint32 outputVoltage;//输出电压
    uint32 inputCurrent;//输入电流
    uint32 outputCurrent;//输出电流
    uint32 inputPower;//输入功率
    uint32 PF;//功率因数
    uint32 dange_current;//漏电流
    uint32 outputPower;//输出功率
    uint32 workState;//当前工作状态
    uint32 currentWorkTime;//当前工作时长
    uint32 cumulativeWorkTime;//累计工作时长
    uint32 powerTemp;//电源温度
    uint32 lampTemp;//灯具温度
    uint32 currentEnergyConsumption;//当前能耗
    uint32 cumulativeEnergyConsumption;//累计能耗
    FAULT_LIST faultList;//故障列表
} RUNNING_STATUS;

/**
*@brief   解析服务器下发的数据
*@param	  pack：数据包缓存
*@param	  msgType：消息类型
*@param	  result：参考 DOWNLOAD_RESULT
*@return  尚未解析的数据包缓存
*/
uint8 *parseServerMessage(uint8 *pack, uint8 *msgType, uint8 *result);

/**
*@brief   创建登陆数据包
*@param	  length：数据包长度
*@param	  id：设备id
*@param	  model：设备型号
*@param	  firmwareVersion：固件版本
*@return  数据包缓存
*/
uint8 *makeLoginPack(uint16 *length, uint32 id,
                     unsigned long model, uint8 firmwareVersion);

/**
*@brief   创建ping数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *makePingPack(uint16 *length);

/**
*@brief   创建设备配置请求数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *makeRequestConfigPack(uint16 *length);

/**
*@brief   创建数据接收确认数据包
*@param	  length：数据包长度
*@param	  result：数据接收结果，详见 DOWNLOAD_RESULT
*@param	  msgType：回复的消息类型
*@return  数据包缓存
*/
uint8 *makeDownloadResponsePack(uint16 *length, uint8 result, uint8 msgType);

/**
*@brief   创建数据点读取数据包
*@param	  length：数据包长度
*@param	  dataPoint：读取的数据点id
*@param	  dataPointBuf：数据点数据缓存
*@param	  dataPointLength：数据点缓存长度
*@return  数据包缓存
*/
uint8 *makeReadDataPointResponsePack(uint16 *length, uint8 dataPoint,uint8 dataType,
                                     uint8 *dataPointBuf, uint8 dataPointLength);



/**
*@brief   创建工厂参数读取数据包
*@param	  length：数据包长度
*@param	  dataPoint：读取的数据点id
*@param	  dataPointBuf：数据点数据缓存
*@param	  dataPointLength：数据点缓存长度
*@return  数据包缓存
*/
uint8 *makeReadFactoryparamPointResponsePack(uint16 *length, uint8 dataPoint,uint8 dataType,
                                     uint8 *dataPointBuf, uint8 dataPointLength) ;




/**
*@brief   初始化上传数据消息
*@return  无
*/
void initUploadDataPoint(void);

/**
*@brief   向数据包缓存填充整型数据点
*@param	  id：数据点id
*@param	  data：数据点的值
*@return  0：填充失败；1：填充成功
*/
uint8 addIntTypeDataPoint(uint8 id, uint32 value);

/**
*@brief   向数据包缓存填充故障列表
*@param	  faultList：故障列表
*@return  0：填充失败；1：填充成功
*/
uint8 addFaultListDataPoint(FAULT_LIST *faultList);

/**
*@brief   结束上传数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *finishUploadDataPoint(uint16 *length);


#endif //CODE_PROTOCOL_H
