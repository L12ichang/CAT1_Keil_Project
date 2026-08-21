# CAT1 50W 一体化电源——校准固件修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 文档定位：**固件侧唯一实施基线**  
> 规范状态：`TARGET_SPEC_FROZEN / IMPLEMENTATION_ALIGNMENT_PENDING`
>
> 本文定义目标实现，不表示当前源码已经完成 V3。旧 `CAL_MQTT_V2`、890mA、Voltage/SET/CalibratedMax Context 绑定只属于历史实现快照，不得覆盖本文。

---

## 1. 本轮目标

把现有 50W 固件整理成：

- 正常业务不依赖 Calibration；
- 支持 11 点 Output Calibration；
- 支持 OCO Calibration；
- 支持 BL0942 U/I/P Calibration；
- 每次完整 Calibration 一次性 STAGE / APPLY / COMMIT；
- 支持 Calibration MQTT Protocol V3；
- 支持 Config / Calibration / Runtime 三类 A/B 掉电安全存储；
- BL0942 Raw/Freshness 长期可信；
- 后续不同功率通过 **Keil Target 切换生成独立固件**，但公共代码和协议不复制。

禁止无理由修改 Boot、OTA 大分区、普通 MQTT、RTC、计划任务业务语义及其他已工作的功能。

---

## 2. 50W 冻结参数

| 参数 | 冻结值 | 归属 |
|---|---:|---|
| 额定功率 | 50W | Product Profile |
| MID | 1 | Product Profile |
| Hardware Max | **1680mA** | Product Profile |
| 默认 HWMAX | **1400mA** | Factory Config 默认值 |
| 默认 SET_OUTCUR | **893mA** | User Config 默认值 |
| RS3 | **120mΩ** | Product Profile |
| 校准点 | **11 点** | Calibration |
| Level | `0,20,...,200` | Calibration |

固定关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

---

## 3. 一个功率段 = 一个独立固件

这是 V3 的正式架构要求。

### 3.1 禁止多功率参数混入同一个固件

50W 固件中只允许存在 50W Product Profile；75W 固件只允许存在 75W Product Profile，以此类推。

禁止继续采用：

```c
static const profile_t profiles[] = {
    PROFILE_50W,
    PROFILE_75W,
    PROFILE_100W,
    ...
};
```

也不允许通过运行时 `find(profileId)` 在一个固件内切换多套功率参数。

### 3.2 推荐使用 Keil 多 Target

Keil 工程最终建议至少提供：

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

每个 Target 只定义一个编译期选择，例如：

```text
CAT1_50W   → PRODUCT_TARGET_50W
CAT1_75W   → PRODUCT_TARGET_75W
CAT1_100W  → PRODUCT_TARGET_100W
```

公共代码完全复用：

```text
MQTT / Protocol V3
Calibration Service
Output / OCO / BL0942 Calibration
Flash A/B
PWM
保护
OTA
RTC / Plan
普通业务
```

每个 Target 只选择对应的：

```text
product_profile_50w.*
product_profile_75w.*
product_profile_100w.*
...
```

目标产物建议固定命名：

```text
CAT1_50W.bin
CAT1_75W.bin
CAT1_100W.bin
CAT1_150W.bin
CAT1_200W.bin
CAT1_240W.bin
```

### 3.3 编译期要求

- 只能有一个 `PRODUCT_TARGET_xxx` 生效；
- 未选择或同时选择多个功率必须编译失败；
- 当前 Target 对应 Profile 参数未冻结时必须编译失败；
- `CAP` 只返回当前编译 Target 的 Product Profile；
- 固件中不得生成 `profilesCsv` 或其他多型号 Catalog。

这样从 50W 切换到其他功率时，只需要：

```text
冻结对应 Product Profile
→ 选择对应 Keil Target
→ Build
→ 得到对应独立固件
```

不修改 Calibration V3 主流程。

---

## 4. 参数职责

### 4.1 Product Profile

只放当前功率固件固定硬件身份与能力：

- Profile ID；
- MID / Model；
- Rated Power；
- Hardware Revision；
- Hardware Max；
- RS3；
- PWM Full Scale / Polarity；
- OCO Hardware Revision；
- 固定硬件保护边界；
- Profile Version / Fingerprint；
- Default HWMAX / SET 仅作为首次初始化默认值。

### 4.2 Factory Config

- HWMAX；
- SN/生产信息；
- 必要每机修调参数；
- `OP_PWM_OFFSET`；
- 其他明确属于工厂配置的数据。

