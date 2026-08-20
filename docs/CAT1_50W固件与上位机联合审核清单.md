# CAT1 50W 固件 × 多功率通用上位机——联合审核与最终验收清单

> 固件仓库：`L12ichang/CAT1_Keil_Project`  
> 固件基线分支：`done/cat1-product-profile-cal-context-20260817`  
> 上位机仓库：`L12ichang/tc-desktop-client`  
> 上位机基线分支：`main`  
> 本文定位：**两个项目修改完成后的最终一致性审核基线**

---

## 1. 审核定位

当前固件第一阶段只冻结 **50W**，但上位机必须保持 **多功率通用校准工作台**。

因此最终审核必须同时确认：

```text
50W 固件
    └─ 单产品、单固件、50W Product Profile

多功率上位机
    ├─ 50W
    ├─ 75W
    ├─ 100W
    ├─ 150W
    ├─ 200W
    └─ 240W
```

两者并不矛盾：

- 单个固件只描述自己对应的产品；
- 上位机本地维护多功率 Product Profile Registry；
- 上位机通过当前设备 CAPABILITIES 核对当前设备身份；
- 未来其他功率固件冻结后复用同一套上位机流程和 Protocol V3。

---

## 2. 第一类审核：禁止把上位机误改成50W专用

必须逐项确认：

- [ ] `PRODUCT_PROFILES` 或等价多功率注册结构仍存在；
- [ ] 50W / 75W / 100W / 150W / 200W / 240W UI产品卡片仍存在；
- [ ] 未冻结功率显示“待核对/未冻结”，而不是被删除；
- [ ] CalibrationRunner 没有硬编码“只允许50W”；
- [ ] 1680mA / 1400mA / 893mA 没有写死到通用Runner；
- [ ] 56V没有写死为所有功率唯一CV；
- [ ] ±1% / ±2%没有写死成所有功率唯一容差；
- [ ] 新增一个功率主要通过新增/冻结 Product Profile 即可支持；
- [ ] 单个设备CAP只返回自己的当前Product，不要求返回所有功率Catalog；
- [ ] 上位机不再依赖设备端 `profilesCsv` 来维持多功率能力。

任何一项失败，都说明上位机架构被错误收窄，不能通过审核。

---

## 3. 50W 当前冻结参数一致性

固件与上位机当前50W Profile必须一致：

| 参数 | 冻结值 |
|---|---:|
| Rated Power | 50W |
| MID | 1 |
| RS3 | 120mΩ |
| Hardware Max | **1680mA** |
| Default HWMAX | **1400mA** |
| Default SET_OUTCUR | **893mA** |
| Formal Points | 11 |
| Level | 0,20,...,200 |

审核：

- [ ] 固件50W Product Profile正确；
- [ ] 上位机50W Product Profile正确；
- [ ] 旧890mA已退出正式50W默认SET；
- [ ] 旧1200mA不再作为默认HWMAX；
- [ ] HWMAX与Hardware Max已拆成两个概念；
- [ ] `SET_OUTCUR <= HWMAX <= Hardware Max` 在两边语义一致。

---

## 4. Product Profile 与 Calibration Run Config 一致性

必须审核上位机已经把“产品资料”和“本次校准条件”拆开。

### Product Profile

用于描述产品固有能力：

- rated power；
- MID；
- RS3；
- Hardware Max；
- default HWMAX；
- default SET_OUTCUR；
- Profile Fingerprint；
- 产品默认Tolerance模板；
- 是否已冻结。

### Calibration Run Config

用于描述本次任务：

- selectedProfile；
- device IMEI/SN；
- `runSetOutcurMa`；
- `loadCvVoltageV`；
- `tolerancePolicy`；
- `stabilizationPolicy`；
- `validationMode`；
- instrument selection。

审核：

- [ ] 两类数据结构已逻辑分离；
- [ ] 本次CV不会修改Product Profile；
- [ ] 本次SET_OUTCUR不会进入Calibration有效性Context；
- [ ] 本次Tolerance不会改变固件硬件上限；
- [ ] Audit中能记录本次Run Config。

---

## 5. SET_OUTCUR 跨端一致性

正式定义：

