#include "stdio.h"
#include "string.h"
#include "Portable.h"
#include "Protocol.h"
#include "Utils.h"
#include "TcpClient.h"
#include "FirmwareUpdater.h"
#include "SystemConfig.h"
#include "NbDriver.h"
#include "hw_gateway.h"
#include "sys_data.h"
#include "app.h"
#include "sys_Vo_Io.h"
#include "sys_bl0942.h"
#include "factory_user_data.h"
#include "aip1302.h"    
#include "sys_pow_drop_check.h"
#include "danger_current_check.h"
#include "sys_bl0942.h"
#include "ntc.h"
#include "sys_Vo_Io.h"
#include "ota.h"

extern  u16  loginfirst_timeout_set;
extern u8 driver_temperarure_warn;
static  u8 longin_upload_flag=0;
extern u16 upload_timer;
extern u8  error_flag_byte;
extern   ds1302_t ds1302;   
extern u1t GetWeek(u2t year, u1t mon, u1t day);
extern   u8 Ds1302_write_time(u8* pSecDa);
extern void set_OTA_ENABLE(void);
extern  u32 dangeo_out;
uint32 timer = 0;
uint32  uploadCounter = 0;
RUNNING_PARAM tempRunningmParam;//系统运行参数
RUNNING_STATUS runningStatus;   //系统运行时数据
uint8 RTC_Param[7] = {0};
u8  fa_test_EN;
u32 fac_en_timer;


u8  Error_0_linght_bak=0; //闪灯
u8  Error_1_OL_bak=0;     //输出过载
u8  Error_Out_LV_bak=0;   //输出低压
u8  Error_3_OV_bak=0;     //输入过压
u8  Error_4_LV_bak=0;     //输入欠压
u8  driver_temperarure_warn_bak=0;   //过温上报
u8 danger_current_warn_bak=0;//





extern void soft_reset(void); 
//开始接收运行参数
void onStartSetRunningParam(void) 
{
    //备份运行参数到临时变量
    memcpy((uint8 * ) & tempRunningmParam,
    (uint8 * )(&getSystemConfig()->runningParam),
    sizeof(RUNNING_PARAM));
}

uint8 onSetOutputVoltageThreshold(uint32 threshold) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.outputVoltageThreshold = threshold;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetOutputCurrentThreshold(uint32 threshold) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.outputCurrentThreshold = threshold;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetUploadPeriod(uint32 period) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.uploadPeriod = period;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetLifeWarningParam(uint8 enable, uint32 time) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.lifeWarningParam.enable = enable;
    tempRunningmParam.lifeWarningParam.time = time;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetBrightness(uint32 brightness) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    runningStatus.brightness = brightness;
    return DOWNLOAD_SUCCESS;
}
u8 onSetDeviceadress(u32 adress)
{//驱动器地址
    runningStatus.driveradress = adress;
    return DOWNLOAD_SUCCESS;
}
u8 onSetNewDeviceadress(u32 adress)
{//更改驱动器地址
    runningStatus.newdriveradress = adress;
    sys_data.mac=adress;
    sys_data_store();
    extern void mac_reset(void);
    mac_reset();
    printf("ys_data.mac=%d\r\n",sys_data.mac);
  return DOWNLOAD_SUCCESS;
}

u8 onSettemp_protectc_value( u32 intValue )
{
  //温度保护设置
    INNRE_TEMP_PRO=intValue;
    sys_data_store();
    return DOWNLOAD_SUCCESS;
}


u8 onSethwmax_outcur_value( u32 intValue )
{
  //硬件最大输出电流设置
    if(intValue>0)
    {
        HWMAX_OUTCUR=intValue;
        sys_data_store();
    }
    return DOWNLOAD_SUCCESS;
}
u8 onSet_setcur_value( u32 intValue )
{
    //额定输出电流设置
    SET_OUTCUR=intValue;
    sys_data_store();
    return DOWNLOAD_SUCCESS;
}
u8 onSet_fa_test_value( u8 intValue )
{     
    fa_test_EN =intValue;
    if( fa_test_EN==1 )
    {
        fac_en_timer=300;//设置产测超时关闭时间
    }
    printf("fa_test_EN0=%d\r\n",fa_test_EN);
     return DOWNLOAD_SUCCESS;
}