### 4.3 User Config

- SET_OUTCUR；
- 温控；
- 告警；
- 平台/MQTT；
- 上报周期；
- 调光设置；
- 计划任务。

---

## 5. Calibration 不绑定运行参数

以下参数不得参与 Calibration 有效性授权：

- SET_OUTCUR；
- 当前 HWMAX；
- 输出电压 / CV；
- Tolerance；
- calibratedMaxCurrent。

`BOUND_OUTPUT_VOLTAGE_01V` 不再作为运行授权或 Calibration Context 条件。

实际 `Vo` 只是采样/报告状态量。

```text
Calibration = Correction，不是 Permission
```

---

## 6. 正常输出链

### 6.1 无有效 Output Calibration

```text
SET_OUTCUR × Brightness
        ↓
默认 PWM 模型
        ↓
OP_PWM_OFFSET
        ↓
硬件保护仲裁
        ↓
PWM
```

无 Calibration 不允许导致 PWM 永久为 0。

### 6.2 有有效 Output Calibration

```text
SET_OUTCUR × Brightness
        ↓
Target Current
        ↓
11点 Output Calibration 反插值
        ↓
直接得到 u16 PWM
        ↓
硬件保护仲裁
        ↓
PWM
```

有有效 Calibration 时不重复叠加 `OP_PWM_OFFSET`。

### 6.3 校准后修改 SET

只要：

```text
0 < SET_OUTCUR <= HWMAX
```

新的 SET_OUTCUR 立即进入正常 Target Current 链，已有 Calibration 继续有效。

---

## 7. 正式 11 点采集

固定：

```text
Level: 0,20,40,60,80,100,120,140,160,180,200
比例 : 0,10,20,30,40,50,60,70,80,90,100%
```

Calibration 模式下 `SET_POINT`：

- 输出已知原始逻辑 PWM；
- 不应用旧 Output Calibration；
- 不叠加 OP_PWM_OFFSET；
- 保留硬件底线保护；
- 返回 Actual PWM。

---

## 8. 每次必须完成整套 Calibration

V3 不支持局部更新。

每次完整 Calibration 必须包含：

1. Output Calibration；
2. OCO Calibration；
3. BL0942 Voltage Calibration；
4. BL0942 Current Calibration；
5. BL0942 Power Calibration。

不设计 UpdateMask，不做 Section Merge。

只有整套 Record 完整、格式正确、CRC 正确，才允许 STAGE。

---

## 9. Calibration 模型

### 9.1 Output

```text
Actual PWM <-> Reference Output Current
```

11 点保存，运行时根据 Target Current 分段线性反插值。

### 9.2 OCO

```text
OCO ADC Raw <-> Reference Output Current
```

保护链和业务修正链必须分离：

```text
OCO Raw → 保守默认换算 → Protection
   │
   └→ OCO Calibration → Corrected Current → MQTT
```

### 9.3 BL0942

- Voltage：Gain/Offset Correction；
- Current：11 点 Raw→Reference；
- Power：11 点 Raw→Reference。

无对应 Calibration 时继续使用默认换算。

---

## 10. 固件不做最终误差 PASS/FAIL

这是 V3 的正式职责边界。

固件只负责：

```text
完整 STAGE
→ APPLY 临时生效
→ SET_OUTPUT 按上位机要求输出
→ 返回 Actual PWM / RAW
```

固件不保存、不计算、不裁决 ±1%、±2% 或其他 Tolerance。

最终：

```text
上位机 + 外部标准仪器
→ 计算误差
→ PASS / FAIL

PASS → COMMIT
FAIL → ABORT
```

固件状态机中不增加 VERIFY/PASS/FAIL 状态。

---

## 11. BL0942 Freshness 与长期稳定性

必须明确：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

检查并修复：

- USART2 ORE / FE / NE；
- HAL TX/RX 返回值；
- gState / RxState / ErrorCode；
- RX 重挂；
- timeout 后状态同步；
- Buffer / Index；
- 长运行 Tick / 计数；
- 芯片无响应与供电域问题。

禁止用周期性 Reset 掩盖根因。

---

## 12. Flash 物理布局

保持大分区不变：

```text
0x08000000~0x08005000 Bootloader
0x08005000~0x08008000 Persistent 12KB
0x08008000~0x08024000 APP
0x08024000~0x08040000 OTA Backup
```

