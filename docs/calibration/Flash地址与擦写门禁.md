# Calibration V3 Flash 地址与擦写门禁

> 当前 V3 校准持久化结构、地址和擦写行为必须以当前分支源码、最新 Keil MAP/HEX 和实机掉电验证为准；本文件不再保留旧 V2 record、旧候选地址或旧功率专项参数。

## 1. 当前持久化语义

校准 Flash 保存的是当前 V3 committed CALP 数据及其完整性元数据。上位机写入链路固定为：

```text
STAGE
-> APPLY
-> QUICK/FULL VERIFY
-> COMMIT
-> READ
-> generation / payloadLength / payloadCrc32 / full-byte compare
```

当前 CALP payload 为 244B，必须保持当前协议版本与固件 codec 一致。

## 2. 地址来源唯一性

任何绝对 Flash 地址都不得从历史文档复制。每次准备量产镜像时必须同时核对：

- 当前 `flash_address_assignment.h` / 校准存储源码；
- 当前 Keil linker/scatter 配置；
- 当前 MAP；
- 当前 HEX/BIN 实际占用范围；
- Boot、APP、OTA、Factory/Property/Plan等相邻持久化区域。

只有上述证据一致，才能把地址标记为当前量产地址。

## 3. 擦写硬门禁

- 写入前必须保证目标范围合法且不会越过物理 Flash；
- 擦除页若与其他记录共享，必须证明邻接记录在更新后保持不变；
- 新数据必须先完整写入并回读，再完成最终有效提交标记；
- COMMIT失败不得破坏上一份有效 committed 校准；
- CRC或结构非法的记录不得在启动时激活；
- Flash失败必须使校准流程失败关闭并保持安全输出状态。

## 4. 掉电验证

源码实现不能替代以下实机验证：

- 擦除前掉电；
- 擦除后掉电；
- payload部分写入后掉电；
- payload完整但最终提交前掉电；
- COMMIT后立即掉电；
- 重启后generation/CRC/payload选择正确；
- 相邻Factory/Property/Plan/OTA记录不被破坏。

## 5. 与算法的边界

Flash层只负责可靠持久化，不参与：

- Output误差判定；
- MCU采样误差判定；
- QUICK/FULL精度阈值；
- BUILD稳定百分比门禁。

是否允许COMMIT由安全、曲线结构/覆盖、Payload、CRC和Flash条件决定；精度数据由验证报告/MES质量规则使用。

完整算法与职责边界见 `../V3六型号真实校准最终设计.md`。
