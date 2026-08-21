# CAT1 50W 固件 × 多功率通用上位机——V3 联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 分支：`main`  
> 本文定位：**V2→V3 跨端唯一字段级协议真源与最终审核基线**  
> 状态：`CURRENT_CODE_V2 / TARGET_V3_FIELD_CONTRACT_FROZEN / IMPLEMENTATION_PENDING`

## 0. 当前实现状态

必须先确认事实：

```text
固件当前代码 = V2
上位机当前代码 = V2
V3功能代码    = 尚未实现
目标          = 两端从V2升级到V3
```

当前源码中的 `CAL_MQTT_V2`、旧 Context、旧 198B Payload、旧 Storage formatVersion=3 只属于 V2 现状，不是 V3 已实现功能。

**从本次冻结开始，不再新增协议设计 P0。** 除非后续真实 Keil/HIL 证明本合同存在物理不可实现或数据类型错误，否则 Codex 必须按本文实现，不得重新设计字段、编号、字节布局、CRC、State 或 Storage。

> **本文不仅审核“协议字段是否一致”，还必须审核原实施方案中的正常运行、Fallback、仪器安全、稳定采样、Flash/OTA、BL0942根因、Audit和业务回归。字段级冻结不得覆盖或删除这些工程要求。**

---

## 1. 产品架构与50W参数

- 固件：一个功率段一个独立固件镜像；当前先完成 50W；
- 上位机：保持多功率 ProductProfileRegistry；
- 设备 CAP 只返回当前编译 Target；
- 不使用 `profilesCsv` 多型号 Catalog。

50W：

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

审核：

- [ ] 单个50W固件没有75/100/150/200/240W运行Profile Catalog；
- [ ] 上位机没有因为当前先做50W而删除其他功率UI/Profile；
- [ ] 未冻结功率可保留但不能误标为“可量产校准”。

---

## 2. SET_OUTCUR / HWMAX / CV / Tolerance

### SET_OUTCUR

- 语义：User Config 当前 100% 目标电流；
- Wire 兼容方案 A：普通属性协议继续发送 `Factory.SET_OUTCUR`；
- 固件内部必须路由并保存到 User Config；
- SET 不进入 Calibration 有效性。

### HWMAX

- Factory Config；
- `0 < HWMAX <= Hardware Max`；
- 50W 默认 1400mA，Hardware Max 固定 1680mA。

### CV

- 仅电子负载本次测试工况；
- 不写设备，不参与 Calibration 有效性。

### Tolerance

- 仅上位机 APPLY 后 PASS/FAIL 策略；
- 校准前不得以最终误差门槛阻止 Correction 生成。

审核：

- [ ] SET改变后Calibration仍有效；
- [ ] CV改变后Calibration仍有效；
- [ ] Tolerance没有进入固件Context/Flash授权；
- [ ] 有效Config重启/OTA后SET不被默认893覆盖。

---

## 3. 11点与PWM域

```text
Percent    = 0,10,...,100
Level      = 0,20,...,200
logicalPwm = level * 5 = 0,100,...,1000
```

- 上位机只发送 Level，不直接写 CCR；
- SET_POINT 返回 actual logical PWM；
- 正式采点不应用旧 Output Calibration；
- 正式采点不叠加 OP_PWM_OFFSET；
- 无有效 Calibration 的正常运行保留 Legacy OP_PWM_OFFSET；
- 有有效 Output Calibration 后不重复 Offset；
- 底层必须拆开 Legacy/Default PWM Path 与 Raw/Calibrated PWM Path。

合法 Target Current 超出Calibration覆盖范围时：

```text
Fallback -> Legacy/Default PWM + OP_PWM_OFFSET + CAL_OUT_OF_RANGE
```

不得直接PWM=0，也不得无约束外推。

---

## 4. Calibration 模型

### Output

```text
actual logical PWM <-> Reference Output Current
```

运行时 Target Current 使用邻近点分段反插值并直接得到u16 logical PWM，不做整数百分比往返量化。

### OCO

```text
OCO ADC Raw <-> Reference Output Current
```

保护链继续使用 Raw/保守换算，Corrected 只用于业务/MQTT。

### BL0942 Current / Power

```text
BL Current Raw <-> Reference Input Current
BL Power Raw   <-> Reference Input Active Power
```

### BL0942 Voltage

第一版固定 Gain-only Q24：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
finalGainQ24 = median(valid gainQ24_i)

correctedVoltage01V =
    (uint64(rawVoltage) * finalGainQ24 + 2^23) >> 24
