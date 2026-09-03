# Codex 量产校准固件任务书（历史索引）

> 状态：历史任务记录，不定义当前 V3 校准算法、工艺门禁、协议语义或量产判定。历史详细内容可从 Git 记录追溯，不应继续留在当前工作文档中干扰代码生成。

## 当前唯一实施依据

所有新的校准分析、代码修改、审核和 Codex 任务，必须先阅读：

```text
docs/V3六型号真实校准最终设计.md
```

上位机对应口径：

```text
tc-desktop-client/docs/V3六型号真实校准上位机实施.md
```

若历史文档、旧任务结果、旧分支说明与上述文件冲突，以上述两个 V3 当前文档为准。

## 当前冻结摘要

```text
Output Control
  = 11点真实物理采集
  -> inverse PWL
  -> TargetCurrent -> CorrectPWM

OCO Output Current Sampling
  = 0% Offset + 90% Gain
  -> Two-Point Linear

BUILD
  = Settling Delay + 固定采样次数 + MEDIAN
  -> 不做精度/波动百分比门禁

APPLY
  = staged曲线真实生效

VERIFY
  = QUICK/FULL完整测量
  -> 误差只记录和报告
  -> 非零误差不提前中止
```

正式量产校准电压：

```text
36 / 40 / 44 / 48 / 52 / 56 V
默认 36V
```

`CalibrationSpan` 使用当前设备 CAP.HWMAX、所选Vout的I-V Limit、Product Hardware Max和安全上界计算；SET不参与Span计算。

## 明确过期的历史口径

以下内容不得从历史任务重新带回当前实现：

- 50W-only 设计；
- 36V-only；
- `HWMAX == Product Default HWMAX`；
- 把36V I-V Limit当作Factory HWMAX；
- OCO 11点独立采样PWL；
- BUILD阶段用Peak-to-Peak、relative rate、Low/Main/High tolerance阻断收点；
- VERIFY任一非零误差超1%/2%/3%/5%立即FAIL；
- 复杂Tolerance/Fitting/Verification/Session工艺参数必须填写；
- 校准前要求未校准设备本身已经满足目标精度。

## Codex执行边界

新的实现任务必须：

1. 先核对当前分支源码，而不是按历史任务书猜实现；
2. 保持历史 SET/HWMAX 默认调光公式；
3. 保持244B V3 payload兼容；
4. 保持OCO wire 11点共线约束，算法语义为两点Offset+Gain；
5. BUILD只保留安全和数据可信性门禁；
6. APPLY后验证staged曲线；
7. QUICK/FULL误差生成完整报告，不因非零误差提前终止；
8. COMMIT/READ只由安全、曲线合法性、payload/CRC/Flash/readback等硬门禁约束；
9. 不虚构Keil、HIL、仪器或掉电测试结果。

## 实机最终证据

量产放行仍必须由真实硬件完成：

- Keil构建/烧录；
- MQTT CAP/BEGIN/STAGE/APPLY/COMMIT/READ；
- DC5200与电子负载测量；
- 36~56V代表工况；
- 0-30%、30-80%、80-100%输出误差和MCU采样误差；
- 掉电重启后的Flash持久化；
- 正常调光与保护回归。

历史 P/I/R 任务ID、50W专项任务和旧文档路径不再写入当前任务入口，避免再次成为代码生成噪音。
