# CAT1 50W 固件 × 多功率通用上位机——V3 联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 分支：`main`  
> 本文定位：**V2→V3 跨端唯一联合审核基线**

## 0. 当前实现状态

必须先确认事实：

```text
固件当前代码 = V2
上位机当前代码 = V2
V3功能代码    = 尚未实现
目标          = 两端从V2升级到V3
```

任何“已支持V3”的结论必须来自后续真实代码、测试、Keil/HIL 证据，不能由本文档文字推断。

---

## 1. 产品架构

- 固件：一个功率段一个独立固件镜像；当前先做 50W；
- 上位机：保持多功率 ProductProfileRegistry；
- 设备 CAP 只返回当前编译 Target；
- 不恢复 `profilesCsv` 多型号 Catalog。

50W：

```text
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
11 points          = Level 0,20,...,200
```

---

## 2. SET_OUTCUR / HWMAX / CV / Tolerance

### SET_OUTCUR

- 语义：User Config 当前100%目标电流；
- 采用兼容方案 A：Wire 继续 `Factory.SET_OUTCUR`；
- 固件内部实际写 User Config；
- 上位机不新增 `User.SET_OUTCUR` Wire；
- SET 不进入 Calibration 有效性。

### HWMAX

- Factory Config；
- `0 < HWMAX <= Hardware Max`；
- 50W 默认1400，Hardware Max固定1680。

### CV

- 仅电子负载本次测试工况；
- 不写设备、不绑定 Calibration。

### Tolerance

- 仅上位机 APPLY 后最终 PASS/FAIL；
- 校准前不得用最终误差门槛阻止拟合。

---

## 3. 11点与PWM域

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
logicalPwm = 0..1000
rawPwm = level * 5
```

审核：

- [ ] 上位机只发送 Level，不写 CCR；
- [ ] SET_POINT 返回 actual logical PWM；
- [ ] 采点不使用旧 Output Calibration；
- [ ] 采点不叠加 OP_PWM_OFFSET；
- [ ] 无 Calibration 正常运行仍保留 Legacy OP_PWM_OFFSET；
- [ ] 有有效 Calibration 正常运行不再重复 Offset；
- [ ] 底层不存在对所有 PWM 路径无条件 `+ OP_PWM_OFFSET`。

---

## 4. Calibration 模型

### Output

```text
actual logical PWM <-> Reference Output Current
```

### OCO

```text
OCO Raw <-> Reference Output Current
```

保护链必须继续使用 Raw/保守换算，Corrected 只用于业务/MQTT。

### BL0942 Current / Power

11 点 Raw→Reference。

### BL0942 Voltage

第一版冻结为：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
finalGainQ24 = median(valid samples)
```

固件：

```text
correctedVoltage01V = (raw * gainQ24 + 2^23) >> 24
```

必须进行多输入电压 HIL 验证；若 Gain-only 无法满足 Tolerance，停止量产放行并重新评审算法，不由 Codex自行增加 Offset/多段模型。

---

## 5. RAW V3

同包返回 Raw + Corrected。

```text
Raw:
or  OCO ADC Raw
bv  BL Voltage Raw
bi  BL Current Raw
bp  BL Power Raw

Corrected:
oi  Output Current mA
iv  Input Voltage 0.1V
ii  Input Current mA
ip  Active Power 0.1W

Context:
lv, pwm, vo, age, vf, flt
```

审核：

- [ ] 11点拟合只用 Raw + Reference；
- [ ] APPLY后验证用 Corrected + Reference；
- [ ] stale 不参与拟合/验证；
- [ ] DIAG与正式RAW分离；
- [ ] `vf/flt` bit表按后续冻结版本实现。

---

## 6. Protocol V3 Operation Code——冻结

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

Wire 使用数字 `o`。

示例：

```json
{"v":3,"o":3,"s":123456,"q":8,"lv":100}
```

公共字段：

```text
v  u8   固定3
o  u8   operation
s  u32  session
q  u32  sequence
rc u8   result
st u8   wire state
```

审核：

- [ ] 两端不再把字符串Operation作为V3正式Wire；
- [ ] V2旧字段仅用于Legacy测试；
- [ ] ACK只返回当前操作所需字段；
- [ ] 普通平台协议不因V3校准改造而改变。

---

## 7. Result Code——冻结

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

审核：

- [ ] 两端数值完全一致；
- [ ] 精确重复请求重放第一次响应，不重复副作用；
- [ ] COMMIT重复请求不再次擦Flash；
- [ ] 同一s/q但参数不同必须拒绝；
- [ ] `CONTEXT_MISMATCH` 旧V2语义退出目标V3。

---

## 8. Wire State——冻结

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

流程：

