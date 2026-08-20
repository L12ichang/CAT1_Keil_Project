# CAT1 50W 固件与上位机——联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 固件基线分支：`done/cat1-product-profile-cal-context-20260817`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 上位机基线分支：`main`  
> 文档定位：**最终联合审核唯一清单**

---

## 1. 使用方式

这份文档不指导某一边单独开发，而用于两边代码修改完成后的最终核对。

审核时必须同时读取：

1. 固件实施基线；
2. 上位机实施方案；
3. 固件最新代码；
4. 上位机最新代码；
5. Protocol V3 Fixtures；
6. Keil Build / MAP / BIN；
7. 上位机单元测试；
8. HIL / 实机校准记录。

任何一个关键字段、单位、算法、状态机或 CRC 定义不一致，都不能判定“校准功能完成”。

---

## 2. 三份文档职责

### A. 固件实施文档

`CAT1_50W校准固件基线与上位机对接方案.md`

回答：

- 固件参数如何分层；
- PWM如何运行；
- 11点如何输出；
- Raw/Corrected如何分流；
- BL0942如何修复与校准；
- Flash如何A/B；
- MQTT V3固件侧如何实现；
- 哪些其他功能不能误改。

### B. 上位机实施文档

`tc-desktop-client/docs/CAT1_50W校准上位机修改实施方案.md`

回答：

- 11点怎么采；
- 仪器怎么配合；
- Raw/Reference怎么保存；
- Calibration怎么生成；
- Apply后怎么验证；
- MQTT V3怎么解析；
- Audit/UI如何调整。

### C. 本文档

回答：

> 两边最终是不是实现了同一套系统。

---

## 3. 50W 参数一致性

必须完全一致：

| 参数 | 固件 | 上位机 | 结果 |
|---|---:|---:|---|
| Rated Power | 50W | 50W | [ ] |
| Hardware Max | 1680mA | 1680mA | [ ] |
| Default HWMAX | 1400mA | 1400mA | [ ] |
| Default SET_OUTCUR | 893mA | 893mA | [ ] |
| RS3 | 120mΩ | 120mΩ | [ ] |
| Formal Points | 11 | 11 | [ ] |
| Level Step | 20 | 20 | [ ] |

必须确认：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

- [ ] 普通 SET_OUTCUR 写入受 HWMAX 限制；
- [ ] Factory HWMAX 写入受 Hardware Max=1680mA 限制；
- [ ] Hardware Max 不可通过普通配置改变；
- [ ] 50W 固件不包含多功率 Catalog；
- [ ] 上位机量产入口第一版只启用 50W。

---

## 4. 参数职责一致性

### Product Profile

- [ ] Hardware Max 在固件 Product Profile；
- [ ] RS3 在 Product Profile；
- [ ] MID/Model/Rated Power 在 Product Profile；
- [ ] 上位机只用于识别/验证，不把 Product Profile 当可写运行参数。

### Factory Config

- [ ] HWMAX 属于 Factory Config；
- [ ] OP_PWM_OFFSET 属于旧无校准补偿/必要工厂配置；
- [ ] 普通用户路径不写 HWMAX；
- [ ] 第一版 Factory 命令不做身份认证，但固件做数值合法性检查。

### User Config

- [ ] SET_OUTCUR 属于 User Config；
- [ ] SET_OUTCUR 不再被叫作“校准额定电流授权”；
- [ ] 上位机校准前不强制改 SET_OUTCUR；
- [ ] OTA 后有效 SET_OUTCUR 保留。

---

## 5. 输出电压解绑审核

必须确认固件和上位机都不存在以下门禁：

```text
运行Vo != 校准Vo => Calibration失效
运行Vo != 规定Vo => PWM=0
```

检查：

- [ ] `BOUND_OUTPUT_VOLTAGE_01V` 不再作为正常业务配置；
- [ ] Calibration Context 不绑定运行输出电压；
- [ ] calibrationVoltage 仅属于上位机测试工况/Metadata；
- [ ] 36V/56V不同工况都可以执行校准；
- [ ] 运行电压变化不使已保存 Calibration 失效。

---

## 6. 无 Calibration 正常运行