```

上位机生成 Gain，固件应用。量产前必须做多输入电压 HIL；若 Gain-only 实测不能满足 Tolerance，再进行新的版本评审，Codex 不得自行增加 Offset/多段模型。

---

# 5. Target Calibration Payload——最终精确 Byte Layout

## 5.1 总体规则

V3 STAGE **只发送 Calibration Payload，不发送 Flash Record**。

冻结：

```text
Payload Magic       = ASCII "CALP"
Payload Version     = 1
Payload Length      = 244 bytes = 0x00F4
Endian              = Little Endian
Point Count         = 11
Level Step          = 20
ValidFlags          = 0x001F
```

所有多字节整数均为 Little Endian。JSON 数字本身不存在 Endian；只有 `ph` 解码后的二进制 Payload 和 Flash Record 使用 Little Endian。

## 5.2 Header，20 bytes

| Offset | Size | Type | Field | Frozen value / meaning |
|---:|---:|---|---|---|
| `0x00` | 4 | byte[4] | magic | `43 41 4C 50` = `CALP` |
| `0x04` | 2 | u16 | payloadVersion | `1` |
| `0x06` | 2 | u16 | payloadLength | `244` |
| `0x08` | 2 | u16 | profileId | 当前产品 Profile ID，50W=`50` |
| `0x0A` | 2 | u16 | profileVersion | 当前 Product Profile Version |
| `0x0C` | 4 | u32 | profileFingerprint | 当前产品固定 Fingerprint |
| `0x10` | 1 | u8 | pointCount | `11` |
| `0x11` | 1 | u8 | levelStep | `20` |
| `0x12` | 2 | u16 | validFlags | 固定 `0x001F` |

`validFlags`：

```text
bit0 Output Calibration
bit1 OCO Calibration
bit2 BL0942 Voltage Calibration
bit3 BL0942 Current Calibration
bit4 BL0942 Power Calibration
bit5..15 Reserved = 0
```

第一版必须整套 Calibration，因此只接受 `0x001F`，不设计部分更新。

## 5.3 Output Calibration，44 bytes

起始 `0x14`，11 × 4B：

```text
struct OutputPoint {
    u16 logicalPwm;
    u16 referenceOutputCurrentMa;
}
```

第 `i=0..10` 点对应：

```text
Level      = i * 20
logicalPwm = i * 100
```

V3 第一版要求 `logicalPwm` 必须严格等于 `0,100,...,1000`。Reference Current 使用外部电子负载/标准输出测量结果，单位 mA。

Section：`[0x14, 0x40)`。

## 5.4 OCO Calibration，44 bytes

起始 `0x40`，11 × 4B：

```text
struct OcoPoint {
    u16 ocoAdcRaw;
    u16 referenceOutputCurrentMa;
}
```

Section：`[0x40, 0x6C)`。

## 5.5 BL0942 Current Calibration，66 bytes

起始 `0x6C`，11 × 6B，**禁止依赖 C struct padding**：

```text
u32 blCurrentRaw;
u16 referenceInputCurrentMa;
```

Section：`[0x6C, 0xAE)`。

## 5.6 BL0942 Power Calibration，66 bytes

起始 `0xAE`，11 × 6B：

```text
s32 blPowerRaw;
u16 referenceInputPower01W;
```

`referenceInputPower01W` 单位 0.1W；u16 可表示到 6553.5W，满足当前及后续功率段。

Section：`[0xAE, 0xF0)`。

## 5.7 BL0942 Voltage Calibration，4 bytes

起始 `0xF0`：

```text
u32 voltageGainQ24;
```

Section：`[0xF0, 0xF4)`。

第一版 Payload **没有 Voltage Offset 字段**。

## 5.8 Payload结构总表

```text
0x000..0x013  Header                         20B
0x014..0x03F  Output 11 × 4B                44B
0x040..0x06B  OCO 11 × 4B                   44B
0x06C..0x0AD  BL Current 11 × 6B            66B
0x0AE..0x0EF  BL Power 11 × 6B              66B
0x0F0..0x0F3  BL Voltage GainQ24             4B
------------------------------------------------
Total                                         244B
```

Payload 不包含 Generation、Flash Magic、Record CRC、Commit Marker、A/B Slot。

---

# 6. Payload CRC32 与 Endian——最终冻结

## 6.1 CRC算法

所有 V3 Payload CRC 和 Flash Record CRC 统一使用：

```text
Name   = CRC-32/ISO-HDLC（CRC32/IEEE）
Poly   = 0x04C11DB7
RefIn  = true
RefOut = true
Init   = 0xFFFFFFFF
XorOut = 0xFFFFFFFF
Check  = 0xCBF43926 for ASCII "123456789"
```

反射实现允许使用 `0xEDB88320`。

## 6.2 Payload CRC覆盖范围

```text
payloadCrc = CRC32(payload[0x000 .. 0x0F3])
```

即 **244 个 Payload bytes 全覆盖**。Payload 内部不再额外放 CRC 字段。

STAGE JSON 中的 `crc` 就是这 244B 的 CRC32。

## 6.3 Hex编码

STAGE / READ_CHUNK 使用字段 `ph`：

- 每个 byte 固定编码 2 个 Hex 字符；
- 不带 `0x`；
- 上位机发送时统一大写；
- 固件解析允许 `0-9/A-F/a-f`；
- 244B 完整 Payload 的 `ph` 长度固定为 **488 chars**。

---

# 7. RAW V3 精确 Schema

RAW 响应字段类型固定：

| Field | Type | Unit / meaning |
|---|---|---|
| `lv` | u16 | 当前正式 Level |
| `pwm` | u16 | actual logical PWM，0..1000，不是 CCR |
| `or` | u16 | OCO ADC Raw |
| `bv` | u32 | BL0942 Voltage Raw |
| `bi` | u32 | BL0942 Current Raw |
| `bp` | s32 | BL0942 Power Raw |
| `oi` | u16 | Corrected Output Current，mA |
| `iv` | u16 | Corrected Input Voltage，0.1V |
| `ii` | u16 | Corrected Input Current，mA |
| `ip` | u16 | Corrected Input Active Power，0.1W |
| `vo` | u16 | 当前输出电压状态，0.1V |
| `age` | u32 | BL0942 最后有效帧年龄，ms |
| `vf` | u16 | Valid/Fresh bit mask |
| `flt` | u16 | Canonical hardware fault mask |

`RAW` 的 Raw 字段必须是真正未应用 Calibration Correction 的采样；Corrected 字段表示当前运行 Correction 路径结果。

11点 FITTING 只使用 Raw + 外部 Reference。APPLIED 验证使用 Corrected + 外部 Reference。

## 7.1 `vf` bit 定义

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
bit11..15 Reserved = 0
```

