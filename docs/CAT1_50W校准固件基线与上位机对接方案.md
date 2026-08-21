# CAT1 50W 一体化电源——校准固件 V2→V3 修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 分支：`main`  
> 文档定位：**固件侧唯一目标实施基线**  
> 当前代码状态：`V2 IMPLEMENTED / V3 NOT IMPLEMENTED`  
> 目标状态：`MIGRATE V2 -> V3`

---

## 0. 最高优先级事实：当前代码仍然是 V2

截至本文本次更新：

```text
当前固件源码 = CAL_MQTT_V2
当前 V3 功能代码 = 0
当前目标 = 在现有 V2 基础上升级改造为 V3
```

当前源码仍明确存在：

- `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION = 2`；
- V2 `profileContext`；
- `calibrationVoltage01V` 绑定；
- `configuredRatedCurrentMa` 绑定；
- `calibratedMaxCurrentMa` 运行授权；
- 198B V2 Calibration Payload；
- `SYS_CALIBRATION_STORAGE_FORMAT_VERSION = 3` 的旧 Storage Record；
- V2 多 Profile Catalog；
- V2 大 JSON Status/Context；
- 当前 PWM calibration path 仍在底层叠加 `OP_PWM_OFFSET`。

这些都是**待 V3 改造的当前代码事实**，不是 V3 已完成能力。

任何文档、测试报告或 Codex 回报都不得使用“V3 已部分实现”描述当前代码，除非后续真实代码提交完成并经过审核。

---

## 1. 本轮目标

把当前 V2 固件升级为：

- 正常业务不依赖 Calibration；
- 支持 11 点 Output Calibration；
- 支持 OCO Calibration；
- 支持 BL0942 输入 Voltage / Current / Active Power Calibration；
- Calibration MQTT Protocol V3；
- 紧凑 JSON，降低 4KiB cJSON TX Pool 与 2KiB JSON Buffer 压力；
- Config / Calibration / Runtime 独立 A/B 物理页；
- BL0942 Raw/Freshness 长期可信；
- 后续不同功率通过独立固件 Target 复用同一套 V3 公共代码。

禁止无理由修改 Boot、OTA 大分区、普通 MQTT、RTC、计划任务业务语义及其他已工作的功能。

---

## 2. 50W 冻结参数

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

固定关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

---

## 3. 一个功率段 = 一个独立固件

单个固件只允许包含自己对应的 Product Profile。

推荐 Keil Target：

```text
CAT1_50W
CAT1_75W
CAT1_100W
CAT1_150W
CAT1_200W
CAT1_240W
```

当前第一阶段只冻结 50W，后续功率复用：

- Calibration MQTT V3；
- Calibration Service；
- Output/OCO/BL0942 Calibration；
- Flash A/B；
- MQTT/OTA/RTC/Plan/Protection 公共代码。

50W 固件最终不得携带 75/100/150/200/240W `profilesCsv`、Catalog 或运行时切换表。

---

## 4. 参数职责

### 4.1 Product Profile

只放产品固定身份与硬件能力：

- profileId / MID / model；
- ratedPower；
- Hardware Max；
- RS3；
- PWM Full Scale / Polarity；
- Hardware Revision；
- 固定保护边界；
- Profile Version / Fingerprint；
- Default HWMAX / SET 仅用于首次初始化。

### 4.2 Factory Config

- HWMAX；
- SN/生产信息；
- 必要工厂参数；
- `OP_PWM_OFFSET`。

### 4.3 User Config

- SET_OUTCUR；
- 用户温控/告警/平台/上报/调光/计划等运行参数。

---

## 5. P0-SET：SET_OUTCUR Wire 兼容方案已冻结为 A

### 5.1 V3 内部正式归属

```text
SET_OUTCUR = User Config
HWMAX      = Factory Config
Hardware Max = Product Profile
```

### 5.2 外部 Wire 第一阶段保持兼容

本轮**不新增** `User.SET_OUTCUR` 普通平台字段。

