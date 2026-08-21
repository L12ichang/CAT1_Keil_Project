# CAT1 50W 固件 × 多功率通用上位机——V2→V3 联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 本文定位：**V3 最终跨端一致性审核基线**  
> 当前代码状态：`FIRMWARE V2 / HOST V2 / V3 NOT IMPLEMENTED`

---

## 0. 审核前置事实

审核开始前必须确认：

```text
当前固件代码最初是 V2
当前上位机代码最初是 V2
V3 是本轮升级目标
```

任何“当前代码已经支持V3”的结论都必须有真实代码提交、测试和跨端证据；不能因为文档写了 V3 就视为实现完成。

Legacy V2 文档、fixture、198B Table、Profile Context 只用于迁移和回归参考，不得覆盖本文。

---

## 1. 产品架构

### 固件

```text
一个功率段 = 一个独立固件镜像
```

单个固件只含自己的 Product Profile。

### 上位机

必须保持多功率通用：

```text
50W / 75W / 100W / 150W / 200W / 240W / future
```

审核：

- [ ] 上位机 ProductProfileRegistry 未被删除；
- [ ] 单个固件不携带多功率 Catalog；
- [ ] CAP只返回当前设备产品；
- [ ] 不依赖 `profilesCsv`；
- [ ] 50W参数未写死进通用 CalibrationRunner。

---

## 2. 50W 冻结参数

```text
Rated Power      = 50W
MID              = 1
RS3              = 120mΩ
Hardware Max     = 1680mA
Default HWMAX    = 1400mA
Default SET      = 893mA
Formal Points    = 11
Level            = 0,20,...,200
```

审核：

- [ ] 固件/上位机一致；
- [ ] `SET_OUTCUR <= HWMAX <= Hardware Max`；
- [ ] 旧890不再作为目标默认SET；
- [ ] 旧“HWMAX=Hardware Max”逻辑已拆开。

---

## 3. SET_OUTCUR P0：兼容方案 A

正式语义：

```text
SET_OUTCUR = User Config
```

第一阶段 Wire 保持：

```json
{"Factory":{"SET_OUTCUR":893}}
```

审核：

- [ ] 上位机继续使用兼容 Wire；
- [ ] 固件解析后写入 User Config；
- [ ] 不新增 `User.SET_OUTCUR` 作为本轮强制协议；
- [ ] 写入有合法性检查；
- [ ] 写入后回读确认；
- [ ] Config A/B持久化；
- [ ] 重启保持；
- [ ] SET改变不使Calibration失效。

HWMAX：

- [ ] `Factory.HWMAX_OUTCUR` 内部归属 Factory Config；
- [ ] 规则为 `0 < HWMAX <= Hardware Max`；
- [ ] 不再要求 HWMAX 必须等于 Hardware Max。

---

## 4. CV / Tolerance

### CV

只属于上位机电子负载工况。

- [ ] 不写固件User Config；
- [ ] 不进入Calibration有效性；
- [ ] 不进入Fingerprint；
- [ ] 运行Vo与校准CV不相等时Calibration仍可用。

### Tolerance

只属于上位机 APPLY 后验收。

- [ ] 固件不保存Tolerance；
- [ ] 固件不做最终PASS/FAIL；
- [ ] 校准前不按最终精度门槛失败；
- [ ] PASS才COMMIT；
- [ ] FAIL走ABORT。

---

## 5. PWM P0

协议只操作：

```text
Level / Verification Percent
```

不得直接控制 TIM CCR。

Logical PWM：

```text
0..1000
logicalPwm = level * 5
```

正式点：

```text
lv=0   -> pwm=0
lv=20  -> pwm=100
lv=100 -> pwm=500
lv=200 -> pwm=1000
```

审核：

- [ ] ACK中的 `pwm` 是 Logical PWM，不是CCR；
- [ ] SET_POINT不应用旧Output Calibration；
- [ ] SET_POINT不叠加OP_PWM_OFFSET；
- [ ] 有Calibration正常运行不叠加OP_PWM_OFFSET；
- [ ] 无Calibration Legacy Path仍保留OP_PWM_OFFSET；
- [ ] TIM极性/ARR/CCR没有泄露成V3协议字段。

---

## 6. 11点完整Calibration

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

每次完整生成：

```text
Output
OCO
BL0942 Voltage
BL0942 Current
BL0942 Power
```

审核：

- [ ] 11点不逐点写Flash；
- [ ] 拟合只用Raw+Reference；
- [ ] 不用Corrected值反向再次拟合；
- [ ] 当前版本不做Section Merge/局部量产更新。

---

## 7. Output / OCO / BL Current / BL Power

### Output

```text
Actual Logical PWM <-> Reference Output Current
```

- [ ] 运行时Target Current反插值到u16 Logical PWM；
- [ ] 避免整数百分比二次量化；
- [ ] 范围外合法Target回退Legacy默认链，不PWM=0。

### OCO

```text
OCO Raw <-> Reference Output Current
```

