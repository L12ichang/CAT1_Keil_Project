# CAT1 50W 固件 × 多功率通用上位机——V3 联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 分支：`main`  
> 本文定位：**V2→V3 跨端唯一字段级协议真源 + 最终联合验收基线**  
> 状态：`CURRENT_CODE_V2 / TARGET_V3_CONTRACT_FROZEN / IMPLEMENTATION_PENDING`

---

## 0. 当前实现状态与文档优先级

必须先确认事实：

```text
固件当前代码 = V2
上位机当前代码 = V2
V3功能代码    = 尚未实现
目标          = 两端从V2升级到V3
```

当前源码中的 `CAL_MQTT_V2`、旧 Context、旧 198B Payload、旧 Storage formatVersion=3 只属于 V2 现状，不是 V3 已实现功能。

本文是字段级唯一真源。固件文档负责“怎么改固件”，上位机文档负责“怎么改工作台”，但以下内容发生冲突时必须以本文为准：

- MQTT V3 Operation / 字段 / rc / st；
- Product Fingerprint；
- Calibration Payload；
- CRC / Endian；
- Flash Calibration Record；
- 开发期 Persistent 初始化策略；
- 跨端 Golden Vector。

字段冻结不得删除已有的 Fallback、安全、仪器控制、BL0942 根因分析、分阶段开发、Audit 和非回归要求。

---

# 1. 产品架构与 Keil 多 Target

## 1.1 固件与上位机角色

```text
固件：一个功率段 = 一个独立固件镜像
上位机：一个通用工作台 = 本地多功率ProductProfileRegistry
```

当前第一阶段只完成 50W；设备 CAP 只返回当前固件 Target 的 Product，不返回多功率 `profilesCsv`。

## 1.2 Keil Target

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

每个 Target 只允许一个：

```text
PRODUCT_TARGET_50W
PRODUCT_TARGET_75W
PRODUCT_TARGET_100W
PRODUCT_TARGET_150W
PRODUCT_TARGET_200W
PRODUCT_TARGET_240W
```

实现门禁：

```text
未选择任何PRODUCT_TARGET_xxx -> 编译失败
同时选择多个               -> 编译失败
目标Profile必填参数未冻结    -> 编译失败
Fingerprint硬件字段未定义    -> 编译失败
```

审核：

- [ ] CAT1_50W.bin 只包含50W Profile；
- [ ] 不存在运行时 `_profiles[]` 多型号 Catalog；
- [ ] 不存在运行时 `find(profileId)` 切功率；
- [ ] 单个bin不含其他功率的参数/型号字符串；
- [ ] 切换Target不复制 MQTT/Calibration/Flash/OTA/保护公共代码。

---

# 2. 50W 冻结参数

```text
Rated Power        = 50W
MID                = 1
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
Formal Points      = 11
Level              = 0,20,...,200
Logical PWM Full   = 1000
```

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

审核：

- [ ] 两端50W Profile一致；
- [ ] 旧890mA不再作为V3默认值；
- [ ] HWMAX与Hardware Max已拆开；
- [ ] 50W Product Target 中硬件修订号、PWM极性、OCO硬件修订号均显式定义。

---

# 3. SET / HWMAX / CV / Tolerance / Calibration 解耦

正式语义：

```text
Hardware Max = 产品硬件绝对能力
HWMAX        = 工厂配置上限
SET_OUTCUR   = 当前用户100%目标电流
CV           = 本次校准电子负载工况
Tolerance    = 上位机验收策略
Calibration  = Correction
```

SET_OUTCUR 第一版 Wire 继续兼容：

```json
{"Factory":{"SET_OUTCUR":893}}
```

但固件内部必须保存到 User Config。

HWMAX：

```text
0 < HWMAX <= Hardware Max
```

必须保证：

```text
改 SET        -> Calibration 不失效
改 CV         -> Calibration 不失效
改 Tolerance  -> Calibration 不失效
```

审核：

- [ ] SET不参与Calibration有效性；
- [ ] 当前HWMAX不参与Calibration有效性；
- [ ] CV不参与Calibration有效性；
- [ ] 运行Vo不参与Calibration有效性；
- [ ] Tolerance不进入固件运行授权；
- [ ] calibratedMaxCurrent旧V2授权语义退出；
- [ ] 无Calibration仍能正常开关/调光。

---

# 4. 11点、PWM域与完整校准

正式点：

```text
Percent    = 0,10,...,100
Level      = 0,20,...,200
logicalPwm = level * 5 = 0,100,...,1000
```

SET_POINT：

- 上位机只发送 Level；
- 不允许写 TIM CCR；
- 采点不应用旧 Output Calibration；
- 采点不叠加 OP_PWM_OFFSET；
- 返回 Actual logical PWM；
- MCU保留硬件最后保护。

无 Output Calibration：

```text
SET_OUTCUR × Brightness
→ Default PWM
→ OP_PWM_OFFSET
→ Protection
→ PWM
```

有 Output Calibration：

```text
Target Current
→ 11点反插值
→ logical PWM
→ 不重复OP_PWM_OFFSET
→ Protection
→ PWM
```

