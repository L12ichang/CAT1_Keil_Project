#include "Portable.h"
#include "stdio.h"
#include "string.h"
#include "Protocol.h"
#include "TcpClient.h"
#include "App.h"
#include "hw_gateway.h"
#include "net_dim.h"
#define TCP_PROTOCOL_VERSION 0x01
#define SEND_PACK_SIZE 255

uint8 sendPack[SEND_PACK_SIZE];//数据包缓存
uint16 sendPackLength = 0;//数据包长度
LIGHT_COMPENSATION_PARAM lightCompensationParam = {0};
TIMING_DIMMING_PARAM timingDimmingParam = {0};

/**
*@brief   初始化上传数据包，填充起始符和消息类型
*@param	  msgType：消息类型
*@return  无
*/
void initUploadMessage(uint8 msgType) 
{
    //起始符
    sendPack[0] = 0x50;
    sendPack[1] = 0x50;
    sendPack[2] = 0x50;
    sendPack[3] = 0x50;
    //消息类型
    sendPack[4] = msgType;
    sendPackLength = 7;//包括消息长度
}

/**
*@brief   向数据包填充数据点
*@param	  pData：序列化后的数据点缓存
*@param	  length：数据点缓存长度
*@return  0：填充失败；1：填充成功
*/
uint8 fillSendPack(uint8 *pData, uint16 length)
{
    if ((sendPackLength + length) >= (SEND_PACK_SIZE - 5)) 
    {
        return 0;
    }
    memcpy(&sendPack[sendPackLength], pData, length);
    sendPackLength += length;
    return 1;
}

/**
*@brief   结束数据包，设置消息长度和结束符
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *finishSendPack(uint16 *length)
{
    //设置消息长度
    sendPack[5] = (sendPackLength - 7) >> 8;
    sendPack[6] = (sendPackLength - 7);
    //设置结束符
    sendPack[sendPackLength] = 0xAA;
    sendPack[sendPackLength + 1] = 0xAA;
    sendPack[sendPackLength + 2] = 0xAA;
    sendPack[sendPackLength + 3] = 0xAA;
    *length = sendPackLength + 4;
    return sendPack;
}

/**
*@brief   创建登陆数据包
*@param	  length：数据包长度
*@param	  id：设备id
*@param	  model：设备型号
*@param	  firmwareVersion：固件版本
*@return  数据包缓存
*/
uint8 *makeLoginPack(uint16 *length, uint32 id, unsigned long model, uint8 firmwareVersion)
{
    uint8 buf[16] = {0};

    buf[0] = id >> 24;
    buf[1] = id >> 16;
    buf[2] = id >> 8;
    buf[3] = id;

    buf[4] = model >> 24;
    buf[5] = model >> 16;
    buf[6] = model >> 8;
    buf[7] = model;

    buf[8] = TCP_PROTOCOL_VERSION >> 24;
    buf[9] = TCP_PROTOCOL_VERSION >> 16;
    buf[10] = TCP_PROTOCOL_VERSION >> 8;
    buf[11] = TCP_PROTOCOL_VERSION;

    buf[12] = firmwareVersion >> 24;
    buf[13] = firmwareVersion >> 16;
    buf[14] = firmwareVersion >> 8;
    buf[15] = firmwareVersion;

    initUploadMessage(MSG_TYPE_LOGIN_REQUEST);
    fillSendPack(buf, 16);

    return finishSendPack(length);
}

/**
*@brief   创建ping数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *makePingPack(uint16 *length) 
{
    initUploadMessage(MSG_TYPE_PING);
    return finishSendPack(length);
}

/**
*@brief   创建设备配置请求数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *makeRequestConfigPack(uint16 *length)
{
    initUploadMessage(MSG_TYPE_REQUEST_CONFIG);
    return finishSendPack(length);
}

/**
*@brief   创建数据接收确认数据包
*@param	  length：数据包长度
*@param	  result：数据接收结果，详见 DOWNLOAD_RESULT
*@param	  msgType：回复的消息类型
*@return  数据包缓存
*/
extern RUNNING_STATUS runningStatus;//系统运行时数据//因增加数据点引用外部变量runningStatus20231007
uint8 *makeDownloadResponsePack(uint16 *length, uint8 result, uint8 msgType)
{
    initUploadMessage(MSG_TYPE_DOWNLOAD_RESPONSE);
    fillSendPack(&result, 1);
    fillSendPack(&msgType, 1);
    return finishSendPack(length);
}