- [ ] 空白/无校准设备可以非零输出；
- [ ] 无校准输出使用旧默认 PWM 模型；
- [ ] 无校准继续使用 OP_PWM_OFFSET 光耦补偿；
- [ ] 缺失/损坏/不兼容 Calibration 只回退 Default，不阻断正常业务；
- [ ] 老设备 OTA 后无新 Calibration 仍正常运行。

---

## 7. 有 Calibration 输出

- [ ] `Target Current = SET_OUTCUR × Brightness`；
- [ ] 有 Output Calibration 时使用 11 点反插值；
- [ ] 直接得到高精度 u16 PWM；
- [ ] 不进行整数百分比往返量化；
- [ ] 有有效 Calibration 后不再叠加 OP_PWM_OFFSET；
- [ ] 修改 SET_OUTCUR 后 Calibration 继续有效；
- [ ] Calibration 范围外合法目标回退 Default，不 PWM=0。

---

## 8. 11点 SET_POINT 审核

固定：

```text
0,20,40,60,80,100,120,140,160,180,200
```

- [ ] 上位机正式点也是 0~100% 每10%；
- [ ] 固件 SET_POINT 将 Level 转换为已知原始逻辑 PWM；
- [ ] SET_POINT 不应用旧 Output Calibration；
- [ ] SET_POINT 不叠加 OP_PWM_OFFSET；
- [ ] SET_POINT 仍受过流/过温/短路等硬件保护；
- [ ] 上位机保存固件实际返回的 PWM，而不是只相信理论百分比。

---

## 9. 校准前/校准后判定审核

### 校准前11点

- [ ] 不要求设备已经满足最终 ±1%/±2%；
- [ ] 允许较大误差作为 Calibration Evidence；
- [ ] 只检查数据有效、稳定、单调、无硬件 Fault、Raw Fresh；
- [ ] 超过硬件安全边界才中止。

### APPLY后验证

- [ ] 最终 Accuracy PASS/FAIL 在 APPLY 后执行；
- [ ] Quick Validation 点与正式11点不同；
- [ ] Full Validation 点与正式11点不同；
- [ ] 主要工作区目标约 ±1%；
- [ ] 低输出区容差按正式产品规范执行；
- [ ] 验证失败执行 ABORT + SafeOff。

---

## 10. Output Calibration 数据一致性

双方必须一致定义：

```text
Actual PWM <-> Reference Output Current
```

审核：

- [ ] 点数量一致；
- [ ] PWM单位一致；
- [ ] Current单位为 mA；
- [ ] Byte Order一致；
- [ ] 插值方向一致；
- [ ] 单调性检查一致；
- [ ] 范围外策略一致。

---

## 11. OCO Calibration 数据一致性

统一关系：

```text
OCO ADC Raw <-> Reference Output Current(mA)
```

审核：

- [ ] RAW字段是真正未校准 ADC；
- [ ] 上位机不使用已Corrected值拟合；
- [ ] 固件 Corrected 值用于业务/MQTT；
- [ ] 固件 Protection 使用 Raw/保守换算；
- [ ] Calibration 不会掩盖真实过流。

---

## 12. BL0942 Calibration 数据一致性

### Input Current

```text
BL Raw Current <-> Reference Input Current
```

### Input Power

```text
BL Raw Power <-> Reference Active Power
```

### Input Voltage

```text
多个有效负载点 BL Uncal Voltage / Reference Voltage
→ Voltage Gain/Correction
```

审核：

- [ ] Raw字段语义、单位完全一致；
- [ ] 上位机和固件没有把旧 `inputCurrentAd` 混作OCO/BL不同含义；
- [ ] Voltage不误做11个负载点的“11段电压量程”；
- [ ] 无 BL Calibration 时回退默认换算。

---

## 13. BL0942 Freshness/稳定性审核

- [ ] 有 `last_valid_frame_tick`；
- [ ] 有 `dataAge`；
- [ ] 有 fresh/stale；
- [ ] 上位机 stale 时不采该点；
- [ ] MQTT业务不把旧值永久当实时值；
- [ ] ORE/FE/NE/HAL状态有诊断；
- [ ] HAL API返回值检查；
- [ ] RX rearm和timeout状态同步；
- [ ] 长时间Soak通过；
- [ ] 没有周期性 Reset BL0942 状态机；
- [ ] 如果存在有限恢复，有错误分类和恢复成功验证。

---

## 14. Protocol V3 Wire Contract

### 外层 Envelope

