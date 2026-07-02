
#include "Portable.h"
#include "Queue.h"
#include "Utils.h"
#include "NbDriver.h"
#include "FirmwareUpdater.h"
#include "hw_uart1.h"   //调佣外部文件
#include "hw_4g_io.h"

#define   POW_ON()    pwr_on()
#define   POW_OFF()   pwr_off()
#define   UART_SEND(buf,size)    hw_uart1_send( buf, size);
void powerUpNbModule(void) 
{
    POW_ON();
    delayMs(750);
    POW_OFF();
}

//++++++++++++++++重启特殊处理+++++++++++++++++++AT+QPOWD
#define  IDLE_delay    0
#define  STAR_delay    1
#define  ON_delay      2
#define  OFF_delay     3
#define  OVER_delay    4
#define  FINISH_delay  5

u8  _state_reset=IDLE_delay;
static u32  timer;
void resetNbModule(void) 
{
     _state_reset=STAR_delay;  
}
void  _4g_reset_idle(void) 
{
  _state_reset=IDLE_delay ;

}
boolean_en  _4g_reset_finish(void)
{
      if( _state_reset==FINISH_delay)
      {
          return BOOL_TRUE;
      }
      else
      {
          return BOOL_FALSE; 
      }  
}

void resetNbModule_machine(void)
{
    if( _state_reset==STAR_delay)
    {
        pwr_on();
        printf("4G_POWEON---------------\r\n");
        timer = Timer_GetTickCount();
        _state_reset=ON_delay;
    }
     else if( _state_reset==ON_delay)
            {
                 if (Timer_PassedDelay(timer, 1000))
                  {
                     pwr_off();
                     printf("4G_POWEOFF-RESET--------\r\n");
                     _state_reset=OFF_delay;
                     timer = Timer_GetTickCount();
                 }
             }
             else if( _state_reset==OFF_delay)
                    {
                           if (Timer_PassedDelay(timer, 5000))
                          {  printf("4G_POWEOFF_delay_FINISH_delay\r\n");
                             _state_reset=FINISH_delay;
                          }
                     }

}

#define UART_RECV_QUEUE_SIZE 2048
 uint8 queueBuf[UART_RECV_QUEUE_SIZE + 1];
QUEUE  usartRecvQueue;
 uint8 uartSendingFlag = 0;
uint8 CREG_common[11]="AT+CREG?\r\n";
volatile uint32 usart_queue_drop_count = 0;

void usartSendData(uint8 *pBuf, uint16 length)
{  
   UART_SEND(pBuf, length);
}

void saveUsartByte(uint8 byte)
{
    if(enqueue(&usartRecvQueue, byte) == 0)
    {
        usart_queue_drop_count++;
    }
}

//system time
static  volatile uint32 TickCount = 0;
static  volatile uint32 timeDelay = 0;

void delayMs(uint32 ms) 
{
    timeDelay = ms;
    while (timeDelay);
}

uint32 Timer_GetTickCount(void) 
{
    return TickCount;
}
//运行到设定延时时间返回1
uint8 Timer_PassedDelay(uint32 startTime, uint32 msDelay) 
{  
     uint32 stoptime = startTime + msDelay;  //到点时间
     uint32 curtime = Timer_GetTickCount();  //获取当前的时间点
    if (stoptime >= startTime) 
    {// 停止的时间大于起始时间
        if ((curtime >= stoptime) || (curtime < startTime)) //当前的时间大于停止时间 小于起始时间
            return 1;
        else
            return 0;
    }
    else 
    {//停止时间小于起始时间
        if ((curtime > stoptime) && (curtime < startTime))//
            return 1;
        else
            return 0;
    }
}

void updateTimeTick(uint32 ms) 
{   
    TickCount += ms;
    if (timeDelay > 0) 
    {
        timeDelay -= 10;
    }
}

void portableInit(void)
{
    createQueue(&usartRecvQueue, UART_RECV_QUEUE_SIZE + 1, queueBuf);
}