uint8 onSet_RTC_Param(unsigned char *buf, unsigned char length)
{
	if(length > 7)
    {
	  length = 7;
    }
    memcpy(RTC_Param, buf, length);   
    ds1302.year = RTC_Param[0];    /* 00~99 */
    ds1302.mon  = RTC_Param[1];    /* 1~12  */
    ds1302.day  = RTC_Param[2];    /* 1~31  */
    ds1302.week = GetWeek(RTC_Param[0],RTC_Param[1], RTC_Param[2]);//RTC_Param[3]; /* 1~7 */ RTC_Param[3];    //
    ds1302.hour = RTC_Param[4];
    ds1302.min  = RTC_Param[5];
    ds1302.sec  = RTC_Param[6];
    printf_buf2("set time0", (u8*)&ds1302, 7);
    Ds1302_write_time((u8*)&ds1302);
    return DOWNLOAD_SUCCESS;
 
}

uint8 onOnOffControl(uint8 value) {
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    runningStatus.workState = value;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetNfcSwitchEnable(uint8 value){
	//TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
	return DOWNLOAD_SUCCESS;
}

uint8 onSetSystemReset(uint8 value)
{
    if(value==1)
    {
              soft_reset(); 
    }
    else
   {
    return DOWNLOAD_SUCCESS;
   }
   return DOWNLOAD_SUCCESS; 
}
uint8 onSetInnerTemperatureProtectEnable(uint8 value)  //设定内部过温使能
{
    if(value==1)
    {
       INNRE_TEMP_PRO_EN=1;
       sys_data_store();
      return DOWNLOAD_SUCCESS;
    }
    else
   {
        INNRE_TEMP_PRO_EN=0;
        sys_data_store(); 
    return DOWNLOAD_SUCCESS;
   }
    
}

uint8 onSetLightCompensation(LIGHT_COMPENSATION_PARAM *lightCompensationParam)
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    memcpy(&tempRunningmParam.lightCompensationParam,
           lightCompensationParam,
           sizeof(LIGHT_COMPENSATION_PARAM));
    return DOWNLOAD_SUCCESS;
}

uint8 onSetTempProtectParam(uint8 enable, uint8 ntcType,
                            uint32 recoveryPoint, uint32 protectPoint,
                            uint8 current) {
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.tempProtectedParam.enable = enable;
    tempRunningmParam.tempProtectedParam.ntcType = ntcType;
    tempRunningmParam.tempProtectedParam.recoveryPoint = recoveryPoint;
    tempRunningmParam.tempProtectedParam.protectPoint = protectPoint;
    tempRunningmParam.tempProtectedParam.current = current;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetSoftLightUp(uint8 enable, uint32 switchTime, uint8 startPower) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    tempRunningmParam.softLightUpParam.enable = enable;
    tempRunningmParam.softLightUpParam.switchTime = switchTime;
    tempRunningmParam.softLightUpParam.startPower = startPower;
    return DOWNLOAD_SUCCESS;
}

uint8 onSetTimingDimmingParam(TIMING_DIMMING_PARAM *timingDimmingParam) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    memcpy(&tempRunningmParam.timingDimmingParam,
           timingDimmingParam,
           sizeof(TIMING_DIMMING_PARAM));
    return DOWNLOAD_SUCCESS;
}

 //uint8 factoryParam[128] = {0};

uint8 onSetFactoryParam(unsigned char *buf, unsigned char length) 
{
    //TODO 检查参数是否合法，如果不合法，则返回DOWNLOAD_PARAM_ERROR
    //TODO 保存工厂参数
	if(length > 128)
    {
		length = 128;
	}
   
	memcpy(sys_data.fa_Parambuf, buf, length);
    factory_user_load_data();
    sys_data_store();
    printf_buf(sys_data.fa_Parambuf,128);
    return DOWNLOAD_SUCCESS;
}


uint8 onSaveRunningParam(void) 
{
    if (0 != memcmp((uint8 * ) & tempRunningmParam, (uint8 * )(&getSystemConfig()->runningParam),   sizeof(RUNNING_PARAM))) 
    {
        //使用新参数
        setRunningParam(&tempRunningmParam);          
        //保存新参数
        if (1 == saveSystemConfig())
        {
            return DOWNLOAD_SAVE_FAIL;
        }
    }
    return DOWNLOAD_SUCCESS;
}


