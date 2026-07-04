#ifndef HW_UART2_H
#define HW_UART2_H

#include "common.h"

typedef enum
{
    BL0942_STATE_IDLE,
    BL0942_STATE_WRITE,
    BL0942_STATE_READ_TX,
    BL0942_STATE_READ_RX,
    BL0942_STATE_READ_READY,
}hw_bl0942_state_en;
extern void  hw_bl0942_init(void) ;
extern void hw_bl0942_uart_tx_handle(void);
extern void hw_bl0942_uart_rx_handle(u8 dat);
extern void hw_bl0942_uart_init(void);
extern void hw_bl0942_uart_read(u8 xdata* buf, u8 length);
extern hw_bl0942_state_en hw_bl0942_get_state(void);

extern void hw_bl0942_uart_write(u8 xdata* buf, u8 length);
extern u32 hw_uart2_get_error_count(void);

extern void hw_uart2_init(void);

extern void hw_uart2_process(void);

extern void hw_uart2_send(u8* buf, u16 length);
extern void hw_uart2_timer(void);

#endif

