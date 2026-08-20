# CAT1 50W 一体化电源——校准固件修改实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 基线分支：`done/cat1-product-profile-cal-context-20260817`  
> 文档定位：**固件侧唯一实施基线**  
> 配套上位机文档：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`  
> 最终联合审核文档：本仓库 `docs/CAT1_50W固件与上位机联合审核清单.md`

---

## 1. 本轮固件修改目标

本轮目标不是重写整个一体化电源，而是把现有 50W 固件整理成一套 **正常业务不依赖校准、可执行 11 点输出校准、可执行 OCO 采样校准、可执行 BL0942 输入 U/I/P 校准、可掉电安全保存校准结果、可与上位机稳定对接** 的固件。

必须同步修复 BL0942 长期运行后停止读取/数据冻结问题，但禁止用周期性状态机 Reset 作为正式根因解决方案。

本轮允许修改的核心范围：

- Product Profile / Factory Config / User Config 参数归属；
- 正常 PWM 输出链；
- 11 点校准控制链；
- OCO Raw/Corrected 分流；
- BL0942 Raw、Freshness 与校准；
- Calibration MQTT Protocol V3；
- Calibration / Config / Runtime Flash A/B；
- Legacy 参数迁移；
- 与以上功能直接相关的测试与诊断。

本轮禁止无理由修改 Boot、OTA 大分区、普通 MQTT 业务语义、RTC、计划任务业务逻辑及其他已工作的功能。

---

## 2. 50W 固件冻结参数

| 参数 | 冻结值 | 归属 |
|---|---:|---|
| 额定功率 | 50W | Product Profile |
| Hardware Max | **1680mA** | Product Profile |
| 默认 HWMAX | **1400mA** | Factory Config 默认值 |
| 默认 SET_OUTCUR | **893mA** | User Config 默认值 |
| RS3 | **120mΩ** | Product Profile |
| 校准点 | **11 点** | Calibration |
| Level | `0,20,...,200` | Calibration |
| 对应百分比 | `0%,10%,...,100%` | Calibration |

固定关系：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

定义：

- `SET_OUTCUR`：用户当前 100% 亮度目标电流；
- `HWMAX`：工厂允许用户调整 SET_OUTCUR 的最大范围；
- `Hardware Max`：硬件真实最大输出/安全能力上限。

Factory 命令第一版不做身份认证，但固件仍必须做参数合法性检查，例如 `HWMAX > 1680mA` 必须拒绝。

---

## 3. 后续其他瓦数如何扩展

50W 先做成标准模板。后续 75W/100W/... 只更换产品参数，不复制校准算法。

建议最终结构：

```text
sys_product_profile.*
    └─ 编译期只选择一个 Product Profile

50W Profile
├─ MID
├─ Model
├─ Rated Power
├─ Hardware Max
├─ Default HWMAX
├─ Default SET_OUTCUR
├─ RS3
└─ 产品固定硬件保护参数
```

后续切瓦数原则上不修改：

- Calibration MQTT V3；
- 11 点流程；
- Output Calibration 算法；
- OCO Calibration 算法；
- BL0942 U/I/P Calibration 算法；
- Flash A/B；
- 上位机状态机。

50W 二进制最终不得继续携带 75/100/150/200/240W 的 Profile Catalog、字符串和参数表。

---

## 4. 参数职责必须拆清

### 4.1 Product Profile

只放产品固定参数：

- MID / Model / Rated Power；
- Hardware Max；
- RS3；
- 固定硬件保护边界；
- Profile Version / Fingerprint；
- 默认 HWMAX、默认 SET_OUTCUR 只作为首次初始化默认值。

Product Profile 不允许普通运行配置覆盖。

### 4.2 Factory Config

放工厂可调、普通用户不可调参数：

- HWMAX；
- SN/生产信息；
- 必要每机修调参数；
- `OP_PWM_OFFSET`（保留用于无校准旧链）。

### 4.3 User Config

放客户运行参数：

- SET_OUTCUR；
- 用户可调温度参数；
- 告警；
- 平台/MQTT；
- 上报周期；
- 调光设置；
- 计划任务。

代码默认值只用于空白/无效 Config 初始化。有效 Config 存在后，重启和 OTA 不得重新覆盖 SET_OUTCUR。

---

## 5. 删除输出电压绑定

`BOUND_OUTPUT_VOLTAGE_01V` 不再是正常业务配置，也不再参与：

- SET_OUTCUR 合法性授权；
- Calibration Context；
- Calibration 是否可用；
- 非零 PWM 输出许可。

实际 `Vo` 是采样/报告状态量。

校准上位机可以选择 36V、56V 或其他工况，固件不判断“哪个电压才允许校准”。如果 Calibration Record 保存参考电压，只作为 Metadata，不参与运行授权。

---

## 6. 正常输出链

### 6.1 无 Output Calibration

必须保留成熟旧链：

```text
SET_OUTCUR × Brightness
        ↓