uint8 onFirmwareUpdate(uint16 packNumber, uint16 totalPackCount, uint8 *pData, uint16 dataLength) //-------------------------------------------------------下载触发--------------------------------------------
{
    
     if(pData[0]==0xA5&&pData[1]==0xA5)//固件下载标志使能
     {
       printf("____OTA__START_");
       memset(firm_name_buffer,0x00,24);
       memcpy(firm_name_buffer,"cat1.bin",strlen("cat1.bin"));//固件名默认cat1.bin
         OTA_LOGI("legacy cmd received local=%s\r\n", firm_name_buffer);
         set_OTA_ENABLE();
     }
     else if(pData[0]==0xA0&&pData[1]==0xA0)
     {
        
         memcpy(firm_name_buffer,&pData[2], 22);//固件名格式cat120250401162230.bin  22个字符
         firm_name_buffer[22]='\0';
         printf("firm_name_buffer=%s\n",firm_name_buffer);
         OTA_LOGI("legacy cmd received local=%s\r\n", firm_name_buffer);
         set_OTA_ENABLE();
     }
     else
     {
       return DOWNLOAD_SAVE_FAIL;
     }
     return DOWNLOAD_SUCCESS;
 }


void uploadAllDataPoint(void) {                                                               
    
    uint8 *pack;
    
    uint16 length;

    initUploadDataPoint();

    runningStatus.brightness    = 98;//调光亮度
    runningStatus.inputVoltage  = 2410;//输入电压
    runningStatus.outputVoltage = 219;//输出电压
    runningStatus.inputCurrent  = 1024;//输入电流
    runningStatus.outputCurrent = 10200;//输出电流
    runningStatus.inputPower    = 54100;//输入功率

    runningStatus.outputPower = 51001;//输出功率
    runningStatus.workState = 1;//当前工作状态
    runningStatus.currentWorkTime = 102;//当前工作时长
    runningStatus.cumulativeWorkTime = 1025;//累计工作时长
    runningStatus.powerTemp = 64;//电源温度
    runningStatus.lampTemp = 87;//灯具温度
    runningStatus.currentEnergyConsumption = 1246;    //当前能耗
    runningStatus.cumulativeEnergyConsumption = 12564;//累计能耗
    runningStatus.faultList.count = 8;
    runningStatus.faultList.list[0] = 0x01;
    runningStatus.faultList.list[1] = 0x02;
    runningStatus.faultList.list[2] = 0x03;
    runningStatus.faultList.list[3] = 0x04;
    runningStatus.faultList.list[4] = 0x05;
    runningStatus.faultList.list[5] = 0x06;
    runningStatus.faultList.list[6] = 0x07;
    runningStatus.faultList.list[7] = 0x08;
    runningStatus.signal = getSignal();    
    addIntTypeDataPoint(DATA_POINT_ID_SIGNAL, runningStatus.signal);
    addIntTypeDataPoint(DATA_POINT_ID_BRIGHTNESS, runningStatus.brightness);
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_VOLTAGE, runningStatus.inputVoltage);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_VOLTAGE, runningStatus.outputVoltage);  
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_CURRENT, runningStatus.inputCurrent);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_CURRENT, runningStatus.outputCurrent);
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_POWER, runningStatus.inputPower);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_POWER, runningStatus.outputPower);
    addIntTypeDataPoint(DATA_POINT_ID_WORK_STATE, runningStatus.workState);
    addIntTypeDataPoint(DATA_POINT_ID_CURRENT_WORK_TIME, runningStatus.currentWorkTime);
    addIntTypeDataPoint(DATA_POINT_ID_CUMULATIVE_WORK_TIME, runningStatus.cumulativeWorkTime);
    addIntTypeDataPoint(DATA_POINT_ID_POWER_TEMP, runningStatus.powerTemp);
    addIntTypeDataPoint(DATA_POINT_ID_LAMP_TEMP, runningStatus.lampTemp);
    addIntTypeDataPoint(DATA_POINT_ID_CURRENT_ENERGY_CONSUMPTION, runningStatus.currentEnergyConsumption);
    addIntTypeDataPoint(DATA_POINT_ID_CUMULATIVE_ENERGY_CONSUMPTION, runningStatus.cumulativeEnergyConsumption);
    addFaultListDataPoint(&runningStatus.faultList);
    pack = finishUploadDataPoint(&length);
    sendTcpData(pack, length);
}

void Offline_report(u32 id ,u32 offline )
{
    uint8 *pack;
    uint16 length;
    initUploadDataPoint();
    runningStatus.driveradress=id;
    addIntTypeDataPoint( DATA_POINT_ID_DRIVER_ADDRESS,runningStatus.driveradress);
    addIntTypeDataPoint( DATA_POINT_ID_OFFLIEN_REPORT,offline);
    pack = finishUploadDataPoint(&length);
    sendTcpData(pack, length); 
}  
void uploadDriverOnedataPoint(u32 driver_id,uint32 datavalue,uint8 data_id) //单独上报一个数据点，如警告，离线通知等
{
  uint8 *pack;
  uint16 length;
  initUploadDataPoint();
  addIntTypeDataPoint( DATA_POINT_ID_DRIVER_ADDRESS,driver_id);
  addIntTypeDataPoint( data_id , datavalue);
  addIntTypeDataPoint( DATA_POINT_ID_CURRENT_ENERGY_CONSUMPTION,total_power_this_time);
  pack = finishUploadDataPoint(&length);
  sendTcpData(pack, length);

}
      
