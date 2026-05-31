# CAT1 固件体积与性能优化执行记录

## 1. 优化边界

- 优化分支：`optimize/firmware-size-performance`
- 基线标签：`baseline-before-size-perf-optimization`
- 备份标签：`backup-before-size-perf-optimization`
- 核心原则：不改变当前功能、不改变 MQTT/中科协议、不改变 OTA 流程、不改变 Flash 布局、不改变 `sys_data_st` 字段布局。

## 2. 必须保持不变的地址与结构

| 项目 | 当前值 | 说明 |
| --- | --- | --- |
| `APROM_OFFSET_ADDR` | `0x08008000` | App 启动向量重定位地址 |
| `DATAROM_STARTADDR` | `0x08005000` | 系统参数主区 |
| `BAKDATAROM_STARTADDR` | `0x08006800` | 系统参数备份区 |
| `APROM_STARTADDR` | `0x08008000` | App 起始地址 |
| `APROM_SAFE_ENDADDR` | `0x08024000` | App 安全结束地址 |
| `OTABAKROM_STARTADDR` | `0x08024000` | OTA 备份区起始地址 |
| `SYS_DATA_ST_EXPECTED_SIZE` | `408` | `sys_data_st` 固定大小，禁止破坏 Flash 兼容性 |

## 3. 优化前验证基线

| 验证项 | 结果 | 说明 |
| --- | --- | --- |
| `bash tools/arm_gcc_syntax_check.sh` | 通过 | host 侧 ARM GCC 语法检查通过 |
| `python3 -m unittest discover -s tests -v` | 通过 | 90 项测试通过 |
| `bash tests/run_mqtt_protocol_tests.sh` | 通过 | 48 项 MQTT/协议测试通过 |
| `python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness` | 通过 | App 基址、Flash 合约、`sys_data_st=408` 检查通过 |

## 4. 优化前固件体积基线

本机当前没有现成 Keil 输出文件，`MDK-ARM-8008000/out/cat1.sct`、`cat1.map`、`cat1.bin`、`cat1.hex` 均不存在。因此本次无法在 macOS host 环境直接记录优化前 Code/RO/RW/ZI 和 bin/hex 大小。

后续需要在 Keil 中执行 Rebuild 后补充以下数据：

| 项目 | 优化前 | 优化后 | 变化 |
| --- | --- | --- | --- |
| Code | 未生成 | 待 Keil Rebuild | 待补充 |
| RO-data | 未生成 | 待 Keil Rebuild | 待补充 |
| RW-data | 未生成 | 待 Keil Rebuild | 待补充 |
| ZI-data | 未生成 | 待 Keil Rebuild | 待补充 |
| bin size | 未生成 | 待 Keil Rebuild | 待补充 |
| hex size | 未生成 | 待 Keil Rebuild | 待补充 |

## 5. 回退起点

- 回到优化前代码：`git checkout optimize/firmware-size-performance && git reset --hard baseline-before-size-perf-optimization`
- 临时查看优化前版本：`git checkout baseline-before-size-perf-optimization`

## 6. Release Size / Map 输出配置记录

Keil 当前目标为 `program`，使用 Arm Compiler 5（`ARM-ADS` / `V5.06 update 7`）。已核对 `MDK-ARM-8008000/project.uvprojx`：

| 配置项 | 当前值 | 结论 |
| --- | --- | --- |
| 编译优化 | `<Optim>4</Optim>` | 已是 size 优化级别 |
| 时间优先优化 | `<oTime>0</oTime>` | 未启用速度优先，符合体积优先 |
| One ELF Section per Function | `<OneElfS>1</OneElfS>` | 已启用函数级 section，便于链接器裁剪未引用函数 |
| Map File | `<AdsLmap>1</AdsLmap>` | 已启用 map 输出 |
| Cross Reference / Symbols / Size Info | `<AdsXref/Lsym/Lszi>` 相关项启用 | 已保留定位体积来源所需信息 |
| MicroLIB | `<useUlib>1</useUlib>` | 当前已启用；本次不再混入独立 MicroLIB 实验 |
| App/OTA 地址 | 由 `check_keil_app_image.py` 校验通过 | App 起始地址和 OTA 分区未改动 |

本阶段不修改业务代码，只记录当前 Release Size 约束。由于本机没有 Keil 可执行环境，最终 Code/RO/RW/ZI 仍需在 Keil Rebuild 后由 `out/cat1.map` 补录。

