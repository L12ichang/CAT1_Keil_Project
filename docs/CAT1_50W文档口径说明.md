# CAT1 50W 校准文档口径说明

> 生效日期：2026-08-21  
> 固件目标分支：`done/cat1-product-profile-cal-context-20260817`  
> 状态：`TARGET_SPEC_AUTHORITY_FROZEN / FIELD_WIRE_CONTRACT_PENDING`

## 1. 当前权威实施文档

后续设计、实现和审核只以以下三份文档为目标规范：

1. 固件：`docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合审核：`docs/CAT1_50W固件与上位机联合审核清单.md`

旧文档、旧测试和旧源码只用于理解当前实现与迁移，不得反向覆盖以上三份文档。

## 2. 版本命名

目标 MQTT 校准协议统一称为：

```text
Calibration MQTT Protocol V3
```

当前源码中的 `CAL_MQTT_V2`、`calibration-mqtt-v2.ts` 等只表示 Legacy 实现。

新的 Flash 校准结构在逐字节布局最终冻结前统一称为：

```text
Target Calibration Record
```

在 Header、字段 offset、Endian、CRC 范围和 Golden Vector 完成冻结之前，不使用“Calibration Record V2”作为目标名称，也不提前指定新的 Storage `formatVersion`。

当前源码中的：

```text
SYS_CALIBRATION_STORAGE_FORMAT_VERSION = 3
```

只代表当前旧 Storage Record 实现版本，与目标 MQTT Protocol V3 不是同一个版本序列。

## 3. 50W 当前目标参数

```text
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
```

固定关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

## 4. 已废弃的旧口径

以下内容不得继续作为目标设计：

- 50W默认 `SET_OUTCUR=890mA`；
- 默认 `HWMAX=1680mA` 并与 Hardware Max 合并；
- CAL_MQTT_V2 作为最终协议；
- Calibration Context 绑定运行 SET_OUTCUR；
- Calibration Context 绑定运行输出电压；
- `calibratedMaxCurrentMa` 作为普通运行授权；
- 缺少 Calibration 时禁止普通非零输出；
- 设备 CAPABILITIES 返回多功率 `profilesCsv`；
- 旧共享2KiB擦除页中切分多个独立持久化事务；
- 21点校准；
- 校准前必须满足最终精度门槛。

## 5. 旧文档分类

以下文件已改为历史资料：

- `docs/calibration/*`
- `docs/量产校准固件开发方案.md`
- `docs/Codex_量产校准固件任务书.md`
- `docs/校准协议冻结与F2-F4实现门禁.md`
- 根目录 `一体化电源电流校准_Codex执行文档.md`

`protocol/fixtures/CAL_MQTT_V2/*` 与旧V2测试可继续用于 Legacy 回归，但不能作为 Protocol V3 的 Golden Vector。

## 6. 仍需进一步冻结的项目

在正式开始跨端 Protocol V3 实现前，联合审核文档还需要冻结：

- 每个 V3 Operation 的 Request/Response 字段；
- RAW V3 精确 Schema、类型和单位；
- State / Result 数值；
- Target Calibration Record 逐Byte布局；
- 新 Storage `formatVersion`；
- CRC覆盖范围与Endian；
- SET_POINT Raw PWM 与 OP_PWM_OFFSET 的底层域定义；
- BL0942 Voltage Correction 固定点格式；
- Golden Vector。
