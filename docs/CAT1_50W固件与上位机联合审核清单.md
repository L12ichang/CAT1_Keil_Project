# CAT1 50W 固件 × 多功率通用上位机——联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 本文定位：**两个项目修改完成后的最终一致性审核基线**  
> 规范状态：`TARGET_SPEC_FROZEN / IMPLEMENTATION_ALIGNMENT_PENDING`
>
> 任何旧文档、V2 fixture 或实现快照均不得覆盖本清单。

---

## 1. 审核定位

当前第一阶段只冻结 50W，但后续固件必须支持通过 Keil Target 生成不同功率的**独立固件镜像**；上位机则保持多功率通用工作台。

```text
上位机
└─ ProductProfileRegistry
   ├─ 50W
   ├─ 75W
   ├─ 100W
   ├─ 150W
   ├─ 200W
   └─ 240W

固件
├─ CAT1_50W.bin   → 只含50W Profile
├─ CAT1_75W.bin   → 只含75W Profile
├─ CAT1_100W.bin  → 只含100W Profile
├─ CAT1_150W.bin  → 只含150W Profile
├─ CAT1_200W.bin  → 只含200W Profile
└─ CAT1_240W.bin  → 只含240W Profile
```

审核：

- [ ] 上位机本地保持多功率 ProductProfileRegistry；
- [ ] 单个固件只描述一个功率段；
- [ ] 单个固件不携带其他功率 Profile 参数；
- [ ] CAP 只返回当前编译 Target 的 Product；
- [ ] 不返回 `profilesCsv`；
- [ ] 上位机不依赖多型号 Catalog。

---

## 2. Keil 多 Target / 独立固件审核

推荐 Keil Target：

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

每个 Target 只定义一个：

```text
PRODUCT_TARGET_50W
PRODUCT_TARGET_75W
PRODUCT_TARGET_100W
...
```

审核：

- [ ] 同一 Build 中只能有一个 Product Target；
- [ ] 未选择 Target 编译失败；
- [ ] 同时选择多个 Target 编译失败；
- [ ] 当前 Target Profile 参数未冻结时编译失败；
- [ ] 不存在运行时 `_profiles[]` 多型号表；
- [ ] 不存在运行时 `find(profileId)` 切换不同功率；
- [ ] `CAT1_50W` 产物只含 50W Profile 常量/字符串；
- [ ] 切换 Target 不复制 MQTT/Calibration/Flash/OTA/保护公共代码；
- [ ] 产物名称能明确区分功率段。

---

## 3. 50W 冻结参数一致性

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

## 4. Product Profile / Run Config 分离

Product Profile：产品固有硬件身份与能力。

Run Config：本次任务的 SET、CV、Tolerance、Stabilization、ValidationMode、Instrument。

审核：

- [ ] CV 不进入 Calibration 有效性；
- [ ] SET 不进入 Calibration 有效性；
- [ ] 当前 HWMAX 不进入 Calibration 有效性；
- [ ] Tolerance 不进入固件运行授权；
- [ ] Run Config 被上位机 Audit 记录。

---

## 5. SET_OUTCUR 一致性

正式定义：

> SET_OUTCUR 是设备当前 100% 运行目标电流。

审核：

- [ ] SET 写 User Config；
- [ ] SET > HWMAX 被拒绝；
- [ ] 校准完成后再次修改 SET，Calibration 仍有效；
- [ ] SET 不参与 Product Fingerprint；
- [ ] SET 不参与 Calibration Record 有效性绑定；
- [ ] 无 Calibration 时 SET 仍可驱动正常输出链。

---

## 6. CV Voltage 一致性

正式定义：

> `loadCvVoltageV` 只是上位机控制电子负载的本次测试工况。

审核：

- [ ] CV 可由上位机按工艺选择；
- [ ] CV 不写入固件 User Config；
- [ ] CV 不作为 Calibration 授权；
- [ ] 固件不要求运行 Vo 等于校准 CV；
- [ ] 36V/56V 等工况变化不会自动使 Calibration 失效。

---

## 7. Tolerance 与最终验证职责

Tolerance 只属于上位机最终验收策略。

职责冻结：

