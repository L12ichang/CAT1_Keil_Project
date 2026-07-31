#ifndef HW_UART1_H
#define HW_UART1_H

#include "common.h"

typedef struct
{
    u32 rx_byte_count;
    u32 tx_byte_count;
    u32 rx_overflow_count;
    u32 rx_rearm_error_count;
    u32 tx_busy_count;
    u32 tx_error_count;
    u32 ore_count;
    u32 fe_count;
    u32 ne_count;
    u32 pe_count;
    u16 rx_high_watermark;
    u16 tx_high_watermark;
} hw_uart1_stats_st;

extern UART_HandleTypeDef huart1;

extern void hw_uart1_init(void);
extern void hw_uart1_process(void);
extern boolean_en hw_uart1_read_byte(u8 *byte);
extern u16 hw_uart1_available(void);
extern u16 hw_uart1_write(const u8 *buf, u16 length);
extern boolean_en hw_uart1_tx_idle(void);
extern void hw_uart1_get_stats(hw_uart1_stats_st *stats);

/* 由 hw_uart2.c 中全局 HAL 回调转发，保持工程只有一组 HAL weak callback 覆盖。 */
extern void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart);
extern void HAL_UART_Tx1CpltCallback(UART_HandleTypeDef *huart);
extern void HAL_UART_Error1Callback(UART_HandleTypeDef *huart);

/* 阶段 8 删除：旧 Portable/NbDriver 发送接口的非阻塞适配。 */
extern u8 hw_uart1_send_with_result(u8 *buf, u32 length);
extern void hw_uart1_send(u8 *buf, u32 length);
extern void hw_uart1_timer(void);

#endif
