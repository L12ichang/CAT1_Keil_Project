# CAT1 50W 一体化电源——校准固件 V2→V3 修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 分支：`main`  
> 文档定位：**固件侧唯一实施基线**  
> 当前代码状态：**V2，尚未进行任何 V3 功能修改**  
> 目标：**将当前 V2 固件升级改造为 Calibration MQTT Protocol V3**  
> 字段级唯一真源：`docs/CAT1_50W固件与上位机联合审核清单.md`

## 0. 最高优先级事实

当前源码仍是 V2：

- `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION = 2`；
- 仍存在旧 `profileContext`；
- 仍绑定 calibrationVoltage / SET / calibratedMax；
- 仍使用旧 198B Calibration Payload；
- 当前 Calibration Storage `formatVersion=3` 是 Legacy；
- 正常 PWM 仍存在 V2 Voltage/Context 门禁；
- Calibration PWM 底层仍会叠加 OP_PWM_OFFSET。

因此本文和联合审核清单中的 V3 均为**待开发目标**。Codex 的任务是把真实代码从 V2 改成 V3，而不是继续修补 V2 Context。

截至当前版，V3 字段、Payload、RAW bits、Operation、Chunk、Storage、CRC 和 Golden Vector 已全部冻结。**不存在允许 Codex 自行决定的协议 P0。**

---

## 1. 50W冻结参数

```text
Rated Power        = 50W
MID                = 1
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
Formal Points      = 11
Level              = 0,20,...,200
Logical PWM        = 0..1000
```

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

第一阶段固件只含50W Product Profile；后续功率通过独立 Keil Target 生成独立固件，不在一个镜像里维护多 Profile Catalog。

---

## 2. 参数归属

### Product Profile

MID、Model、Rated Power、Hardware Max、RS3、Profile Version/Fingerprint、固定硬件保护能力。

### Factory Config

HWMAX、OP_PWM_OFFSET、生产信息等。

### User Config

SET_OUTCUR、温控、告警、平台、调光、计划等。

### SET_OUTCUR Wire兼容

外部普通属性仍兼容：

```json
{"Factory":{"SET_OUTCUR":893}}
```

但固件内部必须保存到 `User Config.SET_OUTCUR`。`Factory.HWMAX_OUTCUR` 合法性改为：

```text
0 < HWMAX <= Hardware Max
```

不得要求 HWMAX 等于 Hardware Max。

---

## 3. Calibration不绑定运行参数

以下参数不得决定 Calibration 是否有效：

- SET_OUTCUR；
- 当前 HWMAX；
- 校准 CV；
- 运行输出电压；
- Tolerance；
- calibratedMaxCurrent。

```text
Calibration = Correction，不是 Permission
```

没有 Calibration 时继续使用原正常输出链，不得因“未校准”永久输出0。

---

## 4. PWM域

协议只操作正式 Level：

```text
logicalPwm = level * 5
Level 0..200 -> logicalPwm 0..1000
```

### 无有效Output Calibration

```text
SET_OUTCUR × Brightness
-> 默认PWM模型
-> OP_PWM_OFFSET
-> 保护仲裁
-> CCR
```

### Calibration SET_POINT / 有有效Calibration

```text
logicalPwm
-> 不叠加OP_PWM_OFFSET
-> 保护仲裁
-> CCR
```

必须重构 `hw_tim1_pwm2`，禁止所有 PWM 路径无条件 `pwm + OP_PWM_OFFSET`。

---

## 5. 校准模型

### Output

11点：`logicalPwm <-> Reference Output Current`，运行时 `Target Current -> 分段反插值 -> u16 logicalPwm`。

### OCO

11点：`OCO ADC Raw <-> Reference Output Current`。

```text
OCO Raw -> 保守默认换算 -> Protection
OCO Raw -> Correction -> Corrected Output Current -> MQTT
```

### BL0942 Current / Power

11点 Raw→Reference。

