# CAT1 50W 一体化电源——校准固件 V2→V3 修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 分支：`main`  
> 文档定位：**固件侧唯一实施基线**  
> 当前代码状态：**V2，尚未进行任何 V3 功能修改**  
> 目标：**将当前 V2 固件升级改造为 Calibration MQTT Protocol V3**

## 0. 最高优先级事实

当前源码仍然是 V2：

- `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION = 2`；
- 仍存在 `profileContext`；
- 仍绑定 `calibrationVoltage01V / configuredRatedCurrentMa / calibratedMaxCurrentMa`；
- 仍使用旧 198B Calibration Payload；
- 当前 Calibration Storage `formatVersion=3` 只是旧 Storage Record 版本；
- 当前正常 PWM 仍存在 Voltage/Calibration Context 门禁；
- 当前 Calibration PWM 底层仍会叠加 `OP_PWM_OFFSET`。

因此本文中的 V3 全部是**目标规范**，不能把现有 V2 代码误认为已经部分完成 V3。

---

## 1. 50W 冻结参数

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

## 2. 一个功率段一个独立固件

第一阶段只实现 50W，但公共架构必须允许后续通过 Keil Target 生成独立功率镜像。

```text
CAT1_50W.bin   -> 只包含 50W Profile
CAT1_75W.bin   -> 只包含 75W Profile
CAT1_100W.bin  -> 只包含 100W Profile
...
```

单个固件不得继续携带全部功率 Profile Catalog；`CAP` 只返回当前编译 Target 的产品身份。

---

## 3. 参数职责

### Product Profile

产品固定身份与硬件能力：MID、Model、Rated Power、Hardware Max、RS3、Profile Version/Fingerprint、固定硬件保护参数。

### Factory Config

工厂/开发可调参数：HWMAX、OP_PWM_OFFSET、生产信息等。

### User Config

用户运行参数：SET_OUTCUR、温控、告警、平台、调光、计划等。

### SET_OUTCUR Wire 兼容策略——已冻结

采用方案 A：**外部 Wire 保持兼容，内部归属重构。**

V3 上位机仍通过现有普通属性命令：

```json
{"Factory":{"SET_OUTCUR":893}}
```

固件接收后必须路由到：

```text
User Config.SET_OUTCUR
```

不得继续把 SET_OUTCUR 作为 Factory Storage 成员。

`Factory.HWMAX_OUTCUR` 继续作为 Factory 命令，但 V3 合法性为：

```text
0 < HWMAX <= Hardware Max
```

禁止要求 HWMAX 必须等于 Hardware Max。

---

## 4. Calibration 不再绑定运行参数

以下参数不得参与 Calibration 有效性授权：

- SET_OUTCUR；
- 当前 HWMAX；
- 校准时电子负载 CV；
- 运行输出电压；
- Tolerance；
- calibratedMaxCurrent。

```text
Calibration = Correction，不是 Permission
```

无 Calibration 仍必须允许正常输出。

---

## 5. 正常 PWM 与 Calibration PWM 域——已冻结

### 5.1 协议层只操作 Level

V3 不允许上位机直接写 TIM CCR。

正式采点：

```text
Level 0   -> 0%
Level 20  -> 10%
...
Level 200 -> 100%
```

固件把 Level 转成标准逻辑 PWM 域：

```text
logicalPwm = 0..1000
rawPwm = level * 5
```

`SET_POINT` ACK 返回实际 `logicalPwm`。

### 5.2 无有效 Output Calibration

```text
SET_OUTCUR × Brightness
-> 默认 PWM 模型
-> OP_PWM_OFFSET
-> 硬件保护仲裁
-> CCR
```

### 5.3 Calibration SET_POINT / 有效 Calibration 正常运行

```text
logicalPwm
-> 不叠加 OP_PWM_OFFSET
-> 硬件保护仲裁
-> CCR
```

因此底层必须明确拆成 Legacy/Default Path 与 Raw/Calibrated Path，禁止在统一硬件出口无条件 `pwm + OP_PWM_OFFSET`。

---

## 6. 11 点完整校准

正式点固定：

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

每次完整 Calibration 必须同时生成：

