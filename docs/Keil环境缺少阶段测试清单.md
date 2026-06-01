# Keil 环境阶段测试清单

本文档用于记录代码迁移到 Keil 环境后的固件体积、性能优化和上板验证结果。

更新时间：2026-06-01

## 0. 当前结论总览

| 阶段 | 当前状态 | 说明 |
| --- | --- | --- |
| Keil Rebuild | 已完成 | 生产宏构建通过，`0 Error(s), 6 Warning(s)` |
| Keil 产物生成 | 已完成 | 已生成 `cat1.map/bin/hex/sct/axf` |
| 产物合约检查 | 已完成 | `check_keil_app_image.py` 不带 `--skip-freshness` 通过 |
| 体积对比 | 已完成 | ROM/bin 减少 5852 字节，RAM 减少 1024 字节 |
| 生产宏构建 | 已完成 | `APP_LOG_ENABLE=0`、`APP_HEX_LOG_ENABLE=0`、`APP_PERF_PROFILE_ENABLE=0` |
| 调试宏构建 | 已完成 | 临时副本开启日志/hex/profiling 后 Keil 通过，`0 Error(s), 1 Warning(s)` |
| J-Link 烧录 | 已完成 | App 区擦除、写入、`verifybin` 成功 |
| App 跳转 | 部分完成 | 写 bootloader release flag 后可跳 App；仍需按烧录指南做断电 30 秒重启确认 |
| 串口日志 | 未完成 | Windows 当前只枚举到 `COM1`，未看到 USB 转串口设备 |
| MQTT/中科协议上板 | 未完成 | 需要真实 CAT1 网络和串口/平台侧确认 |
| OTA 全流程 | 未完成 | 需要平台下发 URL 后上板验证 |
| BL0942/PWM/保护 | 未完成 | 需要真实负载、电参芯片和边界工况 |

## 1. 已完成的 host/自动测试

此前 macOS host 侧记录：

| 测试项 | 结果 |
| --- | --- |
| `git diff --check` | 通过 |
| `bash tools/arm_gcc_syntax_check.sh` | 通过 |
| `python3 -m unittest discover -s tests -v` | 90 项通过 |
| `bash tests/run_mqtt_protocol_tests.sh` | 48 项通过 |
| `python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness` | 通过 |

本次 Windows/Keil 环境补充验证：

| 测试项 | 结果 | 说明 |
| --- | --- | --- |
| `git diff --check` | 通过 | 仅有 CRLF/LF 工作区提示，无 whitespace error |
| `python tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx` | 通过 | fresh Keil 产物检查通过 |
| 生产宏 Keil Rebuild | 通过 | `0 Error(s), 6 Warning(s)` |
| 调试宏 Keil Rebuild | 通过 | 临时副本构建，`0 Error(s), 1 Warning(s)` |
| 协议/登录等价测试 | 48 项通过 | 用 Windows `python -m unittest ...` 等价执行 |
| Windows 可运行单元测试 | 74 项通过 | 排除 Windows 缺失依赖的模块后执行 |
| `python -m unittest discover -s tests -v` | 未全量通过 | 3 个模块导入失败：`fcntl` 不存在、`paho` 未安装 |
| `bash tests/run_mqtt_protocol_tests.sh` | Windows 原命令失败 | Git Bash 中没有 `python3`；已用 `python` 等价执行通过 |
| `bash tools/arm_gcc_syntax_check.sh` | Windows 原命令失败 | 脚本依赖 macOS `xcrun`/SDK，不适用于本机 Windows |

## 2. Keil Rebuild 与产物生成

生产构建目标：

```text
Project: MDK-ARM-8008000/project.uvprojx
Target: program
Compiler: Arm Compiler 5.06 update 6
Output: MDK-ARM-8008000/out
```

生成文件：

| 文件 | 状态 | 大小 | 时间 |
| --- | --- | ---: | --- |
| `MDK-ARM-8008000/out/cat1.map` | 已生成 | 677558 | 2026-06-01 09:49:15 |
| `MDK-ARM-8008000/out/cat1.bin` | 已生成 | 69028 | 2026-06-01 09:49:15 |
| `MDK-ARM-8008000/out/cat1.hex` | 已生成 | 194219 | 2026-06-01 09:49:15 |
| `MDK-ARM-8008000/out/cat1.sct` | 已生成 | 494 | 2026-06-01 09:48:51 |
| `MDK-ARM-8008000/out/cat1.axf` | 已生成 | 1249472 | 2026-06-01 09:49:15 |

