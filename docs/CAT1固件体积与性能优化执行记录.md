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
