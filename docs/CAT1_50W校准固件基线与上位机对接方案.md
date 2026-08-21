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

> **重要：字段级冻结是对原实施方案的补充，不得用“协议已冻结”为理由删除原方案中关于正常输出、Fallback、Flash/OTA、BL0942 根因、测试、模块边界和回归保护的工程要求。**

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

默认值只用于**空白/无效 Config 的首次初始化**。一旦存在有效 User/Factory Config，重启、OTA、重新初始化 Calibration 服务均不得把用户已经修改的 SET_OUTCUR 或合法 HWMAX 强制恢复成默认值。

---

## 2. 参数归属

### Product Profile

MID、Model、Rated Power、Hardware Max、RS3、Profile Version/Fingerprint、固定硬件保护能力。

Product Profile 是编译期产品身份，普通远程命令不得修改。

### Factory Config

HWMAX、OP_PWM_OFFSET、生产信息等。

### User Config

SET_OUTCUR、温控、告警、平台、调光、计划等。

### SET_OUTCUR Wire兼容

外部普通属性仍兼容：

```json
{"Factory":{"SET_OUTCUR":893}}
```

但固件内部必须保存到 `User Config.SET_OUTCUR`。这是**Wire 兼容、内部 Ownership 重构**，不得因为外层字段仍叫 Factory 就继续把 SET_OUTCUR 当成 Factory Config。

`Factory.HWMAX_OUTCUR` 合法性改为：

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

校准完成后用户修改 SET_OUTCUR，只要满足 `0 < SET_OUTCUR <= HWMAX`，新的 Target Current 立即进入运行链，Calibration 保持有效。

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

这个 Legacy/Default Path 是成熟正常业务兜底链，必须保留。

### Calibration SET_POINT / 有有效Calibration

```text
logicalPwm
-> 不叠加OP_PWM_OFFSET
-> 保护仲裁
-> CCR
```

必须重构 `hw_tim1_pwm2`，禁止所有 PWM 路径无条件 `pwm + OP_PWM_OFFSET`。

### Calibration覆盖范围外的合法目标

如果目标电流在 Factory/User 规则上仍合法，但超出当前 Output Calibration 曲线的有效覆盖范围：

```text
Target Current
-> Calibration range miss
-> 回退 Legacy/Default PWM + OP_PWM_OFFSET
-> 记录 CAL_OUT_OF_RANGE 诊断
-> 保持硬件保护
```

不得因为曲线覆盖不足而直接输出 0，也不得做无约束外推。

---

## 5. 校准模型

### Output

11点：`logicalPwm <-> Reference Output Current`，运行时：

```text
Brightness × SET_OUTCUR
-> Target Current
-> 邻近两点分段线性反插值
-> u16 logicalPwm
```

禁止把插值结果先压回整数百分比再转换 PWM，否则会损失校准精度。

### OCO

11点：`OCO ADC Raw <-> Reference Output Current`。

```text
OCO Raw -> 保守默认换算 -> Protection
OCO Raw -> Correction -> Corrected Output Current -> MQTT/业务
```

硬件过流保护不得使用经过 Calibration 修正、Clamp 或 MQTT 格式化后的值。

### BL0942 Current / Power

11点 Raw→Reference：

```text
BL Current Raw <-> Reference Input Current
BL Power Raw   <-> Reference Active Power
```

### BL0942 Voltage

第一版固定 Q24 Gain-only：

```text
gainQ24_i = round(referenceVoltage01V * 2^24 / blVoltageRaw)
finalGainQ24 = median(valid samples)

correctedVoltage01V =
(uint64_t(rawVoltage) * finalGainQ24 + 2^23) >> 24
```

必须使用 `uint64_t` 中间量避免乘法溢出。

该模型是第一版量产候选，不等于已经通过整段输入电压实机验证。量产前必须使用多个真实输入电压点进行 HIL；如果 Gain-only 无法满足 Tolerance，应停止该项量产放行并重新评审模型，不允许 Codex 自行加 Offset 或分段表。

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

固件完成 Hex、CRC、Header、Profile、Flags、点位、单调性和范围检查后，只放 RAM。

**244B Payload 只保存运行 Correction 所需的数据；完整生产过程证据留在上位机 audit/report，不塞进 MCU Flash。**

---

## 7. RAW V3——固件字段冻结

RAW固定返回：

