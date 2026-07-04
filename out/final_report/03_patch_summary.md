# Patch Summary

## MCU Timing And Watchdog

- Added bounded 10 ms tick catch-up and lag counters in `sys_tick`.
- Moved watchdog feed to health-gated loop end.
- Added runtime diagnostics for main-loop cost, tick lag, MQTT publish timeouts, UART drops, and BL0942 errors.

## MQTT Publish/Subscribe

- Converted `send_AT_Command_machine_star()` wait count to `uint32`.
- Added long QMTOPEN/QMTCONN/QMTSUB timeouts.
- Reworked MQTT publish into explicit `QMTPUBEX` states: header, prompt, payload, ack, finish, fail.
- Added publish counters and exposed timeout count to watchdog health.
- Fixed live UART ownership race:
  - `send_AT_Command_machine()` yields while publish owns the UART.
  - `nbModuleProcess()` yields while publish owns the UART.
- Replaced single pending simple-response slot with a two-entry FIFO.

## OTA/Flash Safety

- Added compile-time guards for sys_data-adjacent ZK property/runtime flash records.
- Verified final APP header/checksum/device type in APP and OTA backup by J-Link readback.

## Metering And UART2

- Added USART2 error recovery callback for BL0942 receive path.
- Added BL0942 checksum, timeout, and UART error counters.
- Watchdog observes BL0942 timeout/UART error trends without unconditional reset.

## Logging And Size

- Default APP logs disabled; OTA logs remain available.
- DMA print path is nonblocking and buffer size reduced to preserve Boot-compatible RAM top.
- PWM debug print is compile-time gated.
