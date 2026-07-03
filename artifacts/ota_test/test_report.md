# OTA J-Link + MQTT E2E Test Report

Generated: 2026-07-03T22:12:12+08:00

## Summary

- Final result: PASS for the normal real-device OTA closed loop.
- Branch: `fix/ota-rawtcp-jlink-e2e`
- Base commit before final validation: `44fe628`
- Final OTA fix commit: `912f5fa0ebbbec35164c252d9876241067c166a7`
- MQTT IMEI: `864512081541939`
- Server OTA URL: `http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032`

The device accepted the MQTT OTA command, downloaded the server firmware through the production raw TCP/QIRD path, verified the firmware header/checksum/device type, wrote the upgrade flag, jumped to Boot, Boot copied the OTA image into the APP area, erased the OTA backup area, cleared the upgrade flag, and the new APP came online.

## Project And Partition Facts

- Keil APP project: `MDK-ARM-8008000/project.uvprojx`
- Boot artifact: `boot.bin`
- Target: `program`
- J-Link device selection: `STM32F103RC`
- C/C++ define: `USE_HAL_DRIVER,STM32F103xE,APROM_OFFSET`
- `APROM_OFFSET`: defined
- `APROM_OFFSET_ADDR`: `0x08008000`
- Flash capacity confirmation: J-Link read `0x1FFFF7E0 = 0x0200`, 512 KB
- Boot start: `0x08000000`
- APP start: `0x08008000`
- APP range: `0x08008000..0x08023FFF`
- OTA backup range: `0x08024000..0x0803FFFF`
- Main sys_data: `0x08005000`
- Backup sys_data: `0x08006800`
- Upgrade flag: `sys_data.sn == 0xAA5555AA` in the sys_data pages
- Firmware header: checksum at `0x200`, size at `0x204`, device type at `0x208`
- Expected device type: `0x0003`

## Modified Files

- `Core/Src/LampProtocolLib/ota.c`
- `Core/Src/LampProtocolLib/ota.h`
- `Core/Src/LampProtocolLib/ota_config.h`
- `Core/Src/LampProtocolLib/NbDriver.c`
- `Core/Src/LampProtocolLib/NbDriver.h`
- `Core/Src/LampProtocolLib/Portable.c`
- `Core/Src/LampProtocolLib/Portable.h`
- `Core/Src/common.h`
- `Core/Src/hw_tim4_pwm2.c`
- `Core/Src/hw_uart1.c`
- `Core/Src/hw_uart1.h`
- `MDK-ARM-8008000/project.uvprojx`
- `MDK-ARM-8008000/out/cat1.bin`
- `MDK-ARM-8008000/out/cat1.hex`
- `tests/test_ota_http_download.py`
- `tools/ota_test/pack_ota_firmware.py`
- `tools/ota_test/inspect_ota_bin.py`
- `tools/ota_test/compare_app_header.py`
- `tools/ota_test/mqtt_ota_publish.py`
- `tools/ota_test/jlink_flash.ps1`
- `tools/ota_test/jlink_read_flash.ps1`
- `tools/ota_test/jlink_reset.ps1`
- `artifacts/ota_test/phase1_diagnosis.md`
- `artifacts/ota_test/test_report.md`

## Change Rationale

- Disabled the production `QHTTPREADFILE`/UFS full-file OTA path.
- Added centralized OTA configuration in `ota_config.h`.
- Implemented production raw TCP HTTP GET with `AT+QIRD` small-block reads.
- Removed dependency on HTTP 206, Range, and `Content-Length`.
- Streamed OTA data through a page buffer into the OTA backup partition with bounds checks and readback verification.
- Enforced firmware header validation for checksum, size, and `device_type`.
- On stream verify success, set `sys_data.sn = 0xaa5555aa`, stored sys_data, fed the watchdog, delayed briefly, and jumped to Boot.
- Reduced noisy logs and disabled hex/PWM debug logging in production defaults.
- Fixed APP HardFault by zero-initializing local TIM4 handles before HAL TIM init.
- Added J-Link, MQTT, OTA package, image inspection, and header comparison tools.