```text
固件：
完整Calibration
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
- [ ] 固件没有 VERIFY/PASS/FAIL 状态；
- [ ] 校准前不使用最终误差门槛阻止 Correction；
- [ ] 校准前只检查 Fresh/稳定/仪器有效/基本单调/合理范围/硬件 Fault；
- [ ] Quick/Full Verification 由上位机执行；
- [ ] PASS 才允许 COMMIT；
- [ ] FAIL 必须 ABORT。

---

## 8. 11 点正式采集

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

## 9. 每次必须完整 Calibration

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

## 10. Output Calibration

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

## 11. OCO Calibration

```text
OCO Raw <-> Reference Output Current
```

审核：

- [ ] RAW 真正未校准；
- [ ] Corrected 用于业务/MQTT；
- [ ] Protection 使用 Raw/保守链；
- [ ] Calibration 不掩盖真实过流。

---

## 12. BL0942 Calibration / Freshness

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

## 13. Calibration MQTT Protocol V3

### 13.1 外层 Envelope

必须保持现有：

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

只升级 `SV=cal` 的 `DT`。

### 13.2 Operation

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
- [ ] 不存在旧 `SET_VALIDATION_PERCENT/SET_VERIFY`；
- [ ] 不存在 `READ_INFO/READ_CHUNK` 第一版协议。

### 13.3 公共字段

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

## 14. Result Code / State

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
- [ ] 不新增复杂 Calibration Context 状态；
- [ ] 固件没有 PASS/FAIL 状态。

---

## 15. Operation 行为审核

### CAP

- [ ] 只返回当前 Keil Target 对应产品身份；
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

- [ ] `len=312`；
- [ ] `payloadHex` 固定 624 Hex 字符；
- [ ] JSON `crc` 与 staging image 内 crc32 相同；
- [ ] Generation=0；
- [ ] CommitMarker=0xFFFFFFFF；
- [ ] ValidFlags=0x1F；
- [ ] 固件验证 Record/Fingerprint/CRC；
- [ ] 成功后只放 RAM，不写有效 Flash Slot。

### APPLY

- [ ] 临时使用完整 Calibration；
- [ ] 不写持久化有效槽。

### SET_OUTPUT

- [ ] 只在 APPLIED 使用；
- [ ] 只负责按 pct 输出；
- [ ] 返回 Actual PWM；
- [ ] 不判断误差。

### COMMIT

- [ ] 仅在上位机确认 PASS 后调用；
- [ ] Generation 由固件生成；
- [ ] 最终 Record CRC 由固件重算；
- [ ] A/B Slot 由固件选择；
- [ ] CommitMarker 最后写；
- [ ] 返回 gen/len/crc。

### READ

- [ ] 直接返回当前完整已提交 Record；
- [ ] 返回 gen/len/crc/payloadHex；
- [ ] len=312；
- [ ] 上位机逐 Byte/CRC 核验；
- [ ] CommitMarker=0xA55AA55A。

### ABORT / RELEASE

- [ ] ABORT 不改当前已提交 Record；
- [ ] 临时输出安全关闭；
- [ ] RELEASE 回 IDLE。

---

## 16. Calibration Record V2 字节级冻结

统一 Little Endian。

### Header 32B

| Offset | Size | 字段 |
|---:|---:|---|
| 0x00 | 4 | Magic=`0x324C4143` (`CAL2`) |
| 0x04 | 2 | FormatVersion=2 |
| 0x06 | 2 | RecordLength=`0x0138` |
| 0x08 | 4 | Generation |
| 0x0C | 2 | ProfileId |
| 0x0E | 2 | ProfileVersion |
| 0x10 | 4 | ProfileFingerprint |
| 0x14 | 4 | ValidFlags=`0x0000001F` |
| 0x18 | 4 | Reserved0=0 |
| 0x1C | 4 | Reserved1=0 |

ValidFlags：

```text
bit0 Output
bit1 OCO
bit2 BL Voltage
bit3 BL Current
bit4 BL Power
```

### Payload

```text
0x20 Output:     11 × {u16 pwm, u16 refCurrentMa} = 44B
0x4C OCO:        11 × {u16 raw, u16 refCurrentMa} = 44B
0x78 BL Current: 11 × {u32 raw, u16 refCurrentMa, u16 reserved} = 88B
0xD0 BL Power:   11 × {u32 raw, u32 refPowerMw} = 88B
0x128 BL Voltage: s32 gainQ20 + s32 offsetMv = 8B
```

### Footer

```text
0x130 u32 crc32
0x134 u32 commitMarker
RecordLength = 0x138 = 312B
```

最终已提交 Record：

```text
commitMarker = 0xA55AA55A
```

最终 CRC 覆盖 `[0x00,0x130)`。

审核：

- [ ] 上位机 encode 与固件 decode 逐 Byte 一致；
- [ ] 至少一组 Golden Vector；
- [ ] ValidFlags 当前只允许 0x1F；
- [ ] 不用于局部更新。

---

## 17. CRC32 统一审核

冻结算法：

```text
Name       = CRC-32/ISO-HDLC（CRC32/IEEE）
Poly       = 0x04C11DB7
RefIn      = true
RefOut     = true
Init       = 0xFFFFFFFF
XorOut     = 0xFFFFFFFF
Check      = 0xCBF43926
```

反射实现允许使用 `0xEDB88320`。

审核：

- [ ] 固件对 ASCII `123456789` 结果为 `0xCBF43926`；
- [ ] 上位机结果相同；
- [ ] Calibration Record 使用此算法；
- [ ] Product Fingerprint 使用此算法；
- [ ] 不存在另一套 CRC32 参数。

---

## 18. Product Fingerprint 字节级审核

按固定顺序拼接：

```text
profileVersion      u16 LE
profileId           u16 LE
mid                 u8
hardwareRevision    u16 LE
ratedPowerW         u16 LE
rs3Mohm             u16 LE
hardwareMaxMa       u16 LE
pwmFullScale        u16 LE
pwmPolarity         u8
ocoHardwareRevision u16 LE
```

审核：

- [ ] 固件按显式字段编码，不直接 CRC C 结构体；
- [ ] 上位机按相同字段/顺序/LE 编码；
- [ ] 不受编译器 padding 影响；
- [ ] SET_OUTCUR 不参与；
- [ ] 当前 HWMAX 不参与；
- [ ] CV 不参与；
- [ ] Tolerance 不参与；
- [ ] 计划/MQTT/运行历史不参与。

---

## 19. Flash A/B

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

## 20. 历史数据策略

当前开发阶段不做 Legacy Migration。

V3 首次部署遇到非 V3 Persistent 格式：

```text
只格式化 0x08005000~0x08008000
→ 按当前 Keil Target 初始化 Config A/B
→ Calibration 空
→ Runtime 空
```

审核：

- [ ] Boot 不擦；
- [ ] APP 不擦；
- [ ] OTA Backup 不擦；
- [ ] 旧计划/运行统计/旧配置/旧 Calibration 不迁移；
- [ ] 开发设备必要时重新写 SN/MAC/平台凭证。

---

## 21. JSON / 内存预算

目标：

- 普通 ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ `<1024B`；
- 所有设备 TX `<1536B`；
- 无 `TX Pool Exhausted`。

312B Record 使用 624 Hex 字符，应在第一版单报文预算内实测确认。

审核：

- [ ] STAGE 最大 RX 实测通过；
- [ ] READ 最大 TX 实测通过；
- [ ] cJSON Pool 留有余量；
- [ ] 不因为大 JSON 数字数组导致节点耗尽。

---

## 22. 上位机 UI / 业务审核

- [ ] 多功率产品卡保留；
- [ ] 未冻结功率显示不可量产，不删除；
- [ ] 当前设备 CAP 只匹配一个 Product；
- [ ] SET_OUTCUR 标为 User Config；
- [ ] CV 标为电子负载工况；
- [ ] Tolerance 标为上位机最终验收策略；
- [ ] 不显示“固件 Bound Voltage 必须匹配”旧逻辑；
- [ ] 上位机不要求固件返回所有功率参数。

---

## 23. 最终 HIL 场景

至少执行：

1. 50W Target 编译，确认二进制不含其他功率 Profile；
2. 切换一个待冻结 Target，确认未冻结参数会阻止正式 Build；
3. 50W 空白 V3 Persistent 初始化；
4. 无 Calibration 正常输出；
5. SET=893 完整校准；
6. 修改 SET 后 Calibration 继续有效；
7. 36V / 40V / 48V / 52V / 56V 工况验证 Calibration 不被 Context 自动失效；
8. 11 点完整采集；
9. 校准前大误差仍允许生成 Correction；
10. STAGE 312B staging image；
11. APPLY 后 Quick Validation；
12. APPLY 后 Full Validation；
13. FAIL→ABORT，旧已提交 Calibration 不变；
14. PASS→COMMIT；
15. COMMIT 前后 Generation 正确 +1；
16. COMMIT 掉电恢复；
17. READ 返回完整 312B Record；
18. CRC Golden Vector；
19. Fingerprint Golden Vector；
20. BL0942 stale / ORE / 长稳；
21. 所有 V3 报文无 TX Pool Exhausted；
22. Boot/OTA/普通业务/RTC/Plan 无回归。

---

## 24. 最终通过条件

只有同时满足以下条件才认为本轮 V3 完成：

> **每个功率段是独立固件，优先通过 Keil Target 选择生成；单个固件不混入其他功率 Profile。**

> **上位机保持多功率通用 ProductProfileRegistry。**

> **50W 参数为 Hardware Max 1680mA / Default HWMAX 1400mA / Default SET 893mA / RS3 120mΩ。**

> **无 Calibration 仍可按 SET_OUTCUR 正常运行。**

> **SET/CV/HWMAX当前值/Tolerance 不作为 Calibration 有效性 Context。**

> **每次完整校准 Output + OCO + BL0942 U/I/P，不做局部更新。**

> **固件不做最终误差 PASS/FAIL；上位机外部仪器验收，PASS→COMMIT，FAIL→ABORT。**

> **MQTT V3 字符串 Operation、rc/st、字段、单位一致。**

> **Calibration Record 固定 312B Little Endian。**

> **STAGE 使用 Generation=0 / CommitMarker=0xFFFFFFFF；Generation、final CRC、CommitMarker 由固件 COMMIT 管理。**

> **CRC32 与 Product Fingerprint 有跨端 Golden Vector。**

> **Config/Calibration/Runtime A/B 掉电安全。**

> **开发阶段旧 12KB Persistent 不迁移，首次 V3 部署直接格式化。**

> **完成真实 50W HIL，并保留可审计证据。**