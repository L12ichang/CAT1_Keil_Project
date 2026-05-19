#ifndef HW_UART1_H
#define HW_UART1_H

#include "common.h"

void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_Tx1CpltCallback(UART_HandleTypeDef *huart);
extern void hw_uart1_init(void);

extern void hw_uart1_process(void);

extern void hw_uart1_send(u8* buf, u32 length);
extern void hw_uart1_timer(void);

#endif

