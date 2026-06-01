# CAT1 单片机项目固件体积与性能优化目标文档（Codex 一次性执行版）

> 项目：`CAT1_Keil_Project`  
> 执行对象：Codex / AI 编程代理  
> 执行方式：一次性完成全部优化目标，但必须在 Git 中分阶段提交、可验证、可回退。  
> 核心原则：**不改变当前项目功能、不改变协议、不改变 Flash 布局、不破坏 OTA。**

---

## 0. 本文档的执行目标

请在现有单片机项目中一次性完成以下两类优化：

1. **减小 Keil 编译出来的固件体积**
2. **提高 MCU 运行性能、稳定性和可诊断能力**

但是优化过程中必须保证：

```text
当前项目已有功能不能变化
MQTT 协议不能变化
OTA 流程不能变化
Flash 数据结构不能变化
sys_data_st 大小不能变化
App 起始地址不能变化
Bootloader 兼容性不能变化
```

本任务不是重构项目，不是改协议，不是改业务逻辑，而是在保持功能等价的前提下做体积优化、性能优化和安全性增强。

---

## 1. Git 版本管理强制要求

本项目优化必须全程使用 Git 管理。Codex 执行前、执行中、执行后都必须遵守以下规则。

### 1.1 执行前必须确认 Git 状态

进入项目根目录后先执行：

```bash
git status --short
git branch --show-current
git log --oneline -5
```

如果当前目录还不是 Git 仓库，必须先初始化：

```bash
git init
git add .
git commit -m "baseline: import CAT1 keil project before optimization"
git tag baseline-before-size-perf-optimization
```

如果已经是 Git 仓库，但存在未提交改动，必须先保护现场：

```bash
git status --short
git add .
git commit -m "backup: save working tree before size and performance optimization"
git tag backup-before-size-perf-optimization
```

不得在有未提交改动的状态下直接开始优化。

---

### 1.2 必须创建独立优化分支

禁止直接在 `main`、`master` 或当前稳定分支上直接修改。

创建分支：

```bash
git checkout -b optimize/firmware-size-performance
```

如果分支已存在：

```bash
git checkout optimize/firmware-size-performance
```

---

### 1.3 一次性完成，但必须分阶段 commit

本任务要求 Codex **一次性完成全部目标**，但内部必须按照阶段分多个 commit。  
不要每完成一个阶段就询问用户是否继续；应该连续完成所有阶段，并在每个阶段后提交。

每个 commit 必须满足：

```text
1. 改动范围清晰
2. 可以单独回退
3. 编译或测试失败时必须修复后再提交
4. commit message 必须说明优化目标
```

推荐 commit 顺序：

```text
commit 01: baseline: record build size and optimization constraints
commit 02: build: add release-size build configuration notes and map output requirements
commit 03: log: add APP_LOG_ENABLE and APP_HEX_LOG_ENABLE switches
commit 04: log: disable hex dump output in production builds
commit 05: uart3: fix dma printf tx complete flag and use formatted length
commit 06: legacy: isolate unused HTTP activation path without affecting OTA HTTP
commit 07: hal: remove unused HAL SPI/CRC modules after verification
commit 08: cjson: guard JSON parsing paths without changing protocol format
commit 09: float: remove float printf and convert trivial float math to integer math
commit 10: flash: skip flash erase/write when stored data is unchanged
commit 11: uart2: fix BL0942 receive boundary handling
commit 12: bl0942: add divide-by-zero guards for abnormal sensor data
commit 13: cat1: add UART receive queue drop counter for diagnostics
commit 14: perf: add optional main-loop profiling guarded by APP_PERF_PROFILE_ENABLE
commit 15: verify: record final size/performance comparison and test results
```

如果某个 commit 无法安全完成，可以跳过该优化点，但必须在最终报告里说明原因。

---

### 1.4 每个阶段后必须检查 Git diff

每次 commit 前执行：

```bash
git diff --check
git diff --stat
git diff
```

重点确认：

```text
没有误删 OTA 相关代码
没有改变 Flash 地址
没有改变 sys_data_st 字段布局
没有改变 MQTT payload 字段
没有改动 bootloader 相关偏移地址
没有把临时文件、编译产物、大量无关文件加入 Git
```