默认 PWM 模型
        ↓
OP_PWM_OFFSET 光耦补偿
        ↓
硬件保护仲裁
        ↓
PWM
```

无校准不能导致 PWM=0。

### 6.2 有有效 Output Calibration

```text
SET_OUTCUR × Brightness
        ↓
Target Current
        ↓
11 点 Output Calibration 反插值
        ↓
直接得到高精度 u16 PWM
        ↓
硬件保护仲裁
        ↓
PWM
```

有有效 Output Calibration 时不再叠加 `OP_PWM_OFFSET`，防止重复补偿。

### 6.3 Calibration 不绑定 SET_OUTCUR

校准后客户修改 SET_OUTCUR，只要 `SET_OUTCUR <= HWMAX`，立即生效，Calibration 不失效。

Calibration = Correction，不是 Permission。

---

## 7. 11 点 Calibration 采点基线

正式点固定：

```text
Level: 0,20,40,60,80,100,120,140,160,180,200
比例 : 0,10,20,30,40,50,60,70,80,90,100%
```

### SET_POINT 行为

Calibration 模式下 `SET_POINT` 必须输出已知原始逻辑 PWM 档位：

```text
Level 20  -> 10%
Level 100 -> 50%
Level 200 -> 100%
```

采点时：

- 不应用已存在 Output Calibration；
- 不叠加 OP_PWM_OFFSET；
- 保留真实硬件底线保护。

原因：校准要测量真正的 `PWM -> 实际输出` 传递关系，不能把旧补偿算法混进新曲线。

---

## 8. 三类 Calibration

### 8.1 Output Calibration

输入：

```text
Actual PWM <-> 外部仪器 Reference Output Current
```

保存 11 点关系，运行时根据 `Target Current` 做分段线性反插值，直接输出 u16 PWM，禁止先四舍五入为整数百分比。

校准表范围外但目标仍合法时：

```text
回退无校准默认 PWM + OP_PWM_OFFSET
+ 记录 CAL_OUT_OF_RANGE 诊断
```

不能 PWM=0。

### 8.2 OCO Calibration

推荐使用：

```text
OCO ADC Raw <-> Reference Output Current
```

运行链必须拆开：

```text
OCO Raw ──> 保守默认换算 ──> Protection
   │
   └──────> OCO Calibration ──> Corrected Current ──> MQTT
