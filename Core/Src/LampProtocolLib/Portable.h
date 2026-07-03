#ifndef PORTABLE_H_
#define PORTABLE_H_
typedef unsigned char     uint8;
typedef unsigned short    uint16;
typedef unsigned long     uint32;
typedef   signed char     int8;
typedef   signed short    int16;
typedef   signed long     int32;
#include "type.h"

void powerUpNbModule(void);
void resetNbModule(void);
void delayMs(uint32 ms);
uint32 Timer_GetTickCount(void);
uint32 Timer_PassedMs(uint32 start, uint32 end);
uint8 Timer_PassedDelay(uint32 startTime, uint32 msDelay);
void updateTimeTick(uint32 ms);
uint8 usartSendDataWithResult(uint8 *pBuf, uint16 length);
void usartSendData(uint8 *pBuf, uint16 length);
void saveUsartByte(uint8 byte);
void portableInit(void);
extern volatile uint32 usart_queue_drop_count;
boolean_en  _4g_reset_finish(void);
void _4g_reset_idle(void) ;
#endif
