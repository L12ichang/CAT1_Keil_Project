# 一体化电源全参数计量校准与 21 点 PWM 调光校准——Codex 执行文档

> 文档状态：实施合同 / 可直接交给 Codex 分阶段执行  
> 适用工程：`D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project`  
> 基线日期：2026-07-15  
> 当前硬件状态：实机未连接电脑；只能执行离线审计、编码、构建、仿真、Mock、静态检查和历史产物检查  
> 目标：一次性解决“PWM 输出不准”和“输入/输出电参数上报不准”两个不同问题，同时保持原有功能、256 KB Flash 分区和 OTA 兼容性

## 0. Codex 执行总则

本文件不是把两个校准算法简单串起来，而是规定一个工站会话中的两条独立校准链：

```text
标准计量仪器/可编程交流源/电子负载
                │
                ├─ A. 计量链校准
                │    原始采样 → 零点/增益/必要的分段补偿 → U/I/P/PF/E
                │
                └─ B. PWM 输出链校准
                     目标输出电流 → 21 点曲线 → 逻辑 PWM → LED 输出
```

固定原则：

1. 先校准计量链，再校准 PWM 输出链，最后做联合验证。
2. PWM 只用于产生测试工况，不能用于修正上报数据；计量系数也不能改变 PWM 曲线。
3. 标准仪器是所有精度判定的唯一基准。MCU 自测值只能用于诊断、保护和稳定性辅助。
4. 输入参数的物理边界固定为产品交流输入端子，包含 CX/EMI 支路的实际输入电流；输出参数边界固定为 LED 输出端子。
5. 功率因数只定义在交流输入侧，直流输出侧不虚构“输出功率因数”；输入、输出累计能量都要实现，但必须分别命名为 `inputEnergy` 和 `outputEnergy`，不得混用。
6. 0.5% 是验收门限，不是算法可以无条件保证的承诺。若采样噪声、量化、器件温漂、采样位置或协议分辨率不支持，必须判定能力不满足并提出硬件/协议整改，禁止用经验常数掩盖。
7. 所有正常功能、保护、看门狗、OTA、离线计划、渐变和原有 MQTT 兼容行为必须回归通过。
8. 当前无实机连接时，禁止执行 J-Link 擦写、复位、在线 MQTT 非零输出和串口联调；硬件相关用例必须标记 `NOT_RUN_HARDWARE`，不能伪报通过。
9. 保留工作区已有修改；不得恢复或覆盖已删除的 `MDK-ARM-8008000/out/boot_app_merged.bin`。

## 1. 要解决的问题和最终结果

### 1.1 当前问题

需要同时解决以下问题：

- 输入电压、电流、有功功率、功率因数和累计有功电能存在约 3% 误差。
- 输出电压、电流、功率和累计输出能量缺少正式的零点、增益、时间基准和温漂校准。
- 低于 40% PWM 时误差显著增大，可能同时包含 PWM 驱动非线性、传感器小信号误差、固定偏置、采样滤波和协议量化问题。
- 当前 `CX` 参与输入电流补偿，但固定使用 50 Hz，且未冻结电流采样电阻位于 CX 前还是 CX 后，可能出现物理边界、相位符号和 PF 计算不一致。
- `program` 与 `Release-MinSize` 目标使用不同的 CX 算法，且仅浮点分支存在 MID 2/3 的 `×0.97`、`-7 mA` 经验补丁，导致不同构建的计量结果不一致。
- 当前 PF 先由未做 CX 边界修正的电流计算，随后才生成 `Z_ac_current`；上报电流与 PF 可能不属于同一物理边界。
- BL0942 的频率寄存器已读取但未真正换算使用；`I_RMSOS`、`WA_CREEP`、`GAIN_CR` 等能力没有形成受控标定流程。
- 输入能量同时存在 `CF_CNT` 路径和按分钟积分功率路径，来源和累计口径不统一。
- 输出 ADC 固定按 3300 mV 换算，`vdda` 没有参与；输出电流仅依赖整数毫欧级 `OUTPUT_CUR_SENSOR`，不足以承担 0.5% 的细调。
- 旧协议把输入/输出功率和能量舍入到整数 W/Wh，低功率时仅量化误差就可能超过 0.5%。
- 现有 21 点 PWM 工站只校准输出电流控制，不校准上报计量值。

### 1.2 交付后的结果

完成后必须得到：

1. 一套统一工站任务、安全锁和校准程序；MQTT 只承担正式协议已定义的 PWM 校准动作，计量 raw/系数走本地工厂调试通道。
2. 两套相互独立、可单独判定有效性的校准数据：
   - `meter calibration`：输入/输出计量系数、CX 拓扑参数和能量系数；
   - `PWM curve`：0%、5%……100% 共 21 点逻辑 PWM 曲线。
3. MCU 内统一的高精度计量快照，包含原始寄存器/ADC、校准后高分辨率值、样本时间和故障状态。
4. A/B 原子存储、CRC32、profile 绑定、掉电回滚和分区边界断言。
5. MQTT 正常上报严格保持正式协议的 `EleInfo` 字段、数组顺序、单位、舍入方式和周期，不增加 `EleInfoV2` 或任何私有字段；0.5% 通过标准仪器和本地工站记录验收。
6. 无硬件可运行的完整离线测试工具；接入硬件后可复用同一工具执行低功率、全功率、持久化、OTA 和 72 小时稳定性测试。
7. 每台设备可追溯的 JSONL、CSV、原始仪表记录、系数、曲线、CRC、环境和验收报告。

## 2. 当前源码审计结论

### 2.1 256 KB Flash 与链接边界冻结

只允许使用 `0x08000000–0x0803FFFF`：

| 区域 | 地址 | 大小 | 用途 |
|---|---:|---:|---|
| Boot | `0x08000000–0x08004FFF` | 20 KB | 现有 Bootloader |
| 主参数区 | `0x08005000–0x080067FF` | 6 KB | 系统、属性、计划 |
| 备参数区 | `0x08006800–0x08007FFF` | 6 KB | 对应备份 |
| APP | `0x08008000–0x08023FFF` | 112 KB | Keil 应用 |
| OTA | `0x08024000–0x0803FFFF` | 112 KB | OTA 备份镜像 |

校准槽继续固定为：

- A：`0x08005C00–0x08005CFF`；
- B：`0x08007400–0x080074FF`；
- 每槽 256 字节，位于属性页 `+0x400`；
- 当前记录大小为 80 字节，剩余 176 字节可用于 v2 计量字段；
- 整页读改写时必须验证同页属性、运行统计和 OTA 报告未变化；
- 不改变 APP/OTA 起止地址，不扩大 Keil `0x1C000` 链接上限，不使用 APP 尾部保存参数。

当前 `MDK-ARM-8008000/out/cat1.map` 显示：

- `LR_IROM1` 基址 `0x08008000`，最大 `0x1C000`；
- Total ROM 为 79,972 字节，距 112 KB 上限约 34,716 字节；
- RW 区约 34,600 字节，距 48 KB RAM 上限约 14,552 字节；
- 新实现仍须以每次构建的新 map 为准，不能把本数值当永久余量。

### 2.2 当前输入计量链

`Core/Src/sys_bl0942.c` 当前使用固定名义参数：

- `RL = 10 mΩ`、`Vref = 1.218 V`、`R1 = 0.75`、`R2 = 1720`；
- `I_RMS`、`V_RMS`、`WATT` 和 `CF_CNT` 直接通过名义公式换算；
- `ac_power_S = ac_current * ac_voltage_8209 / 100`；
- `ac_pf = ac_powerpa * 100 / ac_power_S`，并封顶 99；
- `CX` 位于 `factory_user_buff + 0x1E`，单位 0.01 µF，默认 68 表示 0.68 µF；
- 固定点和浮点 CX 分支都使用 50 Hz，而不是 BL0942 实测频率；
- `program` 定义 `BL0942_USE_FLOAT_XCAP_COMPENSATION=1`，`Release-MinSize` 定义为 0；
- MID 2/3 的 `ac_powerpa × 97%` 和 `Z_ac_current - 7 mA` 只在浮点分支存在；
- `CF_CNT` 能量与按分钟积分 `ac_powerpa` 的能量并行存在。