提交前必须执行：

```bash
git status --short
```

只提交和本阶段有关的文件。

---

### 1.5 必须创建阶段性 tag

完成关键阶段后必须打 tag，方便快速回退。

建议 tag：

```bash
git tag baseline-before-size-perf-optimization

git tag after-log-optimization

git tag after-legacy-cleanup

git tag after-hal-pruning

git tag after-flash-uart-bl0942-safety

git tag final-size-perf-optimization
```

---

### 1.6 回退规则

如果最终测试失败，优先按 commit 回退，而不是手动乱改。

回退最后一个 commit：

```bash
git revert HEAD
```

回退某个指定 commit：

```bash
git revert <commit_hash>
```

回退到优化开始前状态：

```bash
git checkout optimize/firmware-size-performance
git reset --hard baseline-before-size-perf-optimization
```

如果只需要临时查看旧版本：

```bash
git checkout baseline-before-size-perf-optimization
```

严禁在未确认的情况下执行：

```bash
git clean -fdx
git reset --hard
```

除非已经确认所有需要保留的文件都已提交或备份。

---

## 2. 项目关键约束

### 2.1 不允许改变的地址和结构

以下内容必须保持不变：

```text
APROM_OFFSET_ADDR
APROM_STARTADDR
APROM_SAFE_ENDADDR
OTABAKROM_STARTADDR
DATAROM_STARTADDR
BAKDATAROM_STARTADDR
SCB->VTOR = APROM_OFFSET_ADDR
App 起始地址：0x08008000
App 安全结束地址：0x08024000
OTA 备份区：0x08024000 起
sys_data_st 大小：408 字节
SYS_DATA_ST_EXPECTED_SIZE = 408
prog_checksum 地址
prog_length 地址
device_type 地址
```

`sys_data_st` 字段布局不得改变。  
即使某些字段已经是 legacy，也不能删除，因为会影响 Flash 参数兼容性。

例如以下字段不得删除：

```c
char openid[33];
char token[33];
```

只允许加注释：

```c
/* legacy reserved, keep flash layout compatible, do not remove */
char openid[33];
char token[33];
```

---

### 2.2 不允许改变的功能

以下功能必须保持当前行为：

```text
1. CAT1 模块初始化
2. CAT1 AT 指令状态机
3. MQTT 连接
4. MQTT 登录
5. MQTT 心跳
6. MQTT 属性上报
7. MQTT 控制下发
8. 中科协议字段格式
9. OTA URL 设置、HTTP 下载、固件搬运和重启
10. BL0942 电压、电流、功率、电量采集
11. PWM 调光
12. 温度保护
13. 过流保护
14. 掉电检测
15. RTC / AIP1302 时间处理
16. 工作计划 / 定时计划
17. Flash 参数保存和读取
18. 看门狗喂狗逻辑
```

---

### 2.3 不允许误删的内容

不能删除：

```text
Core/Src/LampProtocolLib/ota.c 中的 QHTTP OTA 下载流程
Core/Src/LampProtocolLib/Json_Protocol.c
Core/Src/LampProtocolLib/mqtt_zk_protocol.c
Core/Src/CJSON/cJSON.c
Flash 分区定义
OTA 相关宏
Bootloader 兼容代码
```

尤其注意：旧 HTTP 激活链路可以隔离或删除，但 OTA 里的 HTTP AT 命令不能删。

允许清理的是旧激活链路：

```text
Core/Src/gateway/app_active.c
Core/Src/gateway/app_active.h
Core/Src/LampProtocolLib/http_active.c
Core/Src/LampProtocolLib/http_active.h
```

但必须先确认它们不在当前主流程中实际运行。

---

## 3. 执行前基准记录

Codex 必须在修改前记录基准。

### 3.1 记录 Git 基准

```bash
git status --short
git log --oneline -5
git tag baseline-before-size-perf-optimization
```

---

### 3.2 记录编译基准

在 Keil 中执行 Rebuild，记录：

