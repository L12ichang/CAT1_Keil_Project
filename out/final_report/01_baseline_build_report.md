# Baseline Build Report

Date: 2026-07-04
Branch: fix/mcu-timing-mqtt-ota-watchdog

## Required Documents

- Read: `docs/Codex_MCU_时序_MQTT_OTA_闭环修复执行文档.md`
- Read: `docs/嵌入式C代码编写规范.md`

Conflict noted: the generic C coding spec says the watchdog feed must be the first line of `while (1)`, but the task-specific MCU repair objective forbids unconditional feed at the start of the main loop and requires health-gated feed after loop work. The task-specific safety requirement is treated as authoritative for this repair.

## Initial Git State

The workspace was already dirty before code edits:

- Modified: `Core.rar`
- Modified: `docs/嵌入式C代码编写规范.md`
- Deleted: `~$中科协议.docx`
- Untracked: `docs/Codex_MCU_时序_MQTT_OTA_闭环修复执行文档.md`

After the baseline APP build, Keil regenerated:

- Modified: `MDK-ARM-8008000/out/cat1.bin`
- Modified: `MDK-ARM-8008000/out/cat1.hex`

## APP Baseline Build

Command:

```text
D:\Keil_v5\UV4\UV4.exe -b project.uvprojx -j0 -o ..\out\baseline\app_build_log.txt
```

Result:

- Target: `program`
- Compiler: ARMCC V5.06 update 6 build 750
- Errors: 0
- Warnings: 0
- APP bin: `MDK-ARM-8008000/out/cat1.bin`
- APP bin size: 87384 bytes
- Program Size: Code=82226, RO-data=4926, RW-data=1592, ZI-data=33704

Archived baseline artifacts:

- `out/baseline/app_build_log.txt`
- `out/baseline/app.map`
- `out/baseline/app.hex`
- `out/baseline/app.bin`
- `out/baseline/app.sct`

## APP Layout Check

The APP image layout check passed when skipping only the scatter-file timestamp freshness check. UV4 rebuilt the AXF/HEX/BIN, but did not refresh the existing scatter file timestamp.

- APP base: `0x08008000`
- APP safe end: `0x08024000`
- APP flash range: `0x08008000..0x0801D557`
- Scatter base: `0x08008000`
- Scatter size: `0x0001C000`
- Vector table: `0x08008000`
- Header checksum field: `0x08008200`
- Header size field: `0x08008204`
- Header device_type field: `0x08008208`
- Stored checksum: `0x00897439`
- Calculated checksum: `0x00897439`
- Stored size: `0x00015558`
- Device type: `0x0003`
- Remaining APP window margin: 27160 bytes

## Boot Baseline

Boot source/project was not found under `D:\keil_work\CAT1_Keil_Project`. Only `boot.bin` is present in the current workspace.

Boot artifact checks:

- Boot bin: `boot.bin`
- Boot bin size: 11040 bytes
- Boot partition limit: 20480 bytes
- Initial SP: `0x200028E8`
- Reset vector: `0x08000149`
- Embedded `0x08008000` references: 4
- Fits Boot partition: yes

J-Link readback showed the on-chip Boot region matches `boot.bin` byte-for-byte for 11040 bytes. J-Link `verifybin boot.bin,0x08000000` passed.

Boot compile status: blocked because Boot source/project is absent from this workspace. Boot artifact verification was completed instead.

## J-Link Baseline

J-Link non-destructive connection/read succeeded.

- J-Link: SEGGER J-Link Commander V8.10
- Probe S/N: 601012592
- VTref observed: 3.715 V and 3.502 V in read sessions
- Interface: SWD
- Speed: 4000 kHz
- Device setting: STM32F103RC
- SW-DP ID: `0x2BA01477`
- CPUID: `0x412FC231`
- Core: Cortex-M3 r2p1
- Flash-size word at `0x1FFFF7E0`: `0x0200`

Archived J-Link artifacts:

- `out/baseline/jlink_read_flash.log`
- `out/baseline/jlink_read_boot.log`
- `out/baseline/jlink_verify_boot.log`
- `out/baseline/jlink_app_head.bin`
- `out/baseline/jlink_ota_backup_head.bin`
- `out/baseline/jlink_data_head.bin`
- `out/baseline/jlink_boot.bin`

## On-Chip Image Snapshot

On-chip APP and OTA backup headers currently match each other, but differ from the freshly built APP baseline.

Fresh build header:

- Initial SP: `0x200089E0`
- Reset vector: `0x08008145`
- Checksum: `0x00897439`
- Size: `0x00015558`
- Device type: `0x0003`

On-chip APP header:

- Initial SP: `0x200086F0`
- Reset vector: `0x08008145`
- Checksum: `0x00836E86`
- Size: `0x00014614`
- Device type: `0x0003`

On-chip OTA backup header:

- Initial SP: `0x200086F0`
- Reset vector: `0x08008145`
- Checksum: `0x00836E86`
- Size: `0x00014614`
- Device type: `0x0003`

Conclusion: the target is currently running/storing an older APP image than the fresh baseline build. Do not treat an APP verify against the fresh build as a baseline failure until the fresh build is explicitly programmed.