## Build Evidence

Command:

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -b 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\MDK-ARM-8008000\project.uvprojx' -j0 -o 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\tools\ota_test\logs\build_app_after_timfix.log'
```

Result:

- Log: `tools/ota_test/logs/build_app_after_timfix.log`
- `0 Error(s), 0 Warning(s)`
- Program size: `Code=82070 RO-data=4926 RW-data=1592 ZI-data=33704`
- Output: `MDK-ARM-8008000/out/cat1.bin`
- Size: `87228`
- Checksum: `0x008936FB`
- Device type: `0x0003`
- SHA256: `D5E8A894A1A802B0F2FC18E29385B7C0909FB338C6AD35751D8596A2029FE8C0`

## Server OTA Package

- Download log: `tools/ota_test/logs/server_headers.txt`
- File: `tools/ota_test/out/server_download.bin`
- HTTP status: `200`
- Transfer: chunked
- `Content-Length`: absent
- Size: `83476`
- Checksum: `0x00836E86`
- Header size: `0x00014614`
- Device type: `0x0003`
- SHA256: `1ACDCAC310C3615E1F8669BDE54C753C8D871F318542B96F730D867DDA4AB06B`

## J-Link Flash And Readback

Key commands:

```powershell
& tools\ota_test\jlink_flash.ps1 -Image boot.bin -Address 0x08000000
& tools\ota_test\jlink_flash.ps1 -Image MDK-ARM-8008000\out\cat1.bin -Address 0x08008000
& tools\ota_test\jlink_read_flash.ps1
```

Important logs:

- Flash-size probe: `tools/ota_test/logs/jlink_probe_flash_size.log`
- Baseline APP flash after TIM fix: `tools/ota_test/logs/jlink_flash_20260703_220403.log`
- APP running/no HardFault: `tools/ota_test/logs/jlink_regs_after_timfix_20260703_220429.log`
- Final OTA readback: `tools/ota_test/logs/jlink_read_flash_20260703_220918.log`

Readback after MQTT OTA:

- APP header file: `tools/ota_test/logs/app_head_20260703_220918.bin`
- OTA backup header file: `tools/ota_test/logs/ota_backup_head_20260703_220918.bin`
- sys_data head file: `tools/ota_test/logs/data_head_20260703_220918.bin`
- APP header matches server OTA: checksum `0x00836E86`, size `0x00014614`, device type `0x0003`
- APP header no longer matches pre-OTA local image: local checksum `0x008936FB`, local size `0x000154BC`
- OTA backup header is erased: `0xFFFFFFFF` fields
- sys_data first word is `0x00000000`, so the upgrade flag was cleared

Note: APP must be burned as `cat1.bin` at `0x08008000`; the `.hex` still contains placeholder metadata before the post-build binary patch.

## MQTT Evidence

Publish topic:

```text
MS/864512081541939/pcp2dev
```

Subscribed topics:

```text
MS/864512081541939/dev2pcp
MS/864512081541939/dev2plt
MS/864512081541939/offline
```

Payload:

```json
{"SN":"864512081541939","TM":"2026-07-03 22:06:35","SV":"ota","ID":"87595","CT":"W","DT":{"url":"http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032"}}
```

Result log: `tools/ota_test/logs/mqtt_ota_20260703_220635.jsonl`

- `22:06:35`: connected and published
- `22:06:40`: device OTA ack `SV=ota`, `CT=W`, `ER=0`, `ID=87595`
- `22:06:48`: device online login `SV=rept`, `CT=L`, `sver=1`
- `22:07:50`: heartbeat `CT=H`
- `22:08:51`: heartbeat `CT=H`

## Serial Log

- sscom5.13.1 was running and holding the device COM port.
- Codex did not capture a separate serial text file from that GUI session.
- Automated evidence for this run is from MQTT JSONL logs and J-Link flash/register/readback logs.

## Test Matrix

| Test | Result | Evidence |
| --- | --- | --- |
| Keil build | PASS | `build_app_after_timfix.log` |
| Flash capacity sufficient | PASS | J-Link `0x1FFFF7E0 = 0x0200` |
| Baseline boot + APP online | PASS | MQTT online at `22:05:47`, APP no HardFault after TIM fix |
| Normal MQTT OTA | PASS | OTA ack `ER=0`, APP readback matches server package |
| No `Content-Length` dependency | PASS | Server response chunked/no `Content-Length`, OTA succeeded |
| No HTTP 206/Range dependency | PASS | Server response HTTP 200, OTA succeeded |
| QHTTPREADFILE/UFS disabled | PASS | Static config/tests, production macro `OTA_USE_QHTTPREADFILE_UFS 0U` |
| Header checksum validation | PASS static/unit | `tests.test_ota_http_download` |
| Header size validation | PASS static/unit | `tests.test_ota_http_download` |
| Device type validation | PASS static/unit | `tests.test_ota_http_download`, server type `0x0003` |
| APP/OTA capacity bounds | PASS static/unit | `check_keil_app_image.py`, OTA tests |
| Boot copy into APP | PASS | APP readback equals server package |
| Boot cleanup | PASS | OTA backup erased and sys_data flag cleared |
| Post-upgrade online | PASS | MQTT login and heartbeats after OTA |
| Watchdog stability | PASS observed | No watchdog reset loop; device remained online after OTA |
| Network interruption fault injection | NOT LIVE-INJECTED | No live interruption test was run in this pass |
| Download reset fault injection | NOT LIVE-INJECTED | No live reset-during-download test was run in this pass |

## Validation Commands

```powershell
python -m unittest tests.test_ota_http_download
python -m unittest tests.test_keil_app_image_check tests.test_ota_http_download
python tools\check_keil_app_image.py --skip-freshness --json
python tools\ota_test\inspect_ota_bin.py tools\ota_test\out\server_download.bin
python tools\ota_test\compare_app_header.py --ota tools\ota_test\out\server_download.bin --flash tools\ota_test\logs\app_head_20260703_220918.bin
python -m py_compile tools\ota_test\pack_ota_firmware.py tools\ota_test\inspect_ota_bin.py tools\ota_test\compare_app_header.py tools\ota_test\mqtt_ota_publish.py
```

Results:

- `tests.test_ota_http_download`: PASS, 7 tests
- `tests.test_keil_app_image_check tests.test_ota_http_download`: PASS, 9 tests
- `check_keil_app_image.py`: PASS
- Header compare against server package: PASS
- Tool py_compile: PASS

## Known Risks

- Bootloader source is not in this workspace, so Boot behavior was verified through `boot.bin`, J-Link readback, and MQTT online behavior rather than source review.
- The live run validated the normal success path against the real server package. Some negative-path cases are covered by static/unit tests rather than hardware fault injection.
- The repo had untracked/generated artifacts after validation, including binary logs and pycache files. They are intentionally not all part of the source commit.

## Rollback Plan

1. Reflash the previous known-good Boot/App pair or the tracked baseline APP binary with J-Link.
2. Burn APP binaries at `0x08008000` using `.bin`, not `.hex`, when relying on post-build metadata.
3. Clear or seed sys_data pages at `0x08005000` and `0x08006800` according to whether Boot should jump to APP immediately.
4. Verify APP header at `0x08008000 + 0x200` and confirm MQTT online login before sending OTA again.

## Final Completion Standard

- Real-device normal OTA success: YES.
- Final APP header equals OTA package: YES.
- Device came online after Boot copy: YES.
- `test_report.md` generated: YES.
- All requested live negative fault-injection cases: NO, only static/unit coverage for those cases in this pass.
