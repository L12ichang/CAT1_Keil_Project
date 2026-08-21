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

### 4.7 Config A/B、Runtime A/B 职责与逐 Byte Record

#### 4.7.1 共用事务外壳

Config / Runtime / Calibration 都必须是真正 A/B；Config 与 Runtime 的新 Record 共用以下 20B Header：

```text
0x000  4B   Magic
0x004  2B   formatVersion
0x006  2B   recordLength
0x008  4B   generation
0x00C  2B   payloadLength
0x00E  2B   reserved = 0
0x010  4B   payloadCrc32
0x014       Payload
...         recordCrc32
...         commitWord = 0xC0A17EED
```

除明确标注的兼容字节块外，所有多字节数值使用 Little Endian。CRC统一使用 CRC-32/ISO-HDLC：

```text
Poly   = 0x04C11DB7
RefIn  = true
RefOut = true
Init   = 0xFFFFFFFF
XorOut = 0xFFFFFFFF
Check("123456789") = 0xCBF43926
```

规则：

```text
payloadCrc32 = CRC32(完整Payload)
recordCrc32  = CRC32(Header + Payload)
```

`recordCrc32`自身和`commitWord`不参与Record CRC；`commitWord`必须最后写。

#### 4.7.2 Config A/B：CFG1，1116B Record

Config A/B 保存当前正常业务中掉电后仍需保留的 Factory/User、Property、Plan 等配置。Product Profile 本身仍由 Keil Target 编译确定，不以 Flash 为权威来源。

冻结：

```text
Magic         = "CFG1"
formatVersion = 1
payloadLength = 1088B = 0x0440
recordLength  = 1116B = 0x045C
```

Record：

```text
0x000..0x013  Common Header             20B
0x014..0x453  ConfigPayloadV1          1088B
0x454..0x457  recordCrc32                4B
0x458..0x45B  commitWord=0xC0A17EED      4B
Total                                  1116B
```

`ConfigPayloadV1`：

```text
Payload Offset
0x000..0x07F  factoryUserCompat[128]    128B
0x080..0x083  deviceAddress             u32 LE
0x084..0x1B3  propertyConfig            304B
0x1B4..0x43B  planRecords[8]            648B
0x43C..0x43F  reserved                    4B = 0
Total                                  1088B
```

`factoryUserCompat[128]` 是当前 `factory_user_buff[128]` 的兼容字节镜像，其内部既有字节语义保持不变，不按新Record的Little Endian规则重新解释。该兼容块中若存在 MID/RS3 等 Product-owned 影子字段，它们**不是权威 Product Profile**；加载后必须由当前 Keil Product Target 重新同步/校验，不能借 Flash 切换功率型号。

`propertyConfig` 304B 使用显式 Little Endian 编码，对齐当前有效 Property 持久字段：

```text
SubOffset
0x000 s32 lng
0x004 s32 lat
0x008 s32 zone
0x00C s32 cns
0x010 s32 dimTp
0x014 s32 polar
0x018 s32 dlmt
0x01C s32 ulmt
0x020 s32 rti
0x024 s32 rtPwr
0x028 s32 di
0x02C s32 sBri
0x030 s32 sBriTm
0x034 char svrIp[32]
0x054 s32 svrPort
0x058 s32 uPeriod
0x05C s32 hPeriod
0x060 s32 tPeriod
0x064 s32 almValue[17]       // 68B
0x0A8 s32 almRecValue[17]    // 68B
0x0EC s32 almEn[17]          // 68B
Total = 0x130 = 304B
```

`planRecords[8]` 复用当前 ZK Plan 业务语义，但不再带旧独立 Flash Header/Checksum。每条固定 81B：

```text
PlanRecordV1，81B
0x00 u8 valid
0x01 u8 en
0x02 u8 id
0x03 u8 type
0x04 u8 priority
0x05 u8 weekMask
0x06 u8 jobCount
0x07 s8 setOffset
0x08 s8 riseOffset
0x09 8B startDateTime
0x11 8B endDateTime
0x19 28B job[0]
0x35 28B job[1]
Total = 0x51 = 81B
```

DateTime固定8B：