/**
*@brief   向数据包缓存填充读取数据点
*@param	  id：数据点id
*@param	  datapinttype;数据点类型
*@param	  datapintlen;数据点长度
*@param	  data：数据点的内容
*@return  0：填充失败；1：填充成功
*/

/*
uint8 addReadTypeDataPoint(uint8 dataid,uint8 datapinttype, uint8 datapintlen,uint8 * databuf) {
    
    uint8 *buf;
 
    buf = dataid;
    buf+=1;
    buf = datapinttype;
    buf+=1;
    buf = datapintlen;
    buf+=1;
    buf = databuf;
    

    return fillSendPack(buf, 2);
}

*/


/**
*@brief   创建数据点读取数据包
*@param	  length：数据包长度
*@param	  dataPoint：读取的数据点id
*@param	  dataPointBuf：数据点数据缓存
*@param	  dataPointLength：数据点缓存长度
*@return  数据包缓存
*/
uint8 *makeReadDataPointResponsePack(uint16 *length, uint8 dataPoint,uint8 dataType, uint8 *dataPointBuf, uint8 dataPointLength) 
{
    initUploadMessage(MSG_TYPE_READ_RESPONSE);
    addIntTypeDataPoint( DATA_POINT_ID_DRIVER_ADDRESS, DEVICE_ID  );                                 
    fillSendPack(&dataPoint, 1);//dataid
    fillSendPack(&dataType, 1);  //datatype                                  
    fillSendPack(&dataPointLength, 1);  //len                                                                           
    fillSendPack(dataPointBuf, dataPointLength);
    return finishSendPack(length);
}

/**
*@brief   创建工厂参数读取数据包
*@param	  length：数据包长度
*@param	  dataPoint：读取的数据点id
*@param	  dataPointBuf：数据点数据缓存
*@param	  dataPointLength：数据点缓存长度
*@return  数据包缓存
*/
uint8 *makeReadFactoryparamPointResponsePack(uint16 *length, uint8 dataPoint,uint8 dataType,uint8 *dataPointBuf, uint8 dataPointLength) 
{
    initUploadMessage(MSG_TYPE_READ_FAC_PARA_RESPONSE);                                
    fillSendPack(&dataType, 1);  //datatype                                                                                                         
    fillSendPack(dataPointBuf, dataPointLength);
    return finishSendPack(length);
}

/**
*@brief   初始化上传数据消息
*@return  无
*/
void initUploadDataPoint(void) 
{
    initUploadMessage(MSG_TYPE_UPLOAD_DATA);
}

/**
*@brief   向数据包缓存填充整型数据点
*@param	  id：数据点id
*@param	  data：数据点的值
*@return  0：填充失败；1：填充成功
*/
uint8 addIntTypeDataPoint(uint8 id, uint32 value) 
{
    
    uint8 buf[7];
    buf[0] = id;
    buf[1] = DATA_POINT_TYPE_INT;
    buf[2] = 0x04;
    buf[3] = value >> 24;
    buf[4] = value >> 16;
    buf[5] = value >> 8;
    buf[6] = value;
    return fillSendPack(buf, 7);
}

/**
*@brief   向数据包缓存填充故障列表
*@param	  faultList：故障列表
*@return  0：填充失败；1：填充成功
*/
uint8 addFaultListDataPoint(FAULT_LIST *faultList) 
{
    
    uint8 buf[4];
    buf[0] = DATA_POINT_ID_FAULT_LIST;//id
    buf[1] = DATA_POINT_TYPE_FAULT_LIST;//type
    buf[2] = faultList->count + 1;//length
    buf[3] = faultList->count;//fault count
    if (0 == fillSendPack(buf, 4)) 
    {
        return 0;
    }
    return fillSendPack(faultList->list, faultList->count);
}