`BL_FRESH` 第一版阈值：

```text
age <= 500ms
```

正式 11 点 FITTING 最低要求 `vf & 0x003F == 0x003F`。APPLIED 验证要求 `vf & 0x03FF == 0x03FF`。`VO_VALID` 是状态/证据字段，不作为所有点强制拟合门禁。

## 7.2 `flt` bit 定义

第一版只规范当前已存在且能明确映射的故障：

```text
bit0 OUTPUT_OVERLOAD       <- Error_1_OL
bit1 OUTPUT_LOW_VOLTAGE    <- Error_Out_LV
bit2 INPUT_OVERVOLTAGE     <- Error_3_OV
bit3 INPUT_UNDERVOLTAGE    <- Error_4_LV
bit4 OVER_TEMPERATURE      <- 当前温度保护处于限幅/关断状态
bit5..15 Reserved = 0
```

规则：

- 非零正式输出/验证点：上述任何 `flt != 0` 均不得作为有效校准样本；
- Level=0 / pct=0 时允许 `OUTPUT_LOW_VOLTAGE` 因输出关闭自然出现，但其他 Fault 仍然失败；
- 未来新增 Fault 只能占用 Reserved bit，并必须同时更新两端协议版本/测试，禁止临时复用现有 bit。

BL0942 stale 不放进 `flt`，由 `vf.BL_FRESH` 与 `age` 表示；UART诊断计数放 DIAG。

---

# 8. Protocol V3 Operation / CT / Session——最终冻结

## 8.1 Operation Code

```text
0  CAP
1  BEGIN
2  HEARTBEAT
3  SET_POINT
4  RAW
5  STAGE
6  APPLY
7  SET_VERIFY
8  COMMIT
9  READ_INFO
10 READ_CHUNK
11 ABORT
12 RELEASE
13 DIAG
```

## 8.2 外层 Envelope

保持现有中科协议：

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

- `SN/ID/SV/CT` 响应必须与请求关联；
- `SV` 固定 `cal`；
- 响应 `CT` 与请求相同；
- V3 校准响应即使失败也必须返回 `DT.rc`，不使用无 DT 的普通属性短 ACK 代替；
- 普通 `prop/ctrl/rept/alam/ota/plan` 不因 V3 修改。

## 8.3 CT 映射

```text
R: CAP, RAW, READ_INFO, READ_CHUNK, DIAG
W: BEGIN, HEARTBEAT, SET_POINT, STAGE, APPLY,
   SET_VERIFY, COMMIT, ABORT, RELEASE
```

## 8.4 公共字段

### Sessionless request：CAP / DIAG

```text
v  u8 = 3
o  u8
```

### Session request

```text
v  u8 = 3
o  u8
s  u32，非0
q  u32，非0
```

BEGIN 新会话固定 `q=1`。后续新命令 `q` 严格递增。

### Session response

```text
v  u8 = 3
o  u8，回显
s  u32，回显
q  u32，回显
rc u8
st u8
```