现有外部兼容入口继续允许：

```json
{"Factory":{"SET_OUTCUR":893}}
```

但是固件内部不得再把它当 Factory Ownership；解析后必须统一进入：

```text
User Config.SET_OUTCUR
```

即：

```text
Legacy Wire: Factory.SET_OUTCUR
             ↓
统一合法性检查
             ↓
User Config.SET_OUTCUR
             ↓
Config A/B 持久化
```

上位机 V3 也继续使用该 Wire，避免本轮扩大普通平台协议改造范围。

`Factory.HWMAX_OUTCUR` 继续作为开发/工厂配置入口，但 V3 规则改为：

```text
0 < HWMAX <= Hardware Max
```

不再要求 `HWMAX == Hardware Max`。

---

## 6. Calibration 不绑定运行参数

以下内容不得进入 Calibration 有效性授权：

- SET_OUTCUR；
- 当前 HWMAX；
- Calibration CV / 当前输出电压；
- Tolerance；
- calibratedMaxCurrentMa。

V3 删除 V2 的：

```text
calibrationVoltage01V runtime binding
configuredRatedCurrentMa calibration binding
calibratedMaxCurrentMa runtime permission
```

```text
Calibration = Correction，不是 Permission
```

无 Calibration 必须仍能正常输出。

---

## 7. PWM 域 P0 已冻结

### 7.1 协议只操作 Level

上位机不得直接写 TIM CCR。

正式采点：

```text
Level 0   -> 0%
Level 20  -> 10%
...
Level 200 -> 100%
```

固件内部定义标准 Logical PWM Domain：

```text
logicalPwm = 0..1000
```

正式 11 点原则映射：

```text
logicalPwm = level * 5
```

即：

```text
lv=0   -> pwm=0
lv=20  -> pwm=100
lv=100 -> pwm=500
lv=200 -> pwm=1000
```

V3 ACK 中的 `pwm` 表示 **Logical PWM 0..1000**，不是 TIM CCR。

### 7.2 两条硬件路径必须拆开

当前 V2 底层 calibration PWM 仍执行：

```text
CCR = pwm + OP_PWM_OFFSET
```

V3 必须修改。

#### 无 Calibration Legacy Path

```text
Target
→ Default PWM Model
→ OP_PWM_OFFSET
→ Hardware CCR / Polarity
```

#### Calibration SET_POINT / Calibrated Runtime Path

```text
Logical PWM
→ 不叠加 OP_PWM_OFFSET
→ Hardware CCR / Polarity
```

协议层永远不暴露真实 TIM ARR/CCR/负逻辑细节。

硬件极性与 CCR 实现必须保持真实板级行为正确，但不能再次把 `OP_PWM_OFFSET` 偷偷叠加到 calibrated/raw path。

---

## 8. 正式 11 点 Calibration

固定：

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

`SET_POINT`：

- 不应用旧 Output Calibration；
- 不叠加 OP_PWM_OFFSET；
- 保留硬件最后保护；
- 返回 Actual Logical PWM。

每个点上位机读取外部 Reference + MCU RAW。

11 点过程中不逐点写 Flash。

---

## 9. Calibration 数据模型

### 9.1 Output

```text
Actual Logical PWM <-> Reference Output Current
```

运行时：

```text
SET_OUTCUR × Brightness
→ Target Current
→ 11点反插值
→ logicalPwm
```

Calibration 范围外但 Target 合法时回退 Legacy Default + OP_PWM_OFFSET，不得 PWM=0。

### 9.2 OCO

```text
OCO ADC Raw <-> Reference Output Current
```

必须分成：

```text
OCO Raw -> conservative/default -> Protection
OCO Raw -> OCO Correction -> Corrected Output Current -> Business/MQTT
```

### 9.3 BL0942 Current

```text
BL Current Raw <-> Reference Input Current
```

11 点 Raw→Reference 分段修正。

### 9.4 BL0942 Power

