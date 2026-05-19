#ifndef APP_H_
#define APP_H_
#include "common.h"
#include "Protocol.h"
//开始接收运行参数
void onStartSetRunningParam(void);//++++++
uint8 onSetSystemReset(uint8 value);
uint8 onSetInnerTemperatureProtectEnable(uint8 value);  //设定内部过温使能
uint8 onSetOutputVoltageThreshold(uint32 threshold);

uint8 onSetOutputCurrentThreshold(uint32 threshold);

uint8 onSetUploadPeriod(uint32 period);

uint8 onSetLifeWarningParam(uint8 enable, uint32 time);

uint8 onSetBrightness(uint32 brightness);

u8 onSetDeviceadress(u32 adress);
u8 onSetNewDeviceadress(u32 adress);
u8 onSettemp_protectc_value( u32 intValue );
uint8 onOnOffControl(uint8 value);

uint8 onSetNfcSwitchEnable(uint8 value);

uint8 onSetLightCompensation(LIGHT_COMPENSATION_PARAM *lightCompensationParam);

uint8 onSetTempProtectParam(uint8 enable, uint8 ntcType,
                            uint32 recoveryPoint, uint32 protectPoint,
                            uint8 current);

uint8 onSetSoftLightUp(uint8 enable, uint32 switchTime, uint8 startPower);

uint8 onSetTimingDimmingParam(TIMING_DIMMING_PARAM *timingDimmingParam);

uint8 onSetFactoryParam(unsigned char *buf, unsigned char length);

uint8 onSaveRunningParam(void);

uint8 onFirmwareUpdate(uint16 packNumber, uint16 totalPackCount, uint8 *pData, uint16 dataLength);
                       
uint8 onSet_RTC_Param(unsigned char *buf, unsigned char length);


u8 onSethwmax_outcur_value( u32 intValue );

u8 onSet_setcur_value( u32 intValue );
u8 onSet_fa_test_value( u8 intValue );
void uploadAllDataPoint(void);
void uploadDriverDataPoint(u32 id,u32 powerTemp,u32 inputVoltage,u32 inputCurrent,u32 inputPower,u32 currentEnergyConsumption,u32 cumulativeEnergyConsumption)  ;
void Offline_report(u32 id ,u32 offline );
void uploadDriverOnedataPoint(u32 driver_id,uint32 Onedata,uint8 data_id) ;//单独上报一个数据点，如警告，离线通知等


void appProcess(void);



#endif
