# CAT1 50W 文档口径说明

> 生效日期：2026-08-21  
> 分支：`main`  
> 状态：`CURRENT_CODE_V2 / TARGET_V3_CONTRACT_FROZEN / IMPLEMENTATION_PENDING`

## 1. 当前唯一权威文档

后续实现和审核只以以下三份文档为目标规范：

1. 固件：`docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合字段合同与审核：`docs/CAT1_50W固件与上位机联合审核清单.md`

其中第3份是 **V3 Wire / Fingerprint / Payload / Storage / Golden Vector 唯一字段级真源**。如果三份文档发生字段级冲突，以第3份为准并同步修正前两份，禁止由Codex自行选择。

本《文档口径说明》记录已经由人工最终确认的冻结决策；在三份权威文档尚未同步某一最新冻结项时，以本文最新冻结项为准，随后必须同步回三份权威文档。

旧文档、旧fixture和旧源码只用于理解V2现状，不得覆盖以上目标规范。

## 2. 当前代码状态

```text
CAT1_Keil_Project 当前代码 = V2
tc-desktop-client 当前代码 = V2
V3功能代码                  = 尚未实现
当前工作目标                = 将两端从V2完整升级到V3
```

当前固件 `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION=2`、上位机 `calibration-mqtt-v2.ts`、旧198B Payload、旧Context、旧Storage formatVersion=3都属于V2现状。

**文档冻结不等于代码已实现。** 后续必须用真实代码、Keil编译、单测和HIL证明V3完成。

## 3. V3版本空间

```text
Wire Protocol              = Calibration MQTT Protocol V3
Calibration PayloadVersion = 1
Calibration StorageFormat  = 4
```

三者是不同版本空间，不得混淆。

## 4. 最终冻结的核心设计

### 4.1 50W与参数职责

```text
50W Hardware Max       = 1680mA
50W Default HWMAX      = 1400mA
50W Default SET_OUTCUR = 893mA
RS3                    = 120mΩ
11 points              = Level 0/20/.../200
logicalPwm             = 0..1000
```

```text
Hardware Max = Product Profile固定硬件能力
HWMAX        = Factory Config上限
SET_OUTCUR   = User Config当前100%目标电流
CV           = 上位机本次电子负载工况
Tolerance    = 上位机APPLY后验收策略
Calibration  = Correction
```

继续保证：改SET / CV / Tolerance不使Calibration失效。

SET_OUTCUR普通Wire暂保留`Factory.SET_OUTCUR`兼容入口，但固件内部写User Config。

### 4.2 MQTT V3——字符串协议

正式Operation：

```text
CAP
BEGIN
HEARTBEAT
SET_POINT
RAW
STAGE
APPLY
SET_OUTPUT
COMMIT
READ
ABORT
RELEASE
DIAG
```

公共字段：

```text
v / op / sid / seq / rc / st
```

大数据字段统一：

```text
payloadHex
```

第一版明确：

- 不使用数字Operation；
- 不使用`SET_VERIFY`，统一`SET_OUTPUT`；
- 不使用`READ_INFO/READ_CHUNK`，统一单个`READ`。

### 4.3 Result Code

```text
0  OK
1  BAD_REQUEST
2  BAD_STATE
3  BUSY
4  SESSION_EXPIRED
5  RANGE_ERROR
6  DATA_STALE
7  CRC_ERROR
8  FLASH_ERROR
9  HARDWARE_FAULT
10 PROFILE_MISMATCH
```

### 4.4 Wire State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

MCU不增加PASS/FAIL Wire State；最终精度由上位机判断。

### 4.5 Wire Payload ≠ Flash Record

继续冻结：

```text
Wire Calibration Payload = 244B
Flash Calibration Record = 272B
```

上位机：

```text
生成244B Calibration Payload
→ STAGE payloadHex
```

固件：

```text
RAM staged
→ APPLY
→ COMMIT时包装272B CAL4 Record
→ 管理Generation / A-B Slot / Record CRC / CommitWord
```

上位机不构造Flash Record事务字段。

### 4.6 Product Fingerprint 与 50W 正式硬件值

Fingerprint输入固定为18B Little Endian：

```text
profileVersion      u16
profileId           u16
mid                 u8
hardwareRevision    u16
ratedPowerW         u16
rs3Mohm             u16
hardwareMaxMa       u16
pwmFullScale        u16
pwmPolarity         u8
ocoHardwareRevision u16
```

使用CRC-32/ISO-HDLC。

50W E1.1 正式冻结：

```text
hardwareRevision    = 0x0101   // E1.1
pwmPolarity         = 1        // Inverted / 负逻辑
ocoHardwareRevision = 0x0101
```

禁止参与Fingerprint：

```text
SET_OUTCUR
当前HWMAX
CV
Tolerance
MQTT/平台参数
Plan
运行历史
Calibration Generation
```

### 4.7 Config A/B、Runtime A/B 职责

Config A/B 只保存“掉电后仍需保留的配置”，包括：

```text
Factory Config
+ User Config
+ Plan / MQTT / 告警 / 温控 / 调光 / 上报周期等持久配置
```

Product Profile 不写入 Config A/B，由 Keil Product Target 编译确定。

Calibration 数据不写入 Config A/B，单独属于 Calibration A/B。

Runtime A/B 只保存运行统计和必要的 durable runtime safety 状态。第一版至少包括：

```text
totalRunTimeSec          u32   总运行时间，秒
currentRunTimeSec        u32   当前运行时间，秒
totalLightTimeSec        u32   总亮灯时间，秒
currentLightTimeSec      u32   当前亮灯时间，秒
totalEnergy001Wh         u32   总能耗，0.01Wh
currentEnergy001Wh       u32   当前能耗，0.01Wh
calibrationInhibit       u8    Calibration durable inhibit
```

运行统计在 RAM 实时累计，不允许每秒擦写 Flash。正常统计采用周期性 Checkpoint；安全字段 `calibrationInhibit` 按状态转换立即持久化。

Config / Calibration / Runtime 三类 A/B 都采用各自独立的事务 Record；一个物理擦除页一个 Owner，不允许跨 Owner 共用页面。

### 4.8 V3 durable boot/session inhibit

正式冻结：

```text
Owner   = Calibration Service
Storage = Runtime A/B
```

行为：

```text
BEGIN
→ calibrationInhibit = 1
→ 立即持久化 Runtime A/B