Calibration覆盖范围外但Target合法：回退Default Path +诊断，不允许直接PWM=0。

每次必须完整生成：

```text
Output
+ OCO
+ BL0942 Voltage
+ BL0942 Current
+ BL0942 Power
```

不做 UpdateMask / Section Merge / 局部更新。

---

# 5. Calibration 模型

## 5.1 Output

```text
Actual logical PWM <-> Reference Output Current
```

11点分段线性，运行时 Target Current 反插值直接得到 logical PWM。

## 5.2 OCO

```text
OCO ADC Raw <-> Reference Output Current
```

链路必须分离：

```text
OCO Raw -> 保守默认换算 -> Protection
   │
   └----> OCO Correction -> Corrected Output Current -> MQTT/业务
```

Corrected不得反向替代Raw保护链。

## 5.3 BL0942 Current / Power

```text
BL Current Raw <-> Reference Input Current
BL Power Raw   <-> Reference Active Power
```

11点 Raw→Reference。

## 5.4 BL0942 Voltage——V3第一版 Gain-only Q24

上位机：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
finalGainQ24 = median(valid samples)
```

固件：

```text
correctedVoltage01V =
(uint64_t(rawVoltage) * finalGainQ24 + 2^23) >> 24
```

PayloadVersion=1 只保存：

```text
u32 voltageGainQ24
```

**该决定只冻结 V3 第一版，不永久排除 Gain + Offset。**

量产前必须多输入电压 HIL。如果 Gain-only 无法满足 Tolerance：

```text
当前Voltage模型判HIL FAIL
→ 不允许量产放行
→ 升级PayloadVersion
→ 必要时升级MQTT Protocol Version
→ 新版本可采用Gain + Offset/其他实测模型
```

禁止在 PayloadVersion=1 的244B结构中偷偷添加 Offset 或改变字段含义。

---

# 6. Product Fingerprint——最终精确定义

Fingerprint 只绑定固定产品硬件身份，不绑定运行配置。

## 6.1 Fingerprint输入布局，18B

所有多字节字段 Little Endian：

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
profileFingerprint = CRC32(bytes[0x00..0x11])
```

禁止直接 CRC C struct / TypeScript对象序列化。

## 6.2 CRC32

```text
Name   = CRC-32/ISO-HDLC（CRC32/IEEE）
Poly   = 0x04C11DB7
RefIn  = true
RefOut = true
Init   = 0xFFFFFFFF
XorOut = 0xFFFFFFFF
Check  = 0xCBF43926 for ASCII "123456789"
```

反射实现可使用 `0xEDB88320`。

## 6.3 禁止参与 Fingerprint

```text
SET_OUTCUR
当前 HWMAX
CV
Tolerance
MQTT/平台参数
Plan
运行历史
Calibration Generation
```

审核：

- [ ] 两端显式编码18B完全一致；
- [ ] `hardwareRevision/ocoHardwareRevision/pwmPolarity` 是Product Target必填；
- [ ] 未定义时固件编译失败；
- [ ] SET/CV/Tolerance变化不会改变Fingerprint。

---

# 7. Wire Calibration Payload——244B 最终结构

V3 STAGE **只传 Calibration Payload，不传 Flash Record**。

冻结：

```text
Payload Magic       = ASCII "CALP"
Payload Version     = 1
Payload Length      = 244 = 0x00F4
Endian              = Little Endian
Point Count         = 11
Level Step          = 20
ValidFlags          = 0x001F
```

## 7.1 Header，20B

| Offset | Size | Type | Field |
|---:|---:|---|---|
| `0x00` | 4 | byte[4] | `CALP` = `43 41 4C 50` |
| `0x04` | 2 | u16 | payloadVersion=1 |
| `0x06` | 2 | u16 | payloadLength=244 |
| `0x08` | 2 | u16 | profileId |
| `0x0A` | 2 | u16 | profileVersion |
| `0x0C` | 4 | u32 | profileFingerprint |
| `0x10` | 1 | u8 | pointCount=11 |
| `0x11` | 1 | u8 | levelStep=20 |
| `0x12` | 2 | u16 | validFlags=0x001F |

`validFlags`：

```text
bit0 Output
bit1 OCO
bit2 BL Voltage
bit3 BL Current
bit4 BL Power
bit5..15 reserved=0
```

第一版只接受 `0x001F`。

## 7.2 Output，44B

起始 `0x14`，11×4B：

```text
u16 logicalPwm
u16 referenceOutputCurrentMa
```

第i点 `logicalPwm=i*100`。

## 7.3 OCO，44B

起始 `0x40`，11×4B：

```text
u16 ocoAdcRaw
u16 referenceOutputCurrentMa
```

## 7.4 BL Current，66B

起始 `0x6C`，11×6B：

```text
u32 blCurrentRaw
u16 referenceInputCurrentMa
```

禁止依赖 C struct padding。

## 7.5 BL Power，66B

起始 `0xAE`，11×6B：

```text
s32 blPowerRaw
u16 referenceInputPower01W
```

## 7.6 BL Voltage，4B

起始 `0xF0`：

```text
u32 voltageGainQ24
```

