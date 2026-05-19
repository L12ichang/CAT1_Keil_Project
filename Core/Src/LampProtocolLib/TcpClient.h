#ifndef TCP_CLIENT_H_
#define TCP_CLIENT_H_
#include "common.h"
/**
*@brief   登陆回调
*@param	  result：0登陆失败；1：登陆成功
*@return  无
*/
void onLogInResponse(uint8 result);

/**
*@brief   Pong消息回调
*@return  无
*/
void onPongMsg(void);

/**
*@brief   NB事件回调，当有连接成功、断开连接等事件时，调用此函数
*@param	  subEvent：具体NB事件
*@param	  pData：NB事件携带的数据，如服务器下发的数据
*@param	  length：数据长度
*@return  无
*/
void onNBEvent(uint8 subEvent, uint8 *pData, uint16 length);

/**
*@brief   获取信号强度
*@return  信号强度：0：信号弱；1：信号中；2：信号强
*/
uint8 getSignal(void);

/**
*@brief   处理tcp客户端任务
*@return  无
*/
void tcpClientProcess(void);

/**
*@brief   发送tcp数据
*@param	  pData：数据缓存
*@param	  length：数据长度
*@return  无
*/
void sendTcpData(uint8 *pData, uint16 length);

/**
*@brief   初始化tcp客户端
*@param	  id：设备id
*@param	  model：设备型号
*@param	  firmware：固件版本号
*@return  无
*/
void tcpClientInit(uint32 id, uint32 model, uint32 firmware);

void mac_reset(void);



void send_MQTT_Data(char *topic,char *pData);
#endif