```text
BL Power Raw <-> Reference Active Power
```

11 点 Raw→Reference 分段修正。

---

## 10. BL0942 Voltage P0：Q24 Gain-only 已冻结

第一版不做 11 段输入电压 LUT，也不默认保存 Offset。

### 10.1 上位机生成

对每个有效校准点：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
```

要求：

- BL0942 数据必须 fresh；
- Reference 仪器有效；
- 去除无效/明显异常样本；
- 对有效 `gainQ24_i` 取中位数 `median`。

最终：

```text
voltageGainQ24 = median(valid gainQ24_i)
```

### 10.2 固件应用

运行时使用 64-bit 中间量：

```text
correctedVoltage01V =
    (blVoltageRaw * voltageGainQ24 + 2^23) >> 24
```

MCU 不为该算法引入 float。

### 10.3 验证要求

数学模型属于标准 Gain Correction，但**当前 50W 实机尚未证明该模型在完整输入电压范围内满足最终误差**。

因此 V3 HIL 必须验证多个实际输入电压点。若 Gain-only 无法满足 Tolerance，则不得量产放行，必须重新评估 Gain+Offset 或多点模型；未经实机证据禁止 Codex自行升级算法。

---

## 11. RAW V3 P0：同包返回 Raw + Corrected

拟合阶段只使用 Raw；APPLY 后 Verification 使用 Corrected 与外部 Reference 对拍。

### 11.1 当前冻结字段集合

公共：

```text
lv    u16   当前Level
pwm   u16   Actual Logical PWM 0..1000
```

Raw：

```text
or    u16   OCO ADC Raw
bv    u32   BL0942 Voltage Raw
bi    u32   BL0942 Current Raw
bp    s32   BL0942 Active Power Raw
```

Corrected：

```text
oi    u16   Corrected Output Current，mA
iv    u16   Corrected Input Voltage，0.1V
ii    u16   Corrected Input Current，mA
ip    u32   Corrected Input Active Power，0.1W
```

辅助：

```text
vo    u16   Output Voltage，0.1V
age   u32   BL0942 last valid frame age，ms
vf    u16   Valid/Fresh bitmask
flt   u16   Hardware Fault bitmask
```

### 11.2 使用规则

```text
11点 FITTING:
只使用 or/bv/bi/bp + External Reference

APPLY后 VERIFY:
使用 oi/iv/ii/ip + External Reference
同时保留 Raw 作为证据
```

`vf` 与 `flt` 的 bit 定义属于尚待最终字段冻结的 P0，不允许实现时自行发明。

正式 RAW 不携带大量 UART/BL0942诊断计数；这些放入 DIAG。

---

## 12. Calibration MQTT Protocol V3 P0

### 12.1 外层 Envelope 不变

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R/W","DT":{}}
```

本轮只升级 `SV=cal` 的 `DT`。

### 12.2 Operation 使用数字 Code

已冻结：

| `o` | Operation |
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

禁止再使用：

```text
"SET_POINT"
"STAGE_CONFIG"
"SET_VALIDATION_PERCENT"
"READBACK"
```

作为 V3 正式 Operation Wire Value。

### 12.3 公共紧凑字段

```text
v   u8   Protocol Version，固定3
o   u8   Operation Code
s   u32  Session ID
q   u32  Sequence
rc  u8   Result Code
st  u8   Wire State
```

Operation-specific 字段按需出现，不再每个 ACK 返回完整 Status/Context。

示例：

```json
{"v":3,"o":3,"s":123456,"q":8,"lv":100}
```

表示：

```text
V3 / SET_POINT / session=123456 / seq=8 / level=100
```

SET_POINT 成功响应例如：

```json
{"v":3,"o":3,"s":123456,"q":8,"rc":0,"st":1,"lv":100,"pwm":500}
```

### 12.4 CT 建议固定职责

读取类：

```text
CAP / RAW / READ_INFO / READ_CHUNK / DIAG -> CT=R
```

有副作用类：

