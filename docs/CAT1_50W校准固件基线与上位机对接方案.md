# CAT1 50W 一体化电源——校准固件 V2→V3 修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 分支：`main`  
> 文档定位：**固件侧唯一实施基线**  
> 当前代码状态：**V2，尚未进行任何 V3 功能修改**  
> 目标：**将当前 V2 固件升级改造为 Calibration MQTT Protocol V3**  
> 字段级唯一真源：`docs/CAT1_50W固件与上位机联合审核清单.md`

---

## 0. 最高优先级事实与本轮边界

当前真实源码仍是 V2：

- `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION = 2`；
- 仍存在旧 `profileContext`；
- 仍绑定 calibrationVoltage / SET / calibratedMax；
- 仍使用旧 198B Calibration Payload；
- 当前 Calibration Storage `formatVersion=3` 属于 Legacy；
- 正常 PWM 仍存在 V2 Voltage/Context 门禁；
- Calibration PWM 底层仍会叠加 `OP_PWM_OFFSET`。

因此本文中的 V3 全部是**待实现目标**。Codex 的任务是把真实代码从 V2 升级到 V3，不是继续修补 V2 Context。

本轮不是重写整个一体化电源。必须保留已经成熟的正常业务，只围绕以下范围实施：

- Product Profile / Factory Config / User Config 参数归属；
- 正常 PWM 与 Calibration PWM 链路；
- 11 点完整校准；
- OCO Raw / Corrected 分流；
- BL0942 Raw、Freshness、Calibration 与长期冻结根因修复；
- Calibration MQTT Protocol V3；
- Config / Calibration / Runtime 6×2KiB A/B；
- 当前开发阶段 Persistent 初始化；
- 与上述功能直接相关的测试、诊断和 HIL。

Boot、APP、OTA Backup、普通 MQTT、RTC、Plan 业务语义、CAT1 正常业务和硬件最后保护不得被无理由改写。

---

## 1. 50W 冻结参数

| 参数 | 冻结值 | 归属 |
|---|---:|---|
| Rated Power | 50W | Product Profile |
| MID | 1 | Product Profile |
| Hardware Max | **1680mA** | Product Profile |
| Default HWMAX | **1400mA** | Factory Config 默认值 |
| Default SET_OUTCUR | **893mA** | User Config 默认值 |
| RS3 | **120mΩ** | Product Profile |
| Formal Points | **11** | Calibration |
| Level | `0,20,...,200` | Calibration |
| Logical PWM Full Scale | **1000** | Product Profile |
| Hardware Revision | **0x0101** | Product Profile / E1.1 |
| PWM Polarity | **1** | Product Profile / Inverted |
| OCO Hardware Revision | **0x0101** | Product Profile |

固定关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

定义：

- `SET_OUTCUR`：用户当前 100% 亮度目标电流；
- `HWMAX`：工厂允许用户配置 SET_OUTCUR 的上限；
- `Hardware Max`：产品硬件绝对能力边界。

---

## 2. Keil 多 Target / 单功率独立固件——实现门禁

同一套公共源码通过 Keil Target 生成不同功率独立固件：

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

每个 Target **只允许定义一个**：

```text
PRODUCT_TARGET_50W
PRODUCT_TARGET_75W
PRODUCT_TARGET_100W
PRODUCT_TARGET_150W
PRODUCT_TARGET_200W
PRODUCT_TARGET_240W
```

编译门禁：

```text
未选择任何 PRODUCT_TARGET_xxx  -> #error / 编译失败
同时选择两个及以上              -> #error / 编译失败
目标 Profile 必填参数未冻结       -> #error / 编译失败
Fingerprint 必填硬件字段未定义     -> #error / 编译失败
```

单个 `.bin` 只能包含当前功率 Product Profile。禁止继续保留运行时 `_profiles[]` 多型号 Catalog、`find(profileId)` 运行时切功率或其他功率参数/字符串进入当前产物。

切换功率时原则上只改变 Product Profile / Target 参数，不复制：

- MQTT V3；
- Calibration 状态机；
- Output / OCO / BL0942 算法；
- Flash A/B；
- OTA/保护公共代码。

当前第一阶段只要求 `CAT1_50W` 完整冻结和 HIL。

---

## 3. Product / Factory / User 参数职责

### 3.1 Product Profile

只包含产品固定身份与硬件能力：