> `SET_OUTCUR` 是设备 User Config 中的当前100%运行目标电流。

上位机允许操作员根据本次工艺选择SET_OUTCUR，而不强迫永远等于Profile默认值。

流程必须是：

```text
用户输入 runSetOutcurMa
→ 上位机读取当前 HWMAX
→ 检查 0 < SET <= HWMAX
→ 通过用户配置命令写入设备
→ 固件持久化 User Config
→ 上位机回读确认
→ 再进入Calibration
```

审核：

- [ ] SET_OUTCUR写入User Config；
- [ ] 不写入Calibration Context；
- [ ] 写入后有回读；
- [ ] 固件重启仍保持；
- [ ] OTA后仍保持；
- [ ] 校准完成后再次修改SET_OUTCUR，Calibration仍有效；
- [ ] SET > HWMAX被固件拒绝；
- [ ] 上位机也在发送前做前置检查。

---

## 6. HWMAX 与 Hardware Max 一致性

定义：

```text
SET_OUTCUR <= HWMAX <= Hardware Max
```

- Hardware Max：Product Profile固定硬件真实上限；
- HWMAX：Factory Config，允许用户调整SET的最大范围。

审核：

- [ ] Hardware Max不能被普通远程配置改变；
- [ ] HWMAX由Factory开发命令修改；
- [ ] 第一版无需人员身份认证；
- [ ] 但固件仍检查 `HWMAX <= Hardware Max`；
- [ ] 50W HWMAX默认1400mA；
- [ ] 50W Hardware Max固定1680mA。

---

## 7. CV Voltage 跨端一致性

正式定义：

> `loadCvVoltageV` 是上位机控制电子负载时的本次测试工况。

审核：

- [ ] 上位机可按当前校准任务选择CV；
- [ ] CV实际写入/控制电子负载；
- [ ] CV不作为设备User Config；
- [ ] CV不作为固件Calibration授权条件；
- [ ] 固件不要求运行Vo等于校准CV；
- [ ] 36V校准后在其他运行电压下Calibration仍可使用；
- [ ] Calibration Record若保存Reference Voltage，只作为Metadata。

---

## 8. Tolerance 跨端一致性

正式定义：

> Tolerance 是 **APPLY之后** 的验收策略，不是Calibration前的数据采集门禁。

审核：

- [ ] 上位机可使用Profile默认Tolerance；
- [ ] 必要时工艺人员可选择本次Tolerance；
- [ ] 不同功率Profile可以有不同默认Tolerance；
- [ ] 校准前即使误差5%仍可进入拟合，只要硬件和数据有效；
- [ ] APPLY后才正式判PASS/FAIL；
- [ ] 低输出与主要工作区可使用不同误差；
- [ ] Output/OCO/BL0942可分别定义验收要求；
- [ ] Tolerance不写入固件运行授权逻辑。

---

## 9. 11点正式采集一致性

固定：

```text
Percent = 0,10,...,100
Level   = 0,20,...,200
```

审核：

- [ ] 上位机点序正确；
- [ ] 固件只接受合法正式Level；
- [ ] SET_POINT返回实际PWM；
- [ ] Calibration采点不应用旧Output Calibration；
- [ ] Calibration采点不叠加OP_PWM_OFFSET；
- [ ] 无Calibration的正常运行链仍保留OP_PWM_OFFSET；
- [ ] 每点上位机等待稳定后采样；
- [ ] RAW与外部Reference能够按同一点对齐；
- [ ] 11点不逐点写Flash。

---

## 10. Calibration前与Calibration后验证

### Calibration前

只判断：

- Fresh；
- 仪器有效；
- 输出稳定；
- 无硬件Fault；
- 数据合理；
- 基本单调；
- 没有越过硬件安全边界。

审核：

- [ ] 校准前不再直接用±1%/±2%阻止Correction生成。

### APPLY后

正式进行Accuracy Verification。

审核：

- [ ] Quick验证可使用5/45/85%；
- [ ] Full验证可使用5/15/.../95%；
- [ ] 验证点与11个拟合点独立；
- [ ] FAIL走ABORT并安全关闭；
- [ ] PASS后才能COMMIT。

