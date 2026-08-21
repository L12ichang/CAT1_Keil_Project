# CAT1 50W 固件 × 多功率通用上位机——联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 本文定位：**两个项目修改完成后的最终一致性审核基线**  
> 规范状态：`TARGET_SPEC_FROZEN / IMPLEMENTATION_ALIGNMENT_PENDING`
>
> 任何旧文档、V2 fixture 或实现快照均不得覆盖本清单。

---

## 1. 审核定位

当前固件第一阶段只冻结 50W；上位机必须保持多功率通用校准工作台。

```text
50W 固件
└─ 当前产品身份与能力

多功率上位机
├─ 50W
├─ 75W
├─ 100W
├─ 150W
├─ 200W
└─ 240W
```

- [ ] 单个固件只描述当前产品；
- [ ] 上位机本地保持 ProductProfileRegistry；
- [ ] CAP 不返回多型号 Catalog；
- [ ] 上位机不依赖 `profilesCsv`。

---

## 2. 50W 冻结参数一致性

| 参数 | 冻结值 |
|---|---:|
| Rated Power | 50W |
| MID | 1 |
| RS3 | 120mΩ |
| Hardware Max | 1680mA |
| Default HWMAX | 1400mA |
| Default SET_OUTCUR | 893mA |
| Formal Points | 11 |
| Level | 0,20,...,200 |

审核：

- [ ] 固件与上位机完全一致；
- [ ] `SET_OUTCUR <= HWMAX <= Hardware Max`；
- [ ] 旧 890mA 不再作为目标默认值；
- [ ] Hardware Max 与 HWMAX 已拆开。

---

## 3. Product Profile / Run Config 分离

Product Profile：产品固有硬件身份与能力。

Run Config：本次任务的 SET、CV、Tolerance、Stabilization、ValidationMode、Instrument。

审核：

- [ ] CV 不进入 Calibration 有效性；
- [ ] SET 不进入 Calibration 有效性；
- [ ] Tolerance 不进入固件运行授权；
- [ ] Run Config 被上位机 Audit 记录。

---

## 4. SET_OUTCUR 一致性

正式定义：

> SET_OUTCUR 是设备当前 100% 运行目标电流。

审核：

- [ ] SET 写 User Config；
- [ ] SET > HWMAX 被拒绝；
- [ ] 校准完成后再次修改 SET，Calibration 仍有效；
- [ ] SET 不参与 Product Fingerprint；
- [ ] SET 不参与 Calibration Record 有效性绑定。

---

## 5. CV Voltage 一致性

正式定义：

> `loadCvVoltageV` 只是上位机控制电子负载的本次测试工况。

审核：

- [ ] CV 可由上位机按工艺选择；
- [ ] CV 不写入固件 User Config；
- [ ] CV 不作为 Calibration 授权；
- [ ] 固件不要求运行 Vo 等于校准 CV；
- [ ] 36V/56V 等工况变化不会自动使 Calibration 失效。

---

## 6. Tolerance 与最终验证职责

Tolerance 只属于上位机最终验收策略。

必须冻结以下职责：

```text
固件：
完成完整Calibration
→ STAGE
→ APPLY
→ SET_OUTPUT
→ 返回PWM/RAW

上位机：
读取外部标准仪器
→ 计算误差
→ 按Tolerance判断PASS/FAIL

PASS → COMMIT
FAIL → ABORT
```

审核：

- [ ] 固件不计算最终 ±1%/±2% PASS/FAIL；
- [ ] 固件不保存 Tolerance；
- [ ] 固件没有 VERIFY/PASS/FAIL 业务状态；
- [ ] Quick/Full Verification 由上位机执行；
- [ ] PASS 才允许 COMMIT；
- [ ] FAIL 必须 ABORT。

---

## 7. 11 点正式采集

固定：

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

审核：

- [ ] 固件 SET_POINT 只接受正式 Level；
- [ ] SET_POINT 返回 Actual PWM；
- [ ] 采点不应用旧 Output Calibration；
- [ ] 采点不叠加 OP_PWM_OFFSET；
- [ ] 上位机每点等待稳定并采集 RAW + Reference；
- [ ] 11 点不逐点写 Flash。

---

## 8. 每次必须完整 Calibration

V3 不支持局部更新。

每次必须包含：

```text
Output
+ OCO
+ BL0942 Voltage
+ BL0942 Current
+ BL0942 Power
```

审核：

- [ ] STAGE 必须是完整 Record；
- [ ] 不存在 UpdateMask；
- [ ] 不存在 Section Merge；
- [ ] 不存在“只更新 Output”量产路径；
- [ ] 任一必要 Calibration 缺失时 STAGE 失败。

---

## 9. Output Calibration

```text
Actual PWM <-> Reference Output Current
```

审核：

- [ ] 11 点结构一致；
- [ ] 上位机拟合使用 Reference；
- [ ] 固件运行时用 Target Current 反插值；
- [ ] 输出直接得到 u16 PWM；
- [ ] 有 Calibration 时不重复 OP_PWM_OFFSET；
- [ ] Calibration 范围外合法目标回退默认链而不是 PWM=0。