```text
Code size
RO-data size
RW-data size
ZI-data size
Total ROM size
Total RAM size
bin 文件大小
hex 文件大小
map 文件路径
```

如果存在工具脚本，执行：

```bash
python3 -m unittest discover -s tests -v
bash tests/run_mqtt_protocol_tests.sh
python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness
```

如果某些脚本不存在，不要失败退出，必须在最终报告说明：

```text
未找到 xxx 脚本，因此跳过该自动测试。
```

---

## 4. 一次性优化任务清单

以下任务需要一次性连续完成。  
每个任务完成后必须 commit。  
如果某个任务风险较高，可以保留宏开关，默认保持原功能。

---

# Phase 1：Release Size 编译配置与 Map 输出

## 目标

通过编译器和链接器减少固件体积，不改变业务代码。

## 要求

在 Keil 工程中增加或说明 Release Size 配置：

```text
Target: CAT1_Release_Size
Optimization: Optimize for Size
One ELF Section per Function: Enable
Linker remove unused sections: Enable
Map File: Enable
```

如果是 Arm Compiler 6，可测试：

```text
-Os
```

如果稳定，再测试：

```text
-Oz
```

MicroLIB 作为独立实验项，不能和其他改动混在一个 commit 中。

## 验收

```text
Keil 编译通过
map 文件生成
App 起始地址不变
OTA 元数据地址不变
功能代码不变
```

---

# Phase 2：日志系统统一开关

## 目标

生产环境关闭所有 printf 和十六进制 dump，调试环境可以开启。

## 重点文件

```text
Core/Src/common.h
Core/Src/main.c
Core/Src/hw_uart3.c
```

## 实现要求

在 `common.h` 中加入：

```c
#ifndef APP_LOG_ENABLE
#define APP_LOG_ENABLE 0
#endif

#ifndef APP_HEX_LOG_ENABLE
#define APP_HEX_LOG_ENABLE 0
#endif

#if APP_LOG_ENABLE
#define LOG_PRINT(...) dma_printf(__VA_ARGS__)
#else
#define LOG_PRINT(...) do {} while (0)
#endif

#define LOGE(...) LOG_PRINT("[E] " __VA_ARGS__)
#define LOGW(...) LOG_PRINT("[W] " __VA_ARGS__)
#define LOGI(...) LOG_PRINT("[I] " __VA_ARGS__)
#define LOGD(...) LOG_PRINT("[D] " __VA_ARGS__)

#if APP_LOG_ENABLE
#define printf(...) dma_printf(__VA_ARGS__)
#else
#define printf(...) do {} while (0)
#endif
```

调试版：

```text
APP_LOG_ENABLE=1
APP_HEX_LOG_ENABLE=1
```

生产版：

```text
APP_LOG_ENABLE=0
APP_HEX_LOG_ENABLE=0
```

`printf_buf()`、`printf_buf_char()`、`printf_buf2()` 必须受 `APP_HEX_LOG_ENABLE` 控制。

生产关闭时必须写成空实现：

```c
(void)buf;
(void)length;
```

避免未使用参数 warning。

---

## UART3 DMA printf 修复

`hw_uart3.c` 中必须修复 DMA 打印完成标志：

```c
void HAL_DMA_TxCpltCallback(DMA_HandleTypeDef *dma)
{
    if (dma == &hdma_usart3_tx)
    {
        _flag_txing = BOOL_FALSE;
    }
}
```

`dma_printf()` 必须使用 `vsnprintf()` 返回长度，不要再用 `strlen(dma_buffer)` 重新扫描。

建议逻辑：

```c
n = vsnprintf(dma_buffer, DMA_BUFFER_SIZE - 1, format, args);

if (n > 0)
{
    if (n >= DMA_BUFFER_SIZE)
    {
        n = DMA_BUFFER_SIZE - 1;
    }

    if (HAL_UART_Transmit_DMA(&huart3, (uint8_t *)dma_buffer, n) == HAL_OK)
    {
        _flag_txing = BOOL_TRUE;
    }
    else
    {
        _flag_txing = BOOL_FALSE;
    }
}
```

## 验收