- [ ] Protection使用Raw/保守链；
- [ ] Business/MQTT使用Corrected；
- [ ] Corrected不能掩盖真实过流。

### BL Current / Power

- [ ] Current 11点 Raw→Reference；
- [ ] Power 11点 Raw→Reference；
- [ ] 无Calibration回退原默认换算。

---

## 8. BL0942 Voltage P0：Q24 Gain-only

上位机：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
voltageGainQ24 = median(valid gainQ24_i)
```

固件：

```text
correctedVoltage01V =
    (blVoltageRaw * voltageGainQ24 + 2^23) >> 24
```

审核：

- [ ] 上位机使用u32 Q24；
- [ ] 固件使用64-bit中间乘法；
- [ ] 不引入MCU float；
- [ ] stale/无效点不参与median；
- [ ] 第一版不默认加入Offset；
- [ ] 多输入电压HIL验证完成；
- [ ] 若Gain-only不达标，未擅自改算法而是重新评审。

---

## 9. RAW V3 P0

### Raw字段

```text
or u16  OCO Raw
bv u32  BL Voltage Raw
bi u32  BL Current Raw
bp s32  BL Active Power Raw
```

### Corrected字段

```text
oi u16  Output Current mA
iv u16  Input Voltage 0.1V
ii u16  Input Current mA
ip u32  Input Active Power 0.1W
```

### Auxiliary

```text
lv  u16
pwm u16
vo  u16 0.1V
age u32 ms
vf  u16
flt u16
```

审核：

- [ ] FITTING阶段使用Raw；
- [ ] APPLY后VERIFY阶段使用Corrected+Reference；
- [ ] Raw仍保留用于Audit；
- [ ] `vf/flt` bit定义与最终协议一致；
- [ ] 正式RAW不携带大量诊断计数；
- [ ] DIAG独立。

`vf/flt` 精确bit表仍是待冻结P0，未冻结前不得实现自定义bit。

---

## 10. Calibration MQTT Protocol V3

### 10.1 外层

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

只升级 `DT`。

### 10.2 Numeric Operation Code 已冻结

| o | Operation |
|---:|---|
| 0 | CAP |
| 1 | BEGIN |
| 2 | HEARTBEAT |
| 3 | SET_POINT |
| 4 | RAW |
| 5 | STAGE |
| 6 | APPLY |
| 7 | SET_VERIFY |
| 8 | COMMIT |
| 9 | READ_INFO |
| 10 | READ_CHUNK |
| 11 | ABORT |
| 12 | RELEASE |
| 13 | DIAG |

审核：

- [ ] 上下位机相同；
- [ ] 不发送长Operation字符串作为正式V3 Wire值；
- [ ] 不恢复V2 `SET_VALIDATION_PERCENT`/`READBACK`名字。

### 10.3 公共字段

```text
v  u8  fixed=3
o  u8
s  u32
q  u32
rc u8
st u8
```

示例：

```json
{"v":3,"o":3,"s":123456,"q":8,"lv":100}
```

审核：

- [ ] ACK只返回当前操作需要的数据；
- [ ] 不重复返回大Context/Status；
- [ ] 数值类型/range一致；
- [ ] session+seq幂等语义一致。

---

## 11. V3 Wire State 已冻结

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

审核：

- [ ] 不存在COMMITTED长期Wire State；
- [ ] 不存在ABORTED长期Wire State；
- [ ] COMMIT成功后仍为APPLIED；
- [ ] READ_INFO/READ_CHUNK完成后RELEASE→IDLE；
- [ ] ABORT安全关闭、恢复旧committed、返回IDLE。

---

## 12. SET_VERIFY

定义：

> APPLY后设置一个独立验证输出百分比，仅用于上位机外部仪器验收。

审核：

- [ ] 固件不判断Tolerance；
- [ ] 固件不返回PASS/FAIL；
- [ ] 上位机可用5/45/85或5/15/.../95等策略；
- [ ] 返回Actual Logical PWM；
- [ ] 随后RAW返回Corrected供验收。

---

## 13. Result Code P0 状态

已冻结：

- `rc` 为数字；
- UI本地映射文本；
- V2 `CONTEXT_MISMATCH` 不作为V3核心概念。

未冻结：

- 精确数字码表；
- 是否保留V2现有码值以降低迁移成本；
- DATA_STALE/CRC/OUT_OF_RANGE等新增码的具体编号。

审核时若代码在文档冻结前自行决定rc表，直接判定失败。

---

## 14. JSON / TX 内存审核

当前设备现实约束：

```text
ZK_JSON_BUF_SIZE      = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

目标：

```text
ACK        <256B
RAW        <512B
CAP        <768B
READ_CHUNK <768B
全部TX     <1536B
```

审核：

- [ ] 无`TX Pool Exhausted`；
- [ ] 删除多Profile Catalog；
- [ ] 不仅测最终JSON长度，还测cJSON pool峰值；
- [ ] RAW/DIAG分离；
- [ ] 大Calibration通过Chunk读取。