Persistent 12KB 固定按 2KB 页重新划分：

| Page | 地址 | Owner |
|---|---|---|
| 0 | `0x08005000~0x08005800` | Config A |
| 1 | `0x08005800~0x08006000` | Config B |
| 2 | `0x08006000~0x08006800` | Calibration A |
| 3 | `0x08006800~0x08007000` | Calibration B |
| 4 | `0x08007000~0x08007800` | Runtime A |
| 5 | `0x08007800~0x08008000` | Runtime B |

一个物理擦除页只有一个事务 Owner。

统一 A/B 事务：

```text
读当前有效页
→ 构造新 Record
→ 擦非活动页
→ 写 Record（CommitMarker暂不写有效值）
→ 回读/CRC
→ 最后写 CommitMarker
→ 新 Generation 生效
```

---

## 13. Calibration Record V2 字节级冻结

统一 **Little Endian**。

### 13.1 CRC32 统一定义

```text
Name       = CRC-32/ISO-HDLC（CRC32/IEEE）
Poly       = 0x04C11DB7
RefIn      = true
RefOut     = true
Init       = 0xFFFFFFFF
XorOut     = 0xFFFFFFFF
Check      = 0xCBF43926  （ASCII "123456789"）
```

反射实现可使用等价多项式 `0xEDB88320`。

### 13.2 Header，固定 32B

| Offset | Size | 字段 | 说明 |
|---:|---:|---|---|
| 0x00 | 4 | Magic | `0x324C4143`，Little Endian 字节为 `43 41 4C 32`，ASCII `CAL2` |
| 0x04 | 2 | FormatVersion | 固定 2 |
| 0x06 | 2 | RecordLength | 固定 `0x0138` = 312B |
| 0x08 | 4 | Generation | Flash COMMIT 时由固件填写 |
| 0x0C | 2 | ProfileId | 当前编译 Target 对应 Profile ID |
| 0x0E | 2 | ProfileVersion | 当前 Profile 版本 |
| 0x10 | 4 | ProfileFingerprint | 固定工厂/硬件身份 CRC32 |
| 0x14 | 4 | ValidFlags | V3 完整校准固定 `0x0000001F` |
| 0x18 | 4 | Reserved0 | 0 |
| 0x1C | 4 | Reserved1 | 0 |

ValidFlags：

```text
bit0 Output
bit1 OCO
bit2 BL0942 Voltage
bit3 BL0942 Current
bit4 BL0942 Power
```

V3 当前只接受 `0x1F`，不用于局部更新。

### 13.3 Payload

```text
0x20  Output Calibration
      11 × {u16 pwm, u16 refCurrentMa} = 44B

0x4C  OCO Calibration
      11 × {u16 raw, u16 refCurrentMa} = 44B

0x78  BL0942 Current Calibration
      11 × {u32 raw, u16 refCurrentMa, u16 reserved} = 88B

0xD0  BL0942 Power Calibration
      11 × {u32 raw, u32 refPowerMw} = 88B

0x128 BL0942 Voltage Calibration
      s32 gainQ20
      s32 offsetMv
      = 8B
```

### 13.4 Footer

```text
0x130 u32 crc32
0x134 u32 commitMarker
```

最终已提交 Record：

```text
commitMarker = 0xA55AA55A
RecordLength = 0x138 = 312B
```

最终 Record CRC 覆盖 `[0x00,0x130)`，不包含 `crc32` 自身和 CommitMarker。

---

## 14. Product Fingerprint 字节级冻结

Fingerprint 只绑定固定工厂/硬件身份。

按以下顺序生成字节流：

```text
profileVersion      u16 LE
profileId           u16 LE
mid                 u8
hardwareRevision    u16 LE
ratedPowerW         u16 LE
rs3Mohm             u16 LE
hardwareMaxMa       u16 LE
pwmFullScale        u16 LE
pwmPolarity         u8   （0=Normal，1=Inverted）
ocoHardwareRevision u16 LE
```

然后按第 13.1 节 CRC32 算法计算 Fingerprint。

禁止使用：

- C 结构体内存直接 CRC；
- 编译器 padding；
- JSON 文本；
- 字符串拼接。

明确不参与 Fingerprint：

- SET_OUTCUR；
- 当前 HWMAX；
- CV；
- Tolerance；
- 计划任务；
- MQTT/平台参数；
- 运行历史。

---

## 15. STAGE / COMMIT / READ Record 所有权

### 15.1 STAGE 线上格式