```

禁止把 Calibration 修正后的上报值重新用于硬件过流保护。

### 8.3 BL0942 Calibration

分别支持：

- Input Voltage；
- Input Current；
- Active Power。

Current / Power 使用 11 点负载变化形成 Raw→Reference 曲线。

Voltage 第一版不强行做 11 段量程曲线；在相同市电条件下收集多个有效点，由上位机生成稳定 Voltage Gain/Correction 参数。

无 BL0942 Calibration 时继续使用原默认换算。

---

## 9. BL0942 长期冻结修复

本轮 BL0942 Calibration 前提是 Raw 数据稳定可信。

必须检查并修复：

- USART2 ORE；
- FE / NE；
- `HAL_UART_Transmit_IT` / `Receive_IT` 返回值；
- HAL `gState / RxState / ErrorCode`；
- RX 重挂；
- TX/RX 回调与上层状态失步；
- timeout 后状态同步；
- Buffer/Index；
- 长运行 Tick/计数溢出；
- BL0942 芯片无响应；
- BL0942 VDD/供电域问题。

必须新增/明确：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

旧值不得永久当成实时值上报。

**禁止正式方案：周期性或连续 Reset BL0942 状态机。**

允许异常后的有限恢复，但必须有明确错误分类、计数、状态快照和恢复成功判断。

---

## 10. Flash 物理布局

保持大分区不变：

```text
0x08000000~0x08005000 Bootloader
0x08005000~0x08008000 Persistent 12KB
0x08008000~0x08024000 APP
0x08024000~0x08040000 OTA Backup
```

Persistent 12KB 按 2KB 物理擦除页：

| Page | 地址 | Owner |
|---|---|---|
| 0 | `0x08005000~0x08005800` | Config A |
| 1 | `0x08005800~0x08006000` | Config B |
| 2 | `0x08006000~0x08006800` | Calibration A |
| 3 | `0x08006800~0x08007000` | Calibration B |
| 4 | `0x08007000~0x08007800` | Runtime A |
| 5 | `0x08007800~0x08008000` | Runtime B |

原则：**一个物理擦除页只有一个事务 Owner。**

### A/B 统一事务

```text
读当前有效页
→ 构造完整新 Record
→ 擦非活动页
→ 写 Record
→ 回读/CRC
→ 最后 Commit
→ 新 Generation 生效
```

任何阶段掉电都必须至少保留一个旧有效页。

Calibration 11 点采样不逐点写 Flash，只在上位机验证后 COMMIT 一次。

---

## 11. Calibration Record V2 逻辑结构

最终 Flash 只保存运行真正需要的修正数据，不保存全部产线证据。

建议结构：

```text
Header
├─ Magic
├─ Format Version
├─ Generation
├─ Product Profile ID
├─ Profile Version/Fingerprint
├─ Valid Flags
└─ Metadata

Output Calibration
└─ 11 × {PWM, Reference Output Current}

OCO Calibration
└─ 11 × {OCO Raw, Reference Output Current}

BL0942 Current Calibration
└─ 11 × {Raw Current, Reference Current}

BL0942 Power Calibration
└─ 11 × {Raw Power, Reference Power}

BL0942 Voltage Calibration
└─ Gain/Correction Parameters