1. Output Calibration；
2. OCO Calibration；
3. BL0942 Voltage Calibration；
4. BL0942 Current Calibration；
5. BL0942 Power Calibration。

第一版不做局部 Section Update，不做 UpdateMask/Section Merge。

---

## 7. Calibration 模型

### Output

```text
Actual logical PWM <-> Reference Output Current
```

运行时根据 Target Current 做 11 点分段线性反插值，直接得到 u16 logical PWM。

### OCO

```text
OCO ADC Raw <-> Reference Output Current
```

必须分两条链：

```text
OCO Raw -> 保守默认换算 -> Protection
OCO Raw -> OCO Correction -> Corrected Output Current -> MQTT/业务
```

### BL0942 Current / Power

```text
BL Current Raw <-> Reference Input Current
BL Power Raw   <-> Reference Active Power
```

### BL0942 Voltage——已冻结

第一版使用 **Gain-only Q24**，算法由上位机生成，固件只应用。

上位机每个有效点：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
```

剔除 stale/无效样本后，对有效 `gainQ24_i` 取中位数：

```text
finalGainQ24 = median(gainQ24_i)
```

固件运行：

```text
correctedVoltage01V =
    (rawVoltage * finalGainQ24 + 2^23) >> 24
```

实现时必须使用足够宽的中间整数避免乘法溢出。

该模型数学上是标准 Gain 校正，但当前 50W 实机尚未证明在整个输入电压范围满足目标精度。量产放行前必须使用可提供的多个输入电压点进行 HIL 验证；若 Gain-only 不能满足 Tolerance，再升级模型，禁止在没有实测数据时擅自加入 Offset/分段模型。

---

## 8. RAW V3——已冻结总体语义

一个精简 RAW 响应同时提供 **Raw + Corrected**，避免增加额外读取操作。

拟合阶段只使用 Raw；APPLY 后验证阶段使用 Corrected 与外部仪器比较。

至少包含：

```text
lv   当前 Level
pwm  actual logical PWM
or   OCO ADC Raw
bv   BL0942 Voltage Raw
bi   BL0942 Current Raw
bp   BL0942 Power Raw

oi   Corrected Output Current, mA
iv   Corrected Input Voltage, 0.1V
ii   Corrected Input Current, mA
ip   Corrected Input Active Power, 0.1W

vo   必要输出电压状态
age  BL0942 data age
vf   valid/fresh flags
flt  hardware fault flags
```

`vf`/`flt` 的逐 bit 定义仍需在联合审核清单下一轮 P0 中冻结，Codex 不得自行分配。

大量 UART/BL0942 诊断计数放入 `DIAG`，不随正式 11 点 RAW 重复发送。

---

## 9. Calibration MQTT Protocol V3——Operation Code 已冻结

外层中科/MQTT Envelope 不变，只升级 `SV=cal` 的 `DT`。

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

Operation 必须使用数字，不再发送 `"SET_POINT"` 等长字符串。

示例：

```json
{"v":3,"o":3,"s":123456,"q":8,"lv":100}
```

公共字段：

```text
v   u8   Protocol Version，固定3
o   u8   Operation Code
s   u32  Session ID
q   u32  Sequence
rc  u8   Result Code（响应）
st  u8   Wire State（响应）
```

CAP 等会话前操作是否省略 `s`、每个 Operation 的完整 Request/Response 必填表，由联合审核清单继续冻结。

---

## 10. Result Code——已冻结

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

幂等原则：

- 相同 `s + q + operation + parameter digest` 的精确重发：重放第一次响应，不重复执行副作用；
- 同一 `s + q` 但参数不同：拒绝，不能执行第二次；
- COMMIT 重试绝不能再次擦写 Flash；
- `DUPLICATE` 仅用于不能安全重放或检测到冲突重复的场景，正常可重放的重复包仍返回第一次原始结果。

---

## 11. Wire State——已冻结

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

不增加长期 `COMMITTED` 或 `ABORTED` 状态。

```text
IDLE -> BEGIN -> ACTIVE -> STAGE -> STAGED -> APPLY -> APPLIED
APPLIED -> COMMIT -> 仍为 APPLIED -> READ_INFO/READ_CHUNK -> RELEASE -> IDLE
ACTIVE/STAGED/APPLIED -> ABORT -> safe off + restore committed -> IDLE
```

最终 PASS/FAIL 由上位机判断，不进入 MCU Wire State。

---

## 12. STAGE / Flash Record 所有权——已冻结

**上位机不发送完整 Flash Record。**

STAGE 只发送 Target Calibration Payload：

```text
Product Identity/Fingerprint
+ Output Calibration
+ OCO Calibration
+ BL0942 Voltage GainQ24
+ BL0942 Current Calibration
+ BL0942 Power Calibration
```

Wire：

```text
STAGE -> payloadLen + payloadCrc + payload bytes/hex
```

固件只在 RAM 中 STAGE，APPLY 临时使用。

COMMIT 时由固件自己生成最终 Flash Record：

```text
Magic
Storage formatVersion
Generation
Payload Length
Product Identity
Calibration Payload
Record CRC
Commit Marker
```

固件拥有：

- A/B Slot 选择；
- Generation；
- Storage Header；
- Record CRC；
- Commit Marker；
- 掉电事务。

上位机不得设置 Generation/Commit Marker，也不得伪造“已提交 Record”。

新的 Storage `formatVersion` 与 Flash Record 精确字节布局尚未冻结，继续统一称 **Target Calibration Record**。

---

## 13. READ_INFO / READ_CHUNK——已冻结方向

正常 COMMIT 响应只返回摘要：

```text
generation + payloadLength + payloadCrc + valid flags
```

需要逐 Byte 核验时：

```text
READ_INFO
-> generation / totalPayloadLength / payloadCrc