- ProfileId / ProfileVersion；
- MID / Model；
- Rated Power；
- Hardware Max；
- RS3；
- PWM Full Scale / PWM Polarity；
- Hardware Revision；
- OCO Hardware Revision；
- 固定硬件保护边界；
- Product Fingerprint。

### 3.2 Factory Config

工厂/开发可调：

- HWMAX；
- `OP_PWM_OFFSET`；
- SN / 生产信息；
- 其他确认属于每机工厂修调的数据。

### 3.3 User Config

用户运行数据：

- SET_OUTCUR；
- 用户可调温控；
- 告警；
- 平台/MQTT；
- 上报周期；
- 调光；
- Plan 等正常运行配置。

### 3.4 SET_OUTCUR Wire 兼容——保持方案 A

普通属性 Wire 第一版保持兼容：

```json
{"Factory":{"SET_OUTCUR":893}}
```

但固件内部必须路由并保存到：

```text
User Config.SET_OUTCUR
```

不得继续把 SET_OUTCUR 当 Factory Storage 数据。

`Factory.HWMAX_OUTCUR` 继续作为 Factory 命令，合法性：

```text
0 < HWMAX <= Hardware Max
```

禁止要求 `HWMAX == Hardware Max`。

---

## 4. SET / HWMAX / CV / Tolerance / Calibration 永久解耦

以下语义不再混用：

```text
Hardware Max = 产品硬件绝对能力
HWMAX        = 工厂配置上限
SET_OUTCUR   = 当前用户100%目标电流
CV           = 本次校准电子负载工况
Tolerance    = 上位机验收策略
Calibration  = Correction
```

必须保证：

```text
改 SET        -> Calibration 不失效
改 CV         -> Calibration 不失效
改 Tolerance  -> Calibration 不失效
```

Calibration 有效性不得绑定：

- SET_OUTCUR；
- 当前 HWMAX；
- 校准 CV；
- 运行输出电压；
- Tolerance；
- calibratedMaxCurrent。

`BOUND_OUTPUT_VOLTAGE_01V` 不再作为 Calibration 或非零 PWM 授权条件。

---

## 5. 正常输出链与 OP_PWM_OFFSET

### 5.1 无有效 Output Calibration

必须保留成熟旧链：

```text
SET_OUTCUR × Brightness
        ↓
Target Current / 默认 PWM 模型
        ↓
OP_PWM_OFFSET
        ↓
硬件保护仲裁
        ↓
PWM / CCR
```

**无 Calibration 不能导致 PWM=0。**

### 5.2 有有效 Output Calibration

```text
SET_OUTCUR × Brightness
        ↓
Target Current
        ↓
11点 Output Calibration 反插值
        ↓
直接得到高精度 logical PWM
        ↓
硬件保护仲裁
        ↓
PWM / CCR
```

有有效 Calibration 时不再重复叠加 `OP_PWM_OFFSET`。

### 5.3 合法 Target 超 Calibration 覆盖范围

禁止激进外推，也禁止直接 PWM=0：

```text
合法Target超出Calibration覆盖
→ 回退 Default PWM + OP_PWM_OFFSET
→ 记录 CAL_OUT_OF_RANGE / 等价诊断
```

### 5.4 Calibration SET_POINT

正式采点：

```text
Level 0   -> logicalPwm 0
Level 20  -> logicalPwm 100
...
Level 200 -> logicalPwm 1000
```

即：

```text
logicalPwm = level * 5
```

采点时：

- 不应用旧 Output Calibration；
- 不叠加 OP_PWM_OFFSET；
- 不允许上位机直接写 CCR；
- 保留硬件最后保护。

底层必须拆出 Default Path 与 Raw/Calibrated Path，禁止统一出口无条件 `pwm + OP_PWM_OFFSET`。

---

## 6. 每次必须完成一整套 Calibration

正式点：

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

每次必须完整生成：

```text
Output
+ OCO
+ BL0942 Voltage
+ BL0942 Current
+ BL0942 Power
```

第一版：

- 不做 `UpdateMask`；
- 不做 Section Merge；
- 不支持只重校某一类；
- 11 点采样期间不逐点写 Flash；
- 只有整套数据完整才允许 STAGE。

---

## 7. Calibration 算法

### 7.1 Output

```text
Actual logical PWM <-> Reference Output Current
```

11 点分段线性。正常运行由 `Target Current` 反插值直接得到 logical PWM，禁止先回到整数百分比再二次量化。

### 7.2 OCO

```text
OCO ADC Raw <-> Reference Output Current
```

运行必须分流：