```text
APP_LOG_ENABLE=1 时调试打印正常
APP_LOG_ENABLE=0 时不输出调试日志
APP_HEX_LOG_ENABLE=0 时不输出 payload dump
生产 map 中日志字符串明显减少
主循环不再因 printf 长时间等待
```

---

# Phase 3：旧 HTTP 激活链路隔离或删除

## 目标

清理已经不参与主流程的旧 HTTP 激活代码，避免无效体积和维护干扰。

## 允许处理的 legacy 文件

```text
Core/Src/gateway/app_active.c
Core/Src/gateway/app_active.h
Core/Src/LampProtocolLib/http_active.c
Core/Src/LampProtocolLib/http_active.h
```

## 必须保留的 HTTP OTA 代码

```text
Core/Src/LampProtocolLib/ota.c
```

不能删除 OTA 中的：

```text
AT+QHTTPCFG
AT+QHTTPURL
AT+QHTTPGETEX
AT+QHTTPREADFILE
```

## 实现要求

1. 全工程搜索：

```text
app_activate_
http_post_fsm
http_congfig_fsm
CONNECT_CONFIG_HTTP_ACTIVE
CONNECT_CONFIG_WAIT_ACTIVE
```

2. 如果确认旧激活链路没有真实调用路径，则删除无效 include 和空调用。

3. 可以将旧文件移动到：

```text
Core/Legacy/
```

或者直接从 Keil 工程中移除。

4. 不允许删除 `sys_data.openid` 和 `sys_data.token` 字段。

## 验收

```text
Keil 编译通过
MQTT 登录正常
MQTT 心跳正常
OTA 正常
sys_data_st 仍然是 408 字节
没有误删 OTA HTTP 相关逻辑
```

---

# Phase 4：裁剪未使用 HAL 模块

## 目标

减少 HAL 驱动引入的 ROM 占用。

## 优先检查

```text
HAL_SPI_MODULE_ENABLED
HAL_CRC_MODULE_ENABLED
HAL_PWR_MODULE_ENABLED
```

## 执行顺序

```text
1. 先尝试关闭 HAL_SPI_MODULE_ENABLED
2. 再尝试关闭 HAL_CRC_MODULE_ENABLED
3. 最后谨慎评估 HAL_PWR_MODULE_ENABLED
```

涉及文件：

```text
Core/Inc/stm32f1xx_hal_conf.h
MDK-ARM-8008000/project.uvprojx
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_crc.c
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
```

## 注意

`HAL_PWR` 不要贸然删除，因为 standby 或电源相关代码可能依赖。

## 验收

```text
Keil 编译通过
无 undefined reference
启动正常
OTA 正常
低功耗/standby 相关功能不受影响
map 文件 ROM 减少
```

---

# Phase 5：cJSON 使用优化

## 目标

保持 MQTT JSON 协议完全不变，但减少 cJSON 动态内存和无效依赖。

## 不能删除

```text
Core/Src/CJSON/cJSON.c
Core/Src/LampProtocolLib/Json_Protocol.c
Core/Src/LampProtocolLib/mqtt_zk_protocol.c
```

## 优化要求

1. 生产路径不要使用 `cJSON_Print()`。
2. 固定长度发送优先保留 `cJSON_PrintPreallocated()`。
3. 清理旧激活链路中的 `cJSON_Print()`。
4. 所有 `cJSON_Parse()` 后必须判断 NULL。
5. 所有 `cJSON_GetObjectItem()` 后必须判断 NULL。
6. 退出前必须 `cJSON_Delete(root)`。
7. 不得改变 MQTT JSON 字段名、字段类型和业务含义。

## 可选增强

固定格式 JSON 生成可以逐步从 cJSON 改成 `snprintf()`，但必须保证输出完全等价。

优先替换：

```text
登录包
心跳包
属性上报包
```

不建议第一轮替换 JSON 解析逻辑。

## 验收

```text
MQTT 登录包格式不变
MQTT 心跳包格式不变
属性上报格式不变
控制下发解析正常
协议测试通过
cJSON 动态内存使用减少或不增加
```

---

# Phase 6：浮点打印和低风险浮点计算优化

## 目标

减少浮点库、数学库、printf 浮点格式化带来的 ROM 占用，提高运行速度。