- [ ] `SN/TM/SV/ID/CT/DT` 保持中科协议兼容；
- [ ] `SV=cal`；
- [ ] 只压缩 `DT`；
- [ ] 普通平台协议不受影响。

### 公共字段

必须逐项核对名称、类型、范围：

| Wire | 类型 | 含义 | 固件 | 上位机 |
|---|---|---|---|---|
| `v` | u8 | Protocol Version | [ ] | [ ] |
| `o` | u8 | Operation Code | [ ] | [ ] |
| `s` | u32 | Session ID | [ ] | [ ] |
| `q` | u32 | Sequence | [ ] | [ ] |
| `rc` | u8/u16 | Result Code | [ ] | [ ] |
| `st` | u8 | State | [ ] | [ ] |
| `lv` | u16 | Level | [ ] | [ ] |
| `pwm` | u16 | Actual PWM | [ ] | [ ] |
| `gen` | u32 | Generation | [ ] | [ ] |
| `len` | u16 | Record Length | [ ] | [ ] |
| `crc` | u32 | CRC32 | [ ] | [ ] |

---

## 15. Operation Code 唯一表

实现前必须最终冻结，审核时两边逐个比对。当前建议：

| Code | Operation |
|---:|---|
| 0 | CAP |
| 1 | BEGIN |
| 2 | HEARTBEAT |
| 3 | SET_POINT |
| 4 | RAW |
| 5 | STAGE |
| 6 | APPLY |
| 7 | SET_VERIFY |
| 8 | COMMIT |
| 9 | READ_INFO |
| 10 | READ_CHUNK |
| 11 | ABORT |
| 12 | RELEASE |
| 13 | DIAG |

审核：

- [ ] 固件 enum 与此完全一致；
- [ ] TypeScript enum/map 与此完全一致；
- [ ] Fixture覆盖每个操作；
- [ ] 未知 Code 返回 PROTOCOL_ERROR；
- [ ] 读写 CT 方向一致。

如开发阶段变更此表，必须同步更新两边和本文档，不允许单边自行调整。

---

## 16. CAP 一致性

CAP 至少应表达：

- Protocol Version；
- Calibration Format Version；
- Profile ID/Fingerprint；
- Hardware Max；
- 当前 HWMAX；
- 当前 SET_OUTCUR；
- Point Count=11；
- Level Step=20；
- Feature Bitmask；
- Calibration Generation；
- Flash/Commit Ready；
- 必要 Safety/Fault状态。

审核：

- [ ] 无 profilesCsv；
- [ ] 无 75/100/... Catalog；
- [ ] 无 boundVoltage运行门禁；
- [ ] 无 calibratedMax授权门禁；
- [ ] Feature bit定义两边一致。

---

## 17. RAW V3 Schema 一致性

正式拟合 RAW 至少核对：

| 数据 | 单位/类型 | 固件 | 上位机 |
|---|---|---|---|
| Level | u16 | [ ] | [ ] |
| Actual PWM | u16 | [ ] | [ ] |
| OCO Raw | integer | [ ] | [ ] |
| BL Voltage Raw | integer | [ ] | [ ] |
| BL Current Raw | integer | [ ] | [ ] |
| BL Power Raw | integer | [ ] | [ ] |
| Output Voltage | 0.1V或冻结单位 | [ ] | [ ] |
| Data Age | ms | [ ] | [ ] |
| Valid/Fresh Flags | bitmask | [ ] | [ ] |
| Hardware Fault Flags | bitmask | [ ] | [ ] |

- [ ] RAW 不依赖旧 V2 `schemaVersion/available/adc` 嵌套；
- [ ] RAW 不混入大量诊断计数；
- [ ] DIAG与RAW职责分开。

---

## 18. Calibration Record V2 字节级一致性

最终实现前必须另行在代码/fixture中冻结确切 Offset。

审核必须输出：

```text
总长度
Magic
Version
Generation位置
Profile ID位置
Valid Flags位置
每个Output点offset
每个OCO点offset
每个BL Current点offset
每个BL Power点offset
Voltage参数offset
CRC覆盖范围
Commit覆盖规则
Endian
```

检查：

- [ ] TypeScript Encode与C Decode对同一Fixture字节完全一致；
- [ ] CRC32算法一致；
- [ ] Endian一致；
- [ ] `len`一致；
- [ ] Version一致；
- [ ] 无 SET_OUTCUR / 运行Vo授权绑定。

