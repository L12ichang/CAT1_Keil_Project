# CAT1 50W 文档口径说明

> 生效日期：2026-08-21  
> 分支：`main`  
> 状态：`CURRENT_V2 / TARGET_V3 / FIELD_CONTRACT_FREEZING`

## 1. 当前唯一权威文档

后续设计、实现和审核只以以下三份文档为目标规范：

1. 固件：`docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合审核：`docs/CAT1_50W固件与上位机联合审核清单.md`

旧文档、旧 fixture 和旧源码只用于理解 V2 现状和迁移，不得反向覆盖以上三份目标规范。

## 2. 当前代码状态必须统一理解

```text
CAT1_Keil_Project 当前代码 = V2
tc-desktop-client 当前代码 = V2
V3功能代码                  = 尚未实现
当前工作目标                = 将两端从V2升级到V3
```

当前固件中的 `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION=2`、当前上位机的 `calibration-mqtt-v2.ts`、旧198B表、旧Context都属于真实现状，不代表目标设计。

任何文档中的 V3 内容当前都只是**待实现目标规范**。

## 3. 版本命名

目标 Wire Protocol：

```text
Calibration MQTT Protocol V3
```

旧 Wire Protocol：

```text
CAL_MQTT_V2
```

新 Flash 校准记录在最终 Header/Offset/CRC/Golden Vector 冻结前统一称为：

```text
Target Calibration Record
```

不得再用“Calibration Record V2”作为新目标名称。

当前源码 `SYS_CALIBRATION_STORAGE_FORMAT_VERSION=3` 只是旧 Storage Record 版本，与 MQTT Protocol V3 不是同一个版本空间。

## 4. 已冻结的 V3 P0

### 产品/运行参数

```text
50W Hardware Max       = 1680mA
50W Default HWMAX      = 1400mA
50W Default SET_OUTCUR = 893mA
RS3                    = 120mΩ
11 points              = Level 0/20/.../200
```

`SET_OUTCUR <= HWMAX <= Hardware Max`。

SET_OUTCUR 采用兼容方案 A：Wire 保留 `Factory.SET_OUTCUR`，固件内部迁移到 User Config。

### Operation Code

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

V3 使用数字 Operation，不用长字符串。

### RAW

同包返回 Raw + Corrected；拟合只用 Raw，APPLY 后验收使用 Corrected。

### PWM

协议只操作 Level；固件逻辑 PWM 域为 0..1000。上位机不写 CCR。OP_PWM_OFFSET 只保留在无 Calibration Legacy/Default Path，Calibration SET_POINT 和有效 Calibration 正常运行不重复叠加 Offset。

### Wire State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

不设置长期 COMMITTED/ABORTED 状态。

### BL0942 Voltage

第一版由上位机计算 Q24 Gain，各有效点 Gain 取中位数；固件只做定点应用。量产前必须多输入电压 HIL 验证 Gain-only 是否满足 Tolerance。

### Result Code

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

### STAGE / Storage 所有权

STAGE 只传 Target Calibration Payload；上位机不发送完整 Flash Record。

固件拥有：Generation、A/B Slot、Storage Header、Record CRC、Commit Marker 和掉电事务。

READ_INFO / READ_CHUNK 回读的是已提交 Calibration Payload，用于上位机 CRC/Byte Compare，不要求 MQTT 回传整个 Flash Record。

## 5. 明确废弃的旧口径

以下不得继续作为目标设计：

- SET_OUTCUR=890mA；
- HWMAX与Hardware Max合并；
- V2作为最终协议；
- Calibration绑定SET/CV/calibratedMax；
- 缺少Calibration禁止正常输出；
- 设备CAP返回多型号profilesCsv；
- 字符串Operation作为V3正式Wire；
- COMMITTED作为长期V3 Wire State；
- “Calibration Record V2 / 312B / Q20+Offset”作为目标格式；
- 上位机发送完整Flash Record；
- 校准前按最终±1%/±2%精度直接FAIL；
- BL0942用周期Reset掩盖通信冻结。

## 6. 当前仍需继续冻结的 P0

在允许 Codex 做完整 V3 跨端实现前，还需冻结：

1. Target Calibration Payload 逐Byte布局；
2. Endian；
3. Payload CRC算法与覆盖范围；
4. RAW `vf` bit表；
5. RAW `flt` bit表；
6. 每个Operation完整Request/Response必填字段；
7. READ_CHUNK最大长度；
8. Target Calibration Record最终Storage `formatVersion`、Header/CRC/Commit布局；
9. Golden Vector。

这些未冻结项必须先讨论，存在疑问先询问用户，禁止 Codex自行拍板。