第一版 `STAGE` 仍发送完整 312B staging image，以便上下位机统一一个编码器/解码器。

STAGE payload 中：

```text
Generation   = 0
CommitMarker = 0xFFFFFFFF
```

`crc32` 字段必须是**staging image** 对 `[0x00,0x130)` 计算得到的 CRC；JSON 外层 `crc` 必须等于该 `crc32` 字段。

固件验证：

- Magic / Version / Length；
- ProfileId / ProfileVersion / Fingerprint；
- ValidFlags = 0x1F；
- Generation = 0；
- CommitMarker = 0xFFFFFFFF；
- staging CRC；
- 五类 Calibration 数据完整性和基本格式。

STAGE 成功后只存 RAM，不写为有效 Flash Calibration。

### 15.2 COMMIT 所有权

只有上位机外部验证 PASS 后调用 COMMIT。

COMMIT 时由**固件**完成：

```text
读取当前有效 Generation
→ newGeneration = current + 1
→ 将 Staged Record 的 Generation 改为 newGeneration
→ 重算最终 Record crc32
→ CommitMarker保持擦除态/无效态
→ 写入非活动 Calibration Slot
→ 回读并校验最终 CRC
→ 最后写 CommitMarker = 0xA55AA55A
→ 切换为新有效 Generation
```

上位机不得决定：

- Generation；
- A/B Slot；
- 最终 Flash Record CRC；
- CommitMarker 写入时机。

### 15.3 READ

READ 返回当前最终已提交 312B Record：

```text
gen + len + crc + payloadHex
```

上位机必须验证：

- len=312；
- payload 内 Generation == gen；
- payload 内 crc32 == JSON crc；
- CommitMarker == 0xA55AA55A；
- 最终 CRC 正确；
- Profile Fingerprint 正确。

---

## 16. Calibration MQTT Protocol V3

### 16.1 外层 Envelope 保持现有协议

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

普通 `prop/ctrl/rept/alam/ota/plan` 不改变。

### 16.2 Operation 使用字符串

冻结：

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

### 16.3 公共字段

```text
v      u8，固定3
op     string
sid    u32
seq    u32
rc     u8
st     u8
```

操作专用字段：

```text
lv         u16
pct        u8
pwm        u16
gen        u32
len        u16
crc        u32
payloadHex string
```

### 16.4 Result Code

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

### 16.5 State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

---

## 17. Operation 行为

### CAP

只返回当前编译 Target 的：

- protocolVersion=3；
- calibrationFormatVersion=2；
- profileId / profileVersion / fingerprint；
- Rated Power / MID / RS3；
- Hardware Max；
- 当前 HWMAX；
- 当前 SET_OUTCUR；
- pointCount=11；
- levelStep=20；
- generation；
- persistenceReady / ready / fault。

禁止返回 `profilesCsv`。

### BEGIN / HEARTBEAT

使用 sid/seq 维护独占会话和租约。

### SET_POINT

只接受 `0,20,...,200`，输出原始逻辑 PWM。

### RAW

返回算法需要的 OCO Raw、BL0942 Raw U/I/P、必要 Vo、Freshness、fault、actual pwm。

### STAGE

必须携带：

```text
len=312
crc
payloadHex=624个Hex字符
```

固件按第 15.1 节验证 staging image。

### APPLY

将 RAM 中 Staged Calibration 临时用于运行，不写有效 Flash Slot。

### SET_OUTPUT

只允许 APPLIED 状态，输入 `pct=1..99`，使用当前临时 Calibration 输出并返回 Actual PWM。

**只负责输出，不判断误差。**

### COMMIT

只在上位机确认 PASS 后调用，固件按第 15.2 节完成最终 A/B 提交。

### READ

直接返回当前已提交 312B Record，不引入 READ_INFO/READ_CHUNK。

### ABORT

丢弃 staged/applied RAM 临时数据并安全关闭校准输出，不修改当前已提交 Record。

### RELEASE

结束会话回 IDLE。

### DIAG

单独返回 BL0942/UART/Flash 诊断，不污染正式 RAW。

---

## 18. 报文预算

- 普通 ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ 目标 `<1024B`；
- 所有设备 TX 最终 JSON `<1536B`；
- 所有 V3 操作不得出现 `TX Pool Exhausted`。

312B Record 对应 624 个 Hex 字符，满足当前第一版单报文预算目标。

---

## 19. 固件状态机