---

## 10. OCO Calibration

```text
OCO Raw <-> Reference Output Current
```

审核：

- [ ] RAW 真正未校准；
- [ ] Corrected 用于业务/MQTT；
- [ ] Protection 使用 Raw/保守链；
- [ ] Calibration 不掩盖真实过流。

---

## 11. BL0942 Calibration / Freshness

审核：

- [ ] Voltage Gain/Offset 定义一致；
- [ ] Current 11 点 Raw→Reference 一致；
- [ ] Power 11 点 Raw→Reference 一致；
- [ ] 无 Calibration 回退默认换算；
- [ ] `last_valid_frame_tick/dataAge/fresh` 正确；
- [ ] stale 不用于校准；
- [ ] stale 不伪装实时上报；
- [ ] 长稳不靠周期性 Reset。

---

## 12. Calibration MQTT Protocol V3

### 外层 Envelope

必须保持现有：

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

只升级 `SV=cal` 的 `DT`。

### Operation

冻结为字符串：

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

审核：

- [ ] 不使用数字 Operation Code；
- [ ] 上下位机字符串完全一致；
- [ ] 不存在旧 `SET_VERIFY`；
- [ ] 不存在 `READ_INFO/READ_CHUNK` 第一版协议。

### 公共字段

```text
v      u8，固定3
op     string
sid    u32
seq    u32
rc     u8
st     u8
```

专用字段：

```text
lv         u16
pct        u8
pwm        u16
gen        u32
len        u16
crc        u32
payloadHex string
```

审核：

- [ ] 字段名一致；
- [ ] 单位一致；
- [ ] Signed/Unsigned 一致；
- [ ] seq 重试/幂等语义一致；
- [ ] ACK 只返回当前操作需要的数据；
- [ ] 大数据使用 payloadHex。

---

## 13. Result Code / State

### rc

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

### st

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

审核：

- [ ] 上下位机完全一致；
- [ ] 不新增复杂 Context 状态；
- [ ] 固件没有 PASS/FAIL 状态。

---

## 14. Operation 行为审核

### CAP

- [ ] 只返回当前产品身份；
- [ ] 返回 Hardware Max/HWMAX/SET/pointCount/levelStep/fingerprint/generation；
- [ ] 不返回多功率 Catalog。

### BEGIN / HEARTBEAT

- [ ] sid/seq/lease 一致；
- [ ] 会话互斥；
- [ ] 租约超时安全退出。

### SET_POINT

- [ ] 只接受 0,20,...,200；
- [ ] 返回实际 PWM。

### RAW

- [ ] 返回 OCO Raw；
- [ ] 返回 BL Raw U/I/P；
- [ ] 返回 Freshness/Fault；
- [ ] DIAG 与 RAW 分离。

### STAGE

- [ ] 必须携带完整 `payloadHex + len + crc`；
- [ ] 固件验证 Record/Fingerprint/CRC；
- [ ] 只有完整 Calibration 才能 STAGED。

### APPLY

- [ ] 临时使用完整 Calibration；
- [ ] 不写持久化有效槽。

### SET_OUTPUT

- [ ] 只在 APPLIED 使用；
- [ ] 只负责按 pct 输出；
- [ ] 返回 actual pwm；
- [ ] 不判断误差。

### COMMIT

- [ ] 仅在上位机确认 PASS 后调用；
- [ ] A/B 原子提交；
- [ ] 返回 gen/len/crc。

### READ

- [ ] 直接返回当前完整 Record；
- [ ] 返回 gen/len/crc/payloadHex；
- [ ] 上位机逐 Byte/CRC 核验。

### ABORT / RELEASE

- [ ] ABORT 不改当前已提交 Record；
- [ ] 临时输出安全关闭；
- [ ] RELEASE 回 IDLE。

---

## 15. Calibration Record V2 字节级冻结

统一 Little Endian。

CRC32：IEEE `0x04C11DB7`，init `0xFFFFFFFF`，final xor `0xFFFFFFFF`。

### Header 32B

| Offset | Size | 字段 |
|---:|---:|---|
| 0x00 | 4 | Magic=`0x324C4143` (`CAL2`) |
| 0x04 | 2 | FormatVersion=2 |
| 0x06 | 2 | RecordLength |
| 0x08 | 4 | Generation |
| 0x0C | 2 | ProfileId |
| 0x0E | 2 | ProfileVersion |
| 0x10 | 4 | ProfileFingerprint |
| 0x14 | 4 | ValidFlags |
| 0x18 | 4 | Reserved0=0 |
| 0x1C | 4 | Reserved1=0 |

### Payload