CAP/DIAG 响应不带 `s/q`。

## 8.5 幂等

- 精确相同 `s+q+o+参数摘要` 重发：返回第一次响应，不再次执行副作用；
- 同一 `s+q` 但 `o` 或参数不同：`rc=DUPLICATE(8)`；
- `q < lastQ` 且不是可重放缓存：`rc=DUPLICATE(8)`；
- COMMIT 精确重试不得再次擦 Flash；
- Lease 超时：先安全关输出、丢弃未提交 Staged、恢复旧 committed Calibration、清 session，再返回/记录 `LEASE_EXPIRED`。

---

# 9. Result Code / Wire State——最终冻结

## 9.1 Result Code

```text
0  OK
1  NOT_AVAILABLE
2  INVALID_STATE
3  INVALID_ARGUMENT
4  LEASE_EXPIRED
5  BUSY
6  PROTOCOL_ERROR
7  SAFETY_NOT_READY
8  DUPLICATE
9  FLASH_ERROR
10 HARDWARE_FAULT
11 PROFILE_MISMATCH
12 DATA_STALE
13 CRC_ERROR
14 RANGE_ERROR
```

## 9.2 Wire State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

不设置长期 COMMITTED/ABORTED Wire State。

固件内部允许存在 `sessionCommitted` boolean，但它不是 Wire State：COMMIT 成功后 `st` 仍为 APPLIED，同时 `sessionCommitted=true`；此后仅 HEARTBEAT / READ_INFO / READ_CHUNK / RELEASE 合法，ABORT 不得回滚已成功提交的新 Calibration。

---

# 10. 每个 Operation 完整 Request / Response

下表中的字段全部位于 `DT`。

## 10.1 CAP `o=0`, CT=R

Request：

```json
{"v":3,"o":0}
```

Response 必填：

```text
v,o,rc,st
pid  u16 profileId
prv  u16 profileVersion
pf   u32 profileFingerprint
mid  u8
rp   u16 ratedPowerW
rs3  u16 rs3Mohm
hm   u16 HardwareMaxMa
mx   u16 current HWMAX
set  u16 current SET_OUTCUR
pc   u8  =11
ls   u8  =20
fs   u16 =1000 logical PWM full scale
cap  u16 capability mask
cpv  u16 =1 Calibration Payload Version
sfv  u16 =4 Storage Format Version
rma  u16 =500 Raw max age ms
has  u8  committed Calibration exists: 0/1
gen  u32 committed generation, none=0
len  u16 committed payload length, none=0
crc  u32 committed payload CRC, none=0
sr   u8 safetyReady 0/1
pr   u8 persistenceReady 0/1
flt  u16 current canonical fault mask
```

`cap`：

```text
bit0 Output Calibration
bit1 OCO Calibration
bit2 BL Voltage Calibration
bit3 BL Current Calibration
bit4 BL Power Calibration
bit5 SET_VERIFY
bit6 READ_CHUNK
bit7 DIAG
bit8..15 reserved
```

当前 50W V3 量产目标要求 `cap & 0x00FF == 0x00FF`。

## 10.2 BEGIN `o=1`, CT=W

Request 必填：

```text
v,o,s,q=1
pid u16
pf  u32
cpv u16=1
lm  u32 leaseMs，1000..600000
```

Response：公共 Session Response + `pid,pf,cpv,lm`。成功 `st=ACTIVE`。

BEGIN 只验证产品身份/协议能力/硬件安全/持久化可用，不验证 SET 等于某校准值，也不验证运行/校准电压相等。

## 10.3 HEARTBEAT `o=2`, CT=W

Request：公共 Session Request + `lm u32(1000..600000)`。

Response：公共 Session Response + `lm`。允许状态 ACTIVE/STAGED/APPLIED；成功后续租。

## 10.4 SET_POINT `o=3`, CT=W

Request：公共 Session Request + `lv u16`。

`lv` 只允许 `0,20,...,200`。

Response：公共 Session Response + `lv,pwm`。

仅 ACTIVE 合法。`pwm=lv*5`，若保护导致不能按该点输出则不能伪造成功，应返回 HARDWARE_FAULT/SAFETY_NOT_READY。

## 10.5 RAW `o=4`, CT=R

Request：仅公共 Session Request。

Response：公共 Session Response +：

```text
lv,pwm,or,bv,bi,bp,oi,iv,ii,ip,vo,age,vf,flt
```

允许 ACTIVE、APPLIED。若 BL0942 stale，仍可返回快照用于诊断，但 `vf.BL_FRESH=0`；上位机不得把该样本用于 Calibration。设备可同时返回 `rc=DATA_STALE`，但必须保留 `age/vf` 供审计。

## 10.6 STAGE `o=5`, CT=W

