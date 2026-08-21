# CAT1 50W 文档口径说明

> 生效日期：2026-08-21  
> 当前固件实施分支：`done/cat1-product-profile-cal-context-20260817`  
> 状态：`TARGET_SPEC_AUTHORITY_FROZEN / FIELD_WIRE_CONTRACT_PENDING`

## 1. 当前权威文档

当前功能修改以以下三份文档为目标规范：

1. 固件：`CAT1_Keil_Project/docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合审核：`CAT1_Keil_Project/docs/CAT1_50W固件与上位机联合审核清单.md`

具体开发应在各文档标注的目标分支执行。发生参数、协议、状态机、Flash、Calibration 或安全逻辑冲突时，旧历史文档不得覆盖以上三份目标规范。

## 2. 版本命名统一规则

### Target Wire Protocol

```text
Calibration MQTT Protocol V3
```

只表示新的 `SV=cal` 紧凑 MQTT 协议。

### Legacy Wire Protocol

```text
CAL_MQTT_V2
```

只表示当前旧实现和旧 fixture，不是新目标。

### Target Calibration Record

新 Flash 校准记录在最终逐 Byte 布局冻结前统一称为：

```text
Target Calibration Record
```

不再使用“Calibration Record V2”作为目标名称。

当前源码中的：

```text
SYS_CALIBRATION_STORAGE_FORMAT_VERSION = 3
```

只代表旧 Storage Record 实现版本，与 MQTT Protocol V3 不是同一个版本空间。

最终新 Storage `formatVersion` 必须在联合审核文档冻结 Header、Offset、Endian、CRC 和 Golden Vector 后一次性确定。

## 3. 当前50W参数

```text
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
Formal Points      = 11
Level              = 0/20/.../200
```

关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

## 4. 明确废弃的旧口径

以下内容只属于历史实现：

- `SET_OUTCUR=890mA` 作为新默认值；
- 默认 `HWMAX=1680mA` 与 Hardware Max 合并；
- `CAL_MQTT_V2` 作为最终协议；
- Calibration Context 绑定运行 SET_OUTCUR；
- Calibration Context 绑定运行输出电压；
- `calibratedMaxCurrentMa` 作为普通运行授权；
- CAPABILITIES 发布 `profilesCsv` 或多型号 Catalog；
- 缺少 Calibration 时禁止普通非零输出；
- 旧共享擦除页作为最终 Calibration A/B 布局；
- 21点校准。

## 5. 历史资料规则

`docs/calibration/`、旧量产开发方案、旧 Codex 任务书、旧 F2-F4 文档、根目录旧执行文档以及 `protocol/fixtures/CAL_MQTT_V2/` 只用于历史追溯和 Legacy 回归。

新功能不得因为旧测试仍通过而保留已经废弃的业务规则。

## 6. 当前仍需冻结

在完整跨端实现 Protocol V3 前，还需在联合审核文档冻结：

- 每个 Operation 的 Request/Response 字段；
- RAW V3 精确 Schema；
- State / Result Code；
- Target Calibration Record 逐Byte结构；
- 新 Storage `formatVersion`；
- CRC / Endian；
- SET_POINT Raw PWM 与 OP_PWM_OFFSET 域；
- BL0942 Voltage Correction 固定点格式；
- Golden Vector。
