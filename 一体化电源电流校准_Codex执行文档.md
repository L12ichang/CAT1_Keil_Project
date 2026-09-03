# 一体化电源校准 Codex 执行入口（V3）

> 本文件只保留根目录入口，旧 21 点、旧补偿、旧功率专项和旧协议内容已移除。任何新的 Codex 校准任务必须先读取当前分支源码和 `docs/V3六型号真实校准最终设计.md`。

## 当前冻结算法

```text
Output
  11点 0/10/.../100% 实测
  -> PWM -> External Actual Iout
  -> inverse PWL
  -> TargetCurrent -> CorrectPWM

OCO Sampling
  0% Offset
  + 90% Gain Anchor
  -> Two-Point Linear
```

## 当前量产流程

```text
CAP / PRECHECK
-> BEGIN
-> BUILD 11点
   Settling Delay + 固定采样次数 + MEDIAN
-> FIT
-> STAGE
-> APPLY
-> QUICK/FULL VERIFY
   只记录误差，不以非零误差提前中止
-> COMMIT
-> READ / CRC / Byte Compare
-> RELEASE
```

## 当前边界

- 正式校准电压：36/40/44/48/52/56V，默认36V；
- `CalibrationSpan = min(CAP.HWMAX, I-V Limit, Hardware Max, safety envelope)`，SET不参与；
- 当前 SET/HWMAX 来自设备 CAP/Factory，Product Default只做参考；
- BUILD只保留安全和数据有效性门禁；
- 100%端点只判断物理覆盖，不判断精度容差；
- VERIFY误差进入0-30%、30-80%、80-100%报告；
- OCO wire 11点必须是0% Offset + 90% Gain同一直线的兼容展开；
- 244B V3 payload保持当前兼容格式；
- 不得绕过温度、电压、过流/过载、会话和SAFE OFF保护。

## Codex执行规则

1. 先搜索当前 `fix/v3-real-calibration-20260831` 源码，再修改；
2. 不恢复21点校准、旧亮度补偿或旧 V2 业务表；
3. 不把I-V Limit解释成Factory HWMAX；
4. 不把OCO重新实现成11点独立PWL；
5. 不把Tolerance/Peak-to-Peak/relative rate重新加成BUILD前置门禁；
6. 不因为QUICK/FULL某个非零点误差较大就提前停止整条验证；
7. 不虚构Keil、仪器、HIL或掉电测试结果。

完整设计只看：

```text
docs/V3六型号真实校准最终设计.md
```