## 7.7 总表

```text
0x000..0x013 Header                         20B
0x014..0x03F Output                        44B
0x040..0x06B OCO                           44B
0x06C..0x0AD BL Current                    66B
0x0AE..0x0EF BL Power                      66B
0x0F0..0x0F3 BL Voltage GainQ24             4B
------------------------------------------------
Total                                     244B
```

Payload 不包含 Generation、Flash Magic、Record CRC、CommitWord、A/B Slot。

---

# 8. Payload CRC / Hex / Endian

## 8.1 Endian

JSON 数字没有 Endian；只有二进制 Fingerprint Input、Calibration Payload 和 Flash Record 使用 Little Endian。

## 8.2 Payload CRC

```text
payloadCrc32 = CRC32(payload[0x000..0x0F3])
```

完整覆盖244B，Payload内部不再放CRC字段。

## 8.3 `payloadHex`

STAGE / READ 大数据字段统一命名：

```text
payloadHex
```

规则：

- 244B → 固定488个Hex字符；
- 不带`0x`；
- 上位机发送统一大写；
- 固件解析允许`0-9/A-F/a-f`；
- 第一版禁止同时出现`ph`等第二套别名。

---

# 9. RAW V3——清晰字段名 + Raw/Corrected同包

RAW Response 字段：

| Field | Type | Unit / meaning |
|---|---|---|
| `level` | u16 | 当前Level |
| `actualPwm` | u16 | logical PWM 0..1000，不是CCR |
| `ocoRaw` | u16 | OCO ADC Raw |
| `blVoltageRaw` | u32 | BL0942 Voltage Raw |
| `blCurrentRaw` | u32 | BL0942 Current Raw |
| `blPowerRaw` | s32 | BL0942 Power Raw |
| `correctedOutputCurrentMa` | u16 | Corrected Output Current |
| `correctedInputVoltage01V` | u16 | Corrected Input Voltage 0.1V |
| `correctedInputCurrentMa` | u16 | Corrected Input Current |
| `correctedInputPower01W` | u16 | Corrected Active Power 0.1W |
| `outputVoltage01V` | u16 | 当前输出电压 0.1V |
| `blAgeMs` | u32 | 最后有效BL帧年龄 |
| `validFlags` | u16 | 有效/新鲜位 |
| `faultFlags` | u16 | 统一硬件故障位 |

RAW的Raw字段必须是真正未应用Calibration Correction的数据。

使用规则：

```text
11点FITTING    -> Raw + External Reference
APPLY后VERIFY  -> Corrected + External Reference
```

## 9.1 validFlags

```text
bit0  PWM_VALID
bit1  OCO_RAW_VALID
bit2  BL_V_RAW_VALID
bit3  BL_I_RAW_VALID
bit4  BL_P_RAW_VALID
bit5  BL_FRESH
bit6  OUTPUT_CORRECTED_VALID
bit7  BL_V_CORRECTED_VALID
bit8  BL_I_CORRECTED_VALID
bit9  BL_P_CORRECTED_VALID
bit10 VO_VALID
bit11..15 reserved=0
```

第一版：

```text
BL_FRESH = blAgeMs <= 500ms
```

正式FITTING最低要求Raw/PWM/Fresh相关位有效；APPLY后验证要求相应Corrected位有效。

## 9.2 faultFlags

```text
bit0 OUTPUT_OVERLOAD       <- Error_1_OL
bit1 OUTPUT_LOW_VOLTAGE    <- Error_Out_LV
bit2 INPUT_OVERVOLTAGE     <- Error_3_OV
bit3 INPUT_UNDERVOLTAGE    <- Error_4_LV
bit4 OVER_TEMPERATURE      <- 当前温度保护限幅/关断活跃
bit5..15 reserved=0
```

非零正式输出/验证点有Fault不得作为有效样本。Level/percent=0时允许OUTPUT_LOW_VOLTAGE因输出关闭自然出现，但其他Fault仍失败。

BL stale用`blAgeMs/BL_FRESH`表示，不塞入faultFlags；UART诊断计数进入DIAG。

---

# 10. MQTT V3 Wire Contract——最终重新冻结为字符串协议

## 10.1 外层 Envelope

保持现有：

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

规则：

- `SV`固定`cal`；
- `SN/ID/SV/CT`用于请求响应关联；
- Response CT与Request一致；
- 校准失败仍通过`DT.rc`返回，不使用普通属性无DT短ACK；
- 普通`prop/ctrl/rept/alam/ota/plan`不因V3修改。

## 10.2 Operation——13个字符串

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

**正式Wire禁止数字Operation Code。**

`SET_VERIFY`不属于V3第一版；统一叫`SET_OUTPUT`。

`READ_INFO/READ_CHUNK`不属于V3第一版；统一叫`READ`。

## 10.3 CT映射

```text
R: CAP, RAW, READ, DIAG
W: BEGIN, HEARTBEAT, SET_POINT, STAGE, APPLY,
   SET_OUTPUT, COMMIT, ABORT, RELEASE
```

## 10.4 公共字段

### Sessionless Request：CAP / DIAG

```text
v   u8 = 3
op  string
```

