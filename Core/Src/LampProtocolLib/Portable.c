
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

#define NB_PWRKEY_BOOT_PULSE_MS 550UL
#define NB_RESET_ASSERT_MS      320UL
#define NB_RESET_TO_PWRKEY_MS    20UL

void powerUpNbModule(void)
{
    POW_ON();
    delayMs(NB_PWRKEY_BOOT_PULSE_MS);
    POW_OFF();
}

//++++++++++++++++重启特殊处理+++++++++++++++++++AT+QPOWD
#define  IDLE_delay    0
#define  STAR_delay    1
#define  ON_delay      2
#define  OFF_delay     3
#define  OVER_delay    4
#define  FINISH_delay  5
#define  HARD_RESET_delay 6
#define  HARD_PWRKEY_delay 7

u8  _state_reset=IDLE_delay;
static u32  timer;
static u32  reset_timer;
void resetNbModule(void)
{
     _state_reset=STAR_delay;
}
void hardResetNbModule(void)
{
     _state_reset=HARD_RESET_delay;
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
        printf("[BOOT] PWRKEY assert pulse=%lums\r\n", NB_PWRKEY_BOOT_PULSE_MS);
        timer = Timer_GetTickCount();
        _state_reset=ON_delay;
    }
    else if (_state_reset==HARD_RESET_delay)
    {
        /* EC800E hardware reset sequence: assert RESET_N first, then PWRKEY. */
        reset_on();
        reset_timer = Timer_GetTickCount();
        timer = reset_timer;
        printf("[BOOT] hardware reset RESET_N assert\r\n");
        _state_reset=HARD_PWRKEY_delay;
    }
    else if (_state_reset==HARD_PWRKEY_delay)
    {
        if (Timer_PassedDelay(timer, NB_RESET_TO_PWRKEY_MS))
        {
            pwr_on();
            timer = Timer_GetTickCount();
            _state_reset=OVER_delay;
        }
    }
    else if( _state_reset==ON_delay)
            {
                 if (Timer_PassedDelay(timer, NB_PWRKEY_BOOT_PULSE_MS))
                  {
                     pwr_off();
                     printf("[BOOT] PWRKEY release\r\n");
                     _state_reset=FINISH_delay;
                  }
             }
    else if (_state_reset==OVER_delay)
    {
        if (Timer_PassedDelay(reset_timer, NB_RESET_ASSERT_MS))
        {
            reset_off();
        }
        if (Timer_PassedDelay(timer, NB_PWRKEY_BOOT_PULSE_MS))
        {
            pwr_off();
            reset_off();
            printf("[BOOT] hardware reset release RESET_N=%lums PWRKEY=%lums\r\n",
                   NB_RESET_ASSERT_MS,
                   NB_PWRKEY_BOOT_PULSE_MS);
            _state_reset=FINISH_delay;
        }
    }

}

#define UART_RECV_QUEUE_SIZE 4096
 uint8 queueBuf[UART_RECV_QUEUE_SIZE + 1];
QUEUE  usartRecvQueue;
 uint8 uartSendingFlag = 0;
uint8 CREG_common[11]="AT+CREG?\r\n";
volatile uint32 usart_queue_drop_count = 0;

void usartSendData(uint8 *pBuf, uint16 length)
{
   UART_SEND(pBuf, length);
}

uint8 usartSendDataWithResult(uint8 *pBuf, uint16 length)
{
   return hw_uart1_send_with_result(pBuf, length);
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

/* 阶段 2 删除：AT 引擎迁移后移除 delayMs() 忙等及其中断计时依赖。 */
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
        if (timeDelay <= ms)
        {
            timeDelay = 0;
        }
        else
        {
            timeDelay -= ms;
        }
    }
}

void portableInit(void)
{
    createQueue(&usartRecvQueue, UART_RECV_QUEUE_SIZE + 1, queueBuf);
}
