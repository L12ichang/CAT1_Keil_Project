# 历史文档：一体化电源电流校准 Codex 执行文档（已废弃）

> **状态：LEGACY / DO NOT EXECUTE**

本文件曾包含 21 点校准、旧 MID=4 补偿、旧 PWM 假设和早期 MQTT 设计。为避免与当前 11 点 / Protocol V3 / 50W 新参数体系冲突，原执行内容已移除。

后续任何 Codex 开发不得从本文件推导字段、算法、Flash 地址或校准状态机。

## 当前唯一实施依据

1. `docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. `L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. `docs/CAT1_50W固件与上位机联合审核清单.md`

## 当前核心口径

```text
50W Hardware Max       = 1680mA
50W Default HWMAX      = 1400mA
50W Default SET_OUTCUR = 893mA
RS3                    = 120mΩ
Calibration Points     = 11
Level                  = 0/20/.../200
Target MQTT            = Calibration MQTT Protocol V3
```

Calibration 不绑定运行 SET_OUTCUR，不绑定运行输出电压；无 Calibration 时继续使用原默认 PWM + OP_PWM_OFFSET 正常运行。

本文件仅保留历史文件名，不能作为当前实现入口。