结论：现状属于“名义参数换算 + 经验补偿”，可以支撑一般监测，但不是可追溯的 0.5% 计量校准体系。

### 2.3 当前输出计量链

`Core/Src/adc.c` 和 `Core/Src/sys_Vo_Io.c` 当前主要公式为：

```text
ADC_mV = filtered_raw × 3300 / 4095
Vo_0.1V = ADC_Value2 × 53 / 100
Io_mA = ADC_Value4 × 100000 / (OUTPUT_CUR_SENSOR × 834)
Po_0.1W = Vo_0.1V × Io_mA / 1000
```

主要限制：

- 没有每台设备的 ADC 零点和增益系数；
- 没有实际 VDDA 补偿；
- 一阶滤波依赖循环调用频率，不是固定时间窗；
- `OUTPUT_CUR_SENSOR` 以整数 mΩ 表示，粒度过粗；
- 若输出含明显 PWM 纹波，`平均电压 × 平均电流` 不一定等于 `平均瞬时功率`；
- 低 PWM 下固定偏置、量化、噪声和采样相位影响占比更大。

### 2.4 当前上报精度瓶颈

`mqtt_zk_protocol.c` 当前 `EleInfo` 中：

- 输入电流：mA；输入电压：0.1 V；PF：0.001 上报但内部只有 0.01 分辨率；
- 输入功率：0.01 W 先舍入为整数 W；
- 输入能量：0.01 Wh 先舍入为整数 Wh；
- 输出电流：mA；输出电压：0.1 V；输出功率：0.1 W 先舍入为整数 W。

因此即使内部算法正确，低功率时正式协议的量化值也可能无法单独证明 0.5%。本项目不扩展 MQTT 正常上报；精度验收使用标准仪器与本地工站采集的内部 raw/high-resolution 调试数据。协议字段仍完全按原定义生成，报告中要把“内部计量精度”和“协议量化误差”分开列出。

### 2.5 当前 PWM 链

现有实现已经具备以下基础：

- 21 点曲线、线性插值、profile CRC、A/B 槽、tombstone、校准锁和 direct PWM；
- 有效曲线与 legacy 公式二选一；
- 有效曲线下跳过 MID 4 的 `dim-3`；
- `sys_pwm` 统一仲裁，硬件层负责 OCO、偏置和 CCR；
- 校准和 OTA 已有互斥入口。

新工作应在此基础上增量扩展，禁止推倒现有已完成的 PWM 解耦。

## 3. 物理边界与术语冻结

### 3.1 输入侧

标准仪表接在产品交流输入端子之前，测量：

- `inputVoltage`：产品入口 RMS 电压；
- `inputCurrent`：包含 CX/EMI 支路的总 RMS 电流；
- `inputActivePower`：产品总有功功率；
- `inputPowerFactor`：`P / (Vrms × Irms)`；
- `inputEnergy`：入口累计有功电能。

如果 BL0942 电流采样电阻位于 CX 支路之后，固件必须把负载侧电流矢量与 CX 电流矢量合成为入口电流；如果采样电阻位于 CX 之前，则 BL0942 已直接测得入口总电流，禁止再次补偿 CX。

### 3.2 输出侧

标准仪表接在 LED 输出端子，测量：

- `outputVoltage`：输出端电压；
- `outputCurrent`：输出端电流；
- `outputActivePower`：输出平均有功功率；
- `outputEnergy`：由校准后的输出有功功率按准确时间积分得到的累计直流输出能量。

输出侧是直流端，功率因数在物理上不适用；报告和协议必须明确写 `N/A_DC_OUTPUT`，不能用 1.0 或 0 冒充已校准值。

### 3.3 两类“电流准确”的区别

| 问题 | 被校准对象 | 标准值 | 结果 |
|---|---|---|---|
| PWM 调光准确 | 目标百分比到实际 LED 输出电流 | 外部输出电流仪表 | 21 点 PWM 曲线 |
| 电参数上报准确 | ADC/BL0942 原始量到工程量 | 标准功率分析仪/表 | 零点、增益、相位/CX、能量系数 |

两条链可以在同一工站流程执行，但数学模型、CRC、有效位和验收结论必须分开。

## 4. 精度、测试点和判定标准

### 4.1 仪器要求

- 标准功率分析仪/功率计精度不低于 0.05 级，并在有效校准期内；
- 输出电流/电压仪表及电子负载综合不确定度应小于待测阈值的 1/5，目标不高于 0.1%；
- 交流源应能稳定输出产品规定的最低、额定和最高输入电压；
- 记录仪器型号、序列号、量程、滤波/更新率、校准证书编号和有效期；
- 标准仪器读数不能由 MCU `Io_value`、`Z_ac_current` 或计算值替代。

### 4.2 验收点

额定负载 10%–100% 至少使用 6 点，默认：

```text
10%、20%、40%、60%、80%、100%
```

为了定位低于 40% PWM 的问题，工程验证额外加入：

```text
5%、10%、15%、20%、25%、30%、35%、40%
```

PWM 曲线验证仍覆盖 0%、5%……100% 全部 21 点；联合验收还应抽取非节点点，如 7%、13%、33%、57%、73%、97%，检查插值误差。

输入电压至少验证：

```text
最低额定输入、220/230 V 标称点、最高额定输入
```

最终电压点由产品规格冻结，不在代码中写死 180/220/264 V。

### 4.3 误差公式

对电压、电流和有功功率：

```text
relative_error_pct = abs(DUT - REF) / abs(REF) × 100%
要求：每个验收点 <= 0.5%
```

功率因数同时满足：

```text
abs(PF_DUT - PF_REF) <= 0.005
且当 PF_REF >= 0.5 时：abs(PF_DUT - PF_REF) / PF_REF <= 0.5%
```

累计能量使用区间增量：

```text
energy_error_pct = abs(ΔE_DUT - ΔE_REF) / ΔE_REF × 100%
要求：<= 0.5%
```

能量测试时间必须足够长，使标准仪器和 DUT 的最小计数分辨率引入的理论误差不超过 0.1%。

零点不用相对误差，分别冻结绝对限值：

- 输出关闭漏电流；
- 输入空载电流和空载有功功率；
- 输出 ADC 零偏；
- 能量防潜阈值。

这些绝对限值由产品规格和硬件噪声实测决定，不能把 0 除法包装成 0.5%。

### 4.4 内部放行护栏

为了给仪器不确定度、温漂和复测留余量：

- 标定拟合点建议控制在 0.2% 内；
- 独立验证点建议控制在 0.35% 内；
- 对外验收门限为 0.5%；
- 任一参数任一点超过门限即整机不合格，禁止只看平均误差。

PWM 输出电流若也要求 0.5%，现有工站的 `max(target×1%, 10mA)` 必须修改为产品冻结的 0.5% 门限及合理绝对下限；不能把计量 0.5% 自动等同于 PWM 控制 1%。

## 5. 计量算法设计

### 5.1 总体策略

MCU 只执行确定性、低 ROM/RAM、可验证的固定点换算；复杂拟合、异常值剔除、PAVA 单调回归和报告生成全部放在 Python 工站。

首选顺序：

1. 先修正单位、物理边界和采样时序；
2. 每个通道做零点 + 增益；
3. 查看独立验证残差；
4. 只有残差呈稳定的低量程系统性弯曲时，才启用连续双段线性；
5. 只有跨温度/输入电压残差超过门限时，才增加有界温度/线电压补偿；
6. 禁止直接上高阶多项式、神经网络或按 PWM 百分比修正上报数据。

### 5.2 通道零点和增益

输入电压、输入电流、输入有功功率、输出电压和输出电流分别保存独立系数。统一固定点模型：

```c
corrected_raw = max(raw - zero_raw, 0);
value = ((int64_t)corrected_raw * gain_q24 + (1 << 23)) >> 24;
```

要求：

