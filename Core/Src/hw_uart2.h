#ifndef HW_UART2_H
#define HW_UART2_H

#include "common.h"

/*
 * BL0942 USART2 fault-reproduction instrumentation.
 * Keep disabled for every production build.  The dedicated repro build
 * overrides this value to 1 in its isolated source copy.
 */
#ifndef BL0942_REPRO_TEST_ENABLE
#define BL0942_REPRO_TEST_ENABLE    0
#endif

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

#if BL0942_REPRO_TEST_ENABLE
extern volatile u8  g_bl0942_repro_inject_once;
extern volatile u8  g_bl0942_repro_force_recover;

extern volatile u32 g_bl0942_repro_inject_count;
extern volatile u32 g_bl0942_repro_rx_cplt_count;
extern volatile u32 g_bl0942_repro_dummy_rx_count;
extern volatile u32 g_bl0942_repro_error_count;
extern volatile u32 g_bl0942_repro_ore_count;
extern volatile u32 g_bl0942_repro_fe_count;
extern volatile u32 g_bl0942_repro_ne_count;
extern volatile u32 g_bl0942_repro_last_error;
extern volatile u32 g_bl0942_repro_tx_fail_count;
extern volatile u32 g_bl0942_repro_rx_fail_count;
extern volatile u32 g_bl0942_repro_force_recover_count;

extern void hw_bl0942_repro_force_recover(void);
#endif

extern void hw_uart2_init(void);

extern void hw_uart2_process(void);

extern void hw_uart2_send(u8* buf, u16 length);
extern void hw_uart2_timer(void);

#endif