### Session Request

```text
v    u8 = 3
op   string
sid  u32，非0
seq  u32，非0
```

BEGIN新会话固定`seq=1`，后续新命令seq严格递增。

### Session Response

```text
v    u8 = 3
op   string，回显
sid  u32，回显
seq  u32，回显
rc   u8
st   u8
```

CAP/DIAG Response不带sid/seq。

## 10.5 幂等

- 精确相同 `sid + seq + op + 参数摘要` 重发：重放第一次响应，不再次执行副作用；
- 同一 `sid + seq` 但 op/参数不同：`BAD_REQUEST`；
- 旧seq且无可重放缓存：`BAD_STATE`；
- COMMIT精确重试不得再次擦Flash；
- MQTT超时重试必须复用同一seq，而不是生成新seq重复副作用。

---

# 11. Result Code / State——最终冻结

## 11.1 rc，仅11个

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

说明：

- 协议字段缺失/类型错误/冲突重复 → BAD_REQUEST；
- 当前State不允许该Operation → BAD_STATE；
- 会话被其他工位占用 → BUSY；
- lease超时 → SESSION_EXPIRED；
- Level/percent/HW范围错误 → RANGE_ERROR；
- BL数据过期 → DATA_STALE；
- Payload/Record CRC失败 → CRC_ERROR；
- A/B读写/持久化失败 → FLASH_ERROR；
- 硬件保护不允许继续 → HARDWARE_FAULT；
- Product Profile/Fingerprint不一致 → PROFILE_MISMATCH。

第一版不再单独定义 NOT_AVAILABLE / PROTOCOL_ERROR / SAFETY_NOT_READY / DUPLICATE 等额外rc。

## 11.2 st，5个

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

流程：

```text
IDLE
→ BEGIN      -> ACTIVE
→ STAGE      -> STAGED
→ APPLY      -> APPLIED
→ COMMIT     -> COMMITTED
→ READ
→ RELEASE    -> IDLE
```

取消：

```text
ACTIVE/STAGED/APPLIED
→ ABORT
→ safe off
→ 丢弃staged
→ 恢复旧committed Calibration
→ IDLE
```

COMMIT成功后ABORT不能撤销新Calibration；此时应READ/RELEASE。

MCU不设置PASS/FAIL Wire State。

---

# 12. 每个 Operation 完整 Request / Response

以下字段全部位于`DT`。

## 12.1 CAP，CT=R

Request：

```json
{"v":3,"op":"CAP"}
```

Response必填：

```text
v, op, rc, st
profileId               u16
profileVersion          u16
profileFingerprint      u32
mid                     u8
hardwareRevision        u16
ratedPowerW             u16
rs3Mohm                 u16
hardwareMaxMa           u16
hwMaxMa                 u16
setOutcurMa             u16
pwmFullScale            u16
pwmPolarity             u8
ocoHardwareRevision     u16
pointCount              u8  =11
levelStep               u8  =20
payloadVersion          u16 =1
storageFormatVersion    u16 =4
rawMaxAgeMs             u16 =500
capabilities            u16
hasCalibration          u8  0/1
generation              u32 none=0
payloadLength           u16 none=0，否则244
payloadCrc32            u32 none=0
safetyReady             u8  0/1
persistenceReady        u8  0/1
faultFlags              u16
```

`capabilities`：

```text
bit0 Output Calibration
bit1 OCO Calibration
bit2 BL Voltage Calibration
bit3 BL Current Calibration
bit4 BL Power Calibration
bit5 SET_OUTPUT
bit6 READ
bit7 DIAG
bit8..15 reserved
```

当前50W V3完整目标：`capabilities & 0x00FF == 0x00FF`。

CAP只返回当前Product，不返回其他瓦数Catalog。

## 12.2 BEGIN，CT=W

Request：

```text
v, op="BEGIN", sid, seq=1
profileId          u16
profileFingerprint u32
leaseMs            u32，1000..600000
```

Response：

```text
v,op,sid,seq,rc,st
profileId
profileFingerprint
leaseMs
```

成功`st=ACTIVE`。

BEGIN只核对Product/Fingerprint、会话互斥、硬件安全和Persistent可用；不得验证SET等于校准时SET，也不得验证运行/校准电压相等。

## 12.3 HEARTBEAT，CT=W

Request：

```text
v,op="HEARTBEAT",sid,seq
leaseMs u32，1000..600000
```

Response：公共Session Response + `leaseMs`。

允许ACTIVE/STAGED/APPLIED/COMMITTED；成功续租。

## 12.4 SET_POINT，CT=W

Request：

```text
v,op="SET_POINT",sid,seq
level u16，只允许0,20,...,200
```

仅ACTIVE合法。

Response：

```text
v,op,sid,seq,rc,st
level
actualPwm
```

正常 `actualPwm=level*5`。若硬件保护阻止正确输出，不得伪造OK。

## 12.5 RAW，CT=R

Request：仅公共Session Request。

允许ACTIVE/APPLIED。

Response：