Request：公共 Session Request +：

```text
len u16 =244
crc u32 = CRC32(244B payload)
ph  string，固定488 Hex chars
```

仅 ACTIVE 合法。

固件必须：Hex decode → len → CRC → Payload Header → Profile → point count/step → validFlags → section range/monotonic/range 检查；成功后只写 RAM staged。

Response：公共 Session Response + `len,crc`，成功 `st=STAGED`。

## 10.7 APPLY `o=6`, CT=W

Request：仅公共 Session Request。

仅 STAGED 合法。

Response：公共 Session Response + `len,crc`，成功 `st=APPLIED`。

APPLY 仅切换到 RAM staged Correction，不写 Calibration Flash 有效槽。

## 10.8 SET_VERIFY `o=7`, CT=W

Request：公共 Session Request +：

```text
pct u8，0..100
```

仅 APPLIED 且 `sessionCommitted=false` 时合法。

Response：公共 Session Response + `pct,pwm`。

该操作只要求固件按正常 Target Current + staged Calibration 输出；不在 MCU 内判断误差。上位机随后 RAW + 外部仪器完成 PASS/FAIL。

## 10.9 COMMIT `o=8`, CT=W

Request：仅公共 Session Request。

仅 APPLIED、`sessionCommitted=false` 合法。

固件执行 Calibration A/B 原子提交。

Response：公共 Session Response +：

```text
gen u32
len u16 =244
crc u32 payload CRC
```

成功后 `st` 仍为 APPLIED，内部 `sessionCommitted=true`。

## 10.10 READ_INFO `o=9`, CT=R

Request：仅公共 Session Request。

仅 `sessionCommitted=true` 的当前 session 合法。

Response：公共 Session Response + `gen,len,crc`。

## 10.11 READ_CHUNK `o=10`, CT=R

Request：公共 Session Request +：

```text
off u16
n   u16，1..128
```

要求：

```text
off < 244
off + n <= 244
```

Response：公共 Session Response +：

```text
off u16
n   u16 actual returned bytes
ph  string，长度=2*n
```

仅当前 session `sessionCommitted=true` 合法。

## 10.12 ABORT `o=11`, CT=W

Request：仅公共 Session Request。

允许 ACTIVE/STAGED/APPLIED(`sessionCommitted=false`) / FAULT。

行为：safe off → 丢弃 staged → 恢复旧 committed Calibration → 清除 session/inhibit → IDLE。

Response：公共 Session Response，成功 `st=IDLE`。

COMMIT 已成功后 ABORT 返回 INVALID_STATE，不能撤销已原子提交结果。

## 10.13 RELEASE `o=12`, CT=W

Request：仅公共 Session Request。

正常成功流程仅在 `sessionCommitted=true` 后使用；行为：safe off → 清 session/inhibit → IDLE。

Response：公共 Session Response，成功 `st=IDLE`。

未 COMMIT 的取消流程必须走 ABORT，不用 RELEASE 替代。

## 10.14 DIAG `o=13`, CT=R

DIAG 是 sessionless，只读，不改变状态。

Request：

```json
{"v":3,"o":13}
```

Response 必填：

```text
v,o,rc,st
rx   u32 valid BL0942 frame count
ore  u32 USART ORE count
fe   u32 USART FE count
ne   u32 USART NE count
to   u32 protocol/response timeout count
ue   u32 other UART error count
lvt  u32 last_valid_frame_tick
age  u32 current BL data age ms
fr   u8  current fresh 0/1
gs   u8  HAL UART gState snapshot
rs   u8  HAL UART RxState snapshot
ec   u32 HAL UART ErrorCode snapshot
```

DIAG 不承载正式校准 Raw 数据，不参与拟合。

---

# 11. READ_CHUNK 大小——最终冻结

```text
READ_CHUNK_MAX_BYTES = 128
```

原因：

- 128B → 256 Hex chars；
- 加 Envelope/字段后远低于 768B READ_CHUNK 预算；
- 244B Payload 只需 2 个 chunk：`0..127`、`128..243`；
- 给 2KiB 最终 JSON 和 4KiB cJSON TX Pool 留明显余量。

上位机标准读取：

```text
READ_INFO len=244
READ_CHUNK off=0   n=128
READ_CHUNK off=128 n=116
```

设备允许 1..128 的其他合法 n 以支持重试，但不得返回超过128B。

---

# 12. Target Calibration Record——最终 Flash 格式

V2 当前 Storage `formatVersion=3` 是 Legacy。V3 新格式正式冻结为：

```text
Storage Format Version = 4
Record Magic           = ASCII "CAL4"
Record Length          = 272 bytes = 0x0110
Payload Length         = 244 bytes
Endian                 = Little Endian
Commit Word            = 0xC0A17EED
```