```text
0x20 Output: 11 × {u16 pwm, u16 refCurrentMa} = 44B
0x4C OCO: 11 × {u16 raw, u16 refCurrentMa} = 44B
0x78 BL Current: 11 × {u32 raw, u16 refCurrentMa, u16 reserved} = 88B
0xD0 BL Power: 11 × {u32 raw, u32 refPowerMw} = 88B
0x128 BL Voltage: s32 gainQ20 + s32 offsetMv = 8B
```

### Footer

```text
0x130 u32 crc32
0x134 u32 commitMarker = 0xA55AA55A
RecordLength = 0x138 = 312B
```

CRC 覆盖 `[0x00,0x130)`。

审核：

- [ ] 上位机 encode 与固件 decode 逐 Byte 一致；
- [ ] 至少一组 Golden Vector；
- [ ] ValidFlags 当前只表示整套 Calibration 有效，不用于局部更新。

---

## 16. Product Fingerprint

只绑定固定工厂/硬件身份：

```text
Profile Version
Profile ID / MID
Hardware Revision
Rated Power
RS3
Hardware Max
PWM Full Scale
PWM Polarity
OCO Hardware Revision
```

审核：

- [ ] SET_OUTCUR 不参与；
- [ ] 当前 HWMAX 不参与；
- [ ] CV 不参与；
- [ ] Tolerance 不参与；
- [ ] 计划/MQTT/运行历史不参与。

---

## 17. Flash A/B

目标布局：

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页 2KiB。

审核：

- [ ] 一个物理页一个 Owner；
- [ ] 非活动页擦写；
- [ ] 回读/CRC 后最后 Commit；
- [ ] 掉电至少保留一个旧有效页；
- [ ] Calibration 每次完整 Commit；
- [ ] 不写 APP 内旧 programmer 区。

---

## 18. 历史数据策略

当前开发阶段不做 Legacy Migration。

V3 首次部署遇到非 V3 Persistent 格式时：

```text
仅格式化 0x08005000~0x08008000
→ 初始化 Config A/B
→ Calibration 空
→ Runtime 空
```

审核：

- [ ] Boot 不擦；
- [ ] APP 不擦；
- [ ] OTA Backup 不擦；
- [ ] 旧计划/运行统计/旧配置/旧Calibration不迁移；
- [ ] 若 SN/MAC/平台凭证位于旧 Persistent，开发设备允许重新写入/重新配网；
- [ ] 不增加一次性复杂迁移代码。

---

## 19. JSON / TX 内存

目标：

- 普通 ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ `<1024B`；
- 所有 TX `<1536B`；
- 无 `TX Pool Exhausted`。

审核：

- [ ] 大 Calibration 使用 payloadHex；
- [ ] 不使用大数字数组；
- [ ] 不返回完整复杂 Context；
- [ ] 不返回多 Profile Catalog。

---

## 20. 上位机 UI / Audit

UI 必须明确：

- SET_OUTCUR = User Config；
- CV = 电子负载工况；
- Tolerance = 上位机最终验收策略；
- Product Profile = 产品身份/能力。

Audit 至少保存：

- 设备身份；
- Product Profile；
- SET/CV/Tolerance；
- 11 点 RAW/Reference/PWM；
- 完整 Calibration Record；
- APPLY 后验证；
- PASS/FAIL；
- COMMIT/READ 的 generation/len/crc。

---

## 21. HIL 必测场景

1. 空白 V3 设备、无 Calibration 正常输出；
2. 默认 SET=893 完整校准；
3. 修改 SET 后完整校准；
4. 36V/40V/48V/52V/56V 不同工况验证；
5. 11 点完整采集；
6. 校准前存在较大误差仍可生成 Correction；
7. 完整 STAGE；
8. APPLY 后 Quick Validation；
9. APPLY 后 Full Validation；
10. 上位机判 FAIL → ABORT；
11. 上位机判 PASS → COMMIT；
12. READ 完整回读逐 Byte/CRC；
13. Config/Calibration A/B 掉电；
14. BL0942 stale/ORE/长稳；
15. 校准后修改 SET，Calibration 仍有效；
16. 改变运行 Vo，Calibration 不因 CV 绑定失效；
17. V3 首次部署清空旧 Persistent 12KB；
18. Boot/APP/OTA 地址无变化；
19. 所有 V3 报文无 TX Pool Exhausted；
20. 上位机多功率框架仍存在。

---

## 22. 最终通过条件

只有同时满足以下条件才通过：

- 50W 固件功能正确；
- 上位机仍为多功率通用工作台；
- 每次完整 Calibration，不局部更新；
- 固件不做最终误差 PASS/FAIL；
- 上位机使用外部标准仪器进行最终验收；
- PASS→COMMIT，FAIL→ABORT；
- MQTT V3 字段、Operation、rc、st 完全一致；
- Calibration Record 逐 Byte 一致；
- Fingerprint 只绑定固定工厂/硬件身份；
- Config/Calibration/Runtime A/B 掉电安全；
- V3 首次部署不做历史迁移；
- Boot/OTA/普通业务无回归；
- 至少完成 50W 真实硬件端到端 HIL 并保留证据。