```text
OCO Raw -> 保守默认换算 -> Protection
   │
   └----> OCO Correction -> Corrected Output Current -> MQTT/业务
```

硬件过流保护不得使用 Calibration 修正、Clamp 或 MQTT 格式化后的值。

### 7.3 BL0942 Current / Power

```text
BL Current Raw <-> Reference Input Current
BL Power Raw   <-> Reference Active Power
```

11 点 Raw→Reference。

### 7.4 BL0942 Voltage——V3 第一版 Gain-only Q24

本版本采用简单 Gain-only：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
finalGainQ24 = median(valid samples)

correctedVoltage01V =
(uint64_t(rawVoltage) * finalGainQ24 + 2^23) >> 24
```

- Gain 由上位机生成；
- 固件只做定点应用；
- 中间乘法必须使用 `uint64_t`；
- V3 PayloadVersion=1 只保存 `u32 voltageGainQ24`。

**重要：Gain-only 是 V3 第一版实现，不是永久禁止 Offset。**

量产前必须使用多个真实输入电压点做 HIL（例如可提供条件下的 180/200/220/240/260V 或等效覆盖）。如果 Gain-only 无法满足 Tolerance：

```text
本版本Voltage HIL FAIL
→ 停止量产放行
→ 明确升级 PayloadVersion
→ 必要时同时升级 MQTT Protocol Version
→ 新版本可采用 Gain + Offset 或其他经实测证明的模型
```

禁止 Codex 在 PayloadVersion=1 / 244B 中偷偷增加 Offset、复用 Reserved 或改变现有字段含义。

---

## 8. BL0942 Freshness 与长期冻结根因修复

Calibration 的前提是 BL0942 Raw 真实、实时、可验证。

必须新增/明确：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

必须检查：

- `HAL_UART_Transmit_IT` / `HAL_UART_Receive_IT` 返回值；
- USART2 ORE / FE / NE；
- HAL `gState / RxState / ErrorCode`；
- RX 重挂；
- TX/RX callback 与业务状态同步；
- timeout 后状态恢复；
- Buffer / Index；
- Tick / 计数溢出；
- BL0942 芯片无响应；
- BL0942 VDD / 供电域。

根因验收必须形成：

```text
触发/自然冻结条件
→ UART/HAL/协议/芯片状态证据
→ 根因定位
→ 修复点
→ 有限恢复路径
→ 再次收到有效帧
→ 长稳结果
```

允许在**已分类异常后**做有限恢复，但：

- 不允许周期 reset；
- 不允许每次 timeout 无限 reset；
- 恢复后必须重新得到有效帧才算成功；
- recovery 次数、失败次数和最后状态必须通过 DIAG 可观测。

旧值不得永久伪装为实时值。校准期间 stale 点不得参与拟合或验证。

---

## 9. Product Fingerprint——精确定义恢复

Fingerprint 只描述“这份 Calibration 对应哪一种固定硬件产品”，不描述设备当前怎么配置。

### 9.1 编码顺序

按以下 **18 bytes** 顺序显式编码：

| Offset | Size | Type | Field |
|---:|---:|---|---|
| `0x00` | 2 | u16 LE | `profileVersion` |
| `0x02` | 2 | u16 LE | `profileId` |
| `0x04` | 1 | u8 | `mid` |
| `0x05` | 2 | u16 LE | `hardwareRevision` |
| `0x07` | 2 | u16 LE | `ratedPowerW` |
| `0x09` | 2 | u16 LE | `rs3Mohm` |
| `0x0B` | 2 | u16 LE | `hardwareMaxMa` |
| `0x0D` | 2 | u16 LE | `pwmFullScale` |
| `0x0F` | 1 | u8 | `pwmPolarity`，0=Normal，1=Inverted |
| `0x10` | 2 | u16 LE | `ocoHardwareRevision` |

```text
FingerprintInputLength = 18B
profileFingerprint = CRC32(FingerprintInput[0..17])
```

CRC32：CRC-32/ISO-HDLC（IEEE），参数与 Calibration Payload/Record 相同：

```text
Poly   = 0x04C11DB7
RefIn  = true
RefOut = true
Init   = 0xFFFFFFFF
XorOut = 0xFFFFFFFF
Check("123456789") = 0xCBF43926
```

禁止直接对 C struct 做 CRC，避免 padding / 编译器差异。

### 9.2 明确禁止参与 Fingerprint

```text
SET_OUTCUR
当前 HWMAX
CV
Tolerance
MQTT / 平台参数
Plan
运行历史
Calibration Generation
```

`hardwareRevision / ocoHardwareRevision / pwmPolarity` 必须由当前 Keil Product Target 明确定义；未定义时编译失败，不允许静默使用 0 作为默认硬件版本。

50W E1.1：

```text
hardwareRevision    = 0x0101
pwmPolarity         = 1
ocoHardwareRevision = 0x0101
```

---

## 10. Calibration MQTT Protocol V3——重新冻结为清晰字符串协议

只修改 `SV=cal` 的 `DT`，外层 Envelope 和普通 `prop/ctrl/rept/alam/ota/plan` 不变。

### 10.1 Operation

V3 第一版固定：

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

正式 Wire 使用字符串 `op`，**不使用数字 Operation Code**。

公共会话字段：

```text
v    Protocol Version = 3
op   Operation string
sid  Session ID
seq  Sequence
rc   Result Code
st   State
```

示例：

```json
{"v":3,"op":"SET_POINT","sid":123456,"seq":8,"level":100}
```

`SET_VERIFY` 退出 V3 第一版，统一使用 `SET_OUTPUT`。

`READ_INFO + READ_CHUNK` 退出 V3 第一版，统一使用单个 `READ` 返回已提交的 244B Calibration Payload Hex。

### 10.2 Result Code——压缩为 11 个

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

精确重复的同一 `sid + seq + op + 参数摘要` 请求应重放第一次响应，不重复副作用；冲突重复归入 `BAD_REQUEST/BAD_STATE`，第一版不增加专用 DUPLICATE code。

### 10.3 State——5 个简单状态

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

```text
IDLE
→ BEGIN → ACTIVE
→ STAGE → STAGED
→ APPLY → APPLIED
→ COMMIT → COMMITTED
→ READ
→ RELEASE → IDLE
```

未提交取消：

```text
ACTIVE / STAGED / APPLIED
→ ABORT
→ safe off
→ 丢弃 staged
→ 恢复旧 committed Calibration
→ IDLE
```

MCU 不增加 PASS/FAIL 状态。

每个 Operation 的 Request/Response 必填字段以联合审核文档为唯一真源。

---

## 11. Wire Calibration Payload 与 Flash Record 分离——保留

这是正式架构：

```text
上位机生成 Calibration Payload
        ↓