```text
BEGIN / HEARTBEAT / SET_POINT / STAGE / APPLY /
SET_VERIFY / COMMIT / ABORT / RELEASE -> CT=W
```

如果现有中科外层对 CT 有额外固定约束，实现前必须以当前实际 MQTT 路由为准验证；不得无依据修改普通 Envelope。

---

## 13. Wire State P0 已冻结

V3 Wire State 只保留 5 个：

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

### 13.1 COMMIT

COMMIT 成功后：

- 已提交 Calibration 写入 Flash A/B；
- Wire State **仍保持 APPLIED**；
- 上位机可继续 `READ_INFO/READ_CHUNK`；
- 最后 `RELEASE` 回到 IDLE。

不增加 `COMMITTED` 长期状态。

### 13.2 ABORT

从 ACTIVE / STAGED / APPLIED：

```text
safe off
→ 丢弃 staged candidate
→ 恢复旧 committed calibration
→ 清理session
→ IDLE
```

不增加 `ABORTED` 长期 Wire State。

内部实现允许存在启动前不可用标志，但不得扩展 V3 Wire State 语义。

---

## 14. Result Code P0 尚未最终冻结

已确定：

- `rc` 使用数字；
- 不返回中文错误文本作为协议语义；
- 上位机本地映射中文说明；
- V2 的 `CONTEXT_MISMATCH` 不再作为 V3 核心概念。

**精确 rc 数值表仍待联合文档下一轮冻结。**

Codex 在该表冻结前不得自行重排、复用或新增 V3 Result Code。

---

## 15. SET_VERIFY 定义

`SET_VERIFY` 不是“固件判断 PASS/FAIL”。

它只用于 APPLY 后让固件按上位机要求输出独立验证百分比，例如：

```text
5% / 45% / 85%
或
5% / 15% / ... / 95%
```

请求携带目标百分比，上位机随后读取 RAW/Corrected 和外部 Reference，自行按 Tolerance 判断。

固件不保存 Tolerance，也不返回 PASS/FAIL。

---

## 16. 大数据 / TX 内存 P0

当前固件现实约束：

```text
ZK_JSON_BUF_SIZE       = 2048B
ZK_CJSON_TX_POOL_SIZE  = 4096B
```

V3 必须：

- 删除多 Profile Catalog；
- 删除每个 ACK 的完整 Context/Status；
- Operation 数字化；
- Key 缩短；
- RAW 与 DIAG 分离；
- 大 Record 不一次性完整回传。

目标：

```text
普通ACK < 256B
RAW     < 512B
CAP     < 768B
READ_CHUNK < 768B
所有TX JSON < 1536B
```

必须实测无 `TX Pool Exhausted`。

---

## 17. READ_INFO / READ_CHUNK

### READ_INFO

只回当前已提交 Calibration 的摘要：

```text
generation
record/payload length
crc
profile fingerprint
valid flags
```

### READ_CHUNK

用于完整字节级核验。

上位机按 `offset + length` 分块读取并最终重组；禁止一次发送完整大 Hex Record 把 TX 推近 2KiB/4KiB 极限。

每块最大字节数在 Target Calibration Record 总长度最终冻结后计算并写入 Golden Fixture。

---

## 18. Target Calibration Record——当前仍是 P0 Pending

### 18.1 已冻结逻辑内容

每次 V3 Calibration 必须形成一套完整 Correction：

```text
Header / Product Identity
Output 11点
OCO 11点
BL0942 Current 11点
BL0942 Power 11点
BL0942 Voltage Q24 Gain
CRC
Commit Metadata
```

第一版不做局部 Section Merge。

### 18.2 当前明确禁止沿用的未确认旧写法

以下内容不是当前冻结结论：

```text
“Calibration Record V2”名称
FormatVersion=2
312B固定长度
CAL2 Magic
BL Voltage gainQ20 + offsetMv
单个 READ 返回完整312B Record
```

这些内容在未经过本轮确认前不得进入实现。