---

## 19. Stage / Apply / Commit 一致性

### STAGE

- [ ] 上位机生成完整Record；
- [ ] MCU先放RAM Stage；
- [ ] STAGE不破坏旧Flash Calibration；
- [ ] len/crc相同。

### APPLY

- [ ] 临时启用Stage Calibration；
- [ ] 旧Flash Calibration仍保留；
- [ ] ABORT能恢复旧状态。

### COMMIT

- [ ] 只有验证PASS才Commit；
- [ ] 写非活动Calibration页；
- [ ] CRC/Readback成功；
- [ ] Commit Marker最后；
- [ ] Generation增加；
- [ ] 掉电中断旧页仍有效。

---

## 20. READ_INFO / READ_CHUNK 一致性

READ_INFO：

- [ ] Generation；
- [ ] Length；
- [ ] CRC；
- [ ] Valid Flags；
- [ ] Profile ID/Format Version（按最终协议）。

READ_CHUNK：

- [ ] offset定义一致；
- [ ] chunk length上限一致；
- [ ] Hex/字节编码一致；
- [ ] 上位机能拼回完整Record；
- [ ] 拼回后CRC和本地生成Record一致；
- [ ] 单包不会逼近2KiB TX。

---

## 21. JSON/TX/RX 大小审核

当前固件约束：

```text
ZK_JSON_BUF_SIZE = 2048B
ZK_JSON_RX_MAX = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
ZK_CJSON_RX_POOL_SIZE = 4096B
```

最终建议门槛：

| 报文 | 目标 |
|---|---:|
| ACK | <256B |
| RAW | <512B |
| CAP | <768B |
| READ_CHUNK | <768B |
| 任意设备TX | <1536B |
| 任意设备RX | <1800B，给解析保留余量 |

审核：

- [ ] Fixture计算UTF-8 byteLength；
- [ ] 固件实测无 `TX Pool Exhausted`；
- [ ] cJSON TX pool不是临界4096B通过；
- [ ] `cJSON_PrintPreallocated`不失败；
- [ ] Stage若过大已采用Chunk；
- [ ] 不通过单纯增大TX Pool掩盖协议膨胀。

---

## 22. 上位机状态机与固件状态机对应

| 上位机 | 固件 | 检查 |
|---|---|---|
| PRECHECK | IDLE | [ ] |
| READY/BEGIN | ACTIVE | [ ] |
| COLLECTING | ACTIVE | [ ] |
| FITTING | ACTIVE | [ ] |
| STAGING | STAGED | [ ] |
| APPLIED | APPLIED | [ ] |
| VERIFYING | APPLIED | [ ] |
| COMMITTING | COMMITTING/COMMITTED | [ ] |
| READBACK | COMMITTED | [ ] |
| RELEASE | IDLE | [ ] |
| FAILED/ABORT | ABORTED/IDLE | [ ] |

- [ ] Lease超时行为一致；
- [ ] seq幂等重试一致；
- [ ] 重复请求不会重复写Flash；
- [ ] MQTT Publish Busy时上位机重试不会造成状态错乱。

---

## 23. 上位机仪器与安全审核

- [ ] 校准前 Electronic Load INPUT OFF确认；
- [ ] CV工况由上位机设置；
- [ ] SET_POINT后再开启/保持正确Load状态；
- [ ] 每点有稳定性判断；
- [ ] DC5200数据有效；
- [ ] SCPI Load数据有效；
- [ ] 任意异常最终 safeOff；
- [ ] Cancel最终 safeOff；
- [ ] 应用退出时 safeOff；
- [ ] MCU Fault时上位机立即停止当前点。

---

## 24. Audit/生产证据审核

`audit.jsonl` 至少能追踪：

- [ ] Device ID/SN/IMEI；
- [ ] FW Version；
- [ ] Profile/Fingerprint；
- [ ] Hardware Max/HWMAX/SET；
- [ ] Protocol/Format Version；
- [ ] 11点 Raw；
- [ ] 11点 Reference；
- [ ] 稳定窗口；
- [ ] Calibration前误差；
- [ ] 生成Record CRC；
- [ ] APPLY后验证；
- [ ] Commit Generation；
- [ ] Final CRC；
- [ ] PASS/FAIL；
- [ ] 失败原因。

---

## 25. Flash/OTA 联合审核

