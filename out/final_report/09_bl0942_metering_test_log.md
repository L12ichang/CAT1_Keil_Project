# BL0942 Metering Test Log

## Code Changes

- USART2 error callback now:
  - increments a UART2 error counter,
  - clears ORE/FE/NE flags,
  - aborts and restarts receive,
  - releases the BL0942 receive state back to idle.
- BL0942 now tracks:
  - checksum errors,
  - frame timeouts,
  - UART errors.
- Watchdog observes BL0942 timeout/UART error trends but does not reset on isolated or non-growing conditions.

## Final Runtime Evidence

Log: `out/final_report/jlink_final_runtime_uart_owner_20260704_125034.log`

Raw BL0942 counters:

- `bl0942_checksum_error_count=0`
- `bl0942_timeout_count=0x199`
- `bl0942_uart_error_count=0`

Watchdog diagnostic view:

- `bl0942_timeout_count=0x199`
- `bl0942_timeout_bad_count=0`
- `bl0942_uart_error_count=0`
- `bl0942_uart_error_bad_count=0`

MQTT runtime report evidence:

- `out/final_report/mqtt_patrol_utc_time_restore_20260704_124958.jsonl`
- Report `ID=000011`, `SV=rept`, `CT=C`
- `EleInfo` fields were published.
- Test bench reported zero voltage/current/power and an under-voltage alarm earlier, consistent with no live AC metering input during this run.

Result: recovery counters and publish path are working; no UART2 errors or checksum errors were observed on the bench. BL0942 timeouts were observed but did not trip watchdog bad-count logic.