```text
v,op,sid,seq,rc,st
level
actualPwm
ocoRaw
blVoltageRaw
blCurrentRaw
blPowerRaw
correctedOutputCurrentMa
correctedInputVoltage01V
correctedInputCurrentMa
correctedInputPower01W
outputVoltage01V
blAgeMs
validFlags
faultFlags
```

若BL stale，可返回快照和`rc=DATA_STALE`，但必须保留age/flags供审计；上位机不得使用该点拟合或验证。

## 12.6 STAGE，CT=W

Request：

```text
v,op="STAGE",sid,seq
payloadLength u16 =244
payloadCrc32  u32
payloadHex    string，固定488 Hex chars
```

仅ACTIVE合法。

固件必须执行：

```text
Hex decode
→ length
→ payload CRC
→ CALP Header
→ payloadVersion
→ Product/Fingerprint
→ pointCount/levelStep
→ validFlags
→ Section range/monotonic/range
```

成功只放RAM staged，不写有效Calibration Flash。

Response：

```text
v,op,sid,seq,rc,st
payloadLength
payloadCrc32
```

成功`st=STAGED`。

## 12.7 APPLY，CT=W

Request：仅公共Session Request。

仅STAGED合法。

行为：临时切换到RAM staged完整Correction，不提交Flash。

Response：

```text
v,op,sid,seq,rc,st
payloadLength
payloadCrc32
```

成功`st=APPLIED`。

## 12.8 SET_OUTPUT，CT=W

Request：

```text
v,op="SET_OUTPUT",sid,seq
percent u8，0..100
```

仅APPLIED合法。

行为：按正常`SET_OUTCUR × percent` + staged Output Calibration输出，不做精度PASS/FAIL。

Response：

```text
v,op,sid,seq,rc,st
percent
actualPwm
```

上位机随后读取RAW + 外部仪器完成独立验证。

## 12.9 COMMIT，CT=W

Request：仅公共Session Request。

仅APPLIED合法。

固件执行Calibration A/B原子提交。

Response：

```text
v,op,sid,seq,rc,st
generation
payloadLength=244
payloadCrc32
```

成功`st=COMMITTED`。

## 12.10 READ，CT=R

Request：仅公共Session Request。

仅当前COMMITTED session合法。

Response：

```text
v,op,sid,seq,rc,st
generation
payloadLength=244
payloadCrc32
payloadHex=488 Hex chars
```

返回**已提交Calibration Payload**，不是整个272B Flash Record。

上位机必须Hex decode→CRC→与STAGE Payload逐Byte Compare。

第一版不实现READ_INFO/READ_CHUNK。

## 12.11 ABORT，CT=W

Request：仅公共Session Request。

允许ACTIVE/STAGED/APPLIED。

行为：

```text
safe off
→ 丢弃staged
→ 恢复旧committed Calibration
→ 清session/inhibit
→ IDLE
```

Response：公共Session Response，成功`st=IDLE`。

COMMITTED状态调用ABORT返回BAD_STATE。

## 12.12 RELEASE，CT=W

Request：仅公共Session Request。

正常成功流程仅COMMITTED合法。

行为：

```text
safe off
→ 保留新committed Calibration
→ 清session/inhibit
→ IDLE
```

Response：公共Session Response，成功`st=IDLE`。

未COMMIT的取消必须走ABORT。

## 12.13 DIAG，CT=R

Sessionless，只读，不改变State。

Request：

```json
{"v":3,"op":"DIAG"}
```

Response至少：

```text
v,op,rc,st
validFrameCount       u32
oreCount              u32
feCount               u32
neCount               u32
timeoutCount          u32
uartErrorCount        u32
recoveryCount         u32
recoveryFailCount     u32
lastValidFrameTick    u32
blAgeMs               u32
blFresh               u8
uartGState            u8
uartRxState           u8
uartErrorCode         u32
```

DIAG不承载正式校准Raw，不参与拟合。

---

# 13. Session / Lease / 超时行为

- 同一设备同一时刻只允许一个Calibration Session；
- BEGIN时sid由上位机生成，非0；
- BEGIN seq=1，后续递增；
- HEARTBEAT维护lease；
- ACTIVE/STAGED/APPLIED lease超时：safe off、丢弃未提交staged、恢复旧committed Calibration、清session→IDLE；
- COMMITTED lease超时：safe off、保留已经提交的新Calibration、清session→IDLE；
- lease超时后旧session的新命令返回SESSION_EXPIRED或BAD_STATE，不得重新执行旧副作用。

---

# 14. Flash Calibration Record——272B Format4

Wire Payload与Flash Record必须解耦。

冻结：

```text
Storage Format Version = 4
Record Magic           = ASCII "CAL4"
Record Length          = 272 = 0x0110
Payload Length         = 244
Endian                 = Little Endian
CommitWord             = 0xC0A17EED
```

## 14.1 Header，20B

| Offset | Size | Type | Field |
|---:|---:|---|---|
| `0x00` | 4 | byte[4] | `CAL4` = `43 41 4C 34` |
| `0x04` | 2 | u16 | formatVersion=4 |
| `0x06` | 2 | u16 | recordLength=272 |
| `0x08` | 4 | u32 | generation，0无效，首个有效=1 |
| `0x0C` | 2 | u16 | payloadLength=244 |
| `0x0E` | 2 | u16 | reserved=0 |
| `0x10` | 4 | u32 | payloadCrc32 |