生产构建日志摘要：

```text
Program Size: Code=66326 RO-data=2478 RW-data=1480 ZI-data=24872
"out\cat1.axf" - 0 Error(s), 6 Warning(s).
```

当前 warning：

| 文件 | warning | 状态 |
| --- | --- | --- |
| `TcpClient.c` | `#1182-D: a declaration cannot have a label` | 遗留 warning，未阻断构建 |
| `TcpClient.c` | `#177-D: tcp_connect_state_name declared but never referenced` | 生产日志关闭后未引用 |
| `NbDriver.c` | `#550-D: iccid_last_digit_count set but never used` | 遗留诊断变量 |
| `NbDriver.c` | `#177-D: nb_state_name/connect_state_name/nb_iccid_fail_reason_name declared but never referenced` | 生产日志关闭后未引用 |

## 3. 本次 Keil 编译发现的问题点

### 3.1 Keil 命令行路径/环境问题

在原工程路径：

```text
D:\keil work\CAT1_Keil_Project\CAT1_Keil_Project
```

直接用 Keil 命令行 Rebuild 时出现大量：

```text
Fatal error: C3904U: Could not open via file 'out\xxx.__i'.
```

判断：

| 现象 | 结论 |
| --- | --- |
| `out` 目录可写，手动创建 `.__i` 文件成功 | 不是普通权限问题 |
| 复制到无空格临时路径后该错误消失 | 更像 Keil/ARMCC 命令行对带空格路径或已有 UV4 状态处理不稳定 |
| 无空格路径下能进入真实源码编译阶段 | 后续自动化构建建议用无空格路径 |

建议：

```text
自动化 Keil 构建使用 D:\codex_keil_build_xxx\CAT1_Keil_Project 这类无空格路径。
Keil GUI 手动 Rebuild 可继续使用原工程路径。
```

### 3.2 packed 结构体成员取地址问题

Keil 真实源码编译阶段报错：

```text
zk_work_plan.c(333): error #167: "__packed u16 *" incompatible with "u16 *"
zk_work_plan.c(590): error #167: "__packed u16 *" incompatible with "u16 *"
zk_work_plan.c(600): error #167: "__packed u16 *" incompatible with "u16 *"
```

原因：`zk_work_plan.c` 中工作计划存储结构为 packed 布局，不能把 packed 成员地址直接传给普通 `u16 *` 参数。

修复：使用局部 `u16 year/minute` 解析后再赋回 packed 字段。

影响：不改变 JSON 字段、不改变错误码、不改变 Flash 布局、不改变最终计算结果。

详细复盘见：

```text
docs/Keil编译代码问题复盘_20260601.md
```

### 3.3 `zk_plan_delete` 隐式声明

Keil warning：

```text
zk_work_plan.c(887): warning #223-D: function "zk_plan_delete" declared implicitly
```

修复：补充前置声明：

```c
static int zk_plan_delete(int id);
```

影响：不改变功能，仅消除隐式声明风险。

## 4. 地址与 Flash 布局复核

产物合约检查命令：

```powershell
python tools\check_keil_app_image.py --project MDK-ARM-8008000\project.uvprojx
```

结果：

```text
Keil app image check: PASS
```

关键约束：

| 项目 | 期望值 | 当前值 | 状态 |
| --- | --- | --- | --- |
| `APROM_OFFSET_ADDR` | `0x08008000` | `0x08008000` | 通过 |
| `APROM_STARTADDR` | `0x08008000` | `0x08008000` | 通过 |
| `APROM_SAFE_ENDADDR` | `0x08024000` | `0x08024000` | 通过 |
| `OTABAKROM_STARTADDR` | `0x08024000` | `0x08024000` | 通过 |
| `DATAROM_STARTADDR` | `0x08005000` | `0x08005000` | 通过 |
| `BAKDATAROM_STARTADDR` | `0x08006800` | `0x08006800` | 通过 |
| `SYS_DATA_ST_EXPECTED_SIZE` | `408` | `408` | 通过 |
| Scatter `LR_IROM1` | `0x08008000` | `0x08008000` | 通过 |
| Map vector base | `0x08008000` | `0x08008000` | 通过 |
| BIN flash range | `< 0x08024000` | `0x08008000..0x08018DA3` | 通过 |

