# CAT1 50W 文档口径说明

> 生效日期：2026-08-21  
> 分支：`main`  
> 状态：`CURRENT_CODE_V2 / TARGET_V3_FIELD_CONTRACT_FROZEN / IMPLEMENTATION_PENDING`

## 1. 当前唯一权威文档

后续实现和审核只以以下三份文档为目标规范：

1. 固件：`docs/CAT1_50W校准固件基线与上位机对接方案.md`
2. 上位机：`L12ichang/tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`
3. 联合字段合同与审核：`docs/CAT1_50W固件与上位机联合审核清单.md`

其中第3份是 **V3 Wire / Payload / Storage / Golden Vector 唯一字段级真源**。如果三份文档发生字段级冲突，以第3份为准并同步修正前两份，禁止由Codex自行选择。

旧文档、旧fixture和旧源码只用于理解V2现状，不得覆盖以上目标规范。

## 2. 当前代码状态

```text
CAT1_Keil_Project 当前代码 = V2
tc-desktop-client 当前代码 = V2
V3功能代码                  = 尚未实现
当前工作目标                = 将两端从V2完整升级到V3
```

当前固件 `SYS_CALIBRATION_MQTT_PROTOCOL_VERSION=2`、上位机 `calibration-mqtt-v2.ts`、旧198B Payload、旧Context、旧Storage formatVersion=3都属于V2现状。

**文档冻结不等于代码已实现。** 后续必须用真实代码、Keil编译、单测和HIL证明V3完成。

## 3. V3版本空间

```text
Wire Protocol              = Calibration MQTT Protocol V3
Calibration PayloadVersion = 1
Calibration StorageFormat  = 4
```

三者是不同版本空间，不得混淆。

V2旧Storage `formatVersion=3` 不等于 MQTT V3。

## 4. 已冻结的核心设计

### 产品参数

```text
50W Hardware Max       = 1680mA
50W Default HWMAX      = 1400mA
50W Default SET_OUTCUR = 893mA
RS3                    = 120mΩ
11 points              = Level 0/20/.../200
logicalPwm             = 0..1000
```

### 参数职责

- SET_OUTCUR = User Config；Wire暂保留 `Factory.SET_OUTCUR`兼容入口；
- HWMAX = Factory Config；
- Hardware Max = Product Profile固定能力；
- CV = 上位机电子负载本次工况；
- Tolerance = 上位机APPLY后验收策略；
- Calibration不绑定SET/CV/当前HWMAX/calibratedMax。

### Protocol Operation

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

V3使用数字Operation。

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

### Wire State

```text
0 IDLE
1 ACTIVE
2 STAGED
3 APPLIED
4 FAULT
```

无长期COMMITTED/ABORTED状态。

### Target Calibration Payload

```text
Magic          = CALP
PayloadVersion = 1
PayloadLength  = 244B
Endian         = Little Endian
ValidFlags     = 0x001F
CRC            = CRC-32/ISO-HDLC，覆盖完整244B
```

完整Byte Layout以联合审核清单第5节为准。

### RAW

同包返回Raw + Corrected；`vf/flt`逐bit已经冻结，详见联合审核清单第7节。

### READ_CHUNK

```text
Max Chunk = 128B
标准244B回读 = 128B + 116B
```

### Calibration Flash Record

```text
Magic         = CAL4
StorageFormat = 4
RecordLength  = 272B
PayloadLength = 244B
Endian        = Little Endian
CommitWord    = 0xC0A17EED
```

Flash Record由固件生成；STAGE只传Calibration Payload。

### BL0942 Voltage

第一版固定：上位机计算Q24 Gain并对有效点取中位数；固件使用定点乘法应用；量产前做多输入电压HIL验证。

### Golden Vector

联合审核清单已经冻结：

- CRC标准向量；
- Little Endian向量；
- 244B Payload codec向量；
- 272B Flash Record codec向量。

## 5. 明确废弃的旧口径

以下不得继续作为目标设计：

- SET_OUTCUR=890mA；
- HWMAX与Hardware Max合并；
- CAL_MQTT_V2作为最终协议；
- Calibration绑定SET/CV/calibratedMax；
- 无Calibration禁止正常输出；
- 设备CAP返回多型号profilesCsv；
- 字符串Operation作为V3 Wire；
- COMMITTED作为长期Wire State；
- “Calibration Record V2 / 312B / Q20+Offset”；
- 上位机发送完整Flash Record；
- 校准前按最终Tolerance直接FAIL；
- BL0942用周期Reset保活；
- 让Codex在开发阶段继续自由设计Payload/CRC/State/Storage。

## 6. 设计P0状态

此前需要冻结的字段设计项已经全部关闭：

```text
Target Calibration Payload精确Byte布局  DONE
Endian                                  DONE
Payload CRC                             DONE
RAW vf bit                              DONE
RAW flt bit                             DONE
每个Operation Request/Response          DONE
READ_CHUNK大小                          DONE
最终Flash Record格式                    DONE
Golden Vector                           DONE
```

**当前不存在协议设计待定P0。**

后续若出现问题，只按以下两类处理：

1. `IMPLEMENTATION_MISMATCH`：代码没有按已冻结文档实现，修改代码；
2. `HIL_SPEC_DEFECT`：真实硬件/容量测试证明冻结规范不可实现，提供证据后显式升级规范版本。

禁止把普通编码困难、旧V2测试冲突或个人偏好重新包装成新的“P0设计问题”。