void uploadDriverDataPoint(u32 id,u32 powerTemp,u32 inputVoltage,u32 inputCurrent,u32 inputPower,u32 currentEnergyConsumption,u32 cumulativeEnergyConsumption) 
{
    
    uint8 *pack;
    uint16 length;
    initUploadDataPoint();
    runningStatus.driveradress=id;
  //runningStatus.brightness = brightness;//调光亮度
    runningStatus.inputVoltage = inputVoltage;//输入电压
    runningStatus.outputVoltage = Vo_value;//输出电压
    runningStatus.inputCurrent = inputCurrent;//输入电流
    runningStatus.outputCurrent = Io_value;//输出电流
    runningStatus.inputPower = inputPower;//输入功率
    runningStatus.outputPower = Po_value;//输出功率
    runningStatus.PF=ac_pf;   //功率因数
    runningStatus.dange_current  =dangeo_out;//漏电流
    runningStatus.powerTemp = powerTemp;//电源温度
    runningStatus.currentEnergyConsumption = currentEnergyConsumption;//当前能耗
    runningStatus.cumulativeEnergyConsumption = cumulativeEnergyConsumption;//累计能耗
    addIntTypeDataPoint( DATA_POINT_ID_DRIVER_ADDRESS,runningStatus.driveradress);
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_VOLTAGE, runningStatus.inputVoltage);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_VOLTAGE, runningStatus.outputVoltage);
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_CURRENT, runningStatus.inputCurrent);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_CURRENT, runningStatus.outputCurrent);
    addIntTypeDataPoint(DATA_POINT_ID_INPUT_POWER, runningStatus.inputPower);
    addIntTypeDataPoint(DATA_POINT_ID_OUTPUT_POWER, runningStatus.outputPower);
    addIntTypeDataPoint(DATA_POINT_ID_PF,runningStatus.PF);
    addIntTypeDataPoint(DATA_POINT_ID_DANGE_CURRENT,runningStatus.dange_current);
    addIntTypeDataPoint(DATA_POINT_ID_WORK_STATE, runningStatus.workState);
    addIntTypeDataPoint(DATA_POINT_ID_POWER_TEMP, runningStatus.powerTemp);
    addIntTypeDataPoint(DATA_POINT_ID_CURRENT_ENERGY_CONSUMPTION, runningStatus.currentEnergyConsumption);
    addIntTypeDataPoint(DATA_POINT_ID_CUMULATIVE_ENERGY_CONSUMPTION, runningStatus.cumulativeEnergyConsumption);
    pack = finishUploadDataPoint(&length);
    sendTcpData(pack, length);
}