READ_CHUNK(offset,length)
-> 分块返回已提交 Calibration Payload
```

上位机重组完整 Payload 后做 CRC 和逐 Byte Compare。

不通过 MQTT 回传整个 Flash Record Header/CommitMarker；Flash Record 本身由固件单元测试、A/B 掉电测试和存储读回测试验证。

---

## 14. JSON / TX 内存约束

当前固件 V2 已存在约束：

```text
ZK_JSON_BUF_SIZE = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

V3 目标：

- 普通 ACK `<256B`；
- RAW `<512B`；
- CAP `<768B`；
- READ_CHUNK `<768B`；
- 所有最终 TX JSON `<1536B`；
- 无 `TX Pool Exhausted`；
- CAP 不返回多 Profile Catalog；
- ACK 不重复返回完整 Status/Context。

---

## 15. Flash A/B

Persistent 12KB：

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页 2KiB，一个物理页一个事务 Owner。

Calibration 11 点采集期间不得逐点写 Flash，只在验证 PASS 后 COMMIT 一次。

---

## 16. BL0942 长期稳定性

必须新增/明确：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

检查 HAL TX/RX 返回值、ORE/FE/NE、gState/RxState/ErrorCode、RX 重挂、超时状态同步、Buffer/Index、Tick/计数溢出及 BL0942 芯片/供电域。

禁止用周期性 Reset 作为正式保活方案；异常后的有限恢复必须有错误分类和验证证据。

---

## 17. 不得误改的功能

- Boot/APP/OTA 地址与元数据契约；
- 普通 MQTT 业务；
- RTC；
- Plan 业务语义；
- CAT1 正常业务；
- 硬件过流、过温、短路等最后保护。

---

## 18. 当前仍未冻结的 P0

在交给 Codex 做 V3 跨端实现前，联合审核文档还必须继续冻结：

1. Target Calibration Payload 逐 Byte 布局；
2. Endian；
3. Payload CRC32 精确参数与覆盖范围；
4. `vf` bit 定义；
5. `flt` bit 定义；
6. 每个 Operation 完整 Request/Response 字段、必填/可选规则；
7. READ_CHUNK 最大 chunk 大小；
8. Target Calibration Record 最终 Storage `formatVersion` 和 Flash Header/CRC/Commit 精确布局；
9. 至少一组跨端 Golden Vector。

在这些条目冻结前，Codex 可以做 V2 现状审计和与协议无关的基础重构，但不得自行发明 Wire 字段或 Storage 字节格式。
