# 单片机开发调试指南

## 1. 硬件基线

| 项目 | 当前值 |
| --- | --- |
| 目标 MCU | HK32F103CCT6A (J-Link使用STM32F103RC) |
| Flash | 256 KB |
| RAM | 48 KB |
| Bootloader 起始 | `0x08000000` |
| App 起始 | `0x08008000` |
| App 分区上限 | `0x08024000` |
| 系统参数主区 | `0x08005000` |
| 系统参数备份区 | `0x08006800` |
| OTA 备份区 | `0x08024000..0x0803FFFF` |
| 调试/日志串口 | UART3，`1000000-8-N-1` |
| 4G 模组业务串口 | UART1，通常 `115200-8-N-1` |

## 2. Flash分区契约

| 区域 | 地址 | 说明 |
| --- | --- | --- |
| Bootloader | `0x08000000..0x08004FFF` | 启动与IAP，烧录不得覆盖 |
| 系统参数主区 | `0x08005000..0x080067FF` | 设备参数、校准参数 |
| 系统参数备份区 | `0x08006800..0x08007FFF` | 参数备份 |
| App | `0x08008000..0x08023FFF` | 当前应用固件，MDK-ARM-8008000 IROM Size=`0x1C000` |
| OTA 备份区 | `0x08024000..0x0803FFFF` | OTA下载备份 |

## 3. 开发环境

### 构建系统
- 主构建入口：`platformio.ini`（唯一真相源）
- 兼容构建入口：`Makefile`（仅作为PlatformIO wrapper）
- Keil工��：仅作为历史参考和应急排障，不作为日常构建判据

### 自动化入口
```bash
./AUTOMATION_SCRIPT.sh info          # 环境信息
./AUTOMATION_SCRIPT.sh status        # 芯片状态检测
./AUTOMATION_SCRIPT.sh build         # 固件构建
./AUTOMATION_SCRIPT.sh flash --dry-run  # 烧录演练
./AUTOMATION_SCRIPT.sh flash         # 实际烧录
./AUTOMATION_SCRIPT.sh serial-test --duration 20  # 串口测试
./AUTOMATION_SCRIPT.sh login-test    # 登录测试
./AUTOMATION_SCRIPT.sh validate      # 最终验证
./AUTOMATION_SCRIPT.sh gdb-server    # 启动GDB Server
```

### 配置文件
- `config/mcu_workflow.json`：统一配置（烧录基地址、串口参数、J-Link参数）
- 默认固件：`build/pro.elf`

## 4. J-Link SWD连接

| 信号 | 说明 |
| --- | --- |
| SWDIO | 数据线 |
| SWCLK | 时钟线 |
| GND | 必须共地 |
| VTref | 目标电压参考 |
| NRST | 建议连接 |

配置：设备`STM32F103RC`，接口`SWD`，速度`4000 kHz`。连接不稳定时降低到`1000`或`400`。

## 5. 串口连接

### UART3 日志串口
- 推荐端口：`/dev/cu.wchusbserial2320`
- 参数：`1000000-8-N-1`
- 注意：不是`115200`，使用错误波特率会看到乱码

### UART1 业务串口
- 连接4G Cat1模组，PA9/PA10
- 参数：通常`115200-8-N-1`
- 关键AT时序：`AT+CGSN`、`AT+QCCID`、`AT+QMTOPEN`、`AT+QMTCONN`、`AT+QMTSUB`、`AT+QMTPUBEX`

## 6. 烧录流程

### 标准步骤
```bash
./AUTOMATION_SCRIPT.sh build
./AUTOMATION_SCRIPT.sh flash --dry-run   # 先演练
./AUTOMATION_SCRIPT.sh flash             # 实际烧录
```

### 关键规则
- 写入方式：`loadbin {bin},0x08008000`（非BIN先归一化）
- 禁止使用无边界的`loadfile`
- 烧录后必须回读校验
- 写入范围不得覆盖Bootloader区（`0x08000000`）
- 末地址必须小于`0x08024000`，不得覆盖OTA备份区

### 烧录后释放App
J-Link reset/go后若停在Bootloader等待循环，执行：
```text
halt
w1 0x20000000 0x01
go
exit
```

### 正确烧录流程（J-Link Commander）
```jlink
device STM32F103CB
si SWD
speed 1000
connect
halt
erase 0x08008000, 0x08023FFF    // 只擦除APP区，保留OTA备份区
loadbin firmware.bin, 0x08008000 // 写入固件
verifybin firmware.bin, 0x08008000 // 验证
r
g
exit
```

## 7. 调试环境

### GDB调试
- `.vscode/launch.json`：Cortex-Debug + J-Link
- `.vscode/tasks.json`：构建、状态检测、烧录、验证入口
- `AUTOMATION_SCRIPT.sh gdb-server`：启动GDB Server

### 快速J-Link检查
```text
halt
regs
mem32 0xE000ED08,1    # 读VTOR
go
exit
```

通过标准：`VTOR=0x08008000`，PC在App区。

## 8. 日志系统

### 输出目录
- 文本日志：`artifacts/logs/serial_*.log`
- 结构化日志：`artifacts/logs/serial_*.jsonl`
- 验证报告：`artifacts/reports/*.md`

### 日志判定门槛
- 串口日志非空，包含App build date或`[BOOT] app_start`
- 5分钟内无`WDGRST`
- 出现真实IMEI和MQTT topic
- 出现登录ACK或在线状态日志

## 9. 测试验证

### 主机侧快速回归
```bash
python3 -m unittest tests.test_mqtt_protocol_refactor tests.test_login_validation tests.test_mqtt_password_contract tests.test_phase4_login_heartbeat tests.test_mcu_workflow
```

### 构建与烧录演练
```bash
./AUTOMATION_SCRIPT.sh build
./AUTOMATION_SCRIPT.sh flash --dry-run
```

### 硬件在环验证
```bash
./AUTOMATION_SCRIPT.sh status
./AUTOMATION_SCRIPT.sh flash
./AUTOMATION_SCRIPT.sh serial-test --duration 20 --expect "boot startup"
./AUTOMATION_SCRIPT.sh serial-test --duration 60 --expect "MS/"
```

## 10. 故障定位

### J-Link无法连接
- 检查SWDIO/SWCLK/GND/VTref/NRST接线
- 降低`speed_khz`到1000或400
- 先执行`jlink-detect`再执行`status`

### 串口无数据
- 确认波特率为`1000000`，不是`115200`
- 先打开采集再复位目标板
- 检查端口路径是否变化

### 烧录校验失败
- 使用`build/pro.elf`作为输入
- 先执行`flash --dry-run`
- 检查供电和SWD稳定性

### 烧录后停在Bootloader
- 不等于Bootloader被擦除
- 写RAM标志`0x20000000=1`后`go`
- 写入后不要再次reset

## 11. 代码模块划分

- `Core/Src/main.c`：系统初始化、主循环调度
- `Core/Src/LampProtocolLib/`：MQTT、OTA、网关协议栈
- `Core/Src/gateway/`：网关业务
- `Core/Src/hw_*.c`：UART、FLASH、4G IO等板级驱动
- `Core/Src/sys_*.c`：温保、电参、掉电、PWM等系统功能
- `Drivers/`：CMSIS与HAL库
- `tests/`：Python协议与登录测试
