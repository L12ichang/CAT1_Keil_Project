# Phase 1 OTA Diagnosis

Generated: 2026-07-03T22:12:12+08:00

## Scope

- Branch: `fix/ota-rawtcp-jlink-e2e`
- Base commit at start of final validation: `44fe628`
- Keil APP project: `MDK-ARM-8008000/project.uvprojx`
- Boot artifact: `boot.bin`
- Target: `program`
- Connected MCU/J-Link selection: `STM32F103RC`
- Keil define: `USE_HAL_DRIVER,STM32F103xE,APROM_OFFSET`
- `APROM_OFFSET` is defined; `APROM_OFFSET_ADDR` resolves to `0x08008000`.

## Required Production OTA Path

- Production download path is raw TCP + HTTP GET + `AT+QIRD`.
- Module `QHTTPREADFILE`/UFS full-file caching is disabled by default with `OTA_USE_QHTTPREADFILE_UFS 0U`.
- No HTTP Range/206 dependency is used.
- No `Content-Length` dependency is required. The verified server response was HTTP 200, chunked transfer, with no `Content-Length`.
- HTTP GET uses `Accept-Encoding: identity` and `Connection: close`.
- Debug-only paths are disabled:
  - `OTA_DEBUG_DOWNLOAD_ONLY 0U`
  - `OTA_STREAM_TO_BACKUP_DEBUG 0U`
  - `OTA_STREAM_ALLOW_RAW_BIN_TEST 0U`
  - `OTA_DEBUG_CLEAR_ALL_UFS 0U`

## Flash Layout

- Boot: `0x08000000`
- Data/main sys_data: `0x08005000`
- Backup sys_data: `0x08006800`
- APP: `0x08008000..0x08023FFF`
- OTA backup: `0x08024000..0x0803FFFF`
- APP max size: `0x1C000` bytes
- OTA backup capacity: `0x1C000` bytes

## Firmware Header Contract

- Checksum offset: `0x200`
- Size offset: `0x204`
- Device type offset: `0x208`
- Expected `device_type`: `0x0003`
- Placeholder checksum `0x12345678` and size `0x89ABCDEF` are rejected.
- Size must be nonzero, 4-byte aligned, and within APP/OTA backup capacity.

## Final Build Evidence

- Keil command log: `tools/ota_test/logs/build_app_after_timfix.log`
- Result: `0 Error(s), 0 Warning(s)`
- Program size: `Code=82070 RO-data=4926 RW-data=1592 ZI-data=33704`
- Output image: `MDK-ARM-8008000/out/cat1.bin`
- Output size: `87228` bytes
- Output checksum: `0x008936FB`
- Output device type: `0x0003`
- Output SHA256: `D5E8A894A1A802B0F2FC18E29385B7C0909FB338C6AD35751D8596A2029FE8C0`

## Local OTA Package

- Pack command output: `tools/ota_test/out/cat1.bin`
- Info file: `tools/ota_test/out/cat1_ota_info.json`
- Size: `87228`
- Checksum: `0x008936FB`
- Device type: `0x0003`
- SHA256: `D5E8A894A1A802B0F2FC18E29385B7C0909FB338C6AD35751D8596A2029FE8C0`
- `inspect_ota_bin.py` result: valid

## Server OTA URL Check

- URL: `http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032`
- Header log: `tools/ota_test/logs/server_headers.txt`
- Downloaded file: `tools/ota_test/out/server_download.bin`
- HTTP result: `200`
- Transfer mode: `Transfer-Encoding: chunked`
- `Content-Length`: absent
- Download size: `83476`
- Header checksum: `0x00836E86`
- Header size: `0x00014614`
- Device type: `0x0003`
- SHA256: `1ACDCAC310C3615E1F8669BDE54C753C8D871F318542B96F730D867DDA4AB06B`
- `inspect_ota_bin.py` result: valid

## J-Link Probe

- Log: `tools/ota_test/logs/jlink_probe_flash_size.log`
- Connection: SWD OK
- Core: Cortex-M3
- Flash-size register `0x1FFFF7E0`: `0x0200` = 512 KB
- Conclusion: the `0x08000000..0x0803FFFF` layout is safe on the connected board.

## Hardware Debug Findings

- Boot did not jump to APP while `sys_data.sn` was blank. A valid jump flag image was generated and flashed to both data pages for baseline APP bring-up.
- Baseline APP initially HardFaulted after boot jump. J-Link stack/register evidence showed stacked PC `0x200089A8`, LR near `HAL_TIM_Base_Init`, and CFSR `0x00020000`.
- Root cause: `hw_tim4_pwm2_init()` used an uninitialized local `TIM_HandleTypeDef` while `USE_HAL_TIM_REGISTER_CALLBACKS` is `1U`.
- Fix: initialize both local `TIM_HandleTypeDef htim4` declarations with `{0}`.
- Post-fix register check: `tools/ota_test/logs/jlink_regs_after_timfix_20260703_220429.log`, PC `0x0800C5B0`, IPSR `0`, no HardFault.

## Static Test Evidence

- `python -m unittest tests.test_ota_http_download`: PASS, 7 tests.
- `python -m unittest tests.test_keil_app_image_check tests.test_ota_http_download`: PASS, 9 tests.
- `python tools/check_keil_app_image.py --skip-freshness --json`: PASS.
- `python -m py_compile tools/ota_test/pack_ota_firmware.py tools/ota_test/inspect_ota_bin.py tools/ota_test/compare_app_header.py tools/ota_test/mqtt_ota_publish.py`: PASS.

## Live OTA Closure Evidence

- MQTT command log: `tools/ota_test/logs/mqtt_ota_20260703_220635.jsonl`
- Publish topic: `MS/864512081541939/pcp2dev`
- Subscribed topics: `MS/864512081541939/dev2pcp`, `MS/864512081541939/dev2plt`, `MS/864512081541939/offline`
- OTA ack: `SV=ota`, `CT=W`, `ER=0`, `ID=87595` at `2026-07-03 22:06:40`.
- Post-upgrade online login: `SV=rept`, `CT=L`, `sver=1` at `2026-07-03 22:06:48`.
- Heartbeats observed after upgrade: `CT=H` at `22:07:50` and `22:08:51`.
- Final J-Link flash read: `tools/ota_test/logs/jlink_read_flash_20260703_220918.log`.
- APP header readback matches server OTA package:
  - checksum `0x00836E86`
  - size `0x00014614`
  - device type `0x0003`
- OTA backup header is erased (`0xFFFFFFFF` fields), consistent with Boot cleanup after copy.
- Upgrade flag page begins with `0x00000000`, consistent with Boot clearing the jump flag.

## Environment Notes

- MQTTX and sscom were already running. Automated MQTT/J-Link logs were captured. The serial GUI held the COM port, so no separate automated serial-log file was captured from Codex.
- `python -m unittest discover tests` is not a clean Windows signal for this repo because at least one helper imports Unix-only `fcntl`, and some MQTT scripts are not unittest modules. The targeted OTA tests above pass.

## Status

- Normal real-device OTA closed loop: PASS.
- Fault injection cases: covered by static/unit checks for bad checksum, bad size, bad device type, capacity bounds, QHTTPREADFILE disabled path, and image-contract validation; not all were live hardware-injected in this run.