## 必须处理

关闭或删除生产路径中的：

```text
%f
%g
float printf
double printf
```

重点文件：

```text
Core/Src/sys_bl0942.c
Core/Src/sys_pwm.c
Core/Src/adc.c
Core/Src/sys_Vo_Io.c
Core/Src/LampProtocolLib/zk_sunriset.c
```

## 低风险替换示例

原：

```c
ac_powerpa = (u16)((float)ac_powerpa * (1.0f - 0.03));
```

改：

```c
ac_powerpa = (u16)((u32)ac_powerpa * 97 / 100);
```

原：

```c
ADC_Value1 = ((float)adc_average[ADC_CH08_NTC] / 4095) * 3300;
```

改：

```c
ADC_Value1 = (uint32_t)adc_average[ADC_CH08_NTC] * 3300 / 4095;
```

## 注意

`BL0942` 精度相关算法不要一次性大改。  
`zk_sunriset.c` 如果产品需要日出日落计划功能，不得删除。

可以增加宏：

```c
#ifndef APP_SUNRISET_ENABLE
#define APP_SUNRISET_ENABLE 1
#endif
```

默认必须保持开启。

## 验收

```text
无 %f / %g 生产打印
电压/电流/功率计算误差在原有允许范围内
PWM 行为不变
温度保护行为不变
map 中浮点 printf 依赖减少
```

---

# Phase 7：Flash 写前比较优化

## 目标

不改变保存结果，但减少 Flash 重复擦写，提高性能和寿命。

## 重点文件

```text
Core/Src/sys_data.c
Core/Src/hw_flash.c
```

## 建议修改

在 `data_store_data()` 中增加写前比较：

```c
boolean_en data_store_data(u8* buf, u16 size, u32 addr_main)
{
    if (size > 6 * 1024 || addr_main + size > APROM_STARTADDR)
    {
        return BOOL_FALSE;
    }

    if (memcmp((const void *)addr_main, buf, size) == 0)
    {
        return BOOL_TRUE;
    }

    hw_flash_write_bytes(addr_main, buf, size);
    return BOOL_TRUE;
}
```

## 验收

```text
sys_data_store() 保存结果不变
主区和备份区仍然都能保存
断电恢复逻辑不变
重复保存同样数据时不再擦写 Flash
Flash 寿命和运行性能提升
```

---

# Phase 8：UART2 / BL0942 接收边界保护

## 目标

修复潜在越界，提高稳定性。

## 重点文件

```text
Core/Src/hw_uart2.c
```

## 建议修改

把类似逻辑：

```c
if (_index < _rx_length)
{
    _buffer[_index++] = data;
}

HAL_UART_Receive_IT(&huart2, _buffer + _index, 1);

if (_index == _rx_length)
{
    _bl0942_state = BL0942_STATE_READ_READY;
}
```

改成：

```c
if (_index < _rx_length)
{
    _buffer[_index++] = data;
}

if (_index >= _rx_length)
{
    _bl0942_state = BL0942_STATE_READ_READY;
}
else
{
    HAL_UART_Receive_IT(&huart2, _buffer + _index, 1);
}
```

## 验收

```text
BL0942 正常接收
采样周期不变
不会写越界
异常数据不会导致内存破坏
```

---

# Phase 9：BL0942 除零保护

## 目标

异常电参情况下避免除零导致 HardFault。

## 重点文件

```text
Core/Src/sys_bl0942.c
```

## 建议修改

对类似：

```c
ac_pf = (ac_powerpa * 100) / ac_power_S;
```

增加保护：

```c
if (ac_power_S == 0)
{
    ac_pf = 0;
    Z_ac_current = 0;
}
else
{
    ac_pf = (ac_powerpa * 100) / ac_power_S;
}
```

## 验收

```text
正常电参结果不变
无电压/无电流时不崩溃
异常传感器数据不导致 HardFault
```

---

# Phase 10：CAT1 UART 接收队列丢包统计

## 目标

不改变业务逻辑，但增加诊断能力，用于判断是否存在串口数据丢失。

## 重点文件