## 14.2 Payload

```text
0x14..0x107 = 244B Target Calibration Payload
```

## 14.3 Footer

```text
0x108 u32 recordCrc32
0x10C u32 commitWord = 0xC0A17EED
Total = 0x110 = 272B
```

Record CRC：

```text
CRC32(record[0x000..0x107])
```

不包含recordCrc32自身和CommitWord。

## 14.4 A/B Commit

```text
1. 找当前有效Calibration page
2. 选择非活动2KiB page
3. 擦非活动page
4. RAM生成Format4，generation=old+1
5. 写Header + Payload + RecordCRC
6. Readback并验证Payload CRC + Record CRC
7. 最后写CommitWord
8. 再读回CommitWord
9. 新page成为active，旧page仍可回退
```

禁止先擦唯一有效页。

## 14.5 Boot选择

Record必须同时满足：

- Magic=`CAL4`；
- formatVersion=4；
- recordLength=272；
- payloadLength=244；
- reserved=0；
- CommitWord正确；
- Payload CRC正确；
- Record CRC正确；
- CALP Header/Version/Flags正确；
- Payload Profile/Fingerprint匹配当前Product Target。

A/B同时有效时选更新Generation；generation=0无效。

---

# 15. 6×2KiB Persistent物理布局——保持

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页2KiB，一个物理擦除页只有一个Owner。

Config/Runtime同样必须是真正A/B，不能Main/Backup一次保存时同时擦写。

Calibration Record只占Calibration页起始272B，其余空间保持保留/擦除态，不允许其他事务共享。

---

# 16. 当前开发阶段 Persistent 初始化——旧12KB直接格式化，不迁移

**本阶段已确认不做 Legacy Migration。**

启动发现 `0x08005000~0x08008000` 不是合法V3新Persistent布局，包括旧V2格式时：

```text
停止Persistent业务写入
        ↓
仅格式化0x08005000~0x08008000
        ↓
初始化V3 Config
        ↓
Calibration A/B 空
        ↓
Runtime A/B 空
```

明确：

- 不迁移旧Calibration；
- 不迁移旧Config/User；
- 不迁移旧Plan；
- 不迁移旧Runtime；
- 50W新Config按当前默认值初始化，例如HWMAX=1400、SET=893；
- 合法V3 Config建立后不得每次启动重复格式化；
- 旧formatVersion=3不按V3解释。

**绝对不碰：**

```text
0x08000000~0x08005000 Bootloader
0x08008000~0x08024000 APP
0x08024000~0x08040000 OTA Backup
```

这是当前开发/HIL阶段策略，不等于未来量产V2→V3 OTA兼容策略。未来需要量产迁移时另开明确版本设计和测试，不在当前V3第一版偷偷加入半套迁移。

---

# 17. 上位机最终 PASS / FAIL——保持不变

```text
STAGE
→ APPLY
→ SET_OUTPUT
→ RAW + 外部标准仪器
→ 上位机计算Tolerance

PASS -> COMMIT
FAIL -> ABORT
```

MCU：

- 不计算±1%/±2%；
- 不保存Tolerance；
- 不设置PASS/FAIL状态；
- 只执行输出、Raw/Corrected、Stage/Apply/Commit、Storage和硬件最后保护。

Quick/Full验证点保留独立点思想：

```text
Quick: 5 / 45 / 85%
Full : 5 / 15 / 25 / ... / 95%
```

目的：验证插值和实际APPLY，而不是重复0/10/.../100训练点。

校准前误差大不能直接判最终FAIL，只能作为待修正Evidence；APPLY后独立验证超过Tolerance才FAIL。

---

# 18. 上位机稳定采样与仪器安全

稳定采样不能只靠固定sleep：

- 基础等待；
- 连续N组样本；
- Raw/Reference变化率或峰峰值条件；
- BL Fresh；
- 最大等待超时；
- 不稳定/超时样本不进入拟合。

电子负载：

- 任务开始先safeOff；
- Product/CAP/仪器未通过前INPUT OFF；
- SET>HWMAX禁止开始；
- 取消/断线/MQTT超时/BL stale/Operation失败都必须进入安全关闭；
- ABORT/RELEASE后再次确认INPUT OFF；
- 应用退出/窗口关闭有安全关闭策略。

MCU硬件保护继续作为最后生存保护。

---

# 19. BL0942 Freshness / 长稳审核

必须有：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

审核：

- HAL TX/RX返回值；
- ORE/FE/NE；
- gState/RxState/ErrorCode；
- timeout/RX重挂；
- Buffer/Index；
- Tick/计数；
- 芯片与供电域。

根因链必须给出：

```text
冻结/异常触发证据
→ UART/HAL/协议/芯片状态
→ 根因
→ 修复点
→ 有限恢复
→ 再次有效帧
→ 长稳结果
```

禁止周期Reset掩盖根因。异常后的有限恢复必须可分类、可计数、可验证。

---

# 20. JSON / cJSON 内存预算

