# CAT1 50W 一体化电源——校准固件修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 文档定位：**固件侧唯一实施基线**  
> 规范状态：`TARGET_SPEC_FROZEN / IMPLEMENTATION_ALIGNMENT_PENDING`
>
> 口径索引：[`CAT1_50W文档口径说明.md`](CAT1_50W文档口径说明.md)；与旧规划、V2 fixture 或实现证据冲突时，以本文和联合审核清单为准。
> 配套上位机文档：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`  
> 最终联合审核文档：本仓库 `docs/CAT1_50W固件与上位机联合审核清单.md`

---

本文定义目标实现，不表示当前源码、Keil 产物或实机已经完成全部条目。`CAL_MQTT_V2`、890mA 默认值和运行上下文绑定只属于历史实现快照，不得反向覆盖本文 V3 / 893mA / 1400mA 目标口径。

## 1. 本轮目标

把现有 50W 固件整理成：

- 正常业务不依赖校准；
- 支持 11 点 Output Calibration；
- 支持 OCO Calibration；
- 支持 BL0942 U/I/P Calibration；
- 支持完整 Calibration 一次性 STAGE/APPLY/COMMIT；
- 支持 V3 MQTT 对接；
- 支持 Config / Calibration / Runtime 三类 A/B 掉电安全存储；
- BL0942 Raw/Freshness 可长期可信运行。

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

## 3. 参数职责

### Product Profile

只放产品固定硬件身份与能力：

- MID / Model / Rated Power；
- Hardware Max；
- RS3；
- Hardware Revision；
- PWM Full Scale / Polarity；
- OCO Hardware Revision；
- 固定硬件保护边界；
- Profile Version / Fingerprint；
- 默认 HWMAX / SET 仅作为首次初始化默认值。

### Factory Config

- HWMAX；
- SN/生产信息；
- 必要每机修调参数；
- `OP_PWM_OFFSET`。

### User Config

- SET_OUTCUR；
- 温控；
- 告警；
- 平台/MQTT；
- 上报周期；
- 调光设置；
- 计划任务。

---

## 4. Calibration 不绑定运行参数

以下参数不参与 Calibration 有效性授权：

- SET_OUTCUR；
- 当前 HWMAX；
- 输出电压 / CV；
- Tolerance；
- calibratedMaxCurrent。

`BOUND_OUTPUT_VOLTAGE_01V` 不再作为运行授权和 Calibration Context 条件。

实际 `Vo` 只是采样/报告状态量。

Calibration = Correction，不是 Permission。

---

## 5. 正常输出链

### 无有效 Output Calibration

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

### 有有效 Output Calibration

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

---

## 6. 正式 11 点采集

固定：

```text
Level: 0,20,40,60,80,100,120,140,160,180,200
比例 : 0,10,20,30,40,50,60,70,80,90,100%
```

Calibration 模式下 `SET_POINT` 输出已知原始逻辑 PWM：

- 不应用旧 Output Calibration；
- 不叠加 OP_PWM_OFFSET；
- 保留真实硬件底线保护。

---

## 7. 每次必须完成整套 Calibration

V3 不支持局部更新。

每次完整 Calibration 必须包含：

1. Output Calibration；
2. OCO Calibration；
3. BL0942 Voltage Calibration；
4. BL0942 Current Calibration；
5. BL0942 Power Calibration。

不设计 UpdateMask，不做 Section Merge。

只有整套 Record 完整、CRC 正确，才允许 STAGE。

---

## 8. Calibration 模型

### Output

```text
Actual PWM <-> Reference Output Current
```

11 点保存，运行时根据 Target Current 分段线性反插值。

### OCO

```text
OCO ADC Raw <-> Reference Output Current
```

保护链与业务修正链分离：

```text
OCO Raw → 保守默认换算 → Protection
   │
   └→ OCO Calibration → Corrected Current → MQTT