```text
Core/Src/LampProtocolLib/Portable.c
Core/Src/LampProtocolLib/Queue.c
```

## 建议修改

当前：

```c
void saveUsartByte(uint8 byte) 
{  
    enqueue(&usartRecvQueue, byte); 
}
```

改成：

```c
volatile uint32_t usart_queue_drop_count = 0;

void saveUsartByte(uint8 byte) 
{  
    if (enqueue(&usartRecvQueue, byte) == 0)
    {
        usart_queue_drop_count++;
    }
}
```

生产环境不打印，仅保留变量供调试器查看。

## 验收

```text
入队逻辑不变
队列满时系统不崩溃
正常 MQTT / OTA 情况下 usart_queue_drop_count 应为 0
```

---

# Phase 11：可选主循环性能统计

## 目标

帮助定位哪个状态机耗时，不改变生产运行行为。

## 重点文件

```text
Core/Src/main.c
```

## 实现要求

增加宏：

```c
#ifndef APP_PERF_PROFILE_ENABLE
#define APP_PERF_PROFILE_ENABLE 0
#endif
```

只在开启时统计耗时：

```text
tcpClientProcess()
_4G_configModule_machine()
send_AT_Command_machine()
nbSendTcpData_sm()
sys_bl0942_process()
_4G_OTA_machine()
mcu_copy_firmware_machine()
zk_work_plan_process()
json_process()
```

生产环境：

```text
APP_PERF_PROFILE_ENABLE=0
```

调试环境：

```text
APP_PERF_PROFILE_ENABLE=1
```

## 验收

```text
生产环境无额外耗时
调试环境可统计最大耗时
不改变主循环调用顺序
不改变任务调用频率
```

---

## 5. 最终验证要求

Codex 完成所有优化后必须执行最终验证。

### 5.1 Git 验证

```bash
git status --short
git log --oneline --decorate -20
git tag --list
```

最终工作区必须干净：

```text
nothing to commit, working tree clean
```

---

### 5.2 编译验证

Keil Rebuild 必须通过。

记录优化后：

```text
Code size
RO-data size
RW-data size
ZI-data size
Total ROM size
Total RAM size
bin 文件大小
hex 文件大小
map 文件路径
```

必须和 baseline 对比。

输出表格：

```text
项目              优化前        优化后        变化
Code              xxx           xxx           -xxx
RO-data           xxx           xxx           -xxx
RW-data           xxx           xxx           xxx
ZI-data           xxx           xxx           xxx
bin size          xxx           xxx           -xxx
```

---

### 5.3 自动测试验证

如果存在测试脚本，必须执行：

```bash
python3 -m unittest discover -s tests -v
bash tests/run_mqtt_protocol_tests.sh
python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness
```

如果某个脚本不存在，说明原因，不得伪造测试结果。

---

### 5.4 功能回归清单

最终必须确认以下功能未改变：

```text
[ ] CAT1 模块上电初始化正常
[ ] CAT1 AT 指令状态机正常
[ ] MQTT OPEN 正常
[ ] MQTT CONN 正常
[ ] MQTT SUB 正常
[ ] 中科协议登录正常
[ ] MQTT 心跳正常
[ ] 属性上报正常
[ ] 远程控制正常
[ ] OTA URL 设置正常
[ ] OTA HTTP 下载正常
[ ] OTA 文件校验正常
[ ] OTA 搬运和重启正常
[ ] BL0942 电压采集正常
[ ] BL0942 电流采集正常
[ ] BL0942 功率采集正常
[ ] PWM 调光正常
[ ] 温度保护正常
[ ] 过流保护正常
[ ] 掉电检测正常
[ ] RTC/AIP1302 正常
[ ] 工作计划正常
[ ] Flash 参数保存和断电恢复正常
[ ] 看门狗正常
```

---

## 6. 最终交付物

Codex 完成后需要输出：

```text
1. Git 分支名
2. 所有 commit 列表
3. 所有 tag 列表
4. 优化前后固件体积对比
5. 优化前后 RAM 对比
6. 已完成优化项
7. 未完成或跳过的优化项及原因
8. 回退方法
9. 需要人工上板验证的项目
```