STAGE payloadHex
        ↓
固件 RAM staged
        ↓
APPLY 临时生效
        ↓
上位机外部仪器验证
        ↓
PASS -> COMMIT
        ↓
固件包装 Flash Record / Generation / CRC / CommitWord
```

### 11.1 Wire Payload

继续冻结：

```text
Magic          = CALP
PayloadVersion = 1
PayloadLength  = 244B
Endian         = Little Endian
ValidFlags     = 0x001F
```

布局：

```text
0x000..0x013 Header                         20B
0x014..0x03F Output 11×{u16 pwm,u16 refI} 44B
0x040..0x06B OCO    11×{u16 raw,u16 refI} 44B
0x06C..0x0AD BL-I   11×{u32 raw,u16 refI} 66B
0x0AE..0x0EF BL-P   11×{s32 raw,u16 refP} 66B
0x0F0..0x0F3 BL-V   u32 gainQ24             4B
Total = 244B
```

Payload CRC 对完整 244B 使用 CRC-32/ISO-HDLC。

STAGE 使用：

```text
payloadLength = 244
payloadCrc32  = CRC32(payload)
payloadHex    = 488 Hex chars
```

Wire Payload 只保存运行 Correction 所需数据；完整产线 Evidence 留在上位机 Audit/Report。

### 11.2 Flash Record

继续冻结：

```text
Magic         = CAL4
formatVersion = 4
recordLength  = 272B
payloadLength = 244B
Endian        = Little Endian
CommitWord    = 0xC0A17EED
```

```text
0x000 byte[4] CAL4
0x004 u16 formatVersion=4
0x006 u16 recordLength=272
0x008 u32 generation
0x00C u16 payloadLength=244
0x00E u16 reserved=0
0x010 u32 payloadCrc32
0x014..0x107 244B Payload
0x108 u32 recordCrc32
0x10C u32 commitWord
Total = 272B
```

Record CRC 覆盖 `0x000..0x107`，不含自身和 CommitWord。

上位机不生成、不发送 Generation / A-B Slot / Record CRC / CommitWord。

---

## 12. READ——第一版恢复单包完整回读

COMMIT 成功后 `st=COMMITTED`，返回：

```text
generation
payloadLength=244
payloadCrc32
```

随后单个 `READ` 返回：

```text
generation
payloadLength=244
payloadCrc32
payloadHex=488 Hex chars
```

上位机必须：

1. Hex 解码为 244B；
2. 校验 Payload CRC；
3. 与本次 STAGE Payload 逐 Byte 比较；
4. Audit 保存 generation / length / CRC。

第一版不实现 READ_INFO / READ_CHUNK。

原因：244B → 488 Hex chars，使用清晰字段名后仍应满足 2KiB JSON 和 4KiB cJSON Pool 预算。若真实 Keil/HIL 证明单个 READ 不满足内存预算，才作为明确版本升级问题重新评审，不提前复杂化第一版。

---

## 13. RAW / DIAG 原则

RAW 必须同时提供拟合所需 Raw 和 APPLY 后验证所需 Corrected，但字段采用清晰命名，精确 Schema 见联合文档。

原则：

```text
11点拟合      -> Raw + 外部Reference
APPLY后验证   -> Corrected + 外部Reference
```

RAW 中必须包含：

- 当前 Level / Actual PWM；
- OCO Raw；
- BL Voltage/Current/Power Raw；
- Corrected Output Current；
- Corrected Input U/I/P；
- Vo；
- BL age/fresh；
- ValidFlags；
- FaultFlags。

大量 UART / BL0942 诊断计数放 DIAG，不随每个正式点重复发送。

---

## 14. Persistent 6×2KiB 物理布局与 Record——保持物理地址，冻结新格式

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页 2KiB，原则：**一个物理擦除页一个事务 Owner。**

Config / Calibration / Runtime 都必须是真正的 A/B：

```text
读当前有效页
→ RAM构造完整新Record
→ 只擦非活动页
→ 写Header + Payload + RecordCRC
→ Readback / CRC
→ 最后写CommitWord
→ 新 Generation 生效
```

禁止所谓 Main/Backup 在一次保存里同时擦写。

### 14.1 Config / Runtime 共用事务 Header

```text
0x000 4B  Magic
0x004 u16 formatVersion
0x006 u16 recordLength
0x008 u32 generation
0x00C u16 payloadLength
0x00E u16 reserved=0
0x010 u32 payloadCrc32
0x014 Payload
...   u32 recordCrc32
...   u32 commitWord=0xC0A17EED
```

CRC统一 CRC-32/ISO-HDLC：

```text
payloadCrc32 = CRC32(完整Payload)
recordCrc32  = CRC32(Header + Payload)
```

RecordCRC自身和CommitWord不参与RecordCRC；CommitWord最后写。

### 14.2 Config A/B——CFG1

冻结：

```text
Magic         = CFG1
formatVersion = 1
payloadLength = 1088B
recordLength  = 1116B
```

```text
0x000..0x013 Common Header             20B
0x014..0x453 ConfigPayloadV1          1088B
0x454..0x457 recordCrc32                4B
0x458..0x45B commitWord                 4B
```

ConfigPayloadV1：

```text
0x000..0x07F factoryUserCompat[128]
0x080..0x083 deviceAddress u32 LE
0x084..0x1B3 propertyConfig 304B
0x1B4..0x43B planRecords[8] 648B
0x43C..0x43F reserved=0
```

`factoryUserCompat[128]`保留当前`factory_user_buff[128]`字节语义，是旧API兼容影子；其中 Product-owned 字段不作为权威来源，加载后必须由当前 Product Target 重新同步/校验。

`propertyConfig`显式304B：

```text
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
0x064 s32 almValue[17]
0x0A8 s32 almRecValue[17]
0x0EC s32 almEn[17]
```

`planRecords[8]`沿用当前ZK Plan语义，每条81B：

```text
u8 valid/en/id/type/priority/weekMask/jobCount
s8 setOffset/riseOffset
8B startDateTime
8B endDateTime
2 × 28B Job
```

DateTime=`u16 year LE + mon/day/hour/min/sec + reserved`；Job=`cnsMask/timeType/actionCount/reserved + 6×Action`；Action=`u16 minute LE + brightness + reserved`。

Config只有配置真实变化才提交，不做周期无变化写入。

### 14.3 Runtime A/B——RUN1

用户确认：运行统计语义保持当前固件，只换存储。

持久化：

```text
totalRunTimeSec       u32 秒，设备上电期间无论亮灯/不亮灯都累计
totalLightTimeSec     u32 秒，PWM/调光输出>0才累计
totalEnergy001Wh      u32，0.01Wh
calibrationInhibit    u8
```

仅RAM、不持久化：

```text
currentRunTimeSec
currentLightTimeSec
currentEnergy001Wh
```

上述三个 current 值每次上电清0。

为保持现有OTA成功/失败上报恢复能力，Runtime同一Payload保留OTA durable report内部状态。

冻结：

```text
Magic         = RUN1
formatVersion = 1
payloadLength = 48B
recordLength  = 76B
```

```text
0x000..0x013 Common Header            20B
0x014..0x043 RuntimePayloadV1         48B
0x044..0x047 recordCrc32               4B
0x048..0x04B commitWord                4B
```

RuntimePayloadV1：

```text
0x00 u32 totalRunTimeSec
0x04 u32 totalLightTimeSec
0x08 u32 totalEnergy001Wh
0x0C u8  calibrationInhibit
0x0D u8  reserved[3]=0
0x10 u32 otaReportState
0x14 char otaId[8]
0x1C u32 otaUrlHash
0x20 u32 otaImageChecksum
0x24 u32 otaImageSize
0x28 u16 otaDeviceType
0x2A u16 reserved=0
0x2C u32 otaRetryCount
```

保存时机：

```text
运行统计：每8小时一次
掉电检测：立即一次
BEGIN：inhibit=1立即一次
ABORT/RELEASE/启动安全恢复完成：inhibit=0立即一次
OTA durable关键状态变化：立即一次
```

禁止每秒写Flash，禁止HEARTBEAT写Runtime。

### 14.4 A/B Generation 与掉电安全

```text
读取A/B
→ 验证Magic/Version/Length/PayloadCRC/RecordCRC/CommitWord
→ 选择Generation较新的有效页
→ 另一页作为inactive
→ 擦inactive整页
→ generation=old+1
→ 写Header+Payload+RecordCRC
→ Readback验证
→ 最后写CommitWord
```

`generation=0`无效，首个有效=1；溢出跳过0，使用wrap-safe比较。禁止先擦唯一有效页。

---

## 15. 当前开发阶段 Flash 初始化策略——不做 Legacy Migration

本阶段目标是先把 V3 在实机完整跑通，因此**开发阶段不做旧 12KB Persistent 数据迁移**。

启动检测到当前 `0x08005000~0x08008000` 不是合法 V3 新布局（包括旧 V2 Persistent 格式）时：

```text
停止Persistent业务写入
        ↓