BIN 元数据：

| 项目 | 当前值 |
| --- | --- |
| `cat1.bin` size | `69028` |
| `prog_checksum` | `0x006DD617` |
| `prog_length` | `0x00010DA4` |
| `device_type` | `0x0003` |
| calculated checksum | `0x006DD617` |

## 5. 体积对比

遗留基线来自：

```text
D:\keil work\CAT1_Keil_Project\validation_snapshots\legacy_out_20260601_093744
```

对比结果：

| 项目 | 遗留 out | 当前优化后 | 变化 |
| --- | ---: | ---: | ---: |
| Code | 72006 | 66326 | -5680 |
| RO-data | 2650 | 2478 | -172 |
| RW-data | 1476 | 1480 | +4 |
| ZI-data | 25900 | 24872 | -1028 |
| Total RO | 74656 | 68804 | -5852 |
| Total RAM | 27376 | 26352 | -1024 |
| Total ROM | 74880 | 69028 | -5852 |
| `cat1.bin` | 74880 | 69028 | -5852 |
| `cat1.hex` | 210668 | 194219 | -16449 |

结论：

```text
当前优化后 ROM/bin 减少 5852 字节，RAM 减少 1024 字节。
```

## 6. 生产日志关闭构建验证

默认生产宏：

```c
APP_LOG_ENABLE=0
APP_HEX_LOG_ENABLE=0
APP_PERF_PROFILE_ENABLE=0
```

验证结果：

| 检查项 | 状态 | 说明 |
| --- | --- | --- |
| Keil 编译无 error | 通过 | `0 Error(s)` |
| `printf_buf()`/`printf_buf2()` 生产构建为空实现 | 通过 | map 中函数大小为 2 bytes |
| UART3 DMA `_flag_txing` 运行态 | 通过 | J-Link 读到 `_flag_txing=0` |
| 串口调试日志默认不输出 | 未实测 | 当前 Windows 未枚举到 USB 转串口 |
| 主循环是否因 printf 等待卡顿 | 初步通过 | App 运行态 NoException，仍需长时间上板观察 |

## 7. 调试日志打开构建验证

调试宏在临时 Keil 副本中打开，不回填生产工程：

```c
APP_LOG_ENABLE=1
APP_HEX_LOG_ENABLE=1
APP_PERF_PROFILE_ENABLE=1
```

构建结果：

```text
Program Size: Code=72222 RO-data=2650 RW-data=1480 ZI-data=25936
"out\cat1.axf" - 0 Error(s), 1 Warning(s).
```

状态：

| 检查项 | 状态 | 说明 |
| --- | --- | --- |
| Keil 编译通过 | 通过 | 临时副本验证 |
| `printf(...)` UART3 DMA 输出 | 未实测 | 缺少串口枚举 |
| `printf_buf()`/`printf_buf2()` hex dump | 未实测 | 缺少串口枚举 |
| `_flag_txing` DMA 完成后清除 | 初步通过 | 生产构建运行态读到 0；调试输出需串口确认 |

## 8. HAL 裁剪验证

当前状态：

| 项目 | 状态 |
| --- | --- |
| `HAL_SPI_MODULE_ENABLED` | 关闭 |
| `HAL_CRC_MODULE_ENABLED` | 关闭 |
| `HAL_PWR_MODULE_ENABLED` | 保持开启 |
| Keil 链接 undefined reference | 未出现 |
| `HAL_SPI_*` 调用 | 未发现活跃调用 |
| `HAL_CRC_*` 调用 | 未发现活跃调用 |

注意：Standby/电源相关功能仍需上板长测确认。

## 9. J-Link 烧录与 App 启动验证

J-Link 探测：

| 项目 | 结果 |
| --- | --- |
| J-Link SN | `601012592` |
| VTref | 约 `3.41V..3.48V` |
| Core | Cortex-M3 |
| Chip ID 寄存器 | `0x10036414` |
| UID | `0DFF484E 0038008F 345153FF` |