```text
u16 year LE
u8 mon
u8 day
u8 hour
u8 min
u8 sec
u8 reserved=0
```

Job固定28B：

```text
u8 cnsMask
u8 timeType
u8 actionCount
u8 reserved=0
6 × Action
```

Action固定4B：

```text
u16 minute LE
u8 brightness
u8 reserved=0
```

Config 不保存 Calibration 数据，也不保存累计运行统计。配置发生实际变化时才提交新的 Config A/B Generation，不做周期性无变化写入。

#### 4.7.3 Runtime A/B：RUN1，76B Record

Runtime A/B 对外运行统计语义保持当前固件不变；只改变存储方式。

用户确认的持久统计只有：

```text
totalRunTimeSec       总运行时间，秒；设备上电期间无论亮灯与否都累计
totalLightTimeSec     总亮灯时间，秒；PWM/调光输出 > 0 时累计
totalEnergy001Wh      总能耗，单位0.01Wh
calibrationInhibit    Calibration durable safety flag
```

本次上电的以下值只存在 RAM，不进 Flash：

```text
currentRunTimeSec
currentLightTimeSec
currentEnergy001Wh
```

设备重新上电后上述三个 current 值从 0 开始。

为保持当前 OTA 成功/失败上报的 durable 行为，Runtime Payload 同时保留 OTA report 内部状态；它属于内部运行恢复状态，不属于对外运行统计字段。

冻结：

```text
Magic         = "RUN1"
formatVersion = 1
payloadLength = 48B = 0x0030
recordLength  = 76B = 0x004C
```

Record：

```text
0x000..0x013  Common Header             20B
0x014..0x043  RuntimePayloadV1          48B
0x044..0x047  recordCrc32                4B
0x048..0x04B  commitWord=0xC0A17EED      4B
Total                                    76B
```

`RuntimePayloadV1`：

```text
Payload Offset
0x00 u32 totalRunTimeSec
0x04 u32 totalLightTimeSec
0x08 u32 totalEnergy001Wh
0x0C u8  calibrationInhibit
0x0D u8  reserved[3] = 0

0x10 u32 otaReportState
0x14 char otaId[8]
0x1C u32 otaUrlHash
0x20 u32 otaImageChecksum
0x24 u32 otaImageSize
0x28 u16 otaDeviceType
0x2A u16 reserved = 0
0x2C u32 otaRetryCount
Total = 0x30 = 48B
```

Runtime 正常保存策略：

```text
RAM实时累计
→ 每8小时提交一次Runtime A/B
→ 检测到掉电时再立即提交一次Runtime A/B
```

安全/恢复字段例外：

```text
BEGIN                  -> calibrationInhibit=1，立即提交
ABORT / RELEASE        -> calibrationInhibit=0，立即提交
启动完成安全恢复后      -> calibrationInhibit=0，立即提交
OTA durable状态发生关键变化 -> 立即提交
```

禁止每秒写 Flash，禁止 HEARTBEAT 写 Runtime Flash。

#### 4.7.4 A/B 原子提交规则

Config / Runtime / Calibration 统一：

```text
读取A/B并校验
→ 选择Generation较新的有效页
→ 选择另一页为inactive
→ 擦除inactive 2KiB page
→ RAM构造完整新Record，generation=old+1
→ 写Header + Payload + recordCrc32
→ Readback验证PayloadCRC + RecordCRC
→ 最后写commitWord
→ 再读回commitWord
→ 新页成为active
```

禁止一次保存同时擦 A/B 两页；禁止先擦唯一有效页。

`generation=0` 无效，首个有效 Generation=1；递增溢出时跳过0，并使用 wrap-safe 比较选择新旧。

#### 4.7.5 物理页映射

```text
0x08005000 Config A       CFG1
0x08005800 Config B       CFG1
0x08006000 Calibration A  CAL4
0x08006800 Calibration B  CAL4
0x08007000 Runtime A      RUN1
0x08007800 Runtime B      RUN1
```

每页2KiB，一个物理擦除页一个Owner。Record之外剩余字节保持擦除态/Reserved，不允许另一个事务 Owner 复用。

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
- 每秒写Runtime Flash；
- Runtime持久化本次上电currentRun/currentLight/currentEnergy；
- Config/Runtime采用一次保存同时覆盖A和B的伪主备。

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