ABORT / RELEASE
→ calibrationInhibit = 0
→ 立即持久化 Runtime A/B
```

校准过程中异常掉电/复位：

```text
启动读取Runtime A/B
→ 发现calibrationInhibit=1
→ PWM保持OFF
→ 丢弃旧RAM session / staged数据
→ 加载最新有效Committed Calibration
→ 完成安全恢复
→ 清calibrationInhibit并持久化
→ State回IDLE
→ 再恢复普通业务
```

明确不持久化：

```text
sid
seq
heartbeat状态
lease剩余时间
RAM staged Calibration Payload
```

HEARTBEAT 不写 Runtime Flash。

### 4.9 Flash 6×2KiB

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

一个物理擦除页一个Owner。

### 4.10 当前开发阶段初始化策略

发现旧Persistent或非V3新布局：

```text
只格式化0x08005000~0x08008000
→ 初始化V3 Config
→ Calibration空
→ Runtime空
```

当前开发阶段：

- 不迁移旧Calibration；
- 不迁移旧Config/User；
- 不迁移旧Plan；
- 不迁移旧Runtime；
- 不碰Boot / APP / OTA Backup。

合法V3 Persistent建立后不得每次启动重复格式化。

### 4.11 完整Calibration

每次必须全部生成：

```text
Output
+ OCO
+ BL0942 Voltage
+ BL0942 Current
+ BL0942 Power
```

不做UpdateMask，不做局部更新。

### 4.12 PASS / FAIL

```text
STAGE
→ APPLY
→ SET_OUTPUT
→ RAW + 外部标准仪器

PASS → COMMIT
FAIL → ABORT
```

MCU不判断±1%/±2%，Tolerance只属于上位机。

### 4.13 BL0942 Voltage

V3第一版：

```text
Gain-only Q24
```

但这不是永久禁止Offset。

如果多输入电压HIL证明Gain-only不能满足Tolerance：

```text
当前版本判FAIL
→ 升级PayloadVersion
→ 必要时升级MQTT Protocol Version
→ 新版本可采用Gain + Offset或其他实测模型
```

禁止Codex在PayloadVersion=1的244B结构中偷偷加入Offset。

### 4.14 Keil单功率Target

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

每个Target只允许一个`PRODUCT_TARGET_xxx`：

```text
未选择 -> 编译失败
多选   -> 编译失败
必填Profile/Fingerprint字段未冻结 -> 编译失败
```

单个bin禁止携带其他功率Profile Catalog/参数/字符串。

## 5. 明确废弃的旧口径

以下不得继续作为目标V3设计：

- SET_OUTCUR=890mA作为新默认；
- HWMAX与Hardware Max合并；
- CAL_MQTT_V2作为最终协议；
- Calibration绑定SET/CV/calibratedMax；
- 无Calibration禁止正常输出；
- CAP返回多型号profilesCsv；
- **数字Operation `o=0..13`**；
- **短字段`s/q`作为V3正式公共字段**；
- `SET_VERIFY`；
- `READ_INFO / READ_CHUNK`第一版；
- FAULT作为第五个正常流程State；
- 上位机发送完整Flash Record；
- 校准前按最终Tolerance直接FAIL；
- BL0942周期Reset保活；
- 在244B PayloadVersion=1偷偷增加Voltage Offset；
- 把sid/seq/heartbeat/staged Payload持久化到Runtime；
- 每秒写Runtime Flash。

## 6. 仍然保留、不得因协议重冻而删除的工程设计

以下不是“协议细节”，同样属于正式实施规范：

- 无Calibration成熟Default PWM + OP_PWM_OFFSET；
- Calibration范围外合法Target fallback，不直接PWM=0；
- OCO Raw保护链与Corrected业务链分开；
- BL0942 Freshness和可验证根因链；
- 异常后有限恢复，不周期reset；
- 上位机稳定窗口、多样本和最大超时；
- 电子负载safeOff / INPUT OFF；
- Quick/Full独立验证点；
- `audit.jsonl`完整生产证据；
- 成熟MQTT/Serial/Persistence基础设施不无关重写；
- Boot/APP/OTA/普通MQTT/RTC/Plan/CAT1非回归；
- Windows Keil正式Build + 真实50W HIL。

## 7. 审计遗留项状态

以下三项已人工确认并关闭：

```text
Config A/B、Runtime A/B Record职责/字段归属        CLOSED
V3 durable boot/session inhibit Owner/存放位置    CLOSED
50W hardwareRevision/pwmPolarity/OCO Revision    CLOSED
```

Runtime / Config Record 的最终逐 Byte Layout 在实现前必须按上述字段归属机械冻结，但不得改变本节已经确认的 Owner 和语义。

## 8. 后续问题分类

从本版开始，Codex不再自由设计协议。

后续问题只分：

1. `IMPLEMENTATION_MISMATCH`：代码没有按冻结文档实现，修改代码；
2. `HIL_SPEC_DEFECT`：真实Keil/HIL/内存/硬件测试证明冻结规范存在问题，提供证据后显式升级版本。

禁止因为旧V2 fixture、编码习惯或个人偏好再次把已确认设计随意改回另一套。