Footer
├─ CRC32
└─ Commit Marker
```

最终字节级结构由联合协议文档冻结后实现。

---

## 12. Calibration MQTT Protocol V3——固件侧要求

### 12.1 为什么必须瘦身

当前固件：

- `ZK_JSON_BUF_SIZE = 2048B`；
- `ZK_CJSON_TX_POOL_SIZE = 4096B`；
- TX cJSON 使用线性静态池，单报文构建中节点删除不回收；
- 代码已经存在 `TX Pool Exhausted` 诊断。

因此 V3 必须减少字段数、重复状态和大对象。

### 12.2 只修改 `SV=cal` 的 `DT`

外层中科/MQTT Envelope 保持兼容：

```json
{"SN":"...","TM":"...","SV":"cal","ID":"...","CT":"R","DT":{}}
```

普通 `prop/ctrl/rept/alam/ota/plan` 不因本次校准协议瘦身而改变。

### 12.3 V3 紧凑字段

建议公共字段：

| 字段 | 含义 |
|---|---|
| `v` | Protocol Version |
| `o` | Operation Code |
| `s` | Session ID |
| `q` | Sequence |
| `rc` | Result Code |
| `st` | Calibration State |
| `lv` | Level |
| `pwm` | Actual PWM |
| `gen` | Generation |
| `len` | Record Length |
| `crc` | CRC32 |

Operation 使用数字 Code，不再重复长字符串。

建议：

```text
0 CAP
1 BEGIN
2 HEARTBEAT
3 SET_POINT
4 RAW
5 STAGE
6 APPLY
7 SET_VERIFY
8 COMMIT
9 READ_INFO
10 READ_CHUNK
11 ABORT
12 RELEASE
13 DIAG
```

具体 Code 以联合审核文档为唯一真源。

### 12.4 Operation-specific Response

禁止所有 ACK 都返回完整 status/context。

例如 SET_POINT 只需：

```json
{"v":3,"o":3,"q":12,"rc":0,"lv":100,"pwm":500}
```

RAW 只返回校准算法真正需要的 Raw/Freshness。

大量 BL0942 诊断计数放入 DIAG，不随 11 个正式点重复发送。

### 12.5 CAP 不再发多 Profile Catalog

50W 固件只返回当前 50W：

```text
Profile ID / Fingerprint
Hardware Max = 1680
HWMAX = 1400 默认或当前值
SET_OUTCUR = 当前值
Point Count = 11
Level Step = 20
Calibration Feature Bitmask
Generation
```

删除 `profilesCsv` 和其他瓦数列表。

### 12.6 RAW 精简

正式校准 RAW 原则上只包含：

- level / actual pwm；
- OCO Raw；
- BL0942 Voltage Raw；
- BL0942 Current Raw；
- BL0942 Power Raw；
- 必要 Output Voltage；
- Freshness/Data Age/Valid Flags；
- 必要硬件 fault flags。

不默认返回 frameErrors、timeoutErrors、UART errors、NTC Raw、Leak Raw、Frequency、CF Count 等诊断数据。

### 12.7 大数据回读

正常 COMMIT/READ_INFO 只回：

```text
Generation + Length + CRC + Valid Flags
```

需要字节级完整核验时用 `READ_CHUNK` 分块，不把整个 Calibration Record Hex 一次塞入 TX。

STAGE 第一版可继续使用 Hex，但必须满足 RX 2048B 预算；若最终 Record 超出预算，再升级 STAGE_CHUNK，不优先引入 Base64。

### 12.8 报文预算

建议验收门槛：

- 普通 ACK 最终 JSON：目标 `<256B`；
- RAW：目标 `<512B`；
- CAP：目标 `<768B`；
- READ_CHUNK：目标 `<768B`；
- 所有设备 TX 最终 JSON 必须 `<1536B`；
- 所有 V3 操作必须实测无 `TX Pool Exhausted`；
- cJSON TX pool 应保留明显余量，不以 4096B 临界通过为合格。

---

## 13. 固件目标状态机

```text
IDLE
  │ BEGIN
  ▼
ACTIVE
  │ SET_POINT / RAW / HEARTBEAT
  │
  │ STAGE
  ▼
STAGED
  │ APPLY
  ▼
APPLIED
  │ 上位机独立验证
  │
  ├─ ABORT ──> ABORTED/IDLE
  │
  └─ COMMIT
       ▼
     COMMITTED
       │ READ_INFO / READ_CHUNK
       │ RELEASE
       ▼
      IDLE