烧录流程按 `docs/flash_programming_guide.md` 执行安全方式：

```jlink
erase 0x08008000,0x08023FFF
loadbin "MDK-ARM-8008000/out/cat1.bin",0x08008000
verifybin "MDK-ARM-8008000/out/cat1.bin",0x08008000
```

结果：

```text
verifybin: Verify successful.
```

烧录后内存确认：

```text
0x08008000 = 200066F0 08008145 0800C223 0800C0C1
0x08008200 = 006DD617 00010DA4 B5100003
```

启动观察：

| 阶段 | PC | VTOR | 说明 |
| --- | --- | --- | --- |
| J-Link reset 后 | `0x0800277A` | `0x08000000` | 仍在 Bootloader |
| 写 release flag 后 | `0x0800818A` | `0x08008000` | 已跳到 App |
| 继续运行后 | `0x08016954` | App 态 | NoException |

问题点：

```text
烧录和 verify 成功，但 J-Link 软件 reset 后未自动跳 App。
按项目脚本逻辑写 0x20000000 = 0x01 后可跳 App。
仍需严格按 flash_programming_guide.md 做断电 >=30 秒重启确认，因为 J-Link reset 不等价于掉电复位，无法彻底覆盖 IWDG 状态。
```

运行态诊断：

| 变量 | 地址 | 当前值 | 说明 |
| --- | --- | --- | --- |
| `usart_queue_drop_count` | `0x20000354` | `0` | 当前未观察到 UART 队列丢包 |
| `_flag_txing` | `0x20000286` | `0` | UART3 DMA 未卡在发送中 |

烧录现场文件：

```text
D:\keil work\CAT1_Keil_Project\validation_snapshots\board_flash_20260601_101937
```

包含 App 区烧录前备份和 J-Link 日志。

## 10. OTA 功能回归

状态：未完成，需要真实平台和网络环境。

仍需确认：

- 平台下发 OTA URL 后，设备能正确接收 URL。
- `AT+QHTTPCFG` 流程正常。
- `AT+QHTTPURL` 流程正常。
- `AT+QHTTPGETEX` 流程正常。
- `AT+QHTTPREADFILE` 流程正常。
- 固件下载到 OTA 备份区 `0x08024000` 起始区域。
- 固件校验通过。
- 搬运、重启、App 启动正常。
- OTA 过程中没有被关闭日志、HTTP 激活隔离、HAL 裁剪影响。

静态确认：

```text
Core/Src/LampProtocolLib/ota.c 中 QHTTPCFG/QHTTPURL/QHTTPGETEX/QHTTPREADFILE 仍保留。
OTABAKROM_STARTADDR 仍为 0x08024000。
```

## 11. MQTT / 中科协议回归

状态：host 协议测试通过，真实 CAT1 上板未完成。

已完成：

| 项目 | 状态 |
| --- | --- |
| 登录包/心跳包/遗嘱包格式测试 | 通过 |
| MQTT 密码算法测试 | 通过 |
| 中科协议路由/字段契约测试 | 通过 |
| 工作计划协议契约测试 | 通过 |

仍需真实网络验证：

- CAT1 模块上电初始化正常。
- AT 指令状态机正常。
- MQTT OPEN/CONN/SUB 正常。
- 中科协议登录正常。
- 心跳上报正常。
- 属性上报正常。
- 远程控制正常。
- 工作计划查询、写入、删除、执行正常。
- OTA 指令仍按中科协议入口路由。

当前阻塞点：

```text
Windows 只枚举到 COM1，未发现 USB 转串口设备，暂无法抓串口日志或 AT/MQTT 运行日志。
```

## 12. Flash 写前比较回归

状态：静态/合约检查通过，上板功能未完整验证。

已确认：

| 项目 | 状态 |
| --- | --- |
| 参数区地址不变 | 通过 |
| 备份区地址不变 | 通过 |
| `sys_data_st` 大小仍为 408 | 通过 |
| App 烧录未触碰参数区 | 通过，烧录范围仅 `0x08008000..0x08023FFF` |

仍需上板确认：

- `sys_data_store()` 首次保存正常。
- 主区 `DATAROM_STARTADDR` 能保存。
- 备份区 `BAKDATAROM_STARTADDR` 能保存。
- 重复保存相同数据时不会重复擦写 Flash。
- 修改参数后仍能正常写入。
- 断电重启后参数恢复正常。

