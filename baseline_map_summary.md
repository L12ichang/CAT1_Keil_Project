# Baseline Map Summary

采集时间：2026-07-10 00:43 Asia/Shanghai

## Build

- Project: `MDK-ARM-8008000/project.uvprojx`
- Target: `program`
- Build command: `D:\Keil_v5\UV4\UV4.exe -r project.uvprojx -t program -j0 -o out\baseline_rebuild.log`
- Compiler: ARMCC V5.06 update 6 build 750
- Result: 0 errors, 0 warnings
- APP start: `0x08008000`
- APP binary: `MDK-ARM-8008000/out/cat1.bin`
- Baseline artifacts: `output/baseline_cat1.map`, `output/baseline_cat1.bin`, `output/baseline_cat1.hex`, `output/baseline_cat1.axf`

## Program Size

| Metric | Bytes | KiB |
|---|---:|---:|
| Code | 87906 | 85.85 |
| RO-data | 5098 | 4.98 |
| RW-data | 1684 | 1.64 |
| ZI-data | 33684 | 32.89 |
| Total RO Size | 93004 | 90.82 |
| Total RW Size | 35368 | 34.54 |
| Total ROM Size | 93212 | 91.03 |

## Key Object Contributions

ROM = Code + RO-data + RW-data. RAM = RW-data + ZI-data.

| Object | Code | Inc data | RO-data | RW-data | ZI-data | ROM | RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| mqtt_zk_protocol.o | 13322 | 3788 | 669 | 200 | 10920 | 14191 | 11120 |
| ota.o | 10524 | 4838 | 2290 | 130 | 5985 | 12944 | 6115 |
| nbdriver.o | 8072 | 3196 | 77 | 84 | 3581 | 8233 | 3665 |
| zk_property.o | 5518 | 1064 | 0 | 4 | 420 | 5522 | 424 |
| zk_work_plan.o | 4890 | 556 | 12 | 10 | 660 | 4912 | 670 |
| cjson.o | 4340 | 220 | 0 | 20 | 0 | 4360 | 20 |
| app.o | 0 | 0 | 0 | 31 | 0 | 31 | 31 |
| protocol.o | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| systemconfig.o | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| json_protocol.o | 170 | 54 | 0 | 0 | 0 | 170 | 0 |
| sys_bl0942.o | 1192 | 118 | 0 | 64 | 23 | 1256 | 87 |
| hw_uart3.o | 370 | 60 | 0 | 2 | 954 | 372 | 956 |

Note: `systemconfig.o` appears in the map removal/cross-reference sections, but contributes 0 bytes to the final image in this baseline.

## J-Link Baseline

- Probe: J-Link Pro V4, S/N 601012592
- VTref during checks: about 3.42 V to 3.50 V
- Target connection: SWD, Cortex-M3 detected
- APP vector read at `0x08008000`: `20008A28 08008145 0800C5F3 0800C491`
- Flash action: `loadbin cat1.bin, 0x08008000`
- Result: flash contents already matched; `verifybin` successful
- Full-chip erase: not performed
- Read protection/config changes: not performed
- Log: `output/baseline_jlink_flash.log`

## Serial Baseline

- Port discovered: `USB-SERIAL CH340 (COM23)`
- Baud: 1000000, 8N1
- Capture window: 60 seconds after J-Link reset/go
- Log: `baseline_serial_log.txt` and `output/baseline_serial_log.txt`
- Observed startup:

```text
? boot startup1
Mar  4 2025,15:08:28
from aprom
checksum ok
PLL
PLL from HSE
```

## Functional Baseline Status

| Item | Status | Evidence |
|---|---|---|
| Keil rebuild | PASS | `output/baseline_rebuild.log` |
| Hex/bin/map generation | PASS | `output/baseline_cat1.*` |
| J-Link connection | PASS | SWD target detected |
| J-Link APP programming/verify | PASS | Flash contents matched and verify succeeded |
| Boot checksum path | PASS | Serial log contains `checksum ok` |
| APP clock startup | PASS | Serial log contains `PLL from HSE` |
| 4G init | NOT OBSERVED | No UART evidence in 60 s capture |
| IMEI/ICCID | NOT OBSERVED | No UART evidence in 60 s capture |
| MQTT login | NOT OBSERVED | No platform/MQTT evidence captured yet |
| MQTT heartbeat | NOT OBSERVED | No platform/MQTT evidence captured yet |
| MQTT control/query | NOT OBSERVED | No platform/MQTT test captured yet |
| BL0942/NTC/parameter save | NOT OBSERVED | No runtime test captured yet |
| OTA flow | NOT OBSERVED | Not exercised in baseline pass |

