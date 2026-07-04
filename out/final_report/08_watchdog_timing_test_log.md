# Watchdog And Timing Test Log

## Code Changes

- `sys_tick_process()` now performs bounded catch-up with `SYS_TICK_MAX_CATCH_UP=3`.
- Tick lag count and max lag are tracked.
- Watchdog feed is health-gated at loop end.
- Runtime health includes:
  - main-loop cost
  - tick lag trend
  - MQTT publish timeout trend
  - UART1 queue drops
  - BL0942 timeout and UART error trends

## Final Runtime Evidence

Log: `out/final_report/jlink_final_runtime_uart_owner_20260704_125034.log`

Decoded `mcu_runtime_diag`:

- `main_loop_count=0x00844F04`
- `main_loop_last_cost_ms=0`
- `main_loop_max_cost_ms=3`
- `tick_lag_count=1`
- `tick_lag_max_ms=0x329`
- `tick_lag_bad_count=0`
- `mqtt_pub_timeout_count=0`
- `mqtt_pub_timeout_bad_count=0`
- `uart1_queue_drop_snapshot=0`
- `uart1_queue_drop_bad_count=0`
- `bl0942_timeout_count=0x199`
- `bl0942_timeout_bad_count=0`
- `bl0942_uart_error_count=0`
- `bl0942_uart_error_bad_count=0`
- `watchdog_health_fail_count=0`

Core state:

- PC in APP, no exception.
- `VTOR=0x08008000`.
- `CFSR=0x00000000`.

Result: watchdog health gate stayed healthy during MQTT login, heartbeats, command ack, and patrol reports.