只擦除 0x08005000~0x08008000 共6个2KiB页
        ↓
初始化 V3 Config
        ↓
Calibration A/B = 空
        ↓
Runtime A/B = 空
```

开发阶段明确：

- 不迁移旧 Calibration；
- 不迁移旧 Config/User；
- 不迁移旧 Plan；
- 不迁移旧 Runtime；
- 不把旧 formatVersion=3 解释为 V3；
- 初始化后使用当前 Product Profile 默认值，例如 50W `HWMAX=1400 / SET_OUTCUR=893`；
- 只在“新 V3 Persistent 不存在/无效”时格式化一次，合法 V3 Config 存在后不得每次启动重复擦写。

**绝对禁止触碰：**

```text
0x08000000~0x08005000 Bootloader
0x08008000~0x08024000 APP
0x08024000~0x08040000 OTA Backup
```

这是一项**开发阶段策略**。未来若要支持已量产 V2 设备直接 OTA 到 V3，必须另行设计、审核和测试迁移版本，不能在当前实现中偷偷加入半套迁移逻辑。

---

## 16. JSON / cJSON 内存预算

当前 V2 固件已有：

```text
ZK_JSON_BUF_SIZE       = 2048B
ZK_CJSON_TX_POOL_SIZE  = 4096B
```

V3 不再靠极端缩短字段名解决内存问题，而是靠：

- CAP 不返回多功率 Catalog；
- ACK 不重复完整 Context/Status；
- RAW 与 DIAG 分离；
- STAGE 大数据统一使用 `payloadHex`；
- READ 只返回单个 244B Payload，不返回 Flash Record；
- 每个 Operation 只返回自身需要字段。

目标：

```text
普通 ACK  < 256B
RAW       < 768B
CAP       < 1024B
READ      < 1024B
所有 TX   < 1536B
```

验收必须同时记录：

- 最终 JSON 长度；
- cJSON TX Pool 峰值；
- 是否出现 `TX Pool Exhausted`。

仅看最终字符串长度不足以证明 4KiB 线性池安全。

---

## 17. 固件实施顺序

1. **先保存当前代码基线并新建实现分支**，不要在 main 直接大范围开发；
2. Keil 多 Target 门禁 + 50W Product Profile；
3. Product / Factory / User Ownership；
4. 开发阶段 12KB 新布局检测与一次性格式化；
5. 实现统一A/B事务codec；
6. Config CFG1 / Runtime RUN1 / Calibration CAL4 真 A/B；
7. Runtime 8小时checkpoint + 掉电保存 + inhibit/OTA关键状态保存；
8. 先恢复“无 Calibration 仍正常输出”；
9. Raw/Calibrated PWM 硬件出口分离；
10. Product Fingerprint 显式 codec / CRC；
11. 244B Payload 显式 codec / CRC / Golden Vector；
12. V3 字符串 `op/sid/seq/rc/st` 与 13 个 Operation；
13. RAW / DIAG；
14. Output Calibration；
15. OCO Calibration与 Protection 分流；
16. BL0942 Freshness 与冻结根因修复；
17. BL U/I/P Calibration；
18. JSON / cJSON Pool 实测；
19. Windows Keil 正式 Build；
20. 与上位机真实 HIL；
21. Config/Runtime/Calibration掉电注入 + OTA边界 + 长稳 + 普通业务回归。

每一步必须保持可编译、可定位、可回滚。禁止一次性同时重写 PWM、Flash、MQTT、BL0942 后再整体找问题。

---

## 18. 允许修改范围

本轮重点允许：

- `sys_product_profile.*`
- `factory_user_data.*`
- `sys_data.*`
- `flash_address_assignment.*`
- `hw_flash.*`（A/B需要时）
- `sys_pwm.*`
- `hw_tim1_pwm2.*`
- `sys_calibration_service.*`
- `sys_calibration_storage.*`
- `sys_calibration_flash.*`
- `sys_calibration_driver_protocol.*`
- `sys_calibration_mqtt.*`
- `sys_calibration_snapshot.*`
- `sys_calibration_curve.*`
- `sys_Vo_Io.*`
- `sys_bl0942.*`
- `hw_uart2.*`
- `zk_runtime_stats.*`

确有直接关系时：

- `zk_property.*`
- `zk_work_plan.*`
- `mqtt_zk_protocol.*`（仅OTA durable storage迁移/接口适配）

修改其他模块必须在提交说明中回答：

```text
为什么与V3 Calibration闭环直接相关？
如果不改，哪条冻结验收无法实现？
```

禁止借本任务进行无关全局重构。

---

## 19. 不得误改的现有业务

### Boot / APP / OTA

保持：

```text
Bootloader  0x08000000~0x08005000
Persistent  0x08005000~0x08008000
APP         0x08008000~0x08024000
OTA Backup  0x08024000~0x08040000
```

不得改变 APP Vector、Boot 校验 APP 机制及既有 metadata contract。

### 普通 MQTT

除 `SV=cal` V3 和必要 `Factory.SET_OUTCUR -> User Config` 兼容外，不重构登录、在线、property、report、alarm、inspection、OTA 等业务协议。

### RTC / Plan

开发阶段旧 Plan 不迁移，但新固件初始化后的 RTC / Plan 业务语义不能被本任务重写。

### CAT1 / USART2

正常 CAT1 业务必须保留；当前 `_4G_CAT_1` 下 USART2 用于 BL0942，不得因为旧命名恢复废弃 485 业务。

### Hard Protection

上位机负责校准流程和工艺安全，但 MCU 继续保留过流、过温、短路等最后生存保护。

---

## 20. 固件验收矩阵

### 正常运行

- [ ] V3 开发初始化后 50W HWMAX=1400、SET=893；
- [ ] 合法 V3 Config 重启不重复格式化；
- [ ] 无 Calibration 可正常输出；
- [ ] 无 Calibration 保留 OP_PWM_OFFSET；
- [ ] SET 修改立即生效并持久化；
- [ ] SET > HWMAX 拒绝；
- [ ] HWMAX > 1680 拒绝；
- [ ] 改 SET / CV / Tolerance 不使 Calibration 失效；
- [ ] Calibration 覆盖范围外合法 Target 走 Default fallback，不直接0输出。

### Keil Target

- [ ] 未选 Product Target 编译失败；
- [ ] 多选 Product Target 编译失败；
- [ ] Profile / Fingerprint 必填字段未冻结编译失败；
- [ ] CAT1_50W.bin 不包含其他功率 Profile / 字符串；
- [ ] 50W E1.1=`hardwareRevision 0x0101 / pwmPolarity 1 / ocoHardwareRevision 0x0101`；
- [ ] 切 Target 不复制公共业务代码。

### Protocol

- [ ] V3 使用字符串 Operation；
- [ ] 公共字段为 `v/op/sid/seq/rc/st`；
- [ ] `SET_OUTPUT` 不再叫 `SET_VERIFY`；
- [ ] 第一版只有单个 `READ`；
- [ ] rc 仅 0..10；
- [ ] State 为 IDLE/ACTIVE/STAGED/APPLIED/COMMITTED；
- [ ] `payloadHex` 命名统一；
- [ ] 精确重复请求不重复副作用。

### Payload / Fingerprint / Storage

- [ ] Wire Payload = 244B；
- [ ] Calibration Flash Record = 272B；
- [ ] Config=`CFG1 / 1088B Payload / 1116B Record`；
- [ ] Runtime=`RUN1 / 48B Payload / 76B Record`；
- [ ] Little Endian与factoryUserCompat例外规则明确；
- [ ] Payload / Record CRC32参数一致；
- [ ] Fingerprint 18B明确编码并通过跨端测试；
- [ ] SET/HWMAX/CV/Tolerance等运行参数不参与Fingerprint；
- [ ] Generation / Record CRC / CommitWord 只由固件管理；
- [ ] CommitWord 最后写；
- [ ] Config/Calibration/Runtime均为真A/B掉电安全；
- [ ] currentRun/currentLight/currentEnergy不持久化；
- [ ] Runtime每8小时checkpoint并在掉电时立即保存；
- [ ] calibrationInhibit状态转换立即保存；
- [ ] OTA durable report行为迁入Runtime后无回归。

### Calibration

- [ ] 每次完整 Output + OCO + BL U/I/P；
- [ ] 不支持局部 UpdateMask；
- [ ] SET_POINT 11点不叠加旧Calibration/Offset；
- [ ] MCU不判断±1%/±2%；
- [ ] APPLY后由上位机 SET_OUTPUT + 外部仪器判定；
- [ ] PASS→COMMIT；
- [ ] FAIL→ABORT；
- [ ] READ返回244B已提交Payload并可逐Byte比对。

### BL0942

- [ ] Raw 真正未校准；
- [ ] Corrected U/I/P进入业务；
- [ ] age/fresh正确；
- [ ] stale不伪装实时；
- [ ] ORE/FE/NE/timeout有分类计数；
- [ ] 有自然冻结/故障注入证据；
- [ ] 长稳不依赖周期reset；
- [ ] Gain-only Q24通过多输入电压HIL，或明确判FAIL并升级版本。

### Flash开发初始化

- [ ] 旧Persistent只格式化`0x08005000~0x08008000`；
- [ ] 不迁移旧Calibration/Config/Plan/Runtime；
- [ ] 不碰Boot/APP/OTA Backup；
- [ ] 初始化完成后不会每次启动重复擦除。

### 回归

- [ ] Windows Keil官方工程编译通过；
- [ ] Boot启动正常；
- [ ] OTA Backup区域未受影响；
- [ ] OTA durable report状态无回归；
- [ ] 普通MQTT正常；
- [ ] RTC/Plan新业务正常；
- [ ] 告警/温控/调光正常；
- [ ] CAT1正常业务无回归；
- [ ] 无 `TX Pool Exhausted`。

---

## 21. 完成定义

固件完成不等于“能跑一次校准”。必须同时满足：

```text
V2真实代码升级到V3
+ 字符串MQTT V3合同一致
+ 244B Wire Payload / 272B Calibration Record
+ CFG1 1116B / RUN1 76B 持久化合同一致
+ Product Fingerprint精确一致
+ 开发期12KB初始化策略正确
+ 正常无校准业务不回归
+ 11点Output/OCO/BL U/I/P闭环
+ Config/Calibration/Runtime三类A/B掉电安全
+ Runtime 8小时/掉电/安全状态保存正确
+ BL0942长期稳定有证据
+ JSON/cJSON有余量
+ Boot/APP/OTA Backup/普通业务无回归
+ 与多功率通用上位机完成50W真实HIL
```

**V3 字段冻结只能覆盖旧协议歧义，不能删除原实施方案中的 Fallback、安全、诊断、分阶段开发、实机验证和非回归要求。**