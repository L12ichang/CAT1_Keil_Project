# Build And Size Compare

## Baseline

- Build log: `out/baseline/build_log.txt`
- Result: 0 errors, 0 warnings
- Binary: `out/baseline/app.bin`
- Size: 87,384 bytes
- Header checksum: `0x00897439`
- Header size: `0x00015558`
- Vector SP: `0x200089E0`
- Reset: `0x08008145`

## Final

- Build log: `out/final_report/final_app_rebuild_uart_owner_gateway_log.txt`
- Result: 0 errors, 0 warnings
- Keil size: `Code=78382 RO-data=4750 RW-data=1664 ZI-data=33576`
- Binary: `MDK-ARM-8008000/out/cat1.bin`
- Size: 83,364 bytes
- Header checksum: `0x0083CCD1`
- Header size: `0x000145A4`
- Device type: `0x0003`
- Vector SP: `0x200089A8`
- Reset: `0x08008145`
- SHA256: `EFAEA04356B9F4F276D883D8D9E68D259034331925D7FE5C7715AD212F19BA3B`

## Delta

- Binary size decreased by 4,020 bytes versus baseline.
- Vector SP remains below the known Boot-compatible baseline SP.
- Final flash range from image checker: `0x08008000..0x0801C5A3`, below safe end `0x08024000`.

## Host Checks

- `python tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness --json`: PASS
- `python -m py_compile tools/ota_test/pack_ota_firmware.py tools/ota_test/inspect_ota_bin.py tools/ota_test/compare_app_header.py tools/ota_test/mqtt_ota_publish.py`: PASS
- `python -m unittest tests.test_keil_app_image_check tests.test_ota_http_download tests.test_zk_publish_backpressure tests.test_zk_control_contract tests.test_phase4_login_heartbeat tests.test_zk_property_flash_persistence tests.test_zk_work_plan_contract`: PASS, 45 tests