---

## 15. READ_INFO / READ_CHUNK

### READ_INFO

摘要至少包括：

```text
generation
length
crc
profile fingerprint
valid flags
```

### READ_CHUNK

按：

```text
offset + length
```

分块返回二进制Hex/等价紧凑编码。

审核：

- [ ] 上位机可完整重组；
- [ ] 重组后CRC一致；
- [ ] 不一次把完整大Record塞进单条设备TX；
- [ ] 最大chunk由最终Record大小和实际TX预算计算。

---

## 16. Target Calibration Record：仍待下一轮P0冻结

### 已冻结逻辑内容

```text
Product Identity
Output 11点
OCO 11点
BL Current 11点
BL Power 11点
BL Voltage u32 Q24 Gain
CRC/Commit Metadata
```

### 明确不再视为冻结结论

```text
Calibration Record V2
FormatVersion=2
CAL2 Magic
312B固定长度
Q20 + offsetMv
单READ返回312B完整Record
```

### 仍需冻结

- STAGE发送Payload还是完整Flash Record；
- Magic；
- Storage formatVersion；
- Header；
- byte offset/size；
- Endian；
- CRC覆盖；
- Generation/CommitMarker所有权；
- Golden Vector；
- Chunk最大长度。

在这些内容完成前，Codex不得自行设计最终二进制合同。

---

## 17. BL0942 Freshness / 长稳

必须：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

审核：

- [ ] HAL TX/RX返回值检查；
- [ ] ORE/FE/NE分类；
- [ ] gState/RxState/ErrorCode证据；
- [ ] timeout后状态同步；
- [ ] stale不用于校准；
- [ ] stale不伪装实时；
- [ ] 无周期性Reset掩盖根因；
- [ ] 有长稳测试证据。

---

## 18. Flash A/B

目标：

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

审核：

- [ ] 每2KiB物理页一个Owner；
- [ ] 写非活动页；
- [ ] 回读/CRC后最后Commit；
- [ ] 掉电保留旧有效页；
- [ ] Generation选择正确；
- [ ] 不写APP内旧programmer区。

---

## 19. 上位机端到端目标流程

```text
PRECHECK
→ 选择Product Profile
→ 配置SET/CV/Tolerance
→ 写SET并回读
→ CAP
→ BEGIN
→ 11点 SET_POINT + RAW + Instruments
→ FITTING
→ STAGE
→ APPLY
→ SET_VERIFY + RAW Corrected + Instruments
→ PASS ?
   ├─ NO  -> ABORT
   └─ YES -> COMMIT
→ READ_INFO
→ READ_CHUNK完整核验
→ RELEASE
→ COMPLETED
```

---

## 20. UI审核

必须保留：

- 多功率产品卡；
- IMEI/SN；
- 当前Product；
- SET_OUTCUR；
- Electronic Load CV；
- Allowed Tolerance；
- Stabilization；
- ValidationMode；
- 仪器状态。

文案：

```text
SET = 设备User Config（Wire兼容Factory.SET_OUTCUR）
CV = 本次电子负载工况
Tolerance = APPLY后上位机验收
```

---

## 21. 其他功能回归

### 固件

- [ ] Boot地址/APP地址不变；
- [ ] OTA合同不变；
- [ ] RTC正常；
- [ ] Plan语义不变；
- [ ] 普通MQTT不因V3改造；
- [ ] CAT1正常；
- [ ] 硬件保护正常。

### 上位机

- [ ] MQTT基础设施无无必要重写；
- [ ] Serial/DC5200/SCPI保持；
- [ ] safeStorage/settings保持；
- [ ] audit.jsonl保持；
- [ ] Electron安全边界不倒退。

---

## 22. 当前允许 Codex 开发的范围

### 可以

- V2现状审计；
- 50W参数职责重构；
- SET兼容Wire→User Config内部路由；
- PWM Legacy/Calibrated双路径；
- OCO Raw/Corrected分流；
- BL0942 Freshness与根因修复；
- Numeric Operation/Compact JSON基础；
- 5-state wire state框架；
- RAW Raw+Corrected结构；
- Q24 Voltage Gain实现框架；
- Flash A/B基础设施。

### 暂不允许自行决定

- rc最终码表；
- vf/flt bit表；
- Target Calibration Record byte contract；
- Storage formatVersion；
- STAGE最终binary contract；
- Golden Vector。

---

## 23. 最终通过条件

只有同时满足：

> 固件真实从V2升级到V3。

> 上位机真实从V2升级到V3。

> Numeric Operation、5-state、RAW Raw+Corrected、PWM Logical Domain完全一致。

> SET Wire兼容但内部归属User Config。

> BL Voltage Q24 Gain上下位机算法一致并通过实机多电压点验证。

> Target Calibration Record最终合同逐Byte一致。

> 无TX Pool Exhausted。

> Flash掉电安全。

> BL0942长稳有证据。

> Boot/OTA/普通业务/RTC/Plan无回归。