/**
*@brief   结束上传数据包
*@param	  length：数据包长度
*@return  数据包缓存
*/
uint8 *finishUploadDataPoint(uint16 *length)
{
    return finishSendPack(length);
}

/**
*@brief   解析整型数据点
*@param	  pData：数据包缓存
*@param	  value：解析结果
*@return  数据包缓存
*/
uint8 *parseIntTypeValue(uint8 *pData, uint32 *value) 
{
    *value = pData[0] << 24 | pData[1] << 16 | pData[2] << 8 | pData[3];
    return pData + 4;
}

/**
*@brief   解析光衰补偿数据点
*@param	  pData：数据包缓存
*@param	  lightCompensationParam：解析结果
*@return  数据包缓存
*/
uint8 *parseLightCompensationParam(uint8 *pData, LIGHT_COMPENSATION_PARAM *lightCompensationParam, uint8 *result) 
{
    
    uint8 i;
    lightCompensationParam->enable = pData[0];
    lightCompensationParam->count = pData[1];
    if (lightCompensationParam->count > MAX_LIGHT_COMPENSATION_PARAM_ITEM_COUNT)
    {
        *result = DOWNLOAD_DATA_TOOL_LONG;
        return pData + 2;
    }
    pData += 2;
    for (i = 0; i < lightCompensationParam->count; i++) {
        lightCompensationParam->items[i].power = pData[0];
        pData++;
        pData = parseIntTypeValue(pData, &(lightCompensationParam->items[i].runningTime));
    }
    *result = DOWNLOAD_SUCCESS;
    return pData;
}

/**
*@brief   解析定时调光数据点
*@param	  pData：数据包缓存
*@param	  timingDimmingParam：解析结果
*@return  数据包缓存
*/
uint8 *parseTimingDimmingParam(uint8 *pData,TIMING_DIMMING_PARAM *timingDimmingParam, uint8 *result) 
{
    
    uint8 i;

    timingDimmingParam->enable = pData[0];
    timingDimmingParam->count = pData[1];
    if (timingDimmingParam->count > MAX_TIMING_DIMMING_PARAM_ITEM_COUNT) 
    {
        *result = DOWNLOAD_DATA_TOOL_LONG;
        return pData + 2;
    }
    pData += 2;

    for (i = 0; i < timingDimmingParam->count; ++i)
    {
        timingDimmingParam->items[i].power = pData[0];
        pData++;
        pData = parseIntTypeValue(pData, &(timingDimmingParam->items[i].outputTime));
        pData = parseIntTypeValue(pData, &(timingDimmingParam->items[i].switchTime));
    }

    return pData;
}