```

错误/租约超时必须安全退出校准控制，但不能让正常无校准运行永久被禁止。

---

## 14. Legacy OTA 迁移

新固件首次启动：

1. 优先读新 Config A/B；
2. 若无新格式，读取旧 sys_data / Property / Plan；
3. 提取有效 SET_OUTCUR、合法 HWMAX、温度、平台、告警、计划；
4. 构造新 Config Snapshot；
5. 写入新 A/B；
6. 缺失新 Calibration = 正常“未校准”状态；
7. 使用旧无校准 PWM + OP_PWM_OFFSET 正常输出。

禁止 OTA 后把用户已修改 SET_OUTCUR 恢复成 893mA。

---

## 15. 当前固件明确需要纠正的逻辑

- 删除“没有 Calibration / 电压不匹配 => PWM=0”；
- 删除 Calibration Context 对运行 SET_OUTCUR 和运行输出电压的绑定；
- `calibratedMaxCurrent` 不再作为运行授权上限；
- SET_OUTCUR 成为正常 Target Current 核心；
- OCO Raw 保护链与 Corrected MQTT 链分离；
- BL0942 Correction 真正进入业务 U/I/P；
- BL0942 增加 Freshness；
- `sys_data` / Property 等主备同时写改成真正 A/B；
- Calibration MQTT 删除多 Profile Catalog 与重复大 Status；
- RAW Schema 与上位机统一到 V3；
- 旧 `0x0801E000~0x08020000` 编程器写区必须审计并退出新持久化设计。

---

## 16. 主要修改文件

重点：

- `sys_product_profile.*`
- `factory_user_data.*`
- `sys_data.*`
- `flash_address_assignment.*`
- `hw_flash.*`
- `sys_pwm.*`
- `sys_calibration_service.*`
- `sys_calibration_storage.*`
- `sys_calibration_flash.*`
- `sys_calibration_driver_protocol.*`
- `sys_calibration_mqtt.*`
- `sys_calibration_snapshot.*`
- `sys_Vo_Io.*`
- `sys_bl0942.*`
- `hw_uart2.*`

根据迁移需要：

- `zk_property.*`
- `zk_work_plan.*`

新增或修改其他模块必须说明与本次校准闭环的直接关系。

---

## 17. 推荐固件实施顺序

1. 冻结 50W Product Profile：1680 / 1400 / 893 / RS3=120；
2. 建立 Config / Calibration / Runtime A/B；
3. 完成 Legacy 读取和迁移；
4. 恢复正确无 Calibration 输出链；
5. SET_OUTCUR → Target Current；
6. Calibration SET_POINT 原始逻辑 PWM；
7. V3 RAW Snapshot；
8. Output Calibration；
9. OCO Calibration Raw/Corrected 分离；
10. BL0942 根因修复和 Freshness；
11. BL0942 U/I/P Calibration；
12. Calibration MQTT V3 紧凑协议；
13. 与上位机联合 HIL；
14. OTA/掉电/长稳回归。

---

## 18. 必须规避修改的其他功能

### Bootloader / APP / OTA

禁止改变：

- Boot/APP/OTA 地址；
- APP `0x08008000` 起始；
- checksum/length/device type 元数据契约；
- Boot 校验 APP 机制。

### 普通 MQTT

除 `SV=cal` 和必要 Factory 开发命令外，不改普通平台协议语义。

### 计划任务 / RTC

只允许存储迁移，不改原计划语义、RTC 依赖和时间执行逻辑。

### 4G/CAT1

不得误删正常 CAT1 链。USART2 `_4G_CAT_1` 当前用于 BL0942，不恢复旧废弃 485 业务。

### 硬件保护

上位机负责校准业务安全，MCU 仍保留过流、过温、短路等硬件最后保护。

---

## 19. 固件验收重点

- 空白设备自动得到 HWMAX=1400、SET=893；
- 无 Calibration 可正常输出且保留 OP_PWM_OFFSET；
- SET_OUTCUR 修改/重启/OTA 保持；
- SET > HWMAX 拒绝；HWMAX >1680 拒绝；
- 11 点采点不叠加旧 Calibration 和 OP_PWM_OFFSET；
- Calibration 后改 SET 不失效；
- 运行 Vo 改变不使 Calibration 失效；
- Output Calibration 使用高精度 PWM；
- OCO Protection 使用 Raw/保守链；
- BL0942 U/I/P 可修正，Freshness 正确；
- BL0942 长时间运行无永久冻结，且不靠周期 Reset；
- Config/Calibration/Runtime 掉电安全 A/B；
- 校准 V3 所有 TX 无 4KiB pool 耗尽，最终 JSON 留足 2KiB buffer 余量；
- Legacy OTA 参数不丢；
- Boot/OTA/普通业务无回归。

---

## 20. 本文档与其他两份文档的关系

本文只回答：**固件应该怎么改。**

上位机如何改、校准前后算法如何执行、UI/仪器/审计如何调整，以 `tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md` 为准。

最终开发完成后，不以“某一边单测通过”为结论，必须逐条通过 `CAT1_50W固件与上位机联合审核清单.md`，确认协议、单位、状态机、Calibration Record、JSON 大小、算法和回归边界全部一致后才允许进入量产验证。