- 原始 BL0942 24 位寄存器和原始 ADC count 直接进入模型，不能先截断成 0.1 V、mA 或 0.01 W 再校准；
- 中间计算使用 64 位并进行饱和；
- 系数范围、乘法溢出和负数行为有单元测试；
- 输出值使用明确 SI 子单位：mV、µA、mW、PF ppm、µWh；
- legacy 变量由高分辨率结果统一舍入生成，不再维护另一套公式。

### 5.3 低量程双段线性

只有单一零点/增益在独立验证中无法达到 0.5%，且残差可重复时，允许对输入电流、输入有功功率或输出电流启用连续双段模型：

```text
raw <= breakpoint:
    y = (raw-zero) × gain_low
raw > breakpoint:
    y = y_at_breakpoint + (raw-breakpoint) × gain_high
```

断点由训练数据拟合，但必须满足：

- 断点两侧连续；
- 单调不下降；
- 独立验证点改善明显；
- 不使用 PWM 百分比作为分段条件；
- 系数和断点能在 256 字节记录中容纳；
- 不能修复随机噪声、采样混叠或硬件分辨率不足。

### 5.4 CX 电容和输入电流

`CX` 必须从“经验参数”升级为“有明确拓扑和单位的物理参数”。profile 增加：

```text
cx_topology = BEFORE_SHUNT | AFTER_SHUNT | NONE
cx_effective_pf_or_nf
cx_phase_sign
```

标准定义是产品交流入口总电流。若 BL0942 电流采样位于 CX 之后，使用矢量关系：

```text
P_load = V × I_load × PF_load
Q_load = sign_load × V × I_load × sqrt(1 - PF_load²)
Q_cx   = -2π × f × C_eff × V²
I_input = sqrt(P_load² + (Q_load + Q_cx)²) / V
PF_input = P_input / (V_input × I_input)
```

如果采样位于 CX 之前：

```text
I_input = calibrated BL0942 current
PF_input = calibrated active power / (calibrated voltage × I_input)
```

实施要求：

1. 在接硬件前从原理图/BOM确认 CX 与电流采样电阻的位置；没有原理图则在台架通过拆分支路或对比空载矢量验证。
2. 频率必须使用校准后的 BL0942 `FREQ`，不能固定 50 Hz；同时验证 50/60 Hz 产品范围。
3. `C_eff` 可以从 BOM 初值开始，但最终由标准仪器在空载/轻载数据中拟合并受物理范围约束。
4. 先确定无功符号，再做矢量相加；禁止只对 RMS 数值做标量加减。
5. `program` 和 `Release-MinSize` 必须调用同一固定点实现并得到同一黄金向量结果。
6. PF 必须在最终输入边界的 `U/I/P` 上重新计算，不能沿用 CX 修正前的 `ac_pf`。

### 5.5 功率因数

PF 不再保存一个独立“百分比增益”作为首选方案，而是由同一快照中的校准值计算：

```text
apparentPower_mW = V_mV × I_uA / 1,000,000
PF_ppm = clamp(round(P_mW × 1,000,000 / apparentPower_mW), 0, 1,000,000)
```

实现时统一量纲并用 64 位，禁止当前内部 0.01 粗分辨率和封顶 0.99 的行为。若 BL0942 的相位误差使低 PF 工况仍超差，才按芯片能力增加受限的相位校正；该校正必须通过不同 PF 负载独立验证，不能从同一个 P/U/I 比值循环拟合。

### 5.6 输入有功功率和累计能量

- `WATT` 使用独立零点/增益，不以 `U×I×PF` 反算替代芯片有功功率通道；
- `CF_CNT` 作为输入累计有功电能的唯一权威源；
- 保存 `energy_gain_qN`，以计数差换算 µWh；
- 正确处理 24 位计数回绕、复位、首样本和掉电恢复；
- `WA_CREEP` 仅在空载噪声和最小有效功率测试后设置；阈值变更必须纳入 profile；
- 现有按分钟积分 `ac_powerpa` 只保留为诊断/交叉检查，不再作为 `tEc` 的独立权威累计源；
- Flash 持久化按增量和磨损预算执行，不能每秒写入。

### 5.7 输出电压、电流和功率

输出校准直接从 ADC raw count 开始：

- 关断状态采集零点；
- 多个稳定负载点拟合电压/电流增益；
- 如果 MCU 型号和引脚配置允许，接入 Vrefint 或经过验证的 VDDA 测量；
- 若无法获得足够稳定的参考，必须把 VDDA 漂移纳入硬件能力评估；
- 用固定毫秒窗口累加，不再让滤波常数依赖主循环速度；
- 窗口长度覆盖整数个 PWM 周期并记录样本年龄。

输出功率分两种情况：

1. 输出经滤波后近似稳定直流，验证 `P = calibrated_V × calibrated_I` 能在全部工况达到 0.5%；
2. 存在显著 PWM/开关纹波或采样相位偏差，则使用定时器触发的成对采样和无缓冲累加：`Pavg = sum(v[n]×i[n])/N`。

若 ADC 采样率、同步触发、模拟带宽或噪声无法让第二种情况达到 0.5%，必须报告硬件限制，不能继续增加软件拟合阶数。

输出累计能量以校准后的输出有功功率为唯一来源，并保留整数余数避免截断：

```text
accumulator += outputPower_mW × elapsed_ms
deltaOutputEnergy_uWh = accumulator / 3600
accumulator %= 3600
```

计时使用单调毫秒 tick；样本失效、时间间隔异常或设备复位必须有明确处理。输出能量放入独立的版本化运行数据字段，不能占用 256 字节校准记录，也不能与现有输入 `ac_EnergyP` 共用变量。持久化前先审计属性页实际使用末端并保持 `0x400` 校准槽边界断言。

### 5.8 温度和输入电压补偿

先在常温、标称输入完成基础系数。再采集低温/常温/高温和低/中/高输入电压矩阵。仅当残差有稳定趋势时增加：

```text
y_corrected = y_base + kT × (T - T0) + kV × (Vin - Vin0)
```

要求系数有上下限，超出验证范围时不外推或明确降级。PWM 曲线也只允许在实测证明必要时增加按百分比索引的有界 `kT/kV`，首版不建立完整二维表。

## 6. 21 点 PWM 算法设计

### 6.1 曲线职责

21 点固定为：

```text
0%、5%、10%……100%
```

0% 固定逻辑 PWM 为 0 并硬关 OCO。5%–100% 的目标是外部标准仪表测得的 LED 输出电流，不是设备校准后的 `Io_value`。

### 6.2 推荐工站算法

为处理低于 40% PWM 的非线性，工站使用：

1. 低功率密集预扫，获取实际 `PWM → 电流` 单调形状；
2. 上升和下降各扫一次，检查滞回、温升和建立时间；
3. 每个候选点采用固定建立时间、固定窗口和连续两个稳定窗口；
4. 对预扫样本做 PAVA/Isotonic 单调回归，抑制噪声但保留平台；
5. 对目标 5% 间隔反求初始 PWM；
6. 在初值附近执行整数二分/局部搜索；
7. 选择误差最小、满足严格递增和保护条件的 PWM；
8. 全部 21 点完成后做临时曲线全点复测和非节点插值复测。

MCU 继续使用分段线性插值。PAVA 和反求只在 Python 端执行，不增加固件体积。

### 6.3 稳定判据

默认初值：

- 建立时间 500 ms；
- 标准仪表采样间隔 100 ms；
- 窗口 12 点；
- 连续两个窗口合格；
- 窗口极差和线性斜率同时受限；
- 单点最多 16 次搜索；
- direct/高功率单次驻留最多 30 s；
- 保护、限幅、样本过旧或仪表旧读数立即失败并归零。

这些参数写入工站配置和最终报告，不能隐藏在代码常量中。

## 7. 固件架构和代码改造清单

### 7.1 模块边界

保留现有 PWM 模块并新增计量模块：

