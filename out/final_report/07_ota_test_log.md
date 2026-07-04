# OTA Test Log

## What Was Verified Live

- Final APP was written to OTA backup at `0x08024000`.
- Main and backup sys_data pages were seeded with the Boot upgrade flag `0xAA5555AA`.
- Boot copied or accepted the OTA backup image and cleared the sys_data flag.
- APP booted with `VTOR=0x08008000`.
- Final APP and OTA backup headers both match the rebuilt binary:
  - checksum `0x0083CCD1`
  - size `0x000145A4`
  - device type `0x0003`
  - SP `0x200089A8`
  - reset `0x08008145`

Evidence:

- Seed/verify log: `out/final_report/jlink_seed_final_ota_and_flag_uart_owner_gateway_20260704_124325.log`
- Final header readback: `out/final_report/final_header_compare_20260704_125253.txt`
- Runtime proof after Boot handoff: `out/final_report/jlink_final_runtime_uart_owner_20260704_125034.log`
- MQTT online proof after Boot handoff: `out/final_report/mqtt_passive_unique_after_uart_owner_20260704_124358.jsonl`

## What Was Not Re-Run

- A destructive live MQTT OTA download from the existing production URL was not run after the final image. The known server URL points to an older package, not this final build, so using it would replace the fixed firmware.
- OTA raw TCP download logic remains covered by source review and host tests in `tests.test_ota_http_download`.

## Result

- Real Boot-to-APP OTA handoff path for the final image: PASS.
- Final firmware stays online after Boot handoff: PASS.
- Full server-hosted OTA of this exact final binary: NOT RUN because no final package URL was available.