## 12.1 Header，20B

| Offset | Size | Type | Field |
|---:|---:|---|---|
| `0x00` | 4 | byte[4] | `CAL4` = `43 41 4C 34` |
| `0x04` | 2 | u16 | formatVersion = 4 |
| `0x06` | 2 | u16 | recordLength = 272 |
| `0x08` | 4 | u32 | generation，0无效；首个有效=1 |
| `0x0C` | 2 | u16 | payloadLength = 244 |
| `0x0E` | 2 | u16 | reserved = 0 |
| `0x10` | 4 | u32 | payloadCrc32 |

## 12.2 Payload

```text
0x14 .. 0x107 = Target Calibration Payload, 244B
```

## 12.3 Footer

```text
0x108 u32 recordCrc32
0x10C u32 commitWord = 0xC0A17EED
Total = 0x110 = 272B
```

`recordCrc32` 覆盖：

```text
record[0x000 .. 0x107]
```

即 Header + 244B Payload，包含 Header 中的 `payloadCrc32`，不包含 `recordCrc32` 自身和 `commitWord`。

## 12.4 A/B Commit

```text
1. 选择非活动 Calibration 2KiB page
2. 擦除非活动 page
3. 在RAM生成完整Format4 Record，generation=old+1
4. 写0x000..0x10B（Header+Payload+recordCrc）
5. Flash readback并验证Payload CRC + Record CRC
6. 最后写0x10C commitWord
7. 再读回commitWord
8. 新page成为active，旧page保持可回退
```

禁止先擦当前唯一有效页。

## 12.5 Boot选择

一个 Record 只有同时满足以下条件才有效：

- Magic=`CAL4`；
- formatVersion=4；
- recordLength=272；
- payloadLength=244；
- reserved=0；
- commitWord正确；
- payload CRC正确；
- record CRC正确；
- Payload Header/Version/Flags正确；
- Payload profileId/version/fingerprint 与当前编译 Product Profile 匹配。

A/B 同时有效时选择较新的 Generation。Generation=0 保留为无效；正常递增并跳过0。比较使用 wrap-safe u32 代差，不以简单有符号大小长期依赖。

旧 formatVersion=3 Calibration 不直接解释成 V3 Payload；若没有有效 Format4 Calibration，则设备按“未校准但可正常运行”处理。

---

# 13. Golden Vector Set——跨端单测强制使用

Golden Vector 分为三部分；三部分共同保证 Endian、CRC 参数、Offset 和字段类型一致，而不是让一端生成结果给另一端照抄。

## G1：CRC32 标准向量

```text
Input ASCII: 123456789
CRC32:       0xCBF43926
```

固件 C 和上位机 TypeScript 必须各自独立通过。

## G2：Endian 基础向量

```text
u16 0x1234       -> 34 12
u32 0x12345678   -> 78 56 34 12
s32 -2           -> FE FF FF FF
```

## G3：Target Calibration Payload Codec 向量

固定输入：

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
Header bytes[0x00..0x13] =
43 41 4C 50 01 00 F4 00 32 00 01 00 44 33 22 11 0B 14 1F 00

Output point0 @0x14 = 00 00 00 00
Output point1 @0x18 = 64 00 59 00
Output point10@0x3C = E8 03 7D 03

OCO point0   @0x40 = E8 03 00 00
OCO point10  @0x68 = D0 07 7D 03

BL-I point0  @0x6C = A0 86 01 00 00 00
BL-I point10 @0xA8 = 40 0D 03 00 C8 00

BL-P point0  @0xAE = 40 0D 03 00 00 00
BL-P point10 @0xEA = 80 1A 06 00 F4 01

GainQ24       @0xF0 = 00 00 00 01
```

两端必须用各自独立 encoder 生成完整 244B，并对上述固定 Offset 做断言；随后各自使用已通过 G1 的 CRC32 实现计算 Payload CRC。CI 中还必须保存完整 244B Hex fixture 并进行逐 Byte 比较，禁止通过 C struct 内存 dump 代替显式编码。

## G4：Flash Record Codec 向量

使用 G3 Payload，固定：

```text
generation = 7
payloadLength = 244
payloadCrc32 = CRC32(G3完整244B)
```

Record 必须满足：

```text
bytes[0x00..0x03] = 43 41 4C 34  (CAL4)
formatVersion     = 4
recordLength      = 272
payloadLength     = 244
reserved          = 0
payload起始       = 0x14
recordCrc offset  = 0x108
commit offset     = 0x10C
commit bytes      = ED 7E A1 C0
```

Record CRC 由两端测试/固件参考工具按本文算法独立计算。上位机生产协议不发送 Record；G4 主要用于固件 Storage 单测和离线解析测试。

---

# 14. JSON / TX 内存预算

当前 V2 固件约束：

```text
ZK_JSON_BUF_SIZE = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

