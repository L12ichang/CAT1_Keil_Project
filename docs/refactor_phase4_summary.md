# 第四阶段重构问题总结

生成日期：2026-04-18（原内容），2026-05-14（浓缩整理）

本文档浓缩了第四阶段重构中暴露的关键问题、根因、修复方案和经验教训。

## 1. 问题总览

第四阶段重构将协议、启动地址、Bootloader释放、UART DMA、cJSON内存池、IMEI/ICCID解析、系统参数迁移和烧录脚本同时推到了同一条启动登录链路上，暴露了以下关键问题：

| ID | 分类 | 严重级别 | 表现 | 当前状态 |
| --- | --- | --- | --- | --- |
| P0-01 | 启动与烧录 | 阻断 | App基址/VTOR/Bootloader跳转不一致 | 已修复，需冷启动复验 |
| P0-02 | 稳定性 | 阻断 | 看门狗复位循环 | 已修复，需长稳复验 |
| P0-03 | 功能性bug | 阻断 | MQTT topic使用错误IMEI | 已修复 |
| P0-04 | 兼容性 | 阻断 | 参数结构变大导致校准丢失 | 已修复，需带载验证 |
| P1-01 | 稳定性 | 高 | cJSON内存池非对齐HardFault | 已修复 |
| P1-02 | 流程 | 高 | J-Link reset后误判App崩溃 | 已修复 |
| P1-03 | 可观测性 | 高 | 串口0 bytes仍判定通过 | 待改进 |

## 2. 根因分析

不是单一代码错误，而是系统边界没有一次性收敛：

1. **App地址变更是系统级变更**：必须同步链接脚本、向量表、烧录工具、Bootloader释放、OTA上限
2. **MQTT/JSON协议重构穿透底层**：影响UART DMA、SysTick、看门狗、内存池、Flash参数
3. **持久化结构体直接扩大**：没有版本字段和迁移策略
4. **设备身份采集依赖异步AT回包**：IMEI/ICCID解析规则不严格导致混淆
5. **自动化验证缺少设备运行证据门槛**：主机侧测试通过不等于整机通过

## 3. 已实施的修复

### 启动与烧录
- 统一App base为`0x08008000`，链接脚本、VTOR、烧录base一致
- 烧录使用`loadbin {bin},0x08008000`并加`0x08024000`边界检查，保留OTA备份区
- 烧录后写RAM标志`0x20000000=1`释放Bootloader
- 真实IRQ handler增加`used`/`externally_visible`避免链接优化误删
- `__enable_irq()`提前到首次`printf()`和`delayMs()`之前
- 看门狗初始化推迟到长启动之后

### 登录与IMEI
- cJSON静态内存池8字节对齐
- IMEI精确匹配15位数字，ICCID至少18位
- 延迟回包跨状态捕获，刷新MQTT配置

### 参数迁移
- 恢复旧参数区`0x08005000/0x08006800`
- 支持旧340字节结构校验和迁移
- 主备参数区同步保存当前结构

## 4. 硬件连接配置

| 项目 | 值 |
| --- | --- |
| J-Link设备 | STM32F103RC, SWD, 4000kHz |
| 日志串口 | UART3, 1000000-8-N-1 |
| 业务串口 | UART1, 115200-8-N-1 |
| 推荐串口 | `/dev/cu.wchusbserial2320` |

## 5. 烧录前后检查清单

### 烧录前
- [ ] 构建通过，`.isr_vector`为`0x08008000`
- [ ] `flash --dry-run`通过，写入范围未越界
- [ ] Bootloader已有备份和SHA256
- [ ] 串口采集已准备，参数`1000000-8-N-1`

### 烧录后
- [ ] 回读校验通过
- [ ] `VTOR=0x08008000`
- [ ] 串口日志非空，无持续`WDGRST`
- [ ] IMEI为真实15位数字，topic一致
- [ ] 登录状态`ZK_LOGIN_STATE_ONLINE`
- [ ] 主备参数区校验通过

## 6. 标准日志分析流程

1. 查运行域：PC/VTOR在`0x08000000`还是`0x08008000`
2. 查启动链路：Bootloader输出、checksum、PLL、VTOR
3. 查看门狗：是否`WDGRST`、复位间隔
4. 查设备身份：IMEI、ICCID、MQTT topic一致性
5. 查MQTT状态：QMTOPEN/QMTCONN/QMTSUB/QMTPUBEX/登录ACK/心跳
6. 查参数：主备区校验、工厂参数关键字节

## 7. 关键经验教训

1. 嵌入式"协议层重构"会穿透中断、DMA、定时器、看门狗、Flash参数和Bootloader
2. App基址变化是系统级变更，必须同步链接脚本、VTOR、烧录、Bootloader和OTA
3. 持久化结构体变更必须有版本和迁移，否则旧设备参数丢失
4. 不要把J-Link reset后停在Bootloader误判为App崩溃
5. 不要把串口乱码（波特率错误）误判为协议异常
6. 登录成功不能只看"发出了MQTT包"，要读状态机、IMEI、topic和平台ACK
7. 烧录成功不能只看工具返回成功，要检查写入范围和回读校验
8. 校准参数是电源控制的安全边界，Flash地址迁移前必须先备份验证
9. 现场验收必须包含真实断电上电和带载输出测试

## 8. 日常操作速查

```bash
# 构建
./AUTOMATION_SCRIPT.sh build

# 烧录演练
./AUTOMATION_SCRIPT.sh flash --dry-run

# 实际烧录
./AUTOMATION_SCRIPT.sh flash

# 串口验证
./AUTOMATION_SCRIPT.sh serial-test --duration 20 --expect "boot startup"

# 登录测试
./AUTOMATION_SCRIPT.sh login-test

# J-Link快速检查VTOR
# halt → mem32 0xE000ED08,1 → go

# 释放Bootloader等待
# halt → w1 0x20000000 0x01 → go

# 回读Bootloader备份
# savebin artifacts/bootloader/bootloader.bin,0x08000000,0x8000

# 回读系统参数
# savebin artifacts/sys_data_main.bin,0x08005000,0x198
# savebin artifacts/sys_data_bak.bin,0x08006800,0x198
```

## 9. 问题知识库速查

| 现象 | 最可能原因 | 首个检查动作 |
| --- | --- | --- |
| 烧录后像是Bootloader丢失 | 烧录地址错误或停在Bootloader等待 | 回读`0x08000000`，查写入范围 |
| reset/go后不进App | Bootloader等待RAM释放标志 | `w1 0x20000000 0x01; go` |
| 串口反复`WDGRST` | 看门狗复位循环 | 查PC/VTOR、看门狗初始化时机 |
| 串口无日志 | 波特率错或采集窗口错过 | 用`1000000`，先采集再复位 |
| MQTT登录平台无在线 | IMEI/topic/password不一致 | 读IMEI RAM和topic |
| JSON构造HardFault | cJSON pool未对齐 | 检查allocator 8字节对齐 |
| 烧录后功率异常 | 旧校准参数未迁移 | 回读`0x08005000`校验 |