---

## 11. Output Calibration 一致性

数据关系：

```text
Actual PWM <-> Reference Output Current
```

审核：

- [ ] 11点结构、单位和Endian两边一致；
- [ ] 上位机拟合输入使用Reference，不使用已Corrected值；
- [ ] 固件运行时用Target Current反插值；
- [ ] 最终直接得到高精度u16 PWM；
- [ ] 不走整数百分比二次量化；
- [ ] 有Calibration时不重复OP_PWM_OFFSET；
- [ ] Calibration范围外合法目标回退Default+Offset，不PWM=0。

---

## 12. OCO Calibration 一致性

关系：

```text
OCO Raw <-> Reference Output Current
```

审核：

- [ ] RAW是真正未校准值；
- [ ] Corrected只用于MQTT/业务；
- [ ] Protection使用Raw/保守链；
- [ ] Calibration不能掩盖真实过流；
- [ ] 上下位机单位/插值完全一致。

---

## 13. BL0942 Calibration 与稳定性

### Calibration

审核：

- [ ] Voltage Correction两边定义一致；
- [ ] Current 11点 Raw→Reference一致；
- [ ] Power 11点 Raw→Reference一致；
- [ ] 无Calibration回退默认换算。

### Freshness

必须：

```text
last_valid_frame_tick
dataAge
fresh/stale
```

审核：

- [ ] stale数据不用于校准；
- [ ] stale数据不伪装实时上报；
- [ ] 上位机当前点遇stale暂停/失败重试。

### 长期冻结根因

审核：

- [ ] HAL TX/RX返回值检查；
- [ ] ORE/FE/NE有诊断；
- [ ] gState/RxState/ErrorCode有证据；
- [ ] timeout后上下状态同步；
- [ ] 无周期性Reset掩盖根因；
- [ ] 有长稳测试证据；
- [ ] 若硬件/VDD问题未完全复现，文档明确“未证明”而不是伪造结论。

---

## 14. Protocol V3 Wire Contract

建议Operation Code：

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

最终代码必须和本文最终冻结版本一致。

公共字段建议：

```text
v,o,s,q,rc,st,lv,pwm,gen,len,crc
```

审核：

- [ ] 上下位机Code完全一致；
- [ ] 单位完全一致；
- [ ] Signed/Unsigned一致；
- [ ] Endian一致；
- [ ] CRC算法一致；
- [ ] Retry/duplicate seq语义一致；
- [ ] ACK只返回当前操作需要的数据；
- [ ] 不再每个ACK携带完整Context/Status。

---

## 15. JSON / TX内存审核

固件当前约束：

```text
ZK_JSON_BUF_SIZE = 2048B
ZK_CJSON_TX_POOL_SIZE = 4096B
```

审核目标：

- 普通ACK目标 `<256B`；
- RAW目标 `<512B`；
- CAP目标 `<768B`；
- READ_CHUNK目标 `<768B`；
- 所有TX最终JSON `<1536B`；
- 不出现 `TX Pool Exhausted`；
- 不能只验证最终字符串长度，还要观察cJSON TX pool占用；
- 删除设备端多Profile Catalog；
- 大Calibration回读使用READ_CHUNK；
- DIAG与正式RAW分离。

必须提供实测最大报文长度记录。

---

## 16. Calibration Record V2 审核

最终冻结后逐Byte核对：

- [ ] Magic；
- [ ] Format Version；
- [ ] Record Length；
- [ ] Generation；
- [ ] Profile Identity/Fingerprint；
- [ ] Valid Flags；
- [ ] Output 11点offset；
- [ ] OCO 11点offset；
- [ ] BL Current 11点offset；
- [ ] BL Power 11点offset；
- [ ] BL Voltage Correction offset；
- [ ] Metadata；
- [ ] CRC范围；
- [ ] Commit Marker；
- [ ] Endian；
- [ ] 上位机encode结果与固件decode结果逐Byte一致。

至少提供一组Golden Vector。

---

## 17. Flash A/B审核

目标布局：

```text
0x08005000 Config A
0x08005800 Config B
0x08006000 Calibration A
0x08006800 Calibration B
0x08007000 Runtime A
0x08007800 Runtime B
```