```text
lv  u16
pwm u16 OCO Raw前的实际逻辑PWM
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

RAW API 的 Raw 值必须是真正**未经过 Calibration Correction** 的数据，不能用 corrected value 伪装成 raw。

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

所有 Operation 的精确 Request/Response 必填字段以联合审核清单为唯一真源，固件不得自行增删或改名。

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

旧 Calibration `formatVersion=3` 不按V3解释；没有有效Format4时按“未校准但正常可运行”处理。

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

### Config / Runtime 也必须是真正A/B

不得采用“Main和Backup在一次保存中都擦写”的伪主备方式。统一原则：

```text
当前有效A
-> 构造完整新Snapshot
-> 只写B
-> 回读/CRC
-> Commit B
-> B成为新有效
```

下一次反向写A。

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

### 根因修复验收要求

任何 BL0942 修复都必须给出可验证的故障链：

```text
触发/自然冻结条件
-> UART/HAL/协议/芯片状态证据
-> 根因定位
-> 修复点
-> 故障恢复路径
-> 长稳验证
```

允许在**已分类异常之后**执行有限次数恢复动作，但：

- 不允许固定周期 reset；
- 不允许每次 timeout 就无限 reset；
- 恢复后必须重新得到有效帧才算成功；
- recovery 次数、失败次数和最后状态必须可通过 DIAG 观察。

Calibration 收到 `BL_FRESH=0` 时不得继续使用该点。

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

当前 4KiB TX cJSON Pool 是线性池约束，不能仅以最终 JSON 字符串长度判断安全；验收必须同时记录最终 JSON 长度和 TX Pool 峰值/耗尽日志。

---

## 15. 固件实施顺序

1. **先保存代码基线并新建实现分支**，禁止直接在 `main` 大范围开发；
2. 冻结/实现50W Product Profile：1680/1400/893/RS3=120；
3. Product / Factory / User Ownership 拆分；
4. Config / Calibration / Runtime 物理 A/B；
5. 完成旧 Config/User 数据读取与必要迁移；
6. 先恢复“无 Calibration 也能正确输出”的正常链；
7. Raw/Calibrated PWM硬件出口分离；
8. Target Payload显式 codec / CRC / Golden Vector；
9. V3 Session/Result/State/14 Operations；
10. RAW `vf/flt`；
11. Output Calibration；
12. OCO Calibration与Protection分流；
13. BL0942 Freshness与冻结根因修复；
14. BL U/I/P Calibration；
15. Format4 Calibration A/B；
16. JSON长度/Pool测试；
17. Windows Keil正式Build；
18. 与上位机真实HIL；
19. OTA/掉电/长稳回归。

每一步必须保持可编译、可回滚，禁止一次性同时重写 PWM、Flash、MQTT、BL0942 后再整体找问题。

---

## 16. Legacy / OTA 数据策略

这里必须区分两类 Legacy：

### 旧 Calibration Record

旧 `formatVersion=3` Calibration **不直接迁移为 V3 Calibration**。如果不存在合法 Format4 Calibration：

```text
Calibration = uncalibrated
-> 正常 Legacy/Default PWM + OP_PWM_OFFSET 可运行
```

### 旧 Config / User / Plan 等业务数据

有效旧业务配置不能因为 V3 升级被无条件清空。升级逻辑必须优先保护：

- SET_OUTCUR；
- 合法 HWMAX；
- 平台/MQTT 参数；
- 告警/温控；
- 计划任务；
- 其他已确认必须保留的用户配置。

迁移必须：

- 幂等；
- 仅在新 A/B Config 不存在时执行；
- 合法值保留，非法值回到当前 Product Profile 默认；
- 迁移成功后不在每次启动重复写 Flash。

**OTA 后不得把用户已配置 SET_OUTCUR 无条件恢复成893mA。**

---

## 17. 允许修改与禁止扩大范围

本轮允许重点修改：

- `sys_product_profile.*`
- `factory_user_data.*`
- `sys_data.*`
- `flash_address_assignment.*`
- `hw_flash.*`（仅A/B需要时）
- `sys_pwm.*`
- `hw_tim1_pwm2.*`
- `sys_calibration_service.*`
- `sys_calibration_storage.*`
- `sys_calibration_flash.*`
- `sys_calibration_driver_protocol.*`
- `sys_calibration_mqtt.*`
- `sys_calibration_snapshot.*`
- `sys_calibration_curve.*`
- `sys_Vo_Io.*`
- `sys_bl0942.*`
- `hw_uart2.*`

迁移需要时可以修改：

- `zk_property.*`
- `zk_work_plan.*`

修改其他模块必须在提交说明中明确回答：

```text
为什么该文件与V3 Calibration闭环直接相关？
如果不修改它，哪一条冻结验收无法实现？
```

禁止借本任务顺便进行无关全局重构。

---

## 18. 不得误改的现有业务

### Boot / APP / OTA

保持：

```text
Bootloader  0x08000000~0x08005000
Persistent  0x08005000~0x08008000
APP         0x08008000~0x08024000
OTA Backup  0x08024000~0x08040000
```

不得改变 APP vector、Boot 校验 APP 机制以及既有 metadata contract。

### 普通 MQTT

除 `SV=cal` V3 和必要 Factory/User Ownership 兼容外，不重构普通登录、在线、heartbeat、property、alarm、report、inspection、OTA 等业务协议。

### RTC / Plan

允许存储层迁移，但不改变既有 RTC 依赖和计划执行语义。

### CAT1 / USART2

正常 CAT1 业务必须保留；当前 `_4G_CAT_1` 下 USART2 用于 BL0942，不得因为旧命名恢复废弃 485 业务。

### Hard Protection

上位机负责校准流程安全，但 MCU 仍保留过流、过温、短路等最后生存保护。

---

## 19. 固件验收矩阵

### 正常运行

- [ ] 空白设备初始化为 HWMAX=1400、SET=893；
- [ ] 有效 Config 重启后不被默认值覆盖；
- [ ] 无 Calibration 可正常输出；
- [ ] 无 Calibration 路径保留 OP_PWM_OFFSET；
- [ ] SET 修改立即生效并持久化；
- [ ] SET > HWMAX 拒绝；
- [ ] HWMAX >1680拒绝；
- [ ] OTA 后 SET/HWMAX/业务配置保持；
- [ ] Calibration 范围外合法目标走 Default fallback，不输出0。

### 11点 / Output

- [ ] 0/20/.../200 共11点；
- [ ] SET_POINT 不应用旧 Calibration；
- [ ] SET_POINT 不叠加 OP_PWM_OFFSET；
- [ ] actual logical PWM 回执正确；
- [ ] 有 Calibration 使用 u16 分段反插值；
- [ ] 改 SET 后 Calibration 不失效；
- [ ] 改运行 Vo 后 Calibration 不失效。

### OCO

- [ ] Raw 与 Corrected 分离；
- [ ] Protection 不使用 Corrected MQTT 值；
- [ ] OCO Calibration 能改善业务上报精度。

### BL0942

- [ ] Raw 真正未校准；
- [ ] Corrected U/I/P 进入业务；
- [ ] age/fresh 正确；
- [ ] stale 不伪装 realtime；
- [ ] Calibration 不使用 stale；
- [ ] ORE/FE/NE/timeout 有分类计数；
- [ ] 有自然/故障注入恢复证据；
- [ ] 长稳不依赖周期 reset。

### Flash

- [ ] Config A/B 关键写入点断电至少保留一份；
- [ ] Calibration A/B 关键写入点断电至少保留一份；
- [ ] Runtime A/B 关键写入点断电至少保留一份；
- [ ] CommitWord 最后写；
- [ ] 11点采样期间不逐点擦写 Flash；
- [ ] 不再使用 APP 内旧 programmer writable region 作为新持久化区。

### Protocol / Memory

- [ ] 14个Operation逐字段一致；
- [ ] 244B Payload / 272B Record；
- [ ] Golden Vector通过；
- [ ] RAW vf/flt一致；
- [ ] READ_CHUNK=128；
- [ ] ACK/RAW/CAP/Chunk满足JSON预算；
- [ ] 无 `TX Pool Exhausted`。

### 回归

- [ ] Windows Keil官方工程编译通过；
- [ ] Boot启动正常；
- [ ] OTA正常；
- [ ] 普通MQTT正常；
- [ ] RTC/Plan正常；
- [ ] 告警/温控/调光正常；
- [ ] CAT1正常业务无回归。

---

## 20. 完成定义

固件完成不等于“能跑一次校准”。必须同时满足：

```text
V2真实代码升级到V3
+ 正常无校准业务不回归
+ 11点Output/OCO/BL U/I/P闭环
+ V3字段合同一致
+ Flash A/B掉电安全
+ BL0942长期稳定有证据
+ JSON/TX有余量
+ OTA/普通业务回归通过
+ 与多功率通用上位机完成50W HIL
```

**协议字段已冻结，但原实施方案中的工程约束、Fallback、迁移、安全、诊断和测试要求同样属于正式规范，不得再次被精简删除。**