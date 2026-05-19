#ifndef COMMON_H
#define COMMON_H

#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"
#include "type.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "mylib.h"
#include "u32_q.h"

#define DEBUG

#define _4G_CAT_1           //否则为4G网关

#define HARDWARE_VERSION_1  1
#define HARDWARE_VERSION_2  2

#define HARDWARE_VERSION    HARDWARE_VERSION_2  //1是初版，2是5月底的板

/*
ver 2. 修正了命令bug，原因是dali回来的数据没有crc检验。支持iap的同步头脉冲 。
ver 3. 2022.12.7 dali时序兼容不了新唐，改回来。
*/
#define     APP_VERSION         (u16)1

#define xdata



#ifdef  APROM_OFFSET
#define APROM_OFFSET_ADDR        0x08008000   //  //#define APROM_OFFSET_ADDR       0x08003000   //注意修改 prog_checksum prog_length device_type
#else
#define APROM_OFFSET_ADDR       0x08000000   //#define APROM_OFFSET_ADDR       0x08000000   //注意修改 prog_checksum prog_length device_type

#endif
#define BOOT_OFFSET_ADDR       0x08000000   //注意修改 prog_checksum prog_length device_type

#define SYS_BASE_FREQUENCY_HZ    72000000  // 系统的主频       72M
#define RX_BUFF_LENGTH          540
#define TX_BUFF_LENGTH          540
#define printf  dma_printf


#if HARDWARE_VERSION == HARDWARE_VERSION_1
#define RELAY_TO_485()     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET)
#define RELAY_TO_DIM()     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET)

#elif HARDWARE_VERSION == HARDWARE_VERSION_2
#define RELAY_TO_485()     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET)
#define RELAY_TO_DIM()     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET)
#endif


#define RS485_TX_ENABLE()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET)
#define RS485_TX_DISABLE()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET)



//#define COMMUNICATION_DEBUG

typedef enum
{
    WHOLE_COMM_STATE_IDLE,        //0
    WHOLE_COMM_STATE_UART_RXING,  //1
    WHOLE_COMM_STATE_DALI_TXING,  //2
    WHOLE_COMM_STATE_DALI_TX_COMPLETE,//3
    WHOLE_COMM_STATE_DALI_RXING,  //4
    WHOLE_COMM_STATE_DALI_RX_COMPLETE,//5
    WHOLE_COMM_STATE_UART_TXING,  //6
    WHOLE_COMM_STATE_IAP_HEADER,  //7
    WHOLE_COMM_STATE_IAP_HEADER_COMPLETE, //8
}whole_comm_state_en;

extern whole_comm_state_en whole_comm_state;
extern u8 rx_buffer[RX_BUFF_LENGTH];
extern u8 temp_buf_byte[FLASH_PAGE_SIZE];

extern void main_timer(void);
extern void iotx_byte(u8 dat);
//extern void delay_us(uint32_t dly);
extern void printf_buf(u8* buf, u16 length);
extern void printf_buf_u16_dec(u16* buf, u16 length);
extern int dma_printf(const char* format, ...);
extern void Error_Handler(void);
extern void printf_buf2(const char* str, u8* buf, u16 length);

extern u8 debug;
extern u32 debug32;
extern u32 debug32b;
extern u8 kkk;
extern u8 debug8;
extern u8 debug8b;


#endif
