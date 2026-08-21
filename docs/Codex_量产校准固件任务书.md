# 历史任务书：Codex 量产校准固件任务书（已废弃）

> **状态：LEGACY TASK / DO NOT EXECUTE**

本文件原任务编排建立在旧的 `量产校准固件开发方案.md`、旧 Profile Context、旧 Flash 候选布局和早期安全门禁之上。当前设计已经重新冻结，因此原任务书不得再直接交给 Codex 执行。

## 当前 Codex 开发必须读取

1. `docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. `L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. `docs/CAT1_50W固件与上位机联合审核清单.md`

如果旧任务书、旧测试或旧源码与以上文档冲突，以三份当前实施文档为准。

## 原任务书中仍可复用的工程纪律

以下过程纪律继续有效：

- 先审计再实现；
- 不在字段未确认时自行猜协议；
- 固件、上位机、审核分职责执行；
- 修改范围保持单一职责；
- 运行 Host Tests、Keil/MAP、真实 Broker、Flash掉电和HIL；
- 不用主机测试冒充实机结果；
- 提交前检查 diff 和构建证据；
- 不无理由重构 Boot/OTA/RTC/普通业务。

## 已废弃的任务输入

不得继续从原任务书推导：

- 50W默认890mA；
- HWMAX与Hardware Max合并；
- CAL_MQTT_V2为目标协议；
- 固定电压/SET/calibratedMax Context；
- 旧共享页Calibration A/B；
- 无Calibration禁止正常非零输出；
- 旧 tc 路径或旧上位机协议作为最终接口。

## 当前状态

新的 Codex 总执行任务必须等到联合文档把 Protocol V3 的精确 Wire Schema、Target Calibration Record 逐Byte格式、State/Result Code、PWM域和Golden Vector冻结后再生成。
