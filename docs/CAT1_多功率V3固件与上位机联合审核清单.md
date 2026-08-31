# CAT1 多功率 V3 固件与上位机联合审核清单

> 适用：50W / 75W / 100W / 150W / 200W / 240W。  
> 本文是联合审核口径，50W 仅可作为测试样例。

## A. Product / Factory 语义

- [ ] `Hardware Max` 是产品硬件绝对能力。
- [ ] `HWMAX_OUTCUR` 是每机可配置 PWM 满量程电流分母。
- [ ] `SET_OUTCUR` 是当前 100% 业务目标电流。
- [ ] 36V I-V Max 只属于规格安全包络，不得作为默认 PWM 分母。
- [ ] 默认值只在 Flash 空白/无效时恢复，不得每次上电覆盖用户 Factory 值。
- [ ] 校准过程不得写改 `SET_OUTCUR/HWMAX_OUTCUR`。

## B. 默认调光

无有效 Output Calibration 或目标超校准范围时：

```c
PWM = percent * SET_OUTCUR * PWM_USEFUL_RANGE
    / (HWMAX_OUTCUR * 100U);
```

- [ ] 公式与历史基线一致。
- [ ] 分母读取当前持久化 `HWMAX_OUTCUR`。
- [ ] 不使用 36V I-V current cap 代替 HWMAX。
- [ ] fallback 不得输出 0 代替默认调光。

## C. 校准电压与量程

正式电压节点：`25/29/32/36/40/44/48/52/56V`。

```text
CalibrationSpan = min(
  current HWMAX,
  selected-Vout I-V limit,
  Product Hardware Max,
  explicit safety limit
)
```

- [ ] 上位机计算。
- [ ] 固件 BEGIN 独立重算。
- [ ] 两边不一致直接拒绝。
- [ ] 六种 Product Profile 都使用同一逻辑。

## D. 11 点输出真实校准

正式点：`0/10/.../100%`，协议 Level=`0/20/.../200`。

- [ ] 0% 做安全零点门禁。
- [ ] 10%～100% TargetCurrent 由 CalibrationSpan 计算。
- [ ] 外部标准仪表输出电流是唯一物理真值。
- [ ] 每个目标点允许在 Calibration Session 内受保护地修正 `logicalPwm`。
- [ ] 数学预测只用于产生下一候选 PWM。
- [ ] 外部仪表未进入容差前不得接受该校准点。
- [ ] PWM 无余量、响应非单调、保护触发必须 FAIL。
- [ ] 最终 Output 表语义为 `TargetCurrent -> CorrectPWM`。

## E. 采样校准

- [ ] OCO：`Raw -> External Iout`。
- [ ] BL Voltage：现有 V3 GainQ24 结构。
- [ ] BL Current：`Raw -> External Input Current`。
- [ ] BL Power：`Raw -> External Active Power`。
- [ ] 未校准设备自身读数不得作为 Reference Truth。
- [ ] APPLY 后必须读取 Corrected 值重新和外部标准仪表比较。

## F. 244B / 协议

- [ ] 保持 11 点。
- [ ] 保持 V3 244B payload 总长度。
- [ ] 不偷偷复用 Reserved 改字段语义。
- [ ] 协议变化需要明确版本升级。

## G. STAGE / APPLY / VERIFY / COMMIT

```text
BUILD -> STAGE -> APPLY -> VERIFY -> COMMIT -> READ -> RELEASE
```

- [ ] BUILD 成功不等于校准成功。
- [ ] APPLY 后独立复测输出和采样。
- [ ] 任一关键通道超差禁止 COMMIT。
- [ ] COMMIT 后 READ 校验 generation / CRC / 244B byte compare。

## H. Runtime

```text
TargetCurrent = SET_OUTCUR * Brightness / 100
```

- [ ] 曲线内：Current -> CorrectPWM 插值。
- [ ] 曲线外：禁止 extrapolation，fallback 历史默认公式。
- [ ] 修改 SET/HWMAX 后，只根据新 Target 是否在覆盖范围判断是否继续用曲线。

## I. 多功率

- [ ] 50W / 75W / 100W / 150W / 200W / 240W 均有独立 Keil Target/Profile。
- [ ] 公共 calibration/service/PWM/MQTT 代码不复制六份。
- [ ] 上位机使用同一 Runner + Product Registry。
- [ ] 不在通用算法里写死任何一个型号的 SET/HWMAX/I-V 数值。

## J. HIL

- [ ] 至少对每个功率段完成目标电压校准验证。
- [ ] 至少选择低/中/高 Vout 做跨电压验证。
- [ ] 若同一 Current 在不同 Vout 所需 PWM 明显变化，停止使用单一一维曲线，升级电压补偿或多曲线方案。
