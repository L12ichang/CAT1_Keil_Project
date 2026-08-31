# CAT1 一体化电源——多功率 V3 真实校准固件实施基线

> 项目：`L12ichang/CAT1_Keil_Project`  
> 分支：`v3`  
> 文档定位：**多功率 V3 校准固件现行实施基线**  
> 适用功率：50W / 75W / 100W / 150W / 200W / 240W  
> 50W 仅作为示例型号，不作为架构边界。

---

## 1. 固定原则

### 1.1 11 点和 V3 244B 框架保持不变

正式校准点：

```text
Percent = 0,10,20,...,100
Level   = 0,20,40,...,200
PointCount = 11
Payload = 244B
```

不得恢复 21 点作为生产协议，也不得改变现有 V3 payload 总长度。

### 1.2 校准必须是“补准”，不是只记录误差

输出控制校准：

```text
Target Current
→ 候选 PWM
→ 外部标准仪表实测 Iout
→ 主动修正 PWM
→ 实际 Iout 达到 Target 容差
→ 保存 TargetCurrent ↔ CorrectPWM
```

采样校准：

```text
Device Raw + External Truth
→ 计算 Correction
→ APPLY
→ 重新读取 Corrected Value
→ Corrected Value 与 External Truth 比较
→ 合格才允许 COMMIT
```

外部标准仪表是真值源。未经校准的 OCO/BL0942 读数不得反过来作为输出校准真值。

---

## 2. 默认调光链路必须保持历史算法

无有效 Output Calibration、或者目标电流超出校准曲线覆盖范围时，必须执行历史默认公式：

```c
PWM = percent
    * SET_OUTCUR
    * PWM_USEFUL_RANGE
    / (HWMAX_OUTCUR * 100U);
```

其中：

```text
SET_OUTCUR   = 当前设备 100% 业务目标电流
HWMAX_OUTCUR = 当前设备可配置 PWM 满量程电流分母
```

**HWMAX_OUTCUR 不是 36V I-V 最大允许电流。**

36V/44V/56V 等 Product I-V 电流仅用于安全包络和本次 CalibrationSpan，不允许再作为默认 PWM 分母。

校准过程不得修改或覆盖持久化的 `SET_OUTCUR`、`HWMAX_OUTCUR`。

---

## 3. 多功率 Product Profile

各功率独立 Keil Target 共用同一套校准代码，只由 Product Profile 提供产品参数。

| 功率 | Default SET_OUTCUR | Default HWMAX_OUTCUR | Hardware Max |
|---:|---:|---:|---:|
| 50W | 893mA | 1680mA | 1680mA |
| 75W | 1360mA | 2150mA | 2150mA |
| 100W | 1780mA | 2800mA | 2800mA |
| 150W | 2700mA | 4500mA | 4500mA |
| 200W | 3600mA | 6000mA | 6000mA |
| 240W | 4300mA | 7000mA | 7000mA |

语义必须分开：

```text
Hardware Max = 产品硬件绝对能力
HWMAX_OUTCUR = 每机 Factory 可配置 PWM 满量程分母
I-V Limit    = 所选输出电压下规格允许最大输出电流
SET_OUTCUR   = 当前业务 100% 目标电流
```

---

## 4. 允许的校准电压与 CalibrationSpan

校准电压只能选择 Product I-V 表中的正式节点：

```text
25 / 29 / 32 / 36 / 40 / 44 / 48 / 52 / 56V
```

58V 不作为正式校准节点。

本次校准满量程：

```text
CalibrationSpan = min(
    当前设备 HWMAX_OUTCUR,
    所选 Vout 的 Product I-V Limit,
    Product Hardware Max,
    其他显式安全限制
)
```

固件在 `BEGIN` 时必须独立重算并核对 `CalibrationSpan`，不能只信任上位机。

---

## 5. 11 点真实输出校准

每个正式点目标电流：

```text
TargetCurrent = CalibrationSpan * Percent / 100
```

0% 点用于安全关断和零点门禁；10%～100% 每个点都必须以外部标准输出电流作为判定依据。

校准专用 `SET_POINT` 允许携带受保护的候选 `logicalPwm`。固件必须：

1. 校验会话、lease、seq、Product Profile；
2. 校验 selected Vout / CalibrationSpan；
3. 校验 logicalPwm 不超过 Product PWM Full Scale；
4. 执行现有硬件保护和安全关断逻辑；
5. 将实际 `logicalPwm` 回传给上位机；
6. 禁止普通业务路径绕过校准会话直接写 CCR。

候选 PWM 的数学预测只用于加速；只有外部仪表复测进入容差后，该点才是有效校准点。

---

## 6. 244B 数据语义

### 6.1 Output

```text
Target Output Current -> Correct logical PWM
```

运行时按 Target Current 查表，分段线性插值。

禁止将“第一次实际测到的 Current -> 当时 PWM”直接当作校准成功结果。

### 6.2 OCO

```text
OCO Raw -> External Reference Output Current
```

运行时分流：

```text
OCO Raw -> 默认保守换算 -> 硬件保护
   |
   +----> Calibration Correction -> Corrected Output Current -> MQTT/业务
```

保护链不得依赖校准后的展示值。

### 6.3 BL0942

```text
BL Voltage Raw -> External Reference Voltage
BL Current Raw -> External Reference Current
BL Power Raw   -> External Reference Active Power
```

V3 244B 当前电压通道保持既有 GainQ24 结构；若 HIL 证明 Gain-only 不满足规格，应升级 payload/protocol 版本，不得偷偷改变 244B 字段含义。

---

## 7. STAGE / APPLY / VERIFY / COMMIT

完整流程：

```text
BEGIN
→ 11 点真实校准采集
→ 生成 244B
→ STAGE
→ APPLY
→ 独立复测
→ Output / OCO / BL 全部满足容差
→ COMMIT
→ READBACK CRC + 244B byte compare
→ RELEASE
```

必须区分：

```text
Correction Generated != Calibration Passed
```

只有 APPLY 后重新读取实际物理输出和 Corrected Sampling 并满足验收条件，才叫校准成功。

---

## 8. 校准后正常运行

运行目标：

```text
TargetCurrent = SET_OUTCUR * Brightness / 100
```

决策：

```text
TargetCurrent 在有效校准曲线范围内
→ Current -> CorrectPWM 分段插值

无曲线 / 曲线无效 / Target 超覆盖范围
→ 回退历史 SET_OUTCUR / HWMAX_OUTCUR 默认公式
```

**禁止曲线外 extrapolation。**

修改 SET/HWMAX 后，不因为参数改变自动废弃校准曲线；只根据新的 TargetCurrent 是否落在曲线覆盖范围决定使用曲线还是 fallback。

---

## 9. 跨电压有效性

当前一维 `Current -> PWM` 曲线只有在“同一目标电流所需 PWM 对 Vout 基本不敏感”时才能跨不同输出电压共用。

必须通过 36V / 中间代表点 / 56V 等 HIL 验证：

```text
同一 Target Current
→ 不同 Vout
→ 外部实际电流仍满足验收容差
```

如果同一电流在不同 Vout 下所需 PWM 明显不同，则一维曲线不能靠增加点数解决，必须升级为电压补偿、多曲线或二维映射。

---

## 10. 文档口径

现行实施文档统一使用“多功率 V3 / 多型号 V3”。

带 `50W` 名称的文档只允许作为：

- 历史实机结果；
- 单一型号故障记录；
- 特定批次测试证据。

不得再作为整个 V3 架构、协议或算法的唯一实施基线。