---

## 7. 最终回退说明

如果最终优化版本出现问题，可按以下方式回退。

### 回退到优化前

```bash
git checkout optimize/firmware-size-performance
git reset --hard baseline-before-size-perf-optimization
```

### 只回退日志优化

```bash
git revert <log_optimization_commit_hash>
```

### 只回退 HAL 裁剪

```bash
git revert <hal_pruning_commit_hash>
```

### 只回退 Flash 写前比较

```bash
git revert <flash_optimization_commit_hash>
```

### 只回退 BL0942 / UART2 修改

```bash
git revert <uart2_or_bl0942_commit_hash>
```

### 查看某个阶段版本

```bash
git checkout after-log-optimization
```

### 查看最终优化版本

```bash
git checkout final-size-perf-optimization
```

---

## 8. 给 Codex 的最终执行指令

请严格按照以下要求执行：

```text
请在 CAT1_Keil_Project 中一次性完成固件体积与 MCU 性能优化。优化必须在 Git 独立分支 optimize/firmware-size-performance 中完成，并且必须先创建 baseline commit 和 baseline tag。任务需要一次性完成，但每个优化阶段必须独立 commit，并在关键阶段打 tag，确保任何阶段都可以 git revert 或 reset 回退。

优化前必须记录当前 Keil 编译体积、map 信息和测试结果。优化过程中不得改变当前功能、协议、Flash 布局、OTA 流程、sys_data_st 结构大小、App 起始地址和 bootloader 兼容性。

需要完成的优化包括：Release Size 编译配置、生产日志关闭、十六进制 dump 关闭、UART3 DMA printf 完成标志修复、旧 HTTP 激活链路隔离、未使用 HAL SPI/CRC 裁剪、cJSON 安全使用优化、浮点打印移除、低风险浮点转整数、Flash 写前比较、UART2/BL0942 接收边界保护、BL0942 除零保护、CAT1 UART 队列丢包统计、可选主循环性能统计。

所有优化必须默认保持原功能。任何功能裁剪都必须有宏开关，并且默认开启原功能。完成后必须提供 commit 列表、tag 列表、优化前后 Code/RO/RW/ZI/bin 对比、测试结果、回归清单和明确的 git 回退命令。
```

---

## 9. 风险优先级说明

建议一次性执行时按以下顺序处理，风险从低到高：

```text
低风险：
1. Git baseline / branch / tag
2. 编译 map 输出
3. 日志宏开关
4. hex dump 宏开关
5. UART3 DMA printf 标志修复
6. Flash 写前比较
7. UART2 边界保护
8. BL0942 除零保护
9. CAT1 队列丢包统计

中风险：
10. 旧 HTTP 激活链路隔离
11. 未使用 HAL SPI/CRC 裁剪
12. cJSON 安全检查
13. 简单浮点转整数

高风险，需要谨慎：
14. 大规模替换 cJSON
15. 改 CAT1 UART1 发送为 DMA
16. 改 OTA 接收处理节奏
17. 裁剪 zk_sunriset 日出日落算法
```

本次优化建议完成低风险和中风险项目。  
高风险项目必须通过宏保留原行为，或者只输出建议，不强制改动。

---

## 10. 完成标准

本任务只有在以下条件全部满足时才算完成：

```text
[ ] Git 独立分支已创建
[ ] baseline tag 已创建
[ ] 每个优化阶段都有 commit
[ ] 关键阶段有 tag
[ ] 工作区最终干净
[ ] Keil 编译通过
[ ] map 文件生成
[ ] 固件体积有对比数据
[ ] APP_LOG_ENABLE=0 时生产日志关闭
[ ] APP_LOG_ENABLE=1 时调试日志可用
[ ] 旧 HTTP 激活链路不会影响当前主流程
[ ] OTA HTTP 下载流程未被删除
[ ] sys_data_st 仍为 408 字节
[ ] Flash 地址未改变
[ ] MQTT 协议未改变
[ ] OTA 功能未改变
[ ] BL0942 功能未改变
[ ] PWM / 保护 / RTC / 工作计划功能未改变
[ ] 提供明确回退命令
```

