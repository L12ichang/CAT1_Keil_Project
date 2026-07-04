# Static Issue Report

Date: 2026-07-04
Branch: `fix/mcu-timing-mqtt-ota-watchdog`

## Findings

- Boot source is not present in this workspace. Boot behavior was verified from `boot.bin`, J-Link flash/readback, vector table handoff, and sys_data flag clearing.
- APP, OTA backup, and sys_data partition contracts are consistent with the checked source constants:
  - APP base: `0x08008000`
  - APP safe end: `0x08024000`
  - OTA backup base: `0x08024000`
  - main sys_data: `0x08005000`
  - backup sys_data: `0x08006800`
- Final APP image passes `tools/check_keil_app_image.py --skip-freshness --json`.
- Flash parameter records are now compile-time guarded against overlap:
  - property record must fit within `ZK_RUNTIME_FLASH_OFFSET`
  - runtime record must fit before `FLASH_PAGE_SIZE`
- MQTT publish receive ownership was the live reliability issue found during validation. `send_AT_Command_machine()` and `nbModuleProcess()` could drain the shared UART queue during a `QMTPUBEX` publish window. The final patch makes both generic receive paths yield while publish is in header, prompt, payload, or ack state.
- Generic coding-standard conflict noted: the generic C guideline asks watchdog feed at the top of `while(1)`, but the task objective requires health-gated feed after loop work. The task-specific requirement was followed.

## Non-Fixes

- No Boot source refactor was possible.
- No live destructive OTA download from the existing server URL was run after the final image, because the available server package is not this final build and would overwrite it with an older image.