V3 验收：

- ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ_CHUNK `<768B`；
- 所有设备 TX 最终 JSON `<1536B`；
- 无 `TX Pool Exhausted`；
- CAP 不返回多 Profile Catalog；
- ACK 不重复返回完整 Status/Context；
- 实测记录每个 Operation 最大 JSON 长度和 cJSON pool 峰值。

STAGE RX：244B Payload → 488 Hex chars，连同 Envelope 必须远低于 2048B RX 上限。

---

# 15. BL0942 Freshness / 长稳

必须有：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

审核：HAL TX/RX返回值、ORE/FE/NE、gState/RxState/ErrorCode、timeout/RX重挂、Buffer/Index、Tick/计数、芯片与供电域。

禁止周期 Reset 掩盖根因；异常后的有限恢复必须可分类、可计数、可验证。

根因审核必须能给出：

```text
冻结/异常触发证据
-> UART/HAL/协议状态
-> 根因
-> 修复
-> 有限恢复
-> 再次收到有效帧
-> 长稳结果
```

---

# 16. Flash A/B 物理布局

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页 2KiB，一个物理页一个 Owner。Calibration Format4 Record 272B 放在各自 Calibration 物理页起始，其余空间保留/擦除态，不允许被其他事务共享。

Config/Runtime也必须按真正A/B事务验收，不能Main/Backup同时擦写后称为A/B。

---

# 17. 上位机完整流程

```text
PRECHECK
-> SET/CV/Tolerance/Stabilization/Validation Run Config
-> Product/CAP核对
-> 写SET并回读
-> BEGIN
-> 11点 SET_POINT + RAW + Instruments
-> 稳定窗口 + 多样本聚合
-> FITTING
-> 生成244B Payload + CRC
-> STAGE
-> APPLY
-> SET_VERIFY + RAW + Instruments
-> FAIL: ABORT
-> PASS: COMMIT
-> READ_INFO
-> READ_CHUNK 128 + 116
-> 重组244B / CRC / Byte Compare
-> RELEASE
```

校准前不按最终 Tolerance 判 FAIL；APPLY 后才由上位机最终验收。

Quick/Full验证点应保持独立于0/10/.../100正式拟合点，例如Quick 5/45/85、Full 5/15/.../95，用于证明插值和固件实际APPLY效果。

---

# 18. V3 实现后的字段/存储审核

必须逐项证明：

- [ ] 当前 V2 代码已经真实升级到 V3，不是只改宏/文档；
- [ ] 14 个 Operation 的 JSON 与本文逐字段一致；
- [ ] Payload 固定 244B、Little Endian、Version1；
- [ ] CRC32 `123456789 -> 0xCBF43926`；
- [ ] RAW `vf/flt` 完全一致；
- [ ] READ_CHUNK 最大128B；
- [ ] Flash Record Format4 固定272B；
- [ ] Commit Word 最后写，A/B掉电安全；
- [ ] G1/G2/G3/G4 测试存在并通过；
- [ ] 50W 1680/1400/893/RS3=120正确；
- [ ] SET/CV/Tolerance不重新绑定Calibration；
- [ ] 无 Calibration 仍正常输出；
- [ ] OP_PWM_OFFSET 不在 calibrated/raw path 重复叠加；
- [ ] Output/OCO/BL U/I/P 全部闭环；
- [ ] BL0942 长稳有实机证据；
- [ ] JSON/TX 无 Pool Exhausted；
- [ ] 上位机仍为多功率通用工具；
- [ ] Boot/OTA/普通MQTT/RTC/Plan无回归；
- [ ] 50W真实HIL证据完整。

---

# 19. 设计冻结结论

截至本版，以下此前 P0 已全部一次性冻结：

```text
Target Calibration Payload精确Byte布局  DONE
Endian                                  DONE
Payload CRC                             DONE
RAW vf bit                              DONE
RAW flt bit                             DONE
每个Operation Request/Response          DONE
READ_CHUNK大小                          DONE
最终Flash Record格式                    DONE
Golden Vector Set                       DONE
```

**协议设计阶段不再保留待 Codex 自行决定的 P0。** 后续若实现中发现问题，应记录为“实现偏差 / HIL发现”，先对照本文定位根因；只有真实硬件或协议容量证明本文不可实现时，才允许通过明确版本变更重新打开设计，而不是边写代码边改协议。

---

# 20. 恢复：正常业务与Fallback联合审核

字段一致只是必要条件，下面这些运行行为同样必须审核：