当前约束：

```text
ZK_JSON_BUF_SIZE      = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

V3第一版改回清晰字段，不再追求极端短键；内存优化依靠删除重复Context/Catalog/诊断树。

验收目标：

```text
普通ACK  < 256B
RAW      < 768B
CAP      < 1024B
READ     < 1024B
所有TX   < 1536B
```

READ包含488字符`payloadHex`，必须真实测量最终JSON和cJSON Pool峰值。

验收：

- [ ] 无`TX Pool Exhausted`；
- [ ] CAP无多功率Catalog；
- [ ] ACK无全量Context；
- [ ] RAW和DIAG分离；
- [ ] READ只回244B Payload，不回272B Flash Record；
- [ ] 记录每个Operation最大JSON长度及Pool峰值。

如果单READ实测无法满足约束，才允许以**明确协议版本升级**重新评审分块方案；V3第一版不得同时保留READ和READ_CHUNK两套路径。

---

# 21. Golden Vector Set——跨端强制

## G1 CRC32

```text
Input ASCII: 123456789
CRC32:       0xCBF43926
```

C和TypeScript独立通过。

## G2 Endian

```text
u16 0x1234     -> 34 12
u32 0x12345678 -> 78 56 34 12
s32 -2         -> FE FF FF FF
```

## G3 Fingerprint Codec

固定测试输入：

```text
profileVersion      = 1
profileId           = 50
mid                 = 1
hardwareRevision    = 1
ratedPowerW         = 50
rs3Mohm             = 120
hardwareMaxMa       = 1680
pwmFullScale        = 1000
pwmPolarity         = 1
ocoHardwareRevision = 1
```

必须先得到18B：

```text
01 00 32 00 01 01 00 32 00 78 00 90 06 E8 03 01 01 00
```

然后两端各自用G1 CRC实现独立计算Fingerprint并比较。

## G4 Calibration Payload Codec

固定：

```text
profileId          = 50
profileVersion     = 1
profileFingerprint = 0x11223344
pointCount         = 11
levelStep          = 20
validFlags         = 0x001F

Output logicalPwm:
[0,100,200,300,400,500,600,700,800,900,1000]
Output ref mA:
[0,89,179,268,357,447,536,625,714,804,893]

OCO raw:
[1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000]
OCO ref mA:
[0,89,179,268,357,447,536,625,714,804,893]

BL Current raw:
[100000,110000,120000,130000,140000,150000,160000,170000,180000,190000,200000]
BL Current ref mA:
[0,20,40,60,80,100,120,140,160,180,200]

BL Power raw:
[200000,220000,240000,260000,280000,300000,320000,340000,360000,380000,400000]
BL Power ref 0.1W:
[0,50,100,150,200,250,300,350,400,450,500]

Voltage gainQ24 = 0x01000000
```

必须得到：

```text
Payload Length = 244
Header @0x00:
43 41 4C 50 01 00 F4 00 32 00 01 00 44 33 22 11 0B 14 1F 00

Output point0  @0x14 = 00 00 00 00
Output point1  @0x18 = 64 00 59 00
Output point10 @0x3C = E8 03 7D 03

OCO point0     @0x40 = E8 03 00 00
OCO point10    @0x68 = D0 07 7D 03

BL-I point0    @0x6C = A0 86 01 00 00 00
BL-I point10   @0xA8 = 40 0D 03 00 C8 00

BL-P point0    @0xAE = 40 0D 03 00 00 00
BL-P point10   @0xEA = 80 1A 06 00 F4 01