- [ ] Config A/B 2KB页独占；
- [ ] Calibration A/B 2KB页独占；
- [ ] Runtime A/B 2KB页独占；
- [ ] 修改Config不会擦Calibration；
- [ ] 修改Calibration不会擦Config；
- [ ] 老sys_data/property/plan迁移正确；
- [ ] SET_OUTCUR OTA保留；
- [ ] 合法HWMAX迁移正确；
- [ ] 无Calibration OTA后正常运行；
- [ ] 旧 `0x0801E000~0x08020000` 不再作为新写区；
- [ ] Boot/APP/OTA边界未变化。

---

## 26. 普通业务回归审核

必须确认校准修改没有破坏：

- [ ] 4G登录；
- [ ] 心跳；
- [ ] 普通MQTT属性读写；
- [ ] 调光；
- [ ] 周期上报；
- [ ] 巡检；
- [ ] 告警；
- [ ] RTC校时；
- [ ] 计划任务；
- [ ] OTA下载/校验/升级/成功上报；
- [ ] 重启逻辑；
- [ ] 正常BL0942业务电参。

---

## 27. 测试矩阵

### A. 基础

- [ ] Blank Flash → Default HWMAX=1400 / SET=893；
- [ ] 无Calibration正常输出；
- [ ] SET修改/重启保持；
- [ ] SET>HWMAX拒绝；
- [ ] HWMAX>1680拒绝。

### B. Output Calibration

- [ ] 11点采集；
- [ ] 校准前大误差可采集；
- [ ] Apply后精度改善；
- [ ] SET修改后Calibration仍生效；
- [ ] Calibration范围外Default fallback。

### C. OCO

- [ ] Raw保护；
- [ ] Corrected MQTT；
- [ ] Calibration错误不能掩盖真实过流。

### D. BL0942

- [ ] U/I/P Calibration；
- [ ] ORE注入；
- [ ] HAL_BUSY/ERROR；
- [ ] 丢帧/截断；
- [ ] stale；
- [ ] 小时/天Soak；
- [ ] 无周期Reset。

### E. Flash

- [ ] Config写入各阶段断电；
- [ ] Calibration写入各阶段断电；
- [ ] Runtime断电；
- [ ] Migration断电重启；
- [ ] Generation交替。

### F. Protocol

- [ ] 每个Operation Fixture；
- [ ] Wrong Version；
- [ ] Wrong Operation；
- [ ] Wrong seq；
- [ ] Duplicate seq；
- [ ] Wrong CRC；
- [ ] Wrong Length；
- [ ] Chunk边界；
- [ ] 消息大小预算。

### G. End-to-End

- [ ] 上位机选择设备；
- [ ] CAP；
- [ ] BEGIN；
- [ ] 11点；
- [ ] FIT；
- [ ] STAGE；
- [ ] APPLY；
- [ ] VERIFY；
- [ ] COMMIT；
- [ ] READ_INFO/CHUNK；
- [ ] RELEASE；
- [ ] audit.jsonl PASS记录。

---

## 28. 最终审核输出格式

最终审核报告必须明确给出：

### PASS

- 哪些条目已通过；
- 对应固件文件/函数；
- 对应上位机文件/函数；
- 对应Fixture/HIL证据。

### FAIL

每个失败项必须写：

```text
Issue ID
问题描述
固件位置
上位机位置
协议/算法预期
实际实现
风险
修改建议
阻塞等级 P0/P1/P2
```

### 最终结论

只能是：

- `PASS - 可进入量产验证`
- `CONDITIONAL PASS - 仅允许继续HIL，不允许量产`
- `FAIL - 协议/算法/安全未闭环`

不能因为“能跑通一次”就判定完成。

---

## 29. 最终原则

> 两边只有一份协议，不允许固件和上位机各自维护“差不多”的字段。

> Raw、Reference、Corrected 三类数据必须语义唯一。

> Calibration 只做精度修正，不做正常输出授权。

> 校准前收集误差，校准后才做精度 PASS/FAIL。

> MQTT V3 必须通过协议瘦身解决 4KiB cJSON TX Pool 与 2KiB JSON Buffer 压力，而不是单纯扩大内存。

> BL0942 先稳定再校准，禁止用周期性 Reset 掩盖根因。

> 最终是否完成，以本联合审核清单逐条通过为准。