### BL0942 Voltage

第一版固定 Q24 Gain-only：

```text
correctedVoltage01V =
(rawVoltage * gainQ24 + 2^23) >> 24
```

使用 `uint64_t` 中间量；Gain由上位机多个有效样本取中位数后写入 Payload。

---

## 6. Target Calibration Payload——固件实现必须完全按联合文档

固定：

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

固件必须显式逐字段 decode，禁止直接 `memcpy` 到带 padding 的 C struct。

Payload CRC：CRC-32/ISO-HDLC，对完整244B覆盖；`123456789 -> 0xCBF43926`。

STAGE只接收：

```text
len=244 + crc + ph(488 Hex chars)
```

固件完成 Hex、CRC、Header、Profile、Flags、点位和范围检查后，只放 RAM。

---

## 7. RAW V3——固件字段冻结

RAW固定返回：

```text
lv  u16
pwm u16
or  u16 OCO Raw
bv  u32 BL Voltage Raw
bi  u32 BL Current Raw
bp  s32 BL Power Raw
oi  u16 Corrected Output Current mA
iv  u16 Corrected Input Voltage 0.1V
ii  u16 Corrected Input Current mA
ip  u16 Corrected Input Power 0.1W
vo  u16 Output Voltage 0.1V
age u32 BL data age ms
vf  u16
flt u16
```

`vf`：

```text
bit0 PWM_VALID
bit1 OCO_RAW_VALID
bit2 BL_V_RAW_VALID
bit3 BL_I_RAW_VALID
bit4 BL_P_RAW_VALID
bit5 BL_FRESH (age<=500ms)
bit6 OUTPUT_CORRECTED_VALID
bit7 BL_V_CORRECTED_VALID
bit8 BL_I_CORRECTED_VALID
bit9 BL_P_CORRECTED_VALID
bit10 VO_VALID
bit11..15 0
```

`flt`：

```text
bit0 OUTPUT_OVERLOAD    <- Error_1_OL
bit1 OUTPUT_LOW_VOLTAGE <- Error_Out_LV
bit2 INPUT_OVERVOLTAGE  <- Error_3_OV
bit3 INPUT_UNDERVOLTAGE <- Error_4_LV
bit4 OVER_TEMPERATURE   <- 温度保护限幅/关断活跃
bit5..15 0
```

BL stale 用 `age/vf`，不塞入 `flt`。UART错误计数进入 DIAG。

---

## 8. Calibration MQTT V3——固件Operation冻结

```text
0 CAP          R
1 BEGIN        W
2 HEARTBEAT    W
3 SET_POINT    W
4 RAW          R
5 STAGE        W
6 APPLY        W
7 SET_VERIFY   W
8 COMMIT       W
9 READ_INFO    R
10 READ_CHUNK  R
11 ABORT       W
12 RELEASE     W
13 DIAG        R
```

V3使用数字 `o`；会话字段使用：

```text
v=3,o,s,q,rc,st
```

CAP/DIAG sessionless；BEGIN `q=1`；后续 `q` 严格递增。精确重复请求重放第一次响应，不重复副作用。

Result Code：

```text
0 OK
1 NOT_AVAILABLE
2 INVALID_STATE
3 INVALID_ARGUMENT
4 LEASE_EXPIRED
5 BUSY
6 PROTOCOL_ERROR
7 SAFETY_NOT_READY
8 DUPLICATE
9 FLASH_ERROR
10 HARDWARE_FAULT
11 PROFILE_MISMATCH
12 DATA_STALE
13 CRC_ERROR
14 RANGE_ERROR
```

Wire State：

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

COMMIT成功后Wire State仍是APPLIED，内部设置 `sessionCommitted=true`；不增加COMMITTED/ABORTED Wire State。

所有 Operation 的精确 Request/Response 必填字段以联合审核清单第10节为唯一真源，固件不得自行增删或改名。

---

## 9. READ_CHUNK