```

### BL0942

- Voltage：Gain/Offset Correction；
- Current：11 点 Raw→Reference；
- Power：11 点 Raw→Reference。

无对应 Calibration 时继续使用默认换算。

---

## 9. 固件不做最终误差 PASS/FAIL

这是 V3 的明确职责边界。

固件完成：

```text
完整STAGE
→ APPLY临时生效
→ 接受上位机SET_OUTPUT输出指定百分比
→ 返回实际PWM/RAW
```

之后是否满足 ±1%、±2% 或其他 Tolerance，完全由上位机结合外部标准仪器判断。

固件不保存、不计算、不裁决 Tolerance PASS/FAIL。

上位机流程：

```text
PASS → COMMIT
FAIL → ABORT
```

---

## 10. BL0942 Freshness 与长期稳定性

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
- Buffer/Index；
- 长运行 Tick/计数；
- 芯片无响应与供电域问题。

禁止用周期性 Reset 掩盖根因。

---

## 11. Flash 物理布局

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

原则：一个物理擦除页只有一个事务 Owner。

A/B 事务：

```text
读当前有效页
→ 构造新Record
→ 擦非活动页
→ 写Record
→ 回读/CRC
→ 最后写Commit Marker
→ 新Generation生效
```

---

## 12. Calibration Record V2 字节布局

目标 Record 控制在 512B 左右，单个 2KB Slot 预留充分余量。

统一使用 **Little Endian**，CRC 使用 **CRC32/IEEE 0x04C11DB7，init=0xFFFFFFFF，final xor=0xFFFFFFFF**。

### Header，固定 32B

| Offset | Size | 字段 | 说明 |
|---:|---:|---|---|
| 0x00 | 4 | Magic | `0x324C4143`，ASCII `CAL2` |
| 0x04 | 2 | FormatVersion | 固定 2 |
| 0x06 | 2 | RecordLength | 从 Offset 0 到 CommitMarker 结束的总长度 |
| 0x08 | 4 | Generation | 每次 COMMIT +1 |
| 0x0C | 2 | ProfileId | 当前产品 Profile ID |
| 0x0E | 2 | ProfileVersion | 产品 Profile 版本 |
| 0x10 | 4 | ProfileFingerprint | 固定工厂/硬件身份 CRC32 |
| 0x14 | 4 | ValidFlags | 当前版本固定为完整校准标志 |
| 0x18 | 4 | Reserved0 | 0 |
| 0x1C | 4 | Reserved1 | 0 |

### Payload

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

### Footer

```text
0x130 u32 crc32
0x134 u32 commitMarker = 0xA55AA55A
```

当前 RecordLength 固定 `0x138 = 312B`。

CRC 覆盖 `[0x00, 0x130)`，即不包含 `crc32` 自身和 CommitMarker。

V3 每次必须整套提交，因此 ValidFlags 当前固定表示 Output/OCO/BL U/I/P 全部有效；不设计局部有效位更新逻辑。

---

## 13. Product Fingerprint

Fingerprint 只绑定固定工厂/硬件身份，按固定顺序对以下字段做 CRC32：

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

明确不参与：

- SET_OUTCUR；
- 当前 HWMAX；
- CV；
- Tolerance；
- 计划任务；
- MQTT/平台参数；
- 运行历史。

---

## 14. Calibration MQTT Protocol V3

### 14.1 外层 Envelope 保持现有协议

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

普通 `prop/ctrl/rept/alam/ota/plan` 不改变。

### 14.2 Operation 使用字符串

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

不使用数字 Operation Code。

### 14.3 公共字段

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

### 14.4 Result Code

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

### 14.5 State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 COMMITTED
```

---

## 15. Operation 行为

### CAP

返回当前产品身份、Hardware Max、HWMAX、SET、pointCount、levelStep、formatVersion、fingerprint、generation、ready/fault。

不返回 `profilesCsv`。

### BEGIN / HEARTBEAT

使用 sid/seq 维护独占校准会话与租约。

### SET_POINT

只接受 `0,20,...,200`，输出原始逻辑 PWM。

### RAW

返回算法需要的 OCO Raw、BL0942 Raw U/I/P、必要 Vo、Freshness、fault、actual pwm。

### STAGE

请求必须携带完整 `payloadHex + len + crc`，固件验证 RecordLength、Fingerprint、完整性和 CRC 后进入 STAGED。

### APPLY

将完整 Staged Record 临时用于运行，但不写入持久化有效槽。

### SET_OUTPUT