```text
IDLE -> BEGIN -> ACTIVE
ACTIVE -> STAGE -> STAGED
STAGED -> APPLY -> APPLIED
APPLIED -> COMMIT -> APPLIED
APPLIED -> READ_INFO/READ_CHUNK -> RELEASE -> IDLE
ACTIVE/STAGED/APPLIED -> ABORT -> IDLE
```

审核：

- [ ] 不新增长期 COMMITTED/ABORTED Wire State；
- [ ] PASS/FAIL只存在上位机业务状态；
- [ ] ABORT恢复已提交Calibration并安全关输出。

---

## 9. STAGE Payload / Flash Record 所有权——冻结

上位机只生成并发送 **Target Calibration Payload**：

```text
Product Identity/Fingerprint
+ Output 11点
+ OCO 11点
+ BL Voltage GainQ24
+ BL Current 11点
+ BL Power 11点
```

STAGE Wire：

```text
payloadLength + payloadCrc + payload
```

固件在 RAM STAGE，APPLY 临时使用。

固件 COMMIT 时独立生成 Target Calibration Record，包括：

```text
Magic
Storage formatVersion
Generation
Payload Length
Product Identity
Payload
Record CRC
Commit Marker
```

审核：

- [ ] 上位机不设置Generation；
- [ ] 上位机不设置CommitMarker；
- [ ] 上位机不选择A/B Slot；
- [ ] Wire Payload 与 Flash Record 解耦；
- [ ] Storage Header/CRC/Commit全部由固件所有。

---

## 10. COMMIT / READBACK——冻结方向

COMMIT成功只返回摘要：

```text
generation + payloadLength + payloadCrc
```

随后：

```text
READ_INFO
-> generation / totalPayloadLength / payloadCrc

READ_CHUNK(offset,length)
-> 已提交Calibration Payload分块
```

上位机重组后 CRC + Byte Compare。

审核：

- [ ] 不要求MQTT回传完整Flash Record；
- [ ] Flash Header/CommitMarker由固件存储测试核验；
- [ ] READ_CHUNK避免大JSON/4KiB cJSON TX Pool压力。

---

## 11. JSON / TX 内存

固件现有约束：

```text
ZK_JSON_BUF_SIZE = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

验收：

- [ ] ACK `<256B`；
- [ ] RAW `<512B`；
- [ ] CAP `<768B`；
- [ ] READ_CHUNK `<768B`；
- [ ] 所有TX `<1536B`；
- [ ] 无 `TX Pool Exhausted`；
- [ ] 记录各Operation实测最大JSON长度。

---

## 12. BL0942 Freshness / 长稳

必须有：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

审核：

- [ ] HAL TX/RX返回值检查；
- [ ] ORE/FE/NE诊断；
- [ ] gState/RxState/ErrorCode证据；
- [ ] timeout/RX重挂状态一致；
- [ ] stale不伪装实时值；
- [ ] 不靠周期Reset；
- [ ] 有长稳HIL证据。

---

## 13. Flash A/B

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页2KiB，一个物理页一个Owner。

审核掉电：Config、Calibration、Runtime各自在擦除/写入/CRC/commit关键点断电仍至少保留一个有效版本。

---

## 14. 上位机流程

```text
PRECHECK
-> SET/CV/Tolerance Run Config
-> 写SET并回读
-> CAP
-> BEGIN
-> 11点 SET_POINT + RAW + Instruments
-> FITTING
-> STAGE Payload
-> APPLY
-> SET_VERIFY + RAW + Instruments
-> FAIL: ABORT
-> PASS: COMMIT
-> READ_INFO
-> READ_CHUNK + Byte Compare
-> RELEASE
```

审核：

- [ ] 校准前不按最终Tolerance判FAIL；
- [ ] APPLY后才判PASS/FAIL；
- [ ] Audit包含Run Config、Raw、Reference、Corrected、Payload CRC、Generation。

---

## 15. 当前禁止 Codex 自行决定的剩余 P0

以下内容在继续冻结前不得自行实现：

1. Target Calibration Payload 精确 Byte Layout；
2. Endian；
3. Payload CRC算法参数、覆盖范围；
4. `vf` bit表；
5. `flt` bit表；
6. 各 Operation 完整 Request/Response 字段与必填规则；
7. READ_CHUNK 最大长度；
8. Target Calibration Record Storage `formatVersion`、Header、Record CRC/Commit精确布局；
9. Golden Vector。

---

## 16. 最终放行条件

只有以下全部满足才算 V3 完成：

- 当前V2代码真正升级到V3；
- 50W 1680/1400/893正确；
- 上位机仍为多功率通用；
- SET/CV/Tolerance职责正确；
- 11点Output/OCO/BL U/I/P闭环；
- Protocol Code/rc/st/RAW/Payload逐字节一致；
- JSON/TX满足预算；
- Flash A/B掉电安全；
- BL0942长期稳定；
- Boot/OTA/普通MQTT/RTC/Plan无回归；
- 至少完成一套50W真实HIL并保留证据。