- [ ] 无Calibration时设备仍能正常开关/调光；
- [ ] 无Calibration使用成熟Default PWM + OP_PWM_OFFSET；
- [ ] 有Calibration时不重复Offset；
- [ ] SET改变后Calibration不失效；
- [ ] 运行Vo/CV改变后Calibration不失效；
- [ ] 合法Target超Calibration覆盖范围时Fallback Default Path，不直接0输出；
- [ ] 硬件过流/过温/短路等保护不因Calibration被绕过；
- [ ] OCO Corrected不反向替代Raw保护链。

---

# 21. 恢复：Config / OTA / Legacy审核

必须区分旧Calibration和旧业务Config：

### Legacy Calibration

旧Storage formatVersion=3不直接解释成V3 Calibration；没有合法Format4时视为uncalibrated，但设备仍正常运行。

### Legacy业务配置

有效SET_OUTCUR、合法HWMAX、平台/MQTT、告警/温控、Plan等不得因为V3升级无条件清空。

审核：

- [ ] 默认893/1400只在空白/无效Config初始化；
- [ ] 新Config A/B存在后不重复迁移；
- [ ] 迁移幂等；
- [ ] 非法旧值回到对应Profile默认，不把合法旧值覆盖；
- [ ] OTA后SET不会无条件恢复893；
- [ ] Plan/RTC业务语义未因存储迁移改变。

---

# 22. 恢复：上位机稳定采样与仪器安全审核

稳定采样不得只做固定sleep：

- [ ] 支持基础等待；
- [ ] 连续N组样本；
- [ ] Raw/Reference变化率或峰峰值稳定条件；
- [ ] BL Fresh检查；
- [ ] 最大等待超时；
- [ ] 超时/不稳定点不用于拟合。

电子负载：

- [ ] 任务开始先safeOff；
- [ ] Profile/CAP/仪器未通过前INPUT保持OFF；
- [ ] 取消、断线、MQTT超时、BL stale、STAGE/APPLY/VERIFY/COMMIT失败都能INPUT OFF；
- [ ] RELEASE后再次确认INPUT OFF；
- [ ] 应用退出/窗口关闭有安全关闭策略。

---

# 23. 恢复：多功率UI与Audit审核

上位机必须保留：

- [ ] 多功率Product卡/Registry；
- [ ] 未冻结Profile显示不可量产而不是删除；
- [ ] Device SN/IMEI；
- [ ] 本次SET_OUTCUR；
- [ ] Load CV；
- [ ] Tolerance；
- [ ] Stabilization；
- [ ] Quick/Full Validation；
- [ ] 仪器/MQTT连接状态；
- [ ] 11点进度；
- [ ] 最终PASS/FAIL和证据入口。

`audit.jsonl` 至少能追溯：

```text
Device/Product/Firmware
Run Config
11点 actual PWM + Raw + Reference + 稳定统计
生成的Calibration/Payload CRC
APPLY后独立验证
PASS/FAIL原因
COMMIT generation/len/crc
READ_CHUNK Byte Compare
必要DIAG
异常/取消原因
```

---

# 24. 恢复：允许修改范围与非回归审核

固件重点修改范围应围绕：Product/Factory/User、PWM、Calibration、Flash、OCO、BL0942、UART2以及必要Property/Plan迁移；额外模块修改必须能解释与V3闭环的直接关系。

上位机应保留成熟：Electron main/renderer/preload隔离、MQTT QoS1、SerialManager、DC5200、SCPI Load、settings原子写、audit.jsonl、safeStorage、Vitest、打包结构。

非回归必须覆盖：

- [ ] Boot/APP/OTA地址与metadata；
- [ ] Windows Keil官方Build；
- [ ] 普通MQTT登录/在线/property/report/alarm/OTA；
- [ ] RTC/Plan；
- [ ] CAT1正常业务；
- [ ] 温控/告警/调光；
- [ ] Electron MQTT/Serial/Persistence基础设施。

---

# 25. 最终分阶段放行

不得仅凭“能跑一次11点”判定完成。推荐按以下顺序审核：

```text
A. 静态协议审计
   -> V3字段 / Byte Layout / CRC / State

B. 两端单元测试
   -> Golden Vector / codec / interpolation / retry / chunk

C. 固件正式Build
   -> Windows Keil / size / warnings

D. 台架HIL
   -> 50W完整11点 + Quick/Full验证

E. 稳定性
   -> BL0942长稳 / JSON TX Pool / MQTT retry

F. 掉电与OTA
   -> Config/Calibration/Runtime A/B power-cut
   -> OTA配置保持

G. 联合回归
   -> 普通业务 + UI + Audit
```

所有阶段通过后，才能从 `IMPLEMENTATION_PENDING` 进入量产验证状态。

**本联合文档现在既保留完整V3字段合同，也恢复原实施方案中的工程审核思想；后续不得再以“精简文档”为理由删除这些内容。**