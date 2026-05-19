#ifndef CAT1_GATEWAY_H
#define CAT1_GATEWAY_H

#include "common.h"
#include "hw_gateway.h"
#define MSG_TYPE_INVALID       0x00
#define MSG_TYPE_LOGIN_RESPONSE 0x01
#define MSG_TYPE_PONG 0x02
#define MSG_TYPE_DOWNLOAD_DATA 0x03
#define MSG_TYPE_DOWNLOAD_FIRMWARE 0x04
#define MSG_TYPE_READ_DATA_POINT 0x05

#define MSG_TYPE_LOGIN_REQUEST 0x81
#define MSG_TYPE_PING 0x82
#define MSG_TYPE_DOWNLOAD_RESPONSE 0x83
#define MSG_TYPE_UPLOAD_DATA 0x84
#define MSG_TYPE_REQUEST_CONFIG 0x85
#define MSG_TYPE_READ_RESPONSE 0x84

typedef enum 
{
  GATEWAY_STATE_LGIDLE=0,
  GATEWAY_STATE_LOGING,
  GATEWAY_STATE_WAIT_REP,
  GATEWAY_STATE_LOGIN_SUCCES,
  GATEWAY_STATE_PING,
}GATEWAY_STATE_LOGIN;

//extern void sys_gateway_process(void);
//extern void sys_gateway_timer();


//
#endif