/**
*@brief   解析服务器下发的数据
*@param	  pack：数据包缓存
*@param	  msgType：消息类型
*@param	  result：参考 DOWNLOAD_RESULT
*@return  尚未解析的数据包缓存
*/
uint8 *parseServerMessage(uint8 *pack, uint8 *msgType, uint8 *result) 
{
    
    uint8 dataPointId, dataPointType, dataPointLength;
    uint16 msgLength = 0;
    uint32 intValue = 0;
    *msgType = MSG_TYPE_INVALID;
    *result = DOWNLOAD_OTHER_ERROR;
    //起始符和结束符
    if (pack[0] != 0x50 || pack[1] != 0x50 || pack[2] != 0x50 || pack[3] != 0x50)
    {
        return pack + 4;
    }
    pack += 4;
    //消息类型
    *msgType = pack[0];
    pack++;

    //消息长度
    msgLength = pack[0] << 8 | pack[1];
    pack += 2;

    if (pack[msgLength] != 0xAA || pack[msgLength + 1] != 0xAA || pack[msgLength + 2] != 0xAA ||
        pack[msgLength + 3] != 0xAA) 
    {//检查结束符
        return pack + 4;
    }

    *result = DOWNLOAD_SUCCESS;

    //解析消息内容
    switch (*msgType)
    {
        case MSG_TYPE_LOGIN_RESPONSE:
           //  printf("LOG\n");
            onLogInResponse(pack[0]);
            pack += msgLength;
            break;

        case MSG_TYPE_PONG:
            
            onPongMsg();
            break;

        case MSG_TYPE_DOWNLOAD_DATA:

               onStartSetRunningParam();

                        while (msgLength) 
                        {
                            dataPointId = pack[0];//数据点id
                            dataPointType = pack[1];//数据点类型
                            dataPointLength = pack[2];
                            pack += 3;
                            msgLength -= 3;

                            if (dataPointType == DATA_POINT_TYPE_INT) 
                            {
                                    pack = parseIntTypeValue(pack, &intValue);
                                    msgLength -= 4;
                                    if (dataPointId == DATA_POINT_ID_OUTPUT_VOLTAGE_THRESHOLD) 
                                    {
                                        *result = onSetOutputVoltageThreshold(intValue);
                                    }
                                    else if (dataPointId == DATA_POINT_ID_OUTPUT_CURRENT_THRESHOLD) 
                                    {
                                        *result = onSetOutputCurrentThreshold(intValue);
                                    } 
                                    else if (dataPointId == DATA_POINT_ID_UPLOAD_PERIOD) 
                                    {
                                        *result = onSetUploadPeriod(intValue);
                                    } 
                                    else if (dataPointId == DATA_POINT_ID_SET_BRIGHTNESS) 
                                    {
                                        *result = onSetBrightness(intValue);
                                    }
                                    else if (dataPointId == DATA_POINT_ID_DEVICE_ADREESS) 
                                    {          //写驱动器设备地址
                                        *result = onSetDeviceadress( intValue ); 
                                    }
                                     else if (dataPointId == DATA_POINT_ID_SET_NEW_DEVICE_ADREESS) 
                                     {  //驱动器设备更新地址
                                        *result = onSetNewDeviceadress( intValue ); 
                                     }
                                    else if (dataPointId ==   DATA_POINT_ID_SET_TEMP_PROTECT_VALUE) 
                                    {  //设置温度保护阈
                                         *result = onSettemp_protectc_value( intValue ); 
                                     } 
                                     else if (dataPointId ==   DATA_POINT_ID_SET_HWMAX_OUT_CUR) 
                                     {  //设置硬件最大输出电流
                                         *result = onSethwmax_outcur_value( intValue ); 
                                     } 
                                      else if (dataPointId ==   DATA_POINT_ID_SET_SETCUR ) 
                                     {  //设置额定输出电流
                                         *result = onSet_setcur_value( intValue ); 
                                     }

                                
                                
                            }
                            else if (dataPointType == DATA_POINT_TYPE_BOOL)
                            {
                                if (dataPointId == DATA_POINT_ID_ON_OFF_CONTROL) 
                                 {
                                     *result = onOnOffControl(pack[0]);
                                 } 
                                 else if(DATA_POINT_ID_NFC_SWITCH_ENABLE == dataPointId)
                                 {
                                      *result = onSetNfcSwitchEnable(pack[0]);
                                 }
                                 else if(dataPointId ==  DATA_POINT_ID_SET_SYSTEM_RESET)
                                 {
                                    *result = onSetSystemReset(pack[0]); 
                                 }
                                 else if(dataPointId ==  DATA_POINT_ID_SET_TEMP_PROTECT)  //设置过温保护开启
                                 {  
                                    *result = onSetInnerTemperatureProtectEnable(pack[0]); 
                                 }
                                  else if (dataPointId ==   DATA_POINT_ID_SET_FA_TEST ) 
                                 {  //产测通知
                                     *result = onSet_fa_test_value( pack[0] ); 
                                 }
                                 pack++;
                                 msgLength--;
                            } 
                            else if (dataPointType == DATA_POINT_TYPE_LIGHT_COMPENSATION) 
                            {
                                pack = parseLightCompensationParam(pack, &lightCompensationParam, result);
                                if (DOWNLOAD_SUCCESS == *result)
                                {
                                *result = onSetLightCompensation(&lightCompensationParam);
                                msgLength -= (lightCompensationParam.count * 5 + 2);
                                }
                            }
                            else if (dataPointType == DATA_POINT_TYPE_TEMP_PROTECT_PARAM)
                            {
                                *result = onSetTempProtectParam(pack[0], pack[1],
                                                                pack[2] << 24 | pack[3] << 16 | pack[4] << 8 | pack[5],
                                                                pack[6] << 24 | pack[7] << 16 | pack[8] << 8 | pack[9],
                                                                pack[10]);
                                pack += 11;
                                msgLength -= 11;
                            } 
                            else if (dataPointType == DATA_POINT_TYPE_SOFT_LIGHT_UP)
                            {
                                *result = onSetSoftLightUp(pack[0],
                                                           pack[1] << 24 | pack[2] << 16 | pack[3] << 8 | pack[4],
                                                           pack[5]);
                                pack += 6;
                                msgLength -= 6;
                            } 
                            else if (dataPointType == DATA_POINT_TYPE_TIMING_DIMMING)
                           {
                                pack = parseTimingDimmingParam(pack, &timingDimmingParam, result);
                                if (DOWNLOAD_SUCCESS == *result) 
                                {
                                    *result = onSetTimingDimmingParam(&timingDimmingParam);
                                    msgLength -= (timingDimmingParam.count * 9 + 2);
                                }
                            }
                           else if (dataPointType == DATA_POINT_TYPE_LIFE_WARNING) 
                           {
                                *result = onSetLifeWarningParam(pack[0],
                                                                pack[1] << 24 | pack[2] << 16 | pack[3] << 8 | pack[4]);
                                pack += 5;
                                msgLength -= 5;
                            } 
                            else if (dataPointType ==DATA_POINT_TYPE_FACTORY_PARAM  ) 
                            {
                                if(dataPointId==DATA_POINT_ID_FACTORY_PARAM )
                                {
                                    *result = onSetFactoryParam(pack, dataPointLength);
                                      
                                    pack += dataPointLength;
                                    msgLength -= dataPointLength;
                                }
                            }
                            else if (dataPointType == DATA_POINT_TYPE_RTC) 
                            {
                                *result = onSet_RTC_Param(pack, dataPointLength);
                                pack += dataPointLength;
                                msgLength -= dataPointLength;
                            } 
                            else 
                           {
                                 printf("DOWNLOAD_OTHER_ERROR00000000\n");
                                 *result = DOWNLOAD_OTHER_ERROR;
                           }

                            if (*result != DOWNLOAD_SUCCESS) {
                                break;
                           }
                      }//while end

                if (*result == DOWNLOAD_SUCCESS) 
                 {
                    dim_ready(); 
                    *result = onSaveRunningParam();
                 }

                break;

        case MSG_TYPE_DOWNLOAD_FIRMWARE:                                             //下发OTA 
                printf("_OTA_0__________");
                *result = onFirmwareUpdate(pack[0] << 8 | pack[1],
                                           pack[2] << 8 | pack[3],
                                           &pack[4],
                                           msgLength - 4);
                pack += msgLength;
            break;

        case MSG_TYPE_READ_DATA_POINT:                                                 //读数据
             *result = pack[0];
           //pack += msgLength;
             break;
        case  MSG_TYPE_READ_FACDATA_POINT:
            {                   
                    *result = pack[0];
                     printf("dataPointIdpack[0]=%d\n",pack[0]);  
                     printf("msgLengthd=%d\n",msgLength);  
                  // pack += msgLength;
                                      
            }
            break;
            default:
            printf("DOWNLOAD_OTHER_ERROR\n");
            *result = DOWNLOAD_OTHER_ERROR;
            break;
    }
//    printf("return pack + 4;=%c\n",pack[0]); 
//    printf("return pack + 4;=%c\n",pack[3]);    
    return pack + 4;
}