并且 Config / Runtime Record 的逐 Byte Layout 已在本文 4.7 节完成技术冻结：

```text
Config  = CFG1 / Payload 1088B / Record 1116B
Runtime = RUN1 / Payload 48B   / Record 76B
```

因此该项不再留给 Codex 设计。

## 8. 后续问题分类

从本版开始，Codex不再自由设计协议或Persistent格式。

后续问题只分：

1. `IMPLEMENTATION_MISMATCH`：代码没有按冻结文档实现，修改代码；
2. `HIL_SPEC_DEFECT`：真实Keil/HIL/内存/硬件测试证明冻结规范存在问题，提供证据后显式升级版本。

禁止因为旧V2 fixture、编码习惯或个人偏好再次把已确认设计随意改回另一套。

## 9. 2026-08-22 补充冻结：Config A/B 页首 4B 与 Boot OTA Flag

本节只补充本轮已经人工确认的 Boot/Config 物理兼容规则；不删除、不改写前文已经冻结的 Wire、Payload、Record、CRC、A/B、Runtime、Calibration、HIL 和非回归内容。

### 9.1 Boot 不修改，六个 2KiB 页地址保持不变

```text
0x08005000~0x080057FF  Config A
0x08005800~0x08005FFF  Config B
0x08006000~0x080067FF  Calibration A
0x08006800~0x08006FFF  Calibration B
0x08007000~0x080077FF  Runtime A
0x08007800~0x08007FFF  Runtime B
```

Config A/B 仍属于原定 Config 页；不新增页，不移动 Calibration/Runtime，不修改 APP 起始地址。

### 9.2 Config A/B 页首统一保留 4B，CFG1 从 PageBase+4 开始

```text
Config A:
0x08005000~0x08005003  Boot OTA Flag / Config页内保留兼容字段
0x08005004             CFG1 Record 物理起点

Config B:
0x08005800~0x08005803  Reserved / 对称保留
0x08005804             CFG1 Record 物理起点
```

冻结解释：

- Config A/B 页仍各为 2048B；
- CFG1 可用页内空间为 2044B；
- CFG1 Record 仍为 1116B；
- CFG1 Record 内部相对 Offset、1088B Payload、CRC、CommitWord、Golden Vector 全部不变；
- 只改变 CFG1 的物理起始地址为 `PageBase + 4`。

### 9.3 旧布局首次进入 V3

发现旧 V2 Persistent 或非法 V3 布局时，允许按当前开发/HIL策略直接格式化旧 12KiB 参数区，不做迁移：

```text
确认当前不处于 OTA pending
→ 直接擦除 0x08005000~0x08007FFF 六个 2KiB 页
→ 0x08005000 回到 0xFFFFFFFF
→ 从 0x08005004 / 0x08005804 建立 V3 Config
→ Calibration A/B 空
→ Runtime A/B 空
```

不迁移旧 Config/User/Plan/Runtime/Calibration；不碰 Bootloader 代码、APP、OTA Backup。合法 V3 Persistent 建立后不得重复格式化。

### 9.4 正常远程 OTA

现有 Boot OTA 约定继续保留：

```text
0x08005000 = 0xAA5555AA  -> Boot 进入 OTA Copy 流程
0x08005000 != 0xAA5555AA -> 不触发 OTA，按正常 APP 校验/跳转流程处理
```

正常远程 OTA 不格式化这 12KiB 参数区。APP 完成 OTA Backup 准备后停止后续 Config A/B 提交，写入 `0xAA5555AA` 后立即复位；写标志后到复位进入 Boot 前禁止再擦写 Config 页。

### 9.5 新 APP 清除 OTA Flag

新 APP 启动后继续保持现有“OTA 成功后清除升级标志”的业务语义，但不得破坏唯一有效 Config：

```text
读取 Config A/B 最新有效 CFG1
→ 确保另一页至少存在一份完整有效 Config
→ 擦除 Config A 页，使 0x08005000 恢复 0xFFFFFFFF
→ 需要时从 0x08005004 重建 Config A CFG1
```

禁止为了清除 OTA Flag 先擦除唯一有效 Config。