void uploadfaultListDataPoint(void) //合并上报错误警告
 {    
    uint8 *pack;
    uint16 length;
    initUploadDataPoint();
    runningStatus.driveradress=DEVICE_ID;
    addIntTypeDataPoint( DATA_POINT_ID_DRIVER_ADDRESS,runningStatus.driveradress);
//    runningStatus.faultList.list[0] = 0x00;
//    runningStatus.faultList.list[1] = 0x00;
//    runningStatus.faultList.list[2] = 0x00;
//    runningStatus.faultList.list[3] = 0x00;
//    runningStatus.faultList.list[4] = 0x00;
//    runningStatus.faultList.list[5] = 0x00;
//    runningStatus.faultList.list[6] = 0x00;
//    runningStatus.faultList.list[7] = 0x00;
//    runningStatus.faultList.list[8] = 0x00;
    if(error_flag_byte)
    {
     runningStatus.faultList.list[0] = error_flag_byte;       //输入欠压、输入过压、输出 过载、 输出低压、输出闪灯   按位补充报警
     error_flag_byte=0;
    }  
   if(power_down_flag)               //掉电上报
    {
       //  power_down_flag=0;
         printf(" 掉电上报\r\n");
          runningStatus.faultList.list[1] = 0x01;
    }
    else
    {
        
        runningStatus.faultList.list[1] = 0x00;

    }
    if(danger_current_warn_bak!=danger_current_warn)          //漏电上报
    {
         danger_current_warn_bak=danger_current_warn;
         printf(" 漏电报警上报1\r\n");
        if(danger_current_warn)
        {
         runningStatus.faultList.list[2] = 0x02;
        }
        else
        {
          runningStatus.faultList.list[2] = 0x00;
        }
        
    }
    
 

    
   if(driver_temperarure_warn_bak!=driver_temperarure_warn)
   {
        if(driver_temperarure_warn)     //过温上报
        {
           
             printf(" 过温报警\r\n");
             runningStatus.faultList.list[3] = 0x03;
        }
        else
        {
          runningStatus.faultList.list[3] = 0x00;
        
        }
    }
    
    
    
    if(Error_0_linght_bak!=Error_0_linght)
    { 
        Error_0_linght_bak=Error_0_linght;
        if(Error_0_linght)
        { 
            
             printf(" 输出闪灯报警\r\n");
             runningStatus.faultList.list[4] = 0x04;
        }
        else
        {
              
             printf(" 输出闪灯报警\r\n");
             runningStatus.faultList.list[4] = 0x00;
        
        
        }
    }
   if(Error_3_OV_bak!=Error_3_OV) 
   {
         Error_3_OV_bak = Error_3_OV;
         
       if(   Error_3_OV)
        { 
            
             printf(" 输入过压报警\r\n");
             runningStatus.faultList.list[5] = 0x05;
        }
        else
        {       
             runningStatus.faultList.list[5] = 0x00;
        }
    }
   if(Error_Out_LV_bak!=Error_Out_LV)
   {
         Error_Out_LV_bak=Error_Out_LV;
        if(Error_Out_LV)
        { 
         
             printf(" 输出低压报警\r\n");
             runningStatus.faultList.list[6] = 0x06;
        }
        else
        {
            runningStatus.faultList.list[6] = 0x00;
        }
    }
    if(Error_1_OL_bak!=Error_1_OL)
    {
        Error_1_OL_bak=Error_1_OL;
        if(Error_1_OL)
        { 
           
             printf(" 输出过载报警\r\n");
             runningStatus.faultList.list[7] = 0x07;
        }
        else
        {
             runningStatus.faultList.list[7] = 0x00;
        }
    }
    
    if(Error_4_LV_bak!=Error_4_LV)
    {
        Error_4_LV_bak=Error_4_LV;
        
        if(Error_4_LV)
        { 
       
             printf(" 输入低压报警\r\n");
             runningStatus.faultList.list[8] = 0x08;
        }  
        else
        {
            runningStatus.faultList.list[8] = 0x00;
        }  
    }  
    runningStatus.faultList.count=9;
    addFaultListDataPoint(&runningStatus.faultList);
    pack = finishUploadDataPoint(&length);
    sendTcpData(pack, length);
}

void appProcess(void) 
{
 //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++OTA通知++++++++勿动++++++++++++++++++++++++++++++++++++++++++++++++++++++++
         if(OTA_ENABLE_state==0)
        {   
              if (longin_upload_flag==0)
               { 
                  if( gateway_state==GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA&& loginfirst_timeout_set<300&& loginfirst_timeout_set>0) 
                   {  
                      longin_upload_flag=1;
                   } 
               }    
        } 
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++转移过来+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++   
        
    if (!Timer_PassedDelay(timer, 1000)) 
    {
        return;
    }
    timer = Timer_GetTickCount();
    if(OTA_ENABLE_state==0&&( MCU_OTA_state==MCU_OTA__COMPLETE||MCU_OTA_state==MCU_OTA_STATE_IDLE))
    { 
        if(power_down_flag)//掉电单独上报
        { 
              power_down_flag=0;
              uploadDriverOnedataPoint(DEVICE_ID,2,DATA_POINT_ID_OFFLIEN_REPORT) ;//单独上报一个数据点，离线通知即刻上报
        }
        
        
        if (++uploadCounter >=30) //++uploadCounter >= getSystemConfig()->runningParam.uploadPeriod
        {
            uploadCounter = 0;
    
            if(  gateway_state==GATEWAY_STATE_CYCLIC_SCAN_AND_REAPORT_DRIVER_DATA&&longin_upload_flag==1)
            {
               if(danger_current_warn||driver_temperarure_warn||error_flag_byte)//有就一起报警,报警遵循上报周期
                {
                    uploadfaultListDataPoint();
                    
                  //cat1.报警项是：1. 掉电上报2.漏电报警上报3.过温报警，4输出闪灯报警,5.输入过压报警6 输出低压报警，7输出过载报警8输入低压报警
                  //cat1.报警项是：1. 掉电上报3.过温报警5.输入过压报警8输入低压报警
                }
            }            
        }
    } 
}