GainQ24        @0xF0 = 00 00 00 01
```

两端各自生成完整244B fixture并逐Byte比较，再独立计算Payload CRC。

## G5 Flash Record Codec

使用G4 Payload，固定generation=7：

```text
bytes[0x00..0x03] = 43 41 4C 34
formatVersion     = 4
recordLength      = 272
payloadLength     = 244
payload起始       = 0x14
recordCrc offset  = 0x108
commit offset     = 0x10C
commit bytes      = ED 7E A1 C0
```

Record CRC按本文独立计算。

## G6 MQTT V3 Fixtures

至少固定跨端fixture：

```json
{"v":3,"op":"CAP"}
```

```json
{"v":3,"op":"BEGIN","sid":123456,"seq":1,"profileId":50,"profileFingerprint":287454020,"leaseMs":30000}
```

```json
{"v":3,"op":"SET_POINT","sid":123456,"seq":8,"level":100}
```

```json
{"v":3,"op":"SET_OUTPUT","sid":123456,"seq":30,"percent":45}
```

STAGE/READ fixture必须验证字段名统一为`payloadLength/payloadCrc32/payloadHex`。

---

# 22. Audit / 生产证据

上位机`audit.jsonl`至少追溯：

```text
Device/Product/Firmware
Product Fingerprint
Protocol V3 / PayloadVersion1 / StorageFormat4
Run Config: SET/CV/Tolerance/Stabilization/Validation
11点 Level + Actual PWM + Raw + Reference + Stability
生成的244B Payload + CRC
APPLY后独立验证
PASS/FAIL与原因
COMMIT generation/len/crc
READ payloadHex CRC + Byte Compare
必要DIAG
异常/取消原因
```

244B MCU Payload只保存运行Correction数据，不塞完整生产Evidence。

---

# 23. 最终联合审核矩阵

## 产品/架构

- [ ] 上位机仍为多功率通用；
- [ ] 单个固件只含一个功率Target；
- [ ] Keil未选/多选/未冻结Profile均编译失败；
- [ ] CAP只返回当前Product；
- [ ] 无profilesCsv依赖。

## SET/CV/Tolerance

- [ ] SET属于User Config；
- [ ] HWMAX属于Factory Config；
- [ ] CV只属于电子负载工况；
- [ ] Tolerance只属于上位机验收；
- [ ] 改SET/CV/Tolerance不使Calibration失效。

## 完整Calibration

- [ ] 每次Output+OCO+BL U/I/P全部生成；
- [ ] 无UpdateMask/局部更新；
- [ ] 校准前不按最终精度拒绝Correction；
- [ ] APPLY后由上位机SET_OUTPUT+标准仪器判PASS/FAIL；
- [ ] PASS→COMMIT；
- [ ] FAIL→ABORT。

## MQTT V3

- [ ] Operation字符串13个完全一致；
- [ ] 公共字段`v/op/sid/seq/rc/st`；
- [ ] 大数据只叫`payloadHex`；
- [ ] 不存在数字Operation正式Wire；
- [ ] 不存在SET_VERIFY；
- [ ] 不存在READ_INFO/READ_CHUNK第一版；
- [ ] rc仅0..10；
- [ ] st仅IDLE/ACTIVE/STAGED/APPLIED/COMMITTED；
- [ ] 精确重复请求幂等。

## Fingerprint

- [ ] 18B顺序/LE完全一致；
- [ ] CRC参数一致；
- [ ] Product Target硬件字段显式定义；
- [ ] SET/HWMAX/CV/Tolerance/MQTT/Plan/历史不参与。

## Payload / Storage

- [ ] Wire Payload=244B；
- [ ] Flash Record=272B；
- [ ] Payload≠Flash Record；
- [ ] 上位机不生成Generation/RecordCRC/CommitWord；
- [ ] READ返回244B Payload；
- [ ] Payload/Record CRC正确；
- [ ] CommitWord最后写；
- [ ] Calibration A/B掉电安全。

## Flash开发初始化

- [ ] 发现旧Persistent只擦0x08005000~0x08008000；
- [ ] 不迁移旧Calibration/Config/Plan/Runtime；
- [ ] 初始化V3 Config；
- [ ] Calibration/Runtime为空；
- [ ] 不碰Boot/APP/OTA Backup；
- [ ] 合法V3布局后不重复格式化。

## BL0942

- [ ] Raw真正未校准；
- [ ] Fresh/Age可信；
- [ ] stale不用于拟合/验证；
- [ ] ORE/FE/NE/timeout可诊断；
- [ ] 根因链可验证；
- [ ] 不靠周期reset；
- [ ] Gain-only Q24完成多输入电压HIL；
- [ ] 不满足Tolerance时通过版本升级处理，不偷偷加Offset。

## Memory / 回归

- [ ] READ完整payloadHex满足2KiB JSON；
- [ ] cJSON TX Pool无耗尽；
- [ ] Windows Keil官方Build；
- [ ] Boot/APP/OTA Backup无回归；
- [ ] 普通MQTT无回归；
- [ ] RTC/Plan新业务正常；
- [ ] 温控/告警/调光正常；
- [ ] Electron MQTT/Serial/Persistence无无关重写；
- [ ] 50W真实HIL证据完整。

---

# 24. 最终冻结结论

本版 V3 正式采用：

```text
MQTT Operation       = 字符串
Common fields        = v / op / sid / seq / rc / st
Operations           = CAP / BEGIN / HEARTBEAT / SET_POINT / RAW /
                       STAGE / APPLY / SET_OUTPUT / COMMIT / READ /
                       ABORT / RELEASE / DIAG
Result Codes         = 11个（0..10）
States               = IDLE / ACTIVE / STAGED / APPLIED / COMMITTED
Large data field     = payloadHex
READ                 = 第一版单READ，不分块
Wire Payload         = 244B CALP PayloadVersion1
Flash Record         = 272B CAL4 StorageFormat4
Fingerprint          = 固定18B LE + CRC32
Development Flash    = 旧12KB Persistent直接格式化，不迁移
BL Voltage V3        = Gain-only Q24；HIL失败则版本升级支持Gain+Offset
```

同时原样保留：

```text
6×2KiB Flash
一次完整 Output + OCO + BL U/I/P
SET/HWMAX/CV/Tolerance/Calibration解耦
无Calibration正常运行 + OP_PWM_OFFSET
上位机最终PASS/FAIL
MCU硬件最后保护
Keil单功率独立固件
BL0942根因修复
稳定采样 / SafeOff / Audit / 非回归
```

**后续 Codex 执行的是 V2→V3 实现任务，不允许再自行把协议切回数字 Operation、分块 READ、重新绑定 SET/CV，或删掉上述工程约束。**