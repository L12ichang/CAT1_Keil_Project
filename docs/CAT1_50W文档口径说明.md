# CAT1 50W 文档口径说明

> 生效日期：2026-08-20
>
> 固件目标分支：`done/cat1-product-profile-cal-context-20260817`
> 状态：`TARGET_SPEC_FROZEN / IMPLEMENTATION_ALIGNMENT_PENDING`

## 1. 唯一权威文档

CAT1 50W 后续设计、实现、联调和验收只以以下两份文档为规范依据：

1. [`CAT1_50W校准固件基线与上位机对接方案.md`](CAT1_50W校准固件基线与上位机对接方案.md)；
2. [`CAT1_50W固件与上位机联合审核清单.md`](CAT1_50W固件与上位机联合审核清单.md)。

发生数值、协议版本、字段、状态机或安全门禁冲突时，上述两份文档优先；其他文档不得反向覆盖它们。

## 2. 冻结目标口径

| 项目 | 目标口径 |
|---|---|
| Hardware Max | 1680mA |
| 默认 HWMAX | 1400mA |
| 默认 SET_OUTCUR | 893mA |
| RS3 | 120mΩ |
| 正式校准点 | 11 点，Level `0/20/.../200` |
| 校准 MQTT | Protocol V3 紧凑协议 |
| Calibration Record | V2；这是 Flash/字节记录版本，与 MQTT Protocol V3 是两个独立版本空间 |
| 参数关系 | `SET_OUTCUR <= HWMAX <= Hardware Max` |
| Calibration 绑定 | 不绑定运行 SET_OUTCUR，不以运行输出电压或 `calibratedMaxCurrentMa` 作为普通运行授权 |
| CAPABILITIES | 不发布 `profilesCsv` 和多型号 Catalog |

## 3. 文档分类

| 分类 | 文档 | 使用规则 |
|---|---|---|
| 目标规范 | 上述两份权威文档 | 用于后续实现与验收 |
| 历史规划 | `量产校准固件开发方案.md`、`Codex_量产校准固件任务书.md`、根目录旧执行文档 | 只保留问题来源和历史任务，不得用于冻结新字段或参数 |
| 实现快照 | `docs/calibration/` 下 V2/890mA/1680mA 相关证据文档 | 只说明当时源码做到了什么，不代表目标规范 |
| 兼容夹具 | `protocol/fixtures/CAL_MQTT_V2/` | 只用于现有 V2 实现回归；V3 上线前必须建立独立 V3 fixture，不得复用 V2 名义 |

## 4. “目标规范”与“当前实现”边界

目标分支名称用于确定文档口径，不等于其中每一项已经完成。当前源码、V2 fixture、Keil 产物、设备 ACK/回读和 HIL 证据必须分别核对。

`MQTT Protocol V3`、`Calibration Record V2` 和历史源码中的 `Flash record v3` 不属于同一版本序列。文档引用版本号时必须同时写明对象，禁止只写“V2”或“V3”。

旧文档中出现以下内容时，一律视为历史实现或已废弃设计，不得继续作为新实现依据：

- `SET_OUTCUR=890mA`；
- 默认 `HWMAX=1680mA`；
- `CAL_MQTT_V2` 是最终冻结协议；
- Calibration Context 绑定运行 SET_OUTCUR/运行电压；
- `calibratedMaxCurrentMa` 是普通运行授权；
- CAPABILITIES 发布 `profilesCsv` 或多型号 Catalog。