```text
Core/Src/current_calibration.c/.h
  会话、安全锁、超时、direct PWM、联合提交协调

Core/Src/current_cal_curve.c/.h
  21点校验、profile/curve CRC、插值

Core/Src/current_cal_storage.c/.h
  A/B记录v2、section有效位、tombstone、读改写和回读

Core/Src/meter_calibration.c/.h                 [新增]
  计量系数校验、固定点换算、临时/活动系数、profile/CRC

Core/Src/meter_snapshot.c/.h                    [新增]
  BL0942原始寄存器 + ADC raw + 高分辨率工程量的一致快照

Core/Src/LampProtocolLib/zk_calibration_property.c/.h
  严格JSON解析、状态/快照响应；不拟合、不直接写Flash/PWM
```

若 ROM 评估表明两个新增模块适合合并，可以合并 `.c`，但职责和接口仍须分层。

### 7.2 `sys_bl0942.c/.h`

必须完成：

- 保存一次合法数据帧的原始 `I_RMS/V_RMS/WATT/CF_CNT/FREQ/STATUS`；
- 提供带序列号和时间戳的一致 raw snapshot；
- 用统一计量系数生成高分辨率输入值；
- 实际换算 `FREQ`；
- 统一固定点 CX 路径，删除构建目标差异；
- 删除 `×0.97`、`-7mA` 等不可追溯补丁，或只在迁移前放入显式 legacy 分支且校准记录有效后永不使用；
- PF 在最终边界上重算；
- CF_CNT 成为权威能量源；
- `I_RMSOS/WA_CREEP/GAIN_CR` 的写入必须集中、版本化、可回读，默认优先软件系数，避免量产阶段双重校准。

### 7.3 `adc.c` 与 `sys_Vo_Io.c/.h`

必须完成：

- 快照返回原始 ADC count，不仅是已乘 3300 的 mV；
- 固定时间窗滤波/累加，记录窗口开始、结束和样本数；
- 评估并实现 Vrefint/VDDA 补偿能力；
- 由 `meter_calibration` 计算输出 mV/µA/mW；
- legacy `Vo_value/Io_value/Po_value` 由高精度结果统一舍入；
- 保护继续使用经过验证且故障时安全的值；校准数据无效时保持原公式，不能使保护失效；
- 防止协议层直接 `extern` 私有变量。

### 7.4 `sys_pwm.c/.h` 和硬件层

继续遵循：

- 正常模式：有效曲线或 legacy 公式二选一；
- 校准模式：direct 逻辑 PWM 或临时曲线；
- 最终统一限幅、保护、OCO、偏置和 CCR；
- `net_dim.c` 不直接控制 OCO；
- 校准锁期间普通调光、计划、渐变和 reload 可保存请求但不得覆盖物理输出；
- `abort/exit/commit/超时/故障` 后保持关闭；
- 除 TIM1 硬件层外不新增 CCR/OCO 调用。

### 7.5 MQTT 上报

MQTT 正常上报不得扩展，必须严格依据已经正式发布的协议：

- 不增加 `EleInfoV2`、raw、CRC、校准状态或输出能量等私有对象/字段；
- 不改变 `EleInfo` 的字段名、数组顺序、单位、数值类型、舍入方式和上报周期；
- 不用原协议的保留位承载未批准数据；
- 内部高精度值只用于生成协议规定的现有字段，最终量化行为保持协议一致；
- 协议中未定义的输出能量、直流侧 PF、raw ADC/BL0942 数据不得通过正常 MQTT 上报；
- 0.5% 验收使用标准仪表、本地工站 JSONL/CSV，以及串口/J-Link/host debug 获取的内部数据，不依赖增加云端字段；
- 若正式协议将来升级，必须以新的已签字协议版本另行实施，本文档不预授权任何 MQTT schema 变化。

按当前工程 `mqtt_zk_protocol.c` 的实现，至少冻结以下映射；如正式协议文档与源码不一致，先停止编码并以正式协议为准修正映射，不能自行折中：

| 现有字段 | 当前来源/量化 | 本次处理 |
|---|---|---|
| `e[0]` | 现有故障位图 | 不变 |
| `c[0]` | `Z_ac_current`，mA | 内部校准后按原单位生成 |
| `v[0]` | `ac_voltage_8209`，0.1 V | 内部校准后按原单位生成 |
| `f[0]` | 现有 PF 协议量化值 | 保持协议尺度和类型 |
| `p[0]` | `ac_powerpa` 由 0.01 W 四舍五入为整数 W | 不变 |
| `rEc[0]` | 现有本次能量由 0.01 Wh 四舍五入为整数 Wh | 不变 |
| `tEc[0]` | 现有累计能量由 0.01 Wh 四舍五入为整数 Wh | 不变 |
| `oc[0]` | `Io_value`，mA | 内部校准后按原单位生成 |
| `ov[0]` | `Vo_value`，0.1 V | 内部校准后按原单位生成 |
| `op[0]` | `Po_value` 由 0.1 W 四舍五入为整数 W | 不变 |

必须保存校准前后的黄金 MQTT 报文，逐字段比较 key 集合、数组长度、顺序、数值类型和单位；除数值因校准变得更准确外，报文 schema 不得有任何差异。

需要分别出具两项结论：

1. 内部校准后的物理量相对标准仪表是否满足 0.5%；
2. 按正式 MQTT 协议量化后的上报值会产生多大显示误差。

第二项受协议分辨率限制时，应在报告中标明 `PROTOCOL_QUANTIZATION_LIMIT`，不能通过私有字段规避，也不能反向修改内部校准系数让某个整数上报值看起来更接近。

## 8. A/B Flash v2 记录

### 8.1 记录原则

- 仍使用 A/B 两个 256 字节槽；
- 整条记录包含计量 section 和 PWM section，各自有 version/profile CRC/data CRC/valid bit；
- 最外层有 sequence、record CRC 和末尾 valid marker；
- 更新一个 section 时把另一个有效 section 原样 copy-forward；
- 只写非活动槽，回读验证后最后写有效标记；
- 失败继续使用旧槽；
- 禁止把原始采样、报告或仪器信息写入 MCU Flash，这些保存在工站文件中。

### 8.2 建议布局

目标记录大小不超过 208 字节，至少保留 48 字节后续空间。字段示意：

```c
typedef struct {
    u32 magic;
    u16 format_version;       /* 2 */
    u16 record_size;
    u32 sequence;
    u32 record_type;          /* data / meter tombstone / pwm tombstone / all tombstone */
    u32 valid_sections;
    u32 combined_profile_crc;
    u32 meter_profile_crc;
    u32 pwm_profile_crc;

    u16 curve_version;
    u16 point_count;
    u16 logical_pwm[21];
    u32 curve_crc;

    meter_coefficients_v1_t meter;
    u32 meter_crc;

    s16 calibration_temp_01c;
    u16 calibration_input_01v;
    u32 calibration_epoch;
    u32 reserved[];
    u32 record_crc;
    u32 valid_marker;         /* 最后写 */
} calibration_flash_record_v2_t;
```

`meter_coefficients_v1_t` 至少包含输入 U/I/P、输出 U/I 的 zero/gain、能量增益、CX effective value/topology、可选双段断点/第二增益和 flags。输出功率默认派生，不必无条件另存一组系数；只有独立标定证明需要时才启用。

必须有：

```c
STATIC_ASSERT(sizeof(calibration_flash_record_v2_t) <= 0x100);
STATIC_ASSERT(offsetof(..., valid_marker) % 4 == 0);
STATIC_ASSERT(CURRENT_CAL_PROPERTY_USED_END <= 0x400);
STATIC_ASSERT(0x08007400 + 0x100 <= 0x08008000);
STATIC_ASSERT(APROM_SAFE_ENDADDR == 0x08024000);
STATIC_ASSERT(OTABAKROM_ENDADDR == 0x0803FFFF);
```

### 8.3 profile 与失效规则

拆分 profile：

- `meter_profile`：SID、MID、DRV_VERSION、板卡/计量版本、BL0942 模式与寄存器配置、分流器/分压器配置、CX 拓扑与名义值、ADC 参考方式、输出传感器拓扑、算法版本；
- `pwm_profile`：meter profile 标识、SET_OUTCUR、HWMAX_OUTCUR、OP_PWM_OFFSET、TIM1 PSC/ARR、PWM2 模式/极性、逻辑最大值和曲线算法版本。

失效矩阵：