## 13. BL0942 / UART2 回归

状态：未完成，需要接入真实 BL0942 电参芯片。

仍需确认：

- UART2 初始化为 BL0942 读取路径。
- 电压采集正常。
- 电流采集正常。
- 有功功率采集正常。
- 电量累计正常。
- 无电压或无电流时不触发除零异常。
- 异常短包、校验错误、接收满长度时不发生越界。
- 采样周期不变。

## 14. PWM / ADC / 保护逻辑回归

状态：未完成，需要真实负载和边界工况。

仍需确认：

- 上电默认 100% 输出正常。
- 0%、1%、50%、100% 调光输出正常。
- 产测模式 PWM 输出正常。
- 输出电压 `Vo_value` 上报与保护判断正常。
- 输出电流 `Io_value` 上报与过流保护正常。
- 输出功率 `Po_value` 上报与闪灯/过载判断正常。
- 温度保护正常。
- 低温保护正常。
- 输入过压、欠压、掉电检测正常。

## 15. CAT1 UART 接收队列诊断

已通过 J-Link 读取：

```text
usart_queue_drop_count = 0
```

当前结论：

```text
短时间 App 运行态未观察到 UART 队列丢包。
```

仍需在以下场景长时间观察：

- MQTT 登录。
- 心跳上报。
- 属性上报。
- OTA 下载。
- 大流量 AT 数据接收。

## 16. 可选主循环性能统计

调试构建已验证 Keil 编译通过：

```c
APP_PERF_PROFILE_ENABLE=1
```

仍需在调试器中观察：

```c
app_perf_max_tick[]
```

重点比较：

- `tcpClientProcess()`
- `_4G_configModule_machine()`
- `send_AT_Command_machine()`
- `nbSendTcpData_sm()`
- `sys_bl0942_process()`
- `_4G_OTA_machine()`
- `mcu_copy_firmware_machine()`
- `zk_work_plan_process()`
- `json_process()`

生产构建必须继续保持：

```c
APP_PERF_PROFILE_ENABLE=0
```

## 17. 后续建议测试顺序

1. 按 `docs/flash_programming_guide.md` 要求，对当前已烧录板子执行断电 >=30 秒后重新上电。
2. 用 J-Link 只读确认 `PC` 位于 `0x08008000..0x08023FFF`，`VTOR=0x08008000`，且 `NoException`。
3. 接入 USB 转串口或确认 CAT1 调试串口枚举，记录启动日志。
4. 验证 CAT1 联网、MQTT OPEN/CONN/SUB、登录、心跳、属性上报。
5. 验证远程控制和工作计划查询/写入/删除/执行。
6. 验证 BL0942 电压、电流、功率、电量累计。
7. 验证 Flash 参数保存、重复保存不擦写、断电恢复。
8. 验证 PWM/ADC/温度/过流/掉电保护边界行为。
9. 验证 OTA URL 下发、HTTP 下载、备份区写入、校验、搬运、重启。
10. 调试宏版本单独烧录，验证 UART3 日志、hex dump 和 `app_perf_max_tick[]`。

## 18. 发布前剩余缺口

| 阶段 | 当前状态 | 发布前需要补齐 |
| --- | --- | --- |
| Keil Rebuild | 已完成 | 保留日志和产物 |
| 产物检查 | 已完成 | 发布前重新跑一次 fresh check |
| 体积对比 | 已完成 | 如继续改代码需重算 |
| 生产日志关闭 | 部分完成 | 串口侧确认默认无调试输出 |
| 调试日志打开 | 构建完成 | 串口侧确认 UART3 DMA 输出和 hex dump |
| App 启动 | 部分完成 | 断电 >=30 秒重启后确认自动进入 App |
| OTA | 未完成 | URL 下载、校验、搬运、重启 |
| MQTT/中科协议 | 未完成 | 登录、心跳、上报、控制、计划 |
| BL0942 | 未完成 | 电参采集和异常数据保护 |
| Flash | 未完成 | 写前比较、主备区、断电恢复 |
| PWM/保护 | 未完成 | 调光、温度、过流、掉电 |