固定：

```text
READ_CHUNK_MAX_BYTES = 128
```

244B Payload标准读回：

```text
off=0   n=128
off=128 n=116
```

Response `ph` 长度固定等于 `2*n`。

---

## 10. Flash Calibration Record——Format4

V3新 Calibration Storage：

```text
Magic         = CAL4
formatVersion = 4
recordLength  = 272B
payloadLength = 244B
Endian        = Little Endian
CommitWord    = 0xC0A17EED
```

布局：

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
Total 0x110 = 272B
```

Record CRC覆盖 `0x000..0x107`，不含自身和CommitWord。

A/B：擦非活动页 → 写Header/Payload/RecordCRC → 回读双CRC → 最后写CommitWord → 再回读。禁止先擦唯一有效页。

旧 `formatVersion=3` 不按V3解释；没有有效Format4时按“未校准但正常可运行”处理。

---

## 11. Persistent物理布局

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页2KiB，一个物理页一个Owner。Calibration Record只占页面起始272B，其余空间不得被其他事务共享。

---

## 12. BL0942长期稳定性

必须实现：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

检查 HAL TX/RX返回值、ORE/FE/NE、gState/RxState/ErrorCode、RX重挂、timeout状态同步、Buffer/Index、Tick/计数及芯片/供电域。

DIAG固定返回 valid frame、ORE/FE/NE/timeout/UART error、last valid tick、age、fresh、HAL states/ErrorCode。

禁止周期Reset掩盖根因。

---

## 13. Golden Vector强制测试

固件必须实现联合审核清单 G1/G2/G3/G4：

- CRC `123456789 -> 0xCBF43926`；
- Little Endian基础向量；
- 244B `CALP` Payload固定Offset向量；
- 272B `CAL4` Record固定Offset/Commit向量。

禁止使用 C struct dump 作为Wire编码。

---

## 14. JSON/TX预算

```text
ACK        <256B
RAW        <512B
CAP        <768B
READ_CHUNK <768B
所有TX     <1536B
```

必须实测不出现 `TX Pool Exhausted`，并记录cJSON TX Pool峰值。CAP删除多Profile Catalog，普通ACK不再附带整套Context/Status。

---

## 15. 固件实现顺序

1. Product/Factory/User参数归属与50W 1680/1400/893；
2. Config/Calibration/Runtime物理A/B；
3. 无Calibration正常PWM + OP_PWM_OFFSET；
4. Raw/Calibrated PWM硬件出口分离；
5. Target Payload显式codec/CRC/Golden Vector；
6. V3 Session/Result/State/14 Operations；
7. RAW `vf/flt`；
8. Output Calibration；
9. OCO Calibration与Protection分流；
10. BL0942 Freshness/根因修复；
11. BL U/I/P Calibration；
12. Format4 Calibration A/B；
13. JSON长度/Pool测试；
14. 与上位机HIL。

---

## 16. 不得误改

- Boot/APP/OTA地址和元数据；
- 普通MQTT业务；
- RTC；
- Plan语义；
- CAT1正常业务；
- 过流、过温、短路等硬件最后保护。

---

## 17. 固件完成条件

- 当前真实V2源码升级到V3；
- 14个Operation与联合文档逐字段一致；
- Payload=244B、Record=272B；
- CRC/Endian/Golden Vector通过；
- `vf/flt`一致；
- READ_CHUNK=128B；
- 无Calibration仍正常输出；
- 有Calibration不重复OP_PWM_OFFSET；
- 改SET不使Calibration失效；
- Voltage/CV不使Calibration失效；
- Output/OCO/BL U/I/P Correction全部生效；
- BL0942长稳有证据；
- Calibration A/B掉电安全；
- 无TX Pool Exhausted；
- Boot/OTA/普通业务无回归。

**本版协议设计P0已清零。实现过程中不得再以“文档没定义”为理由自行发明Wire或Storage格式。**