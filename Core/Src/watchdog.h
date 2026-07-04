#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#include "common.h"

typedef struct
{
    u32 main_loop_count;
    u32 main_loop_last_cost_ms;
    u32 main_loop_max_cost_ms;
    u32 tick_lag_count;
    u32 tick_lag_max_ms;
    u32 tick_lag_bad_count;
    u32 mqtt_pub_timeout_count;
    u32 mqtt_pub_timeout_bad_count;
    u32 uart1_queue_drop_snapshot;
    u32 uart1_queue_drop_bad_count;
    u32 bl0942_timeout_count;
    u32 bl0942_timeout_bad_count;
    u32 bl0942_uart_error_count;
    u32 bl0942_uart_error_bad_count;
    u32 watchdog_health_fail_count;
} mcu_runtime_diag_t;

extern void watchdog_feed_dog(void);
extern void watchdog_init(void);
extern void watchdog_loop_begin(void);
extern void watchdog_loop_end(void);
extern void mcu_runtime_diag_process(void);
extern boolean_en mcu_health_is_ok(void);
extern const mcu_runtime_diag_t *mcu_runtime_diag_get(void);


#endif