每页2KiB。

审核：

- [ ] 一个物理页一个Owner；
- [ ] 不在当前有效页原地修改；
- [ ] 非活动页擦写；
- [ ] CRC/回读通过后Commit；
- [ ] 掉电保留旧页；
- [ ] Generation选择正确；
- [ ] Config修改Plan不会丢SET/HWMAX；
- [ ] Calibration每次完整Commit；
- [ ] 不写旧APP内部programmer区域。

---

## 18. Legacy OTA审核

审核：

- [ ] 新Config不存在时可以读取Legacy；
- [ ] SET_OUTCUR保留；
- [ ] 合法HWMAX保留；
- [ ] 平台/告警/计划保留；
- [ ] 缺Calibration正常运行；
- [ ] 无Calibration走原PWM+OP_PWM_OFFSET；
- [ ] 不每次启动重复迁移/擦Flash；
- [ ] Boot/APP/OTA地址不改变。

---

## 19. 上位机UI审核

根据当前校准工作台设计，必须保留：

### 产品身份区

- [ ] 多功率产品卡；
- [ ] 50W显示已冻结；
- [ ] 其他功率可显示待核对/未冻结；
- [ ] 设备CAP结果与当前选择Profile有清晰匹配状态。

### 本次校准工况区

- [ ] IMEI/SN；
- [ ] Product Profile；
- [ ] SET_OUTCUR输入；
- [ ] Electronic Load CV输入；
- [ ] Allowed Tolerance；
- [ ] Stabilization；
- [ ] Validation Mode；
- [ ] 仪器连接状态。

### 语义

- [ ] SET明确标为写入设备User Config；
- [ ] CV明确标为电子负载工况；
- [ ] Tolerance明确标为校准后验收；
- [ ] 不再显示“固件Bound Voltage必须匹配”旧逻辑。

---

## 20. 其他功能回归审核

### 固件

- [ ] Bootloader不变；
- [ ] OTA契约不变；
- [ ] RTC正常；
- [ ] 计划任务语义不变；
- [ ] 普通MQTT不因V3修改；
- [ ] CAT1正常；
- [ ] 硬件保护正常。

### 上位机

- [ ] MQTT基础设施未被无必要重写；
- [ ] Serial/DC5200/SCPI成熟能力保持；
- [ ] safeStorage/settings正常；
- [ ] audit.jsonl正常；
- [ ] Electron安全边界不倒退；
- [ ] UI其他页面无回归。

---

## 21. 最终HIL场景

至少执行：

1. 50W空白设备、无Calibration正常输出；
2. 50W默认SET=893校准；
3. 修改本次SET_OUTCUR后进行校准；
4. 同一50W不同CV工况校准/验证；
5. 不同Tolerance策略验证；
6. 11点完整采集；
7. 校准前大误差仍生成Correction；
8. APPLY后快速验证；
9. APPLY后完整验证；
10. 验证失败ABORT；
11. COMMIT掉电；
12. Config写入掉电；
13. READ_CHUNK完整回读；
14. BL0942 stale；
15. BL0942 ORE注入；
16. BL0942长稳；
17. OTA前后SET保持；
18. 校准后再修改SET，Calibration保持；
19. 上位机切换到75/100等卡片时框架仍存在且未冻结产品被正确阻止量产；
20. 所有Calibration V3报文无TX Pool Exhausted。

---

## 22. 最终通过条件

只有同时满足以下条件才允许认为本轮修改完成：

> **50W固件功能正确。**

> **上位机仍然是多功率通用工作台。**

> **本次SET_OUTCUR / CV / Tolerance语义正确且互不混淆。**

> **Protocol V3上下位机字段、单位、Code、CRC完全一致。**

> **Calibration Record逐Byte一致。**

> **无校准仍可正常运行。**

> **Output/OCO/BL0942三类Correction正确。**

> **BL0942长期稳定性有实机证据。**

> **Flash掉电安全。**

> **JSON/TX内存压力满足预算。**

> **Boot/OTA/普通业务/计划/RTC没有被误改。**

> **至少完成50W真实硬件端到端HIL，并保留可审计证据。**
