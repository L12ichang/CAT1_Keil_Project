# Final Acceptance Checklist

## Build

- [x] Keil APP build completed with 0 errors and 0 warnings.
- [x] Final binary size is within APP partition.
- [x] Final image checker passed.
- [x] Final vector SP remains Boot-compatible.

## Flash And Boot

- [x] Boot artifact verified earlier against on-chip Boot.
- [x] Final APP programmed to OTA backup and verified.
- [x] sys_data upgrade flag programmed and verified in both data pages.
- [x] Boot cleared upgrade flag.
- [x] APP is running with `VTOR=0x08008000`.
- [x] APP header readback matches final binary.

## MQTT

- [x] Device login publish observed.
- [x] Time request publish observed.
- [x] Heartbeat publishes observed.
- [x] Inbound command on `plt2dev` observed by device.
- [x] Simple command response `ER=0` observed.
- [x] Patrol cyclic report observed.
- [x] Final publish counters: success `13`, fail `0`, timeout `0`.

## Timing And Watchdog

- [x] 10 ms tick catch-up bounded.
- [x] Watchdog feed health-gated.
- [x] Final runtime fault status is clear.
- [x] Final watchdog health fail count is 0.

## Metering

- [x] BL0942 timeout/UART error recovery code added.
- [x] BL0942 counters exposed to watchdog diagnostics.
- [x] No UART2 errors observed in final runtime snapshot.
- [x] No BL0942 checksum errors observed in final runtime snapshot.

## Limitations

- [ ] Full server-hosted MQTT OTA of this exact final binary was not run because no final package URL was available.
- [ ] Boot source could not be reviewed because only `boot.bin` is present.

Final status: PASS for build, flash, Boot handoff, MQTT pub/sub, watchdog health, and metering recovery instrumentation on real hardware. Server-hosted OTA of this exact final binary remains externally blocked by missing package URL.
