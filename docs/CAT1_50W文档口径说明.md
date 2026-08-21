# CAT1 50W 文档口径说明

> 生效日期：2026-08-21  
> 当前规范分支：`main`  
> 状态：`V2_CURRENT / V3_TARGET / P0_PARTIALLY_FROZEN`

## 1. 当前代码状态必须统一表述

截至本文更新：

```text
CAT1_Keil_Project 当前代码 = V2
 tc-desktop-client 当前代码 = V2
V3 功能实现                  = 尚未开始/尚未形成有效代码提交
当前开发目标                 = V2 -> V3
```

不得把“已经写入V3设计文档”描述成“代码已经实现V3”。

当前源码中的 `CAL_MQTT_V2`、V2 Profile Context、198B旧表、旧Storage Record等仍是真实现现状，后续要按V3文档逐项替换。

## 2. 当前三份权威目标文档

后续实现只以：

1. 固件：`CAT1_Keil_Project/docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合审核：`CAT1_Keil_Project/docs/CAT1_50W固件与上位机联合审核清单.md`

作为目标规范。

旧文档、旧fixture、旧测试只用于理解V2现状和迁移，不能覆盖上述V3目标。

## 3. 版本命名

### 当前 Legacy Wire

```text
CAL_MQTT_V2
```

### 目标 Wire

```text
Calibration MQTT Protocol V3
```

### 当前旧 Storage

源码：

```text
SYS_CALIBRATION_STORAGE_FORMAT_VERSION = 3
```

它只是V2时代现有Storage实现版本，与MQTT Protocol V3不是同一版本序列。

### 新 Calibration 存储

逐Byte格式冻结前统一称：

```text
Target Calibration Record
```

目前禁止使用以下未确认旧叫法作为目标：

```text
Calibration Record V2
FormatVersion=2
CAL2
312B固定长度
```

新 Storage `formatVersion` 只有在 Header/Offset/Endian/CRC/Golden Vector 全部冻结后才能确定。

## 4. 已冻结 V3 P0

### Operation

正式使用数字 Operation Code：

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

示例：

```json
{"v":3,"o":3,"s":123456,"q":8,"lv":100}
```

### SET_OUTCUR

采用兼容方案A：

```text
Wire仍兼容 Factory.SET_OUTCUR
内部正式归属 User Config
```

本轮不强制新增 `User.SET_OUTCUR` Wire。

### RAW V3

同一RAW响应提供：

```text
Raw + Corrected + Freshness/Fault
```

FITTING只用Raw；APPLY后VERIFY使用Corrected+Reference。

### PWM

```text
协议只操作Level/Verification Percent
Logical PWM = 0..1000
正式点 logicalPwm = level * 5
ACK pwm不是TIM CCR
```

无Calibration Legacy Path保留OP_PWM_OFFSET；SET_POINT和calibrated path不叠加OP_PWM_OFFSET。

### Wire State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

不增加COMMITTED/ABORTED长期Wire State。

### BL0942 Voltage

第一版：

```text
上位机：多个有效点 -> Q24 Gain -> median
固件：整数定点应用Q24 Gain
```

不默认加入Offset；实机多输入电压点验证不达标时重新评审。

## 5. 50W 参数

```text
Hardware Max       = 1680mA
Default HWMAX      = 1400mA
Default SET_OUTCUR = 893mA
RS3                = 120mΩ
Formal Points      = 11
Level              = 0/20/.../200
```

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

## 6. 明确废弃的V2业务口径

不得继续作为V3目标：

- 默认SET=890mA；
- HWMAX与Hardware Max合并；
- V2作为最终Wire；
- Calibration绑定运行SET；
- Calibration绑定运行输出电压；
- `calibratedMaxCurrentMa`作为普通运行授权；
- CAP返回多功率`profilesCsv`；
- 无Calibration禁止普通非零输出；
- 21点校准；
- 校准前使用最终Tolerance直接FAIL；
- V2大Context/Status每个ACK重复返回；
- V2完整READBACK一次发送大Calibration内容。

## 7. 当前剩余P0

以下仍必须在Codex全面实现前继续冻结：

1. `rc` Result Code 精确数字表；
2. RAW `vf` / `flt` bit定义；
3. Target Calibration Record：STAGE Payload还是完整Flash Record；
4. Target Calibration Record Header/Offset/Size/Endian；
5. 新 Storage `formatVersion`；
6. CRC覆盖范围与事务字段所有权；
7. Golden Vector；
8. STAGE/READ_CHUNK最大分块长度。

这些内容未冻结前，Codex不得自行拍板。
