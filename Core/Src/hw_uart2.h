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

typedef struct
{
    u32 ore_count;
    u32 fe_count;
    u32 ne_count;
    u32 uart_error_count;
    u32 tx_start_fail_count;
    u32 rx_start_fail_count;
    u32 abort_fail_count;
    u32 hal_busy_count;
    u32 hal_timeout_count;
    u32 uart_error_code;
    u32 last_error_code;
    u8 uart_g_state;
    u8 uart_rx_state;
    u8 transfer_state;
    u8 buffer_index;
    u8 rx_length;
} hw_uart2_diag_st;

extern void  hw_bl0942_init(void) ;
extern void hw_bl0942_uart_tx_handle(void);
extern void hw_bl0942_uart_rx_handle(u8 dat);
extern void hw_bl0942_uart_init(void);
extern boolean_en hw_bl0942_uart_read(u8 xdata* buf, u8 length);
extern hw_bl0942_state_en hw_bl0942_get_state(void);

extern boolean_en hw_bl0942_uart_write(u8 xdata* buf, u8 length);
extern boolean_en hw_bl0942_uart_recover(void);
extern u32 hw_uart2_get_error_count(void);
extern boolean_en hw_uart2_get_diag(hw_uart2_diag_st *diag);

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