仅允许 APPLIED 状态，输入 `pct=1..99` 或按上位机需要的合法百分比，使用当前临时 Calibration 输出并返回实际 PWM。

**SET_OUTPUT 只负责输出，不判断误差。**

### COMMIT

只有上位机确认 PASS 后才调用。固件执行完整 Record A/B 原子写入，返回 gen/len/crc。

### READ

直接返回当前已提交 Record：

```text
gen + len + crc + payloadHex
```

Record 当前仅 312B，不引入 READ_INFO/READ_CHUNK。

### ABORT

丢弃 staged/applied 临时数据并安全关闭校准输出，不修改当前已提交 Record。

### RELEASE

结束会话回到 IDLE。

### DIAG

单独返回 BL0942/UART/Flash 等诊断，不污染正式 RAW。

---

## 16. 报文预算

- 普通 ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ 目标 `<1024B`；
- 所有设备 TX 最终 JSON `<1536B`；
- 所有 V3 操作不得出现 `TX Pool Exhausted`。

大 Calibration 数据继续用 `payloadHex`，不使用大量 JSON 数字数组。

---

## 17. 固件状态机

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
  │ SET_OUTPUT由上位机做外部验收
  ├─ ABORT → IDLE
  └─ COMMIT
       ▼
   COMMITTED
       │ READ
       │ RELEASE
       ▼
      IDLE
```

固件不增加 VERIFY/PASS/FAIL 状态。

---

## 18. 历史数据策略：开发阶段不迁移

当前仍处开发阶段，V3 首次部署不实现 Legacy Migration。

首次识别到 Persistent Storage 不是 V3 新格式时：

1. 只格式化 `0x08005000~0x08008000` 这 12KB Persistent 区；
2. 不触碰 Boot、APP、OTA Backup；
3. Config 使用 V3 默认值初始化；
4. Calibration 初始为空；
5. Runtime 初始为空；
6. 历史计划、运行统计、旧配置、旧 Calibration 不迁移。

若开发设备的 SN/MAC/平台凭证原本存放在旧 Persistent 区，允许开发阶段重新写入/重新配网，不为少量历史开发设备增加一次性迁移代码。

---

## 19. 推荐实施顺序

1. 冻结 50W Product Profile；
2. 冻结新 6 页 Flash 布局；
3. 实现 V3 首次格式化初始化；
4. 恢复无 Calibration 正常输出；
5. SET_OUTCUR → Target Current；
6. SET_POINT 原始逻辑 PWM；
7. V3 RAW；
8. Output/OCO/BL0942 U/I/P 完整 Calibration；
9. Calibration Record V2 encode/decode/CRC；
10. MQTT V3；
11. APPLY / SET_OUTPUT / COMMIT / READ；
12. BL0942 Freshness 与长期稳定；
13. 与上位机联合 HIL；
14. 掉电/OTA/长稳回归。

---

## 20. 固件验收重点

- 空白 V3 Config 得到 HWMAX=1400、SET=893；
- 无 Calibration 可正常输出；
- 校准后修改 SET 不使 Calibration 失效；
- CV 改变不使 Calibration 失效；
- 每次必须完整校准 Output/OCO/BL0942 U/I/P；
- 不支持局部更新；
- APPLY 后固件不做 Tolerance PASS/FAIL；
- SET_OUTPUT 只负责输出；
- PASS/FAIL 完全由上位机判断；
- Calibration Record 长度、Endian、CRC、Fingerprint 与上位机逐 Byte 一致；
- Config/Calibration/Runtime A/B 掉电安全；
- V3 首次部署清空旧 12KB Persistent，不做 Legacy Migration；
- Boot/APP/OTA 地址不变；
- V3 JSON 无 TX Pool Exhausted；
- BL0942 长稳不靠周期 Reset。

---

## 21. 最终原则

> **固件负责完整 Calibration 的执行、临时 APPLY、持久化和运行 Correction。**

> **固件完成完整一次校准后，不负责判断误差是否满足要求。**

> **最终 PASS/FAIL 由上位机结合外部标准仪器判断；PASS→COMMIT，FAIL→ABORT。**

> **每次都完整校准 Output + OCO + BL0942 U/I/P，不做局部更新。**