```text
IDLE
  │ BEGIN
  ▼
ACTIVE
  │ SET_POINT / RAW / HEARTBEAT
  │ STAGE完整Record
  ▼
STAGED
  │ APPLY
  ▼
APPLIED
  │ SET_OUTPUT由上位机外部验收
  ├─ ABORT → IDLE
  └─ COMMIT
       ▼
   COMMITTED
       │ READ
       │ RELEASE
       ▼
      IDLE
```

固件不增加 VERIFY / PASS / FAIL 状态。

---

## 20. 历史数据策略：开发阶段不迁移

当前仍处开发阶段，V3 首次部署不实现 Legacy Migration。

首次识别到 Persistent Storage 不是 V3 新格式时：

1. 只格式化 `0x08005000~0x08008000` 这 12KB Persistent 区；
2. 不触碰 Boot、APP、OTA Backup；
3. Config 使用当前 Keil Target 对应 Product Profile 默认值初始化；
4. Calibration 初始为空；
5. Runtime 初始为空；
6. 历史计划、运行统计、旧配置、旧 Calibration 不迁移。

如果开发设备 SN/MAC/平台凭证原本存放在旧 Persistent 区，开发阶段重新写入/重新配网，不增加一次性 Legacy Migration 代码。

---

## 21. 推荐实施顺序

1. 把 Product Profile 重构成单功率编译期 Profile；
2. 在 Keil 工程建立 50/75/100/150/200/240W Target；
3. 当前先冻结并启用 50W Target：1680 / 1400 / 893 / RS3=120；
4. 建立新 6 页 Flash；
5. 实现 V3 首次格式化初始化；
6. 恢复无 Calibration 正常输出；
7. SET_OUTCUR → Target Current；
8. SET_POINT 原始逻辑 PWM；
9. V3 RAW；
10. Output/OCO/BL0942 U/I/P 完整 Calibration；
11. 312B Calibration Record encode/decode/CRC；
12. MQTT V3；
13. APPLY / SET_OUTPUT / COMMIT / READ；
14. BL0942 Freshness 与长期稳定；
15. 与上位机联合 HIL；
16. 掉电/OTA/长稳回归；
17. 其他功率 Profile 冻结后直接启用对应 Keil Target，不复制 V3 主链。

---

## 22. 固件验收重点

- [ ] 50W Target 只包含 50W Profile；
- [ ] 其他 Target 只包含各自 Profile；
- [ ] 固件中不存在多功率 `_profiles[]` Catalog；
- [ ] 切换 Keil Target 可直接生成对应独立固件；
- [ ] 50W 空白 Config 得到 HWMAX=1400、SET=893；
- [ ] 无 Calibration 可正常输出；
- [ ] 校准后修改 SET 不使 Calibration 失效；
- [ ] CV 改变不使 Calibration 失效；
- [ ] 每次完整校准 Output/OCO/BL0942 U/I/P；
- [ ] 不支持局部更新；
- [ ] APPLY 后固件不做 Tolerance PASS/FAIL；
- [ ] SET_OUTPUT 只负责输出；
- [ ] Fingerprint 字节顺序、LE、CRC32 与上位机一致；
- [ ] CRC32 `123456789 -> 0xCBF43926`；
- [ ] STAGE 使用 Generation=0、CommitMarker=0xFFFFFFFF；
- [ ] COMMIT 的 Generation / final CRC / CommitMarker 由固件管理；
- [ ] READ 返回最终已提交 312B Record；
- [ ] Config/Calibration/Runtime A/B 掉电安全；
- [ ] V3 首次部署清空旧 12KB Persistent；
- [ ] Boot/APP/OTA 地址不变；
- [ ] V3 JSON 无 TX Pool Exhausted；
- [ ] BL0942 长稳不靠周期 Reset。

---

## 23. 最终原则

> **每个功率段单独固件，优先通过 Keil 切 Target 生成，不把所有功率参数混进同一个二进制。**

> **公共固件框架、Protocol V3、Calibration 算法、Flash 结构保持一致；功率差异只通过当前 Target 的 Product Profile 注入。**

> **固件负责完整 Calibration 的执行、临时 APPLY、运行 Correction 和持久化。**

> **固件完成完整一次校准后，不负责判断误差是否满足要求。**

> **最终 PASS/FAIL 由上位机结合外部标准仪器判断；PASS→COMMIT，FAIL→ABORT。**

> **每次完整校准 Output + OCO + BL0942 U/I/P，不做局部更新。**