## 7. 日志与 UART3 DMA 优化

- 在 `Core/Src/common.h` 增加 `APP_LOG_ENABLE`、`APP_HEX_LOG_ENABLE`、`LOGE/LOGW/LOGI/LOGD`。默认值均为 `0`，生产构建默认移除调试打印入口。
- `printf(...)` 默认展开为空语句；调试时可通过编译宏设置 `APP_LOG_ENABLE=1` 恢复 `dma_printf(...)`。
- `printf_buf()`、`printf_buf_char()`、`printf_buf2()` 受 `APP_HEX_LOG_ENABLE` 控制；生产构建为空实现，避免 OTA payload、串口 payload dump 带来 ROM 字符串和运行耗时。
- `Core/Src/hw_uart3.c` 中 `dma_printf()` 使用 `vsnprintf()` 返回长度发送，不再额外 `strlen()` 扫描；发送失败时清除 `_flag_txing`。
- `HAL_DMA_TxCpltCallback()` 在 `dma == &hdma_usart3_tx` 时清除 `_flag_txing`，避免 DMA 发送完成后仍等待超时。

验证方式：host 侧语法检查和协议测试；上板调试时分别用 `APP_LOG_ENABLE=0/1`、`APP_HEX_LOG_ENABLE=0/1` 验证生产关闭与调试打开行为。

## 8. Flash / UART2 / BL0942 稳定性优化

- `Core/Src/sys_data.c`：`data_store_data()` 写 Flash 前先 `memcmp()` 当前 Flash 内容，数据未变化时直接返回成功，减少重复擦写；主区和备份区调用路径不变。
- `Core/Src/hw_uart2.c`：BL0942 接收中断在 `_index >= _rx_length` 时立即置 `BL0942_STATE_READ_READY`，不再继续向 `_buffer + _index` 申请下一字节，避免越界接收。
- `Core/Src/sys_bl0942.c`：`ac_power_S == 0` 时不再计算 `ac_pf = ... / ac_power_S`，异常电参下将 `ac_pf`、`Z_ac_current` 置 0；同时对小电流修正减 7 的路径增加下限保护，避免无符号下溢。
- `Core/Src/sys_bl0942.c`：把 3% 功率修正从浮点乘法改成 `* 97 / 100` 的整数计算，保持取整方向一致，减少运行时浮点开销。
- `Core/Src/LampProtocolLib/Portable.c`：增加 `volatile uint32 usart_queue_drop_count`，队列满导致 `enqueue()` 失败时仅计数，不打印、不改变正常收包逻辑。

验证重点：Flash 地址不变，`sys_data_st` 仍为 408 字节，BL0942 正常数据路径计算不变，异常无电压/无电流数据不触发除零。

## 9. 旧 HTTP 激活链路隔离核对

- `Core/Src/gateway/app_active.c`、`Core/Src/LampProtocolLib/http_active.c` 当前未加入 `MDK-ARM-8008000/project.uvprojx`，不会进入 Keil active image。
- `app_active.h`、`http_active.h` 已是默认 inline stub，未定义 legacy implementation 宏时不执行旧 HTTP 激活状态机。
- 本次移除 `main.c`、`sys_tick.c`、`NbDriver.c` 中不需要的 `http_active.h`/`app_active.h` 残留 include；`sys_tick.c` 仅保留 `app_active.h`，用于当前空 timer stub，避免影响已有测试约束。
- `Core/Src/LampProtocolLib/ota.c` 未改动，`AT+QHTTPCFG`、`AT+QHTTPURL`、`AT+QHTTPGETEX`、`AT+QHTTPREADFILE` OTA 下载流程未删除。

## 10. HAL SPI / CRC 裁剪

- 全工程未发现 `HAL_SPI_*`、`HAL_CRC_*` API 调用，本次关闭 `Core/Inc/stm32f1xx_hal_conf.h` 中的 `HAL_SPI_MODULE_ENABLED` 和 `HAL_CRC_MODULE_ENABLED`。
- 从 `MDK-ARM-8008000/project.uvprojx` 与 `project.uvoptx` 移除 `stm32f1xx_hal_spi.c`、`stm32f1xx_hal_crc.c` 工程引用，避免未使用 HAL 模块参与 Keil 构建。
- `HAL_PWR_MODULE_ENABLED` 保持开启，原因是 HAL 基础初始化、RCC/standby 或电源相关路径可能依赖，不能贸然裁剪。