### 18.3 尚待下一轮冻结

必须继续确定：

- Wire STAGE 是发送 Calibration Payload 还是完整 Flash Record；
- Magic；
- 新 Storage formatVersion；
- Header 字段；
- 每个 Section offset/size；
- Endian；
- CRC 覆盖范围；
- Generation/Commit Marker 的所有权；
- Golden Vector；
- 最大 STAGE/READ_CHUNK 长度。

在这些内容冻结前 Codex只能做结构准备，不能自行设计最终二进制格式。

---

## 19. BL0942 Freshness 与长期冻结修复

必须新增/明确：

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
- timeout 后 MCU/HAL 状态同步；
- Buffer/Index；
- 长运行 Tick/计数；
- BL0942 芯片无响应；
- BL0942 VDD/供电域。

禁止周期性 Reset 作为正式保活方案。

异常后的有限恢复可以存在，但必须有错误分类和恢复成功证明。

---

## 20. Flash 目标布局

保持：

```text
0x08000000~0x08005000 Bootloader
0x08005000~0x08008000 Persistent 12KB
0x08008000~0x08024000 APP
0x08024000~0x08040000 OTA Backup
```

Persistent：

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每个 2KiB 物理页只允许一个 Owner。

A/B：

```text
写非活动页
→ 回读/CRC
→ Commit最后生效
→ Generation选择最新有效页
```

---

## 21. 当前代码升级重点

当前 V2 代码必须明确改造：

1. `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION 2 -> 3`，但只有完整 V3 Handler 落地后才能改宏；
2. 删除 V2 Profile Context 对 Voltage/SET/calibratedMax 的授权；
3. 删除多 Profile `profilesCsv`；
4. Operation 字符串 -> 数字 `o`；
5. 长字段 -> 紧凑 V3 字段；
6. RAW -> V3 Raw+Corrected；
7. V2 `READBACK` -> `READ_INFO/READ_CHUNK`；
8. V2 `SET_VALIDATION_PERCENT` -> `SET_VERIFY`；
9. Wire State 改为5态；
10. PWM raw/calibrated path 去除 OP_PWM_OFFSET；
11. SET_OUTCUR Wire兼容但内部迁移为 User Config；
12. HWMAX 改为独立 Factory ceiling；
13. BL Voltage改为上位机生成 Q24 Gain、固件定点应用；
14. 旧198B/Storage v3最终由 Target Calibration Record 新格式替换；
15. Flash重构到完整2KiB Owner页。

---

## 22. Codex 开发门禁

在下一轮 P0 完成前：

### 可以开始

- V2现状审计；
- 50W参数职责重构；
- Config/Factory/User边界准备；
- PWM两条路径重构；
- OCO Raw/Corrected分流；
- BL0942 Freshness/根因诊断；
- JSON基础紧凑化公共设施；
- Flash A/B基础设施。

### 不允许自行决定

- V3 Result Code 最终码表；
- Target Calibration Record byte layout；
- 新 Storage formatVersion；
- `vf/flt` bit位置；
- STAGE二进制所有权边界；
- Golden Vector。

---

## 23. 最终原则

> 当前代码仍是 V2，没有完成任何 V3 功能实现。

> 本文描述的是从 V2 升级到 V3 的目标规范。

> Protocol V3 使用数字 Operation Code 与紧凑字段。

> SET_OUTCUR Wire保持兼容，但内部正式归属 User Config。

> RAW V3 同时返回 Raw + Corrected，分别服务拟合与 APPLY 后验证。

> V3 协议只操作 Level/Logical PWM，不暴露 TIM CCR。

> V3 Wire State 固定 IDLE/ACTIVE/STAGED/APPLIED/FAULT 五态。

> BL0942 Voltage 第一版使用上位机生成 Q24 Gain，中位数稳健汇总，固件定点应用。

> Target Calibration Record 的逐Byte格式仍需下一轮 P0 冻结，不允许 Codex自行发明。
