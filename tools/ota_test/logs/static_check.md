# OTA Static Check

Generated: 2026-07-03T22:12:12+08:00

## Project

- Branch: `fix/ota-rawtcp-jlink-e2e`
- Base commit: `44fe628`
- Keil project: `MDK-ARM-8008000/project.uvprojx`
- Target: `program`
- Device selected by J-Link: `STM32F103RC`
- Keil define: `STM32F103xE`
- APP base: `0x08008000`
- APP safe end: `0x08024000`
- OTA backup base: `0x08024000`
- OTA backup end: `0x0803FFFF`
- Connected flash size: 512 KB (`0x0200` from `0x1FFFF7E0`)

## Build

- Build log: `tools/ota_test/logs/build_app_after_timfix.log`
- Status: PASS
- Errors: 0
- Warnings: 0
- Output: `MDK-ARM-8008000/out/cat1.bin`
- Size: `87228`
- Checksum: `0x008936FB`
- Device type: `0x0003`

## Raw TCP OTA Configuration

- `OTA_USE_RAW_TCP_STREAM 1U`
- `OTA_USE_QHTTPREADFILE_UFS 0U`
- `OTA_DEBUG_DOWNLOAD_ONLY 0U`
- `OTA_STREAM_TO_BACKUP_DEBUG 0U`
- `OTA_STREAM_ALLOW_RAW_BIN_TEST 0U`
- `OTA_RAW_TCP_QIRD_LEN 512U`
- `OTA_EXPECTED_DEVICE_TYPE 0x0003U`

## Validation Commands

```powershell
python -m unittest tests.test_ota_http_download
python -m unittest tests.test_keil_app_image_check tests.test_ota_http_download
python tools\check_keil_app_image.py --skip-freshness --json
python tools\ota_test\inspect_ota_bin.py tools\ota_test\out\cat1.bin --device-type 0x0003 --max-size 0x1C000
python tools\ota_test\inspect_ota_bin.py tools\ota_test\out\server_download.bin --device-type 0x0003 --max-size 0x1C000
python -m py_compile tools\ota_test\pack_ota_firmware.py tools\ota_test\inspect_ota_bin.py tools\ota_test\compare_app_header.py tools\ota_test\mqtt_ota_publish.py
```

## Results

- OTA source tests: PASS.
- Keil APP image contract tests: PASS.
- Local packed OTA image: PASS.
- Server OTA image: PASS.
- Server response: HTTP 200, chunked transfer, no `Content-Length`.
- Live APP readback after OTA matches the server package, not the pre-OTA local image.

## Evidence Files

- `tools/ota_test/out/cat1.bin`
- `tools/ota_test/out/cat1_ota_info.json`
- `tools/ota_test/out/server_download.bin`
- `tools/ota_test/logs/server_headers.txt`
- `tools/ota_test/logs/jlink_probe_flash_size.log`
- `tools/ota_test/logs/jlink_read_flash_20260703_220918.log`
- `tools/ota_test/logs/mqtt_ota_20260703_220635.jsonl`
- `artifacts/ota_test/phase1_diagnosis.md`
- `artifacts/ota_test/test_report.md`