| 参数变化 | meter | PWM curve |
|---|---|---|
| BL0942/分流器/分压器/CX/ADC参考/传感器硬件 | 失效 | 视输出控制关系决定，默认联合失效 |
| SET_OUTCUR/HWMAX_OUTCUR/OP_PWM_OFFSET/TIM1 | 保留 | 失效 |
| SID/MID/DRV_VERSION/板卡版本 | 失效 | 失效 |
| 仅平台上报周期 | 保留 | 保留 |

任何失效都写 tombstone。即使参数改回原值也不自动复活旧记录。

### 8.4 v1 迁移

- 能读取现有 80 字节 v1 PWM 记录；
- v1 曲线 profile 匹配时可作为 `PWM_VALID + METER_EMPTY` 启动；
- 首次成功全参数 commit 写 v2；
- 不在启动时自动擦除 v1；
- 两槽中 v1/v2 并存时按合法 sequence 和显式版本迁移规则选择；
- 为 v1/v2、单槽损坏和回滚建立黄金镜像测试。

## 9. 校准协议和状态机

### 9.1 外层

继续使用已经冻结的中科外层和现有 `DT.Calibration` 定义，不增加 MQTT 动作或响应字段：

- `SV="prop"`；
- `readInfo/readStatus/readCurveStatus` 使用协议已规定的 `CT="R"`；
- 其余既有校准动作使用协议已规定的 `CT="W"`；
- 响应只回显正式协议要求的 `SN/ID/SV/CT` 和既有响应内容；
- 不增加 `readRawSnapshot/readMeterStatus/writeMeterChunk/applyMeterTemporary` 等私有 MQTT 动作；
- 不在 `readStatus/readInfo` 中追加未被正式协议定义的 BL0942 raw、ADC raw、计量系数或新状态字段；
- 协议层只严格解析和响应，不拟合、不直接写 Flash、不直接操作 PWM。

计量校准需要的 raw 数据、pending 系数和高精度值通过本地工厂调试通道处理：host 单元测试、调试串口或 J-Link RAM mailbox。量产选择哪一种由工装条件冻结，但均不得伪装成 MQTT 正常上报或私有 MQTT 命令。若正式协议已经另有工厂校准字段，则只能按该正式定义映射，并以协议文档为准。

### 9.2 动作

MQTT 只保留现有已冻结动作：

| 动作 | 目的 | 是否可改变输出 |
|---|---|---|
| `readInfo` | 读取能力、版本、profile、section 状态 | 否 |
| `enter` | 建立既有 PWM 校准会话并归零 | 只会关断 |
| `setPwm` | direct PWM 搜索 | 是，受安全门控 |
| `readStatus` | 读取正式协议已规定的状态字段 | 否 |
| `writeCurveChunk` | 写 RAM pending 21 点曲线 | 否 |
| `readCurveStatus` | 读取曲线接收和合法状态 | 否 |
| `applyTemporary` | RAM 预览曲线并保持关断 | 否 |
| `setTestPercent` | 使用临时曲线验证 0..100% | 是，受安全门控 |
| `commit` | 输出归零后按既有语义提交 | 只会关断 |
| `abort` | 丢弃 pending 并关断 | 只会关断 |
| `exit` | 仅提交成功后退出并保持关断 | 只会关断 |

计量系数的采集、临时应用和写入属于本地工厂工具流程，不生成任何 MQTT action。若本地工具把 meter pending 与 curve pending 合并为同一 v2 记录，既有 `commit` 的 MQTT 请求和 ACK 仍保持原格式；固件内部行为变化不得改变线上协议可见内容。

### 9.3 状态机

```text
本地工站（非MQTT）：
  METER_COLLECTING → METER_PENDING → METER_TEMP_APPLIED → METER_VERIFIED_HOST

既有MQTT PWM状态机：
  IDLE ─ enter → READY ─ setPwm/readStatus → PWM_SEARCH
       ─ writeCurveChunk → CURVE_PENDING
       ─ applyTemporary/setTestPercent → TEMP_APPLIED/VERIFIED_HOST
       ─ commit → COMMITTING → COMMITTED ─ exit → IDLE

任意活动状态 ─ abort/超时/严重故障 → 关断 → IDLE
```

设备不能自行证明外部标准仪表已达到 0.5%；`METER_VERIFIED_HOST/VERIFIED_HOST` 只存在于本地工站记录，不通过 MQTT 上报。设备只验证结构、CRC、profile、范围、输出为零和既有状态转换。工站未通过全部验证时不得发送 commit。

### 9.4 幂等和分片

- `sessionId` 最大 32 字节；
- `seq` 为严格递增 uint32；
- 相同 seq 和相同参数摘要只重发 ACK；
- 相同 seq 不同参数返回 `SEQ_CONFLICT`；
- commit 重试不能再次擦写；
- 曲线固定三次、每次最多 7 点；
- 计量系数不通过 MQTT JSON 分片；本地工厂工具使用规范二进制记录、CRC 和回读校验；
- 曲线重复相同字段幂等，重复不同值返回 `CHUNK_CONFLICT`；
- 所有整数做类型、负数、上限、乘法溢出和 JSON 数值精度校验。

### 9.5 本地原始快照（禁止 MQTT 上报）

固件内部保留结构化 raw snapshot，供 host 测试、调试串口或 J-Link RAM mailbox 读取。它可以包含 BL0942 `I_RMS/V_RMS/WATT/CF_CNT/FREQ/STATUS`、ADC raw、VDDA、温度、logical PWM、CCR/OCO、保护码、样本序号和年龄，但不得序列化进正常 MQTT 报文，也不得追加到既有 `readStatus` 响应。

BL0942 字段必须来自同一合法帧；ADC 字段必须来自同一完成窗口。样本不一致或过旧时，本地工具明确判错，禁止拼凑。Release 构建可保留只读 getter，但调试输出默认关闭，避免增加正常通信负载。

## 10. 安全、并发和回退

- 会话默认 30 s，允许 10–300 s；使用 `Timer_GetTickCount()` 的无符号差处理回绕；
- 单次 direct/高功率驻留最多 30 s，`readStatus` 不延长高功率驻留；
- 默认不启用任何非零输出；低功率需 `--enable-output`；超过 25% 需 `--enable-high-power`；commit 需独立 `--commit`；
- OTA 忙时拒绝 enter；校准活动时拒绝 OTA 和相关工厂电气参数写入；
- 输入过欠压、过温、ADC/BL0942 失效、逻辑上限和现有保护始终有效；
- 当前未真正接入的短路/过流 FSM 不能在报告中冒充已验证保护；
- 100% 测试前必须完成台架保护注入，失败立即停止高功率验收；
- 计量记录无效时内部回退旧计量公式并记录本地诊断状态；MQTT 仍只按正式协议上报既有字段，不增加 `LEGACY/UNCALIBRATED/VALID` 状态字段；
- PWM 曲线无效时回退 legacy 调光公式；两个回退状态互不冒充；
- abort/exit/commit/故障后不恢复进入前亮度。

## 11. 工站、调试和测试工具

### 11.1 目录和文件

保留 `tools/current_calibration/` 的兼容入口，新增：

