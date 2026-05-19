# 中科协议固件烧录确认与 Keil 验证

## 结论

本次中科协议 App 固件烧录使用：

- `MDK-ARM-8008000/project.uvprojx`

不要把 `MDK-ARM-8000000/project.uvprojx` 作为本次 App 更新固件烧录入口。当前项目资料和旧构建产物显示：

- Bootloader 区域：`0x08000000`
- App 区域：`0x08008000`
- `MDK-ARM-8008000/out/cat1.map` 中 `__Vectors` 为 `0x08008000`
- `MDK-ARM-8008000/out/cat1.sct` 中 `LR_IROM1` 为 `0x08008000`
- `MDK-ARM-8000000/out/cat1.map` 中 `__Vectors` 为 `0x08000000`

所以保留 Bootloader、只更新 App 时，只能烧录 8008000 工程重新生成的 App 产物。

## 烧录前检查

Windows Keil 重新编译前，本仓库提供本地安全检查脚本：

```bash
python3 tools/check_keil_app_image.py
```

默认检查 `MDK-ARM-8008000/project.uvprojx`，并确认：

- 工程目录是 `MDK-ARM-8008000`
- 工程定义了 `APROM_OFFSET`
- 工程包含 `mqtt_zk_protocol.c`
- 工程包含 `net_dim.c`
- `cat1.sct` 的 `LR_IROM1` 是 `0x08008000`
- `cat1.map` 的 `__Vectors` 和 `LR_IROM1` 是 `0x08008000`
- `cat1.sct` 的 IROM Size 是 `0x1C000`，即 APP 分区 `0x08008000..0x08023FFF`
- `cat1.bin` 写入范围不会越过 `0x08024000`
- `cat1.sct/map/bin/hex` 不早于源码和工程文件

如果只是检查旧参考产物的地址，不要求产物时间新于源码，可以使用：

```bash
python3 tools/check_keil_app_image.py --skip-freshness
```

注意：正式烧录前不能使用 `--skip-freshness` 作为放行条件。正式烧录必须先在 Keil 里重新 `Rebuild all target files`，再运行默认检查。

## Windows Keil 操作

1. 打开 `MDK-ARM-8008000/project.uvprojx`。
2. 执行 `Rebuild all target files`。
3. 确认 Build Output 为 `0 Error`。
4. 检查 `MDK-ARM-8008000/out/cat1.map`：
   - `__Vectors = 0x08008000`
   - `Load Region LR_IROM1 (Base: 0x08008000, ...)`
5. 检查 `MDK-ARM-8008000/out/cat1.sct`：
   - `LR_IROM1 0x08008000`
6. 检查 `MDK-ARM-8008000/out/cat1.bin`：
   - 文件时间必须是本次 Keil Rebuild 后生成
   - 文件大小必须小于 `0x1C000`
   - 结束地址 `0x08008000 + bin_size` 必须小于 `0x08024000`
   - 禁止擦写 `0x08024000..0x0803FFFF` OTA 备份区

## 烧录规则

推荐使用 Keil 在 `MDK-ARM-8008000` 工程内直接 `Download`。

如使用外部烧录工具：

- 优先使用 `MDK-ARM-8008000/out/cat1.hex`
- 如果使用 `cat1.bin`，起始地址必须设置为 `0x08008000`
- 不要执行 Full Chip Erase
- 不要把 `MDK-ARM-8000000/out/cat1.bin` 当作本次 App 固件烧录

## 烧录后验证

串口波特率：`1000000`。

上电后确认日志包含：

- `QMTOPEN`
- `QMTCONN`
- `QMTSUB`
- `QMTPUBEX`

验证标准中科控制：

```json
{"SV":"ctrl","CT":"W","DT":{"cnCtrl":[{"cns":1,"bri":80,"last":0}]}}
```

期望：

- 亮度变为 80
- 回复 `ER:0`
- 回复 `SV` 仍为 `ctrl`

验证巡检：

```json
{"SV":"ctrl","CT":"W","DT":{"DO":"patrol"}}
```

期望：

- 回复 `ER:0`
- 回复 `SV` 仍为 `ctrl`

## Makefile 说明

当前工作区没有可用的 `Makefile`、`platformio.ini` 或完整非 Keil 构建配置，所以不能用 Makefile 可靠编译本次固件。

如需 Makefile 构建，需要单独从 `MDK-ARM-8008000/project.uvprojx` 抽取源文件列表、include 路径、宏定义、ARMCC 参数、链接地址和 post-build 规则。该工作属于新的构建系统迁移任务，不作为本次烧录放行路径。
