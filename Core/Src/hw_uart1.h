#ifndef HW_UART1_H
#define HW_UART1_H

#include "common.h"

void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_Tx1CpltCallback(UART_HandleTypeDef *huart);
extern void hw_uart1_init(void);

extern void hw_uart1_process(void);

extern u8 hw_uart1_send_with_result(u8* buf, u32 length);
extern void hw_uart1_send(u8* buf, u32 length);
extern void hw_uart1_resume_rx(void);

#endif