```text
tools/full_calibration/
├─ full_calibration_station.py   总流程/CLI/安全门控
├─ device_transport.py          仅封装正式MQTT协议的既有动作与Mock
├─ local_debug_transport.py     本地raw/计量系数接口；禁止封装成MQTT
├─ jlink_mailbox.py             J-Link RAM mailbox读写、调用和回读校验
├─ meter_adapter.py             标准仪表抽象接口
├─ manual_meter.py              无仪表协议时人工粘贴标准值
├─ mock_meter.py                离线仿真仪表
├─ scpi_meter.py                SCPI/TCP/串口适配器；具体命令由仪表型号配置
├─ electronic_load.py           负载抽象；首版支持manual/mock
├─ ac_source.py                 交流源抽象；首版支持manual/mock
├─ fit_meter_coeffs.py          零点/增益/双段拟合、交叉验证、残差分析
├─ fit_pwm_curve.py             预扫、PAVA、反求、二分和21点校验
├─ fixed_point_model.py         与MCU完全一致的Q格式参考实现
├─ simulate_plant.py            PWM/传感器/CX/噪声/温漂仿真
├─ flash_record_tool.py         v1/v2序列化、CRC、A/B镜像只读分析
├─ dataset_schema.py            JSONL/CSV字段和版本
├─ validate_dataset.py          完整性、单位、时间、仪表校准期检查
├─ generate_report.py           Markdown/JSON/CSV合格报告
├─ configs/
│  ├─ station.example.json
│  ├─ device_profile.example.json
│  └─ instruments.example.json
├─ fixtures/
│  ├─ ideal.jsonl
│  ├─ low_load_nonlinear.jsonl
│  ├─ xcap_after_shunt_50hz.jsonl
│  ├─ xcap_after_shunt_60hz.jsonl
│  ├─ noisy_adc.jsonl
│  └─ flash_v1_v2_images/
└─ tests/
   ├─ test_fixed_point.py
   ├─ test_meter_fit.py
   ├─ test_xcap_vector.py
   ├─ test_energy.py
   ├─ test_pwm_fit.py
   ├─ test_protocol.py
   ├─ test_state_machine.py
   ├─ test_storage.py
   ├─ test_fault_injection.py
   └─ test_replay.py
```

公共 CRC、协议和日志代码应从现有 `tools/current_calibration/calibration_station.py` 提取为共享模块，不能复制后形成两套不一致实现。旧 CLI 保持可用。

### 11.2 仪表适配接口

统一接口至少提供：

```python
class MeterAdapter:
    def identify(self) -> dict: ...
    def configure(self, config: dict) -> None: ...
    def read_snapshot(self) -> dict: ...
    def health(self) -> dict: ...
    def close(self) -> None: ...
```

`read_snapshot()` 返回同一时刻或仪器定义的同一聚合窗口中的输入 U/I/P/PF/E 和输出 U/I/P/E。每条记录包含仪器时间戳、量程、状态、过载位和新样本序号。人工模式必须要求操作者确认读数时间和单位，不能静默沿用上一次输入。

### 11.3 数据集字段

每个样本至少记录：

- run/session/device/firmware/profile/sequence；
- 工况：输入电压、频率、负载模式、PWM/百分比、温度；
- 标准仪表全部原始值和单位；
- BL0942 原始寄存器、ADC raw、设备高分辨率值；
- 样本时间、样本年龄、稳定窗口统计；
- 系数/曲线版本和 CRC；
- 保护、限幅、通信、仪表状态；
- 操作者、仪器 ID、校准日期；
- pass/fail/NOT_RUN 和原因。

原始 JSONL 只追加不修改；CSV 和报告可由 JSONL 重新生成。报告必须保存使用的工具 Git revision 和配置文件 SHA-256。

## 12. 当前无实机连接时的执行和测试

本阶段允许完成编码，但禁止声称实机精度合格。

### 12.1 第一步：只读基线和分区检查

执行：

```powershell
git status --short
python tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --app-base 0x08008000 --safe-end 0x08024000 --json
Select-String -Path MDK-ARM-8008000/out/cat1.map -Pattern "Load Region LR_IROM1","Execution Region ER_IROM1","Total ROM Size"
```

只读取历史构建产物；不得运行 J-Link 工具。记录当前 `boot_app_merged.bin` 为用户已有删除状态。

### 12.2 第二步：构建两个 Keil 目标

当前 Windows 下 `tools/mcu_workflow.py` 直接导入 `fcntl` 会失败，因此在修复其跨平台文件锁前使用 Keil 命令行：

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -b 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\MDK-ARM-8008000\project.uvprojx' -t 'program' -o 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\MDK-ARM-8008000\out\build_program.log'

& 'D:\Keil_v5\UV4\UV4.exe' -b 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\MDK-ARM-8008000\project.uvprojx' -t 'Release-MinSize' -o 'D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project\MDK-ARM-8008000\out\build_release_minsize.log'
```

要求：

- 两目标均零 error、零新增 warning；
- 两目标对相同 raw 黄金向量产生相同计量结果；
- APP 不超过 112 KB；
- map 最高 APP 地址小于 `0x08024000`；
- 整片最高合法地址不超过 `0x0803FFFF`；
- 记录新增 ROM/RAM；
- 修复 `mcu_workflow.py` 时使用 Windows `msvcrt` 或跨平台抽象，不为了测试额外引入大型运行时依赖。

### 12.3 第三步：现有测试

```powershell
python -m unittest tools.current_calibration.test_calibration_logic -v
python tools/check_keil_app_image.py --json
```

现有测试失败时先区分基线缺陷和新增回归；不得修改测试去迎合错误输出。

### 12.4 第四步：新增纯算法测试

```powershell
python -m unittest discover -s tools/full_calibration/tests -p "test_*.py" -v
```

必须覆盖：

- CRC-32/ISO-HDLC 公共向量和小端序列化；
- Q 格式舍入、负数、饱和、最大 raw 和 64 位边界；
- 单点、双点、双段连续拟合；
- 训练点与独立验证点严格分离；
- 异常值、旧样本、缺字段、单位错误；
- CX 位于采样前/后、50/60 Hz、容/感无功符号；
- PF 0、接近 1、分母 0、低负载；
- CF_CNT 正常增量、回绕、复位、掉电续算和防潜；
- PAVA 单调回归、平台、非单调噪声、插值边界；
- 0%、5%、40%、100% 和非节点 PWM；
- v1/v2 A/B 镜像、CRC 坏、profile 错和 sequence 回绕；
- 全部非法状态转换、seq 重发/冲突和 commit 幂等。

### 12.5 第五步：仿真工厂

```powershell
python tools/full_calibration/simulate_plant.py --scenario ideal --output tools/full_calibration/out/ideal.jsonl
python tools/full_calibration/simulate_plant.py --scenario low-load-nonlinear --output tools/full_calibration/out/low_load.jsonl
python tools/full_calibration/simulate_plant.py --scenario xcap-after-shunt --frequency-hz 50 --output tools/full_calibration/out/xcap50.jsonl
python tools/full_calibration/simulate_plant.py --scenario xcap-after-shunt --frequency-hz 60 --output tools/full_calibration/out/xcap60.jsonl
```

仿真模型至少可注入：

- 零偏、增益误差、低量程弯曲；
- ADC 量化和噪声；
- PWM 脉冲宽度偏斜、死区和平台；
- 交流电压和温度系数；
- CX 容差、频率和相位符号；
- 仪表延迟、旧读数、丢包；
- 保护、超时和复位故障。

算法必须能在可校准场景恢复已知参数；在信息不足或能力不足场景必须明确失败，不能总是输出一组“合格系数”。

### 12.6 第六步：Mock 端到端

```powershell
python tools/full_calibration/full_calibration_station.py --mode mock-full --config tools/full_calibration/configs/station.example.json --scenario low-load-nonlinear
python tools/full_calibration/full_calibration_station.py --mode mock-protocol --config tools/full_calibration/configs/station.example.json
python tools/full_calibration/full_calibration_station.py --mode replay --dataset tools/full_calibration/fixtures/ideal.jsonl
```

Mock 必须完整走：

```text
readInfo → enter → raw采集 → meter拟合 → 分片 → 临时应用 →
meter独立验证 → PWM预扫/21点 → 临时曲线 → 联合验证 →
commit幂等 → 模拟重启 → A/B加载 → 报告
```

### 12.7 第七步：静态和回归检查

- 全工程搜索 `__HAL_TIM_SET_COMPARE`、`oco_on/off`、Flash 写入和 legacy 计量公式；
- 建立允许调用白名单；
- 验证 `program`/`Release-MinSize` 计量算法一致；
- 检查 cJSON 分配失败、超长数组、64 位 JSON 精度和 MQTT 包长；
- 用 AddressSanitizer/普通 CPython 测试工站工具边界；
- 对 C 算法建立 host mirror 黄金向量；如无可用 MCU host 编译链，至少由 Python 生成头文件向量并在 Keil debug 自测入口中验证；
- 所有硬件用例输出 `NOT_RUN_HARDWARE`，报告汇总不得显示“全量通过”。

## 13. 实机连接后的台架流程

### 13.1 解锁前检查

1. 核对原理图/BOM：BL0942 shunt、CX、电压分压、ADC 分压、输出传感器和地参考。
2. 标准仪器和电子负载接线双人复核；设置过流、过压、功率和温度硬件限制。
3. MQTT `readInfo/readStatus` 确认输出为零；示波器确认 OCO 关闭。
4. J-Link 采用 reset-pin 模式；任何 halt/烧录前再次通过 MQTT 确认输出为零。
5. 串口可选使用 COM23、1,000,000 波特率，只采集日志，不作为精度基准。

### 13.2 保护和小功率门槛

依次验证：

- 0% OCO/漏电流；
- 会话超时、MQTT 断开、上位机崩溃；
- 输入过欠压、过温、ADC/BL0942 失效；
- direct PWM 驻留超时；
- 校准锁与调光/计划/reload/OTA 互斥；
- 台架可控的短路/过流保护注入。

任一保护失败，不允许进入超过 25% 的测试。

### 13.3 输入计量校准

1. 输出关闭，采集输入电流/功率零点和空载 CX 特征。
2. 在多个输入电压点校准 `V_RMS`。
3. 在 10%–100% 负载点同步采集标准 U/I/P/PF/E 与 BL0942 raw。
4. 如可用，加入不同 PF 或已知相位负载，验证相位/PF，而不是只用一种 LED 工作点。
5. 根据 shunt/CX 拓扑拟合 `I/P/C_eff`，计算频率和 PF。
6. 用独立保留点验证单段模型；不合格才评估双段模型。
7. 用足够长区间校准 CF_CNT 能量系数和 WA_CREEP。
8. 临时应用计量系数，不写 Flash。

### 13.4 输出计量校准

1. 输出关闭采集 ADC 零点、VDDA 和漏电流。
2. 在 5%–100% 多点同步采集输出标准 U/I/P/E 与 ADC raw。
3. 重点增加 5%–40% 点，检查偏置、量化、采样相位和滤波。
4. 用示波器判断输出纹波；选择 `Vavg×Iavg` 或同步 `avg(V×I)`。
5. 拟合输出 U/I 系数，独立验证输出 P。
6. 用标准仪器输出能量或标准功率的时间积分验证 `outputEnergy` 的时间基准、整数余数、复位和持久化。
7. 临时应用后复测，不写 Flash。

### 13.5 PWM 21 点校准

1. 计量临时系数已验证，但 PWM 判定仍用外部输出仪表。
2. 0% 验证硬关断和漏电流。
3. 低功率预扫、PAVA、反求、局部二分获取 5%–100% 点。
4. 做上升/下降扫描，发现滞回或热漂移则暂停分析。
5. 临时应用 21 点曲线并复测全部点及非节点点。
6. 任一点超差、非单调、保护或温升异常都不 commit。

### 13.6 联合提交与持久化

1. 输出归零。
2. 工站验证 meter 和 curve 的 profile/CRC、全部 bitmap 和报告完整性。
3. 发送一次 `commit`；模拟 ACK 丢失后重发相同请求，确认不重复擦写。
4. `exit` 后保持关闭。
5. J-Link reset，不擦参数；重新读取 A/B、sequence、section CRC。
6. 复测 0/5/10/40/50/100% 和输入 U/I/P/PF/E。
7. 执行 OTA，确认 v2 记录保留且 APP/OTA 边界不变。

### 13.7 全工况与 72 小时

联合验证矩阵至少包括：

- 低/标称/高输入电压；
- 10/20/40/60/80/100% 负载；
- 常温及产品要求的温度点；
- 50/60 Hz（若产品覆盖）；
- 冷机、热稳态；
- 断网恢复、看门狗复位和 OTA 后。

72 小时连续带载：

- 每分钟记录一次 DUT 和标准仪表快照；
- 记录环境、板温、输入源、负载、保护和通信状态；
- 至少每 12 小时执行一轮规定点复测，或按安全台架自动切换；
- 结束后重新计算各参数最大误差、P95、漂移斜率和能量累计误差；
- 所有规定点仍须 <=0.5%，不能用 72 小时平均值掩盖瞬时超限。

## 14. 测试矩阵

### 14.1 计量算法

- 输入/输出各通道零点、增益、双段连续性和饱和；
- 原始值缺失、checksum 错、样本过旧、更新序列不一致；
- 固定点与 Python 参考逐位一致；
- CX 拓扑、频率、容差和相位符号；
- PF 分母零、PF 接近 1、低 PF、负功率异常；
- CF_CNT 回绕、复位、防潜和掉电；
- 旧变量由高精度结果舍入，无第二套漂移算法；
- 正式 `EleInfo` 的字段、数组顺序、单位、类型、舍入和周期保持不变，并验证没有生成 `EleInfoV2` 或其他私有上报对象；单独计算协议量化误差。

### 14.2 PWM

- 21 点缺失、重复、递减、越界、首点非零；
- PAVA、搜索迭代耗尽、分辨率不足、仪表不稳定；
- 0/1/4/5/6/39/40/41/99/100% 插值；
- legacy/curve 不叠加；MID4 旧补偿只在 legacy 网络来源生效；
- normal/direct/preview 仲裁和 OCO/CCR 一致。

### 14.3 协议和状态机

- 每个动作正常、缺字段、错类型、负数、溢出、超长字符串/数组；
- seq 重发、冲突、旧序号、最大值和 session 冲突；
- meter/curve 分片幂等、交叠、缺片、CRC/profile 不同；
- 未验证 commit、commit 重发、未提交 exit；
- cJSON 分配失败和 MQTT 包长边界；
- fuzz 输入不得写 Flash、不得产生非零 PWM。

### 14.4 Flash

- 空白、单槽、双槽、sequence 最新选择、v1/v2 混合；
- 单槽 header/payload/section CRC/record CRC/valid marker 损坏；
- 擦除后、页中、CRC 前后、valid marker 前后复位；
- 50 次 A/B 轮换；
- 同页非校准字节前后 SHA/CRC 一致；
- 参数变化写 tombstone，改回原值不复活；
- OTA 后记录保留；
- 地址永不越过 `0x0803FFFF`。

### 14.5 原功能回归

- 开关、调光、渐变、计划、RTC；
- BL0942、NTC、告警、运行统计；
- 参数持久化、恢复出厂、能量清零；
- OTA、断网恢复、4G 重连、看门狗；
- 未校准设备 legacy 行为；
- meter-only、curve-only、full-valid 和两个 section 均无效四种启动状态。

## 15. 分阶段 Codex 实施顺序和门禁

### 阶段 A：冻结事实与接口

1. 保存 git 状态和构建/map 基线。
2. 完成 PWM/OCO/Flash/BL0942/ADC/MQTT 调用图。
3. 确认原理图/BOM可得性；把 CX/shunt 位置列为硬件接入前强制门禁。
4. 冻结物理边界、单位、误差公式、正式 MQTT 协议逐字段映射、不扩展检查表和 v2 本地记录序列化。
5. 生成 CRC/profile 黄金向量。

门禁：所有字段单位、Flash 布局和回退行为可由单元测试表达。

### 阶段 B：先做离线工具和数学参考

1. 提取现有工站共享 CRC/协议/日志代码。
2. 实现 fixed-point mirror、meter fit、CX、energy、PAVA 和仿真器。
3. 实现 mock meter/device/source/load。
4. 建立 ideal/failure fixtures 和报告 schema。

门禁：全部离线算法测试通过；不可校准场景能正确失败。

### 阶段 C：计量固件，不接输出

1. 实现一致 raw snapshot。
2. 实现固定点系数、CX/PF/energy 和 high-resolution snapshot。
3. legacy 变量由新快照舍入；无有效系数回退旧公式。
4. 两 Keil 目标结果一致。

门禁：构建/map合格，黄金向量逐位一致，原保护编译路径未破坏。

### 阶段 D：v2 存储和协议

1. 实现 v1 读取、v2 A/B、section CRC/profile/tombstone。
2. 增加本地 meter debug transport/J-Link mailbox 和内部高精度快照；不增加 MQTT 动作、响应字段或正常上报对象。
3. 做状态机、fuzz、掉电模型和 commit 幂等测试。

门禁：Mock 端到端通过，同页非校准数据不变。

### 阶段 E：PWM 工站升级

1. 增加预扫、PAVA、低负载密集点和联合报告。
2. 保持现有 direct/lock/storage 安全语义。
3. Mock 走完 21 点临时应用、验证、commit/reboot。

门禁：所有失败路径自动归零，未显式解锁不产生输出。

### 阶段 F：实机低功率解锁

1. 原理图/CX拓扑确认。
2. 保护、0%、小功率和采样能力验证。
3. 验证协议、快照、仪表适配和误差底噪。

门禁：保护注入通过，低功率数据证明硬件有达到 0.5% 的可能。

### 阶段 G：全参数 + 21 点 + 回归

1. 输入/输出计量校准。
2. PWM 21 点校准。
3. 联合全工况、commit、reset、OTA 和回归。
4. 72 小时稳定性。

门禁：全部参数全部规定点通过，报告可追溯。

每个阶段单独形成可审查提交；不得把计量重构、协议、Flash 和高功率实测混成一个提交。

## 16. 停止条件和硬件整改触发

出现以下任一情况，软件阶段不得继续宣称可以达到 0.5%：

- 标准仪器不确定度或量程分辨率不满足要求；
- BL0942 小信号 RMS/相位能力在 10% 负载达不到门限；
- ADC 有效位数、VDDA 漂移、噪声或采样混叠超过预算；
- 输出功率需要同步采样但现有硬件/定时资源无法实现；
- CX/shunt 物理位置不明且无法验证；
- 正式协议只允许整数 W/Wh，同时验收要求又规定低负载“MQTT量化后显示值”必须 <=0.5%；这在数学上无法同时满足时必须报告协议约束冲突，不能私自扩展字段或篡改校准系数；
- 保护注入失败；
- 低/高温或输入范围内残差不可重复；
- 双段线性仍无法在独立点达到 0.5%；
- APP 超过 112 KB 或 RAM/Flash 分区越界。

可能的硬件整改包括：提高 shunt/分压电阻精度和温漂等级、增加精密参考/VDDA 测量、改变输出采样放大倍数、增加同步采样能力、调整 BL0942 满量程利用率、优化 CX 采样位置或采用更高精度计量器件。是否整改由误差预算和实测数据决定，不能预先靠软件猜测。

## 17. 最终验收清单

### 17.1 构建与资源

- `program` 和 `Release-MinSize`：零 error、零新增 warning；
- APP <=112 KB，最高地址 `<0x08024000`；
- 整体地址不超过 `0x0803FFFF`；
- ROM/RAM 增量有报告；
- `boot_app_merged.bin` 删除状态未被改变。

### 17.2 功能和精度

- 输入 U/I/P/PF/E 全部点合格；
- 输出 U/I/P/E 全部点合格；输出侧 PF 明确为 `N/A_DC_OUTPUT`；
- 21 点 PWM 及非节点插值合格；
- 0% 泄漏和能量防潜合格；
- 低于 40% PWM 的误差来源有数据结论，不再依赖经验补丁；
- 72 小时后所有参数仍合格。

### 17.3 安全和持久化

- 超时、断网、故障和 abort 可靠关断；
- 保护、计划、渐变、OTA 和工厂参数互斥正确；
- A/B、掉电、commit 幂等、v1迁移、tombstone、reset 和 OTA 通过；
- 同页属性、运行统计和 OTA 报告未被破坏。

### 17.4 可追溯性

- 每台设备有原始 JSONL、CSV、报告和校准结论；
- 包含设备身份、固件、profile、CRC、系数、曲线、环境和仪器证书；
- 任意报告可从不可变原始数据重新生成；
- 硬件未执行项明确标记，不把仿真结果当实机证明。

## 18. 禁止事项

- 禁止把 PWM 曲线用于修正计量上报。
- 禁止用设备自身 Io 代替标准仪表闭环校准 PWM。
- 禁止保留两套 build-dependent 的 CX/MID 经验算法。
- 禁止固定 50 Hz 计算覆盖 60 Hz 产品。
- 禁止在 CX 拓扑未知时盲目做 RMS 标量加减。
- 禁止在校准后仍用 0.01 PF 内部精度和 0.99 封顶。
- 禁止同时把 CF_CNT 和分钟功率积分当权威总能量。
- 禁止在协议层直接拟合、写 CCR/OCO 或写 Flash。
- 禁止分片即写 Flash、逐点写 Flash或 commit 重发重复擦除。
- 禁止关闭保护、看门狗或用阻塞延时等待仪表稳定。
- 禁止用平均误差替代逐点最大误差。
- 禁止训练点兼作全部验收点。
- 禁止算法在能力不足时仍输出“PASS”。
- 禁止修改 Boot、APP/OTA 边界和 Keil `0x1C000` 上限。
- 禁止恢复或覆盖 `MDK-ARM-8008000/out/boot_app_merged.bin`。

## 19. 交付物

1. 本文档的冻结版、决策记录和字段/单位表。
2. 计量/PWM调用图、CX拓扑结论、误差预算和资源预算。
3. `meter_calibration`、`meter_snapshot`、v2 storage、协议和受控现有文件修改。
4. 统一固定点黄金向量、CRC/profile 测试向量和 Flash v1/v2 镜像。
5. `tools/full_calibration/` 全部工站、Mock、拟合、仿真、Flash 和报告工具。
6. 离线单元/fuzz/状态/存储/仿真/回放报告。
7. 两 Keil 目标构建日志、map、ROM/RAM 差异和 warning 清单。
8. 实机保护、小功率、全工况、21 点、reset、OTA、回归和 72 小时报告。
9. 每台量产设备的可追溯台账。

## 20. 学术和器件依据

以下资料用于选择方法，不替代本机原理图、器件误差预算和台架实测：

1. LED PWM 低占空比下，驱动器/LED 的响应时间和脉冲偏斜会造成平均输出非线性：  
   <https://www.sciencedirect.com/science/article/abs/pii/S0141938207000777>
2. TI Piccolo 数字电源软件指南展示了按温度保存 PWM/传感器增益并插值的工程方法：  
   <https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/234/DLPU061_5F00_Piccolo_5F00_SW_5F00_Guide_5F00_201803.pdf>
3. BL0942 官方产品页、应用说明和数据手册，用于寄存器、RMS、功率、相位、CF_CNT、I_RMSOS、WA_CREEP 和 GAIN_CR 的能力边界：  
   <https://www.belling.com.cn/en/product_info.html?id=370>  
   <https://www.belling.com.cn/media/file_object/bel_product/BL0942/guide/BL0942%20APP%20Note_V1.04_cn.pdf>  
   <https://www.belling.com.cn/media/file_object/bel_product/BL0942/datasheet/BL0942_V1.04_en.pdf>
4. 多参数电能计量校准通常需要分别处理 RMS U/I、P/S/Q、增益、偏置和相位，并验证低量程/谐波工况：  
   <https://www.mdpi.com/1996-1073/14/2/390>
5. 智能电表校准中对相位和功率偏置进行独立处理的研究：  
   <https://www.mdpi.com/1424-8220/22/19/7536>
6. LED 驱动中通过线路电压自适应补偿改善输出精度的研究：  
   <https://ietresearch.onlinelibrary.wiley.com/doi/10.1049/iet-cds.2016.0171>
7. 温度补偿恒流 LED 驱动研究：  
   <https://www.sciencedirect.com/science/article/pii/S1879239124001000>
8. LED 调光技术和 PWM/电流混合方式综述：  
   <https://www.mdpi.com/2079-9292/10/17/2163>

---

执行本文件时，Codex 必须先完成当前阶段的门禁再进入下一阶段。当前没有实机连接，因此允许完成阶段 A–E 的代码、构建和离线验证；阶段 F–G 必须等待设备、标准仪表和安全负载接入，并在报告中保持 `NOT_RUN_HARDWARE`，不得用仿真结果代替实机 0.5% 验收。
