# Codex 执行文档：MCU 时序 / MQTT / 喂狗 / OTA / 计量 / 日志 / 固件体积闭环修复（补全文件版）

> 适用对象：当前 STM32F103CBT6 / HK32F103CCT6A CAT.1 智慧电源工程，含 Boot 固件与 APP 固件。  
> 当前设备状态：设备已通过 J-Link 连接到电脑。  
> 执行目标：只修复已确认的 MCU 时序、MQTT 发布订阅、喂狗、OTA 校验、计量采集、日志输出、固件体积问题；不得擅自重构非目标功能。  
> 本文档为补全缺失文件后的最新版执行约束文档。

---

## 0. 最高执行原则

Codex 必须严格遵守以下原则：

1. **先静态确认，再局部修复，再逐项验证。**
2. **不得为了编译通过删除业务逻辑。**
3. **不得私自修改平台 MQTT 协议、Topic 命名、JSON 字段、数据点 ID、Flash 分区、Boot 跳转地址。**
4. **不得把 OTA 重新改回 `QHTTPREADFILE / UFS / Range / Content-Length` 依赖方案。**
5. **不得大范围重构工程目录、HAL 初始化流程、外设引脚、Keil 工程结构。**
6. **每完成一个功能修复，必须使用 J-Link 烧录 / 校验 / 实机验证。**
7. **所有修改必须可回退，必须在 Git 中形成小提交。**
8. **必须保留 Boot + APP 闭环，不允许只验证 APP 编译通过。**

---

## 1. 本次补全文件后的新增结论

本次补全文件包括：

- `Portable.c / Portable.h`
- `Protocol.c / Protocol.h`
- `zk_protocol_internal.h`
- `zk_runtime_stats.c / zk_runtime_stats.h`
- `zk_property.c / zk_property.h`
- `hw_tim1_pwm2.c / hw_tim1_pwm2.h`
- `hw_tim2.c / hw_tim2.h`
- `sys_serial_port.c / sys_serial_port.h`

补全后新增确认以下问题：

### 1.1 `Portable.c` 已确认存在 4G 接收队列丢包计数，但没有形成健康门控

`Portable.c` 中存在：

```c
#define UART_RECV_QUEUE_SIZE 4096
volatile uint32 usart_queue_drop_count = 0;

void saveUsartByte(uint8 byte)
{
    if(enqueue(&usartRecvQueue, byte) == 0)
    {
        usart_queue_drop_count++;
    }
}
```

这说明 4G UART 接收队列溢出已经能被统计，但是当前主循环喂狗逻辑没有把 `usart_queue_drop_count` 纳入健康判断。

**修复要求：**

- 保留 `usart_queue_drop_count`；
- 增加运行期诊断读取接口；
- MQTT / OTA 压力测试期间必须记录该计数；
- 若连续增长，判定为 4G 接收处理能力不足或主循环被阻塞；
- 不得在中断内打印日志。

---

### 1.2 `Portable.c` 的 `delayMs()` 是忙等，禁止在主业务路径新增使用

当前实现：

```c
static volatile uint32 timeDelay = 0;

void delayMs(uint32 ms)
{
    timeDelay = ms;
    while (timeDelay);
}
```

该函数依赖 10ms tick 中断递减 `timeDelay`，属于阻塞式忙等。

**修复要求：**

- 不得在 MQTT、OTA、计量、主循环业务路径新增 `delayMs()`；
- 4G 上电初始化中已有 `delayMs(750)` 暂时保留，但必须确认只在初始化阶段使用；
- 若存在运行期调用，改为状态机定时等待。

---

### 1.3 `sys_serial_port.c` 中 `sys_serial_port_data_in()` 被整段注释

当前 `sys_serial_port.c` 中，`sys_serial_port_data_in()` 的实现被 `/* ... */` 整段注释，`sys_serial_port_init()` 为空。

这意味着：

- PC 串口 IAP 协议入口可能已经失效；
- 离线编程器协议入口可能已经失效；
- 头文件仍声明 `sys_serial_port_data_in()` / `sys_serial_port_process()`，存在接口与实现不一致风险；
- 若工程中引用 `sys_serial_port_process()`，可能存在链接风险或由其他文件重复实现。

**Codex 必须先确认：**

1. 当前工程是否仍需要 PC 串口 IAP / 离线编程器协议；（已经不需要）
2. `sys_serial_port_process()` 是否在其他文件实现；
3. `sys_serial_port_data_in()` 是否被 UART 接收路径调用；
4. Boot OTA 是否依赖该串口路径。

**修复边界：**

- 若当前产品明确不使用该串口协议：保留禁用，但必须清理头文件声明或加宏保护，避免误调用；
- 若仍需要该协议：恢复实现，但必须加长度边界、状态超时、互斥状态校验；
- 不得把该功能与 4G MQTT OTA 混合改造。

---

### 1.4 `hw_tim2.c` 当前是高优先级空中断 / 历史残留

当前 `hw_tim2_timeout_callback()` 中实际业务回调被注释：

```c
switch (tim2_user)
{
    case TIM2_DALI:
    {
     //   hw_dali_tx_handle();
    }
    break;
    case TIM2_LED:
    {
       // app_led_timeout();
    }
    break;
}
```

并且 NVIC 优先级设置为：

```c
HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
```

这属于高优先级空中断 / 历史残留风险。

**修复要求：**

- 若 TIM2 当前不再使用：不要启动 TIM2，并将优先级降级；
- 若 DALI / LED 仍需 TIM2：恢复对应回调，并补齐验证；
- `hw_tim2_start(t)` 必须防止 `t <= DELAY_TIME + 1` 导致 ARR 下溢；
- 不允许保留最高优先级但没有实际用途的中断。

---

### 1.5 `zk_property.c` 的属性 Flash 主备机制方向正确，但必须验证布局不重叠

`zk_property.c` 使用主备 Flash 记录，并通过 magic / version / size / seq / checksum 校验。

当前地址：

```c
#define ZK_PROPERTY_FLASH_MAIN_ADDR   (DATAROM_STARTADDR + FLASH_PAGE_SIZE)
#define ZK_PROPERTY_FLASH_BACKUP_ADDR (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE)
```

`zk_runtime_stats.c` 又使用：

```c
#define ZK_RUNTIME_FLASH_OFFSET       0x200UL
#define ZK_RUNTIME_FLASH_MAIN_ADDR    (DATAROM_STARTADDR + FLASH_PAGE_SIZE + ZK_RUNTIME_FLASH_OFFSET)
#define ZK_RUNTIME_FLASH_BACKUP_ADDR  (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE + ZK_RUNTIME_FLASH_OFFSET)
```

这说明属性配置和运行时统计存储在同一数据页内不同偏移处。

**必须补充静态断言：**

```c
STATIC_ASSERT(sizeof(zk_property_flash_record_t) <= ZK_RUNTIME_FLASH_OFFSET);
STATIC_ASSERT((ZK_RUNTIME_FLASH_OFFSET + sizeof(zk_runtime_flash_record_t)) <= FLASH_PAGE_SIZE);
```

如果工程没有 `STATIC_ASSERT`，可定义兼容宏：

```c
#define STATIC_ASSERT_CONCAT_(a, b) a##b
#define STATIC_ASSERT_CONCAT(a, b) STATIC_ASSERT_CONCAT_(a, b)
#define STATIC_ASSERT(cond) typedef char STATIC_ASSERT_CONCAT(static_assert_, __LINE__)[(cond) ? 1 : -1]
```

---

### 1.6 `zk_property.h` 声明不完整

`zk_property.c` 中实现了：

```c
void zk_device_config_init(void);
void zk_device_config_refresh_iccid(void);
```

但 `zk_property.h` 当前只声明：

```c
const zk_device_config_t *zk_device_config_get(void);
boolean_en zk_device_config_restore_defaults(void);
boolean_en zk_device_config_commit(const zk_device_config_t *config);
```

**修复要求：**

补齐头文件声明，避免隐式声明或跨文件调用不一致：

```c
void zk_device_config_init(void);
void zk_device_config_refresh_iccid(void);
```

---

### 1.7 `Protocol.c` 中数据包构造和解析边界不足

当前 `Protocol.c` 中 `fillSendPack()` 返回失败，但大量调用方未检查返回值。`parseServerMessage()` 没有输入长度参数，仅根据报文内部 `msgLength` 读取数据，这对异常下发包不安全。

**修复要求：**

- 不改变旧协议字段；
- 增加长度边界检查；
- 每次 `fillSendPack()` 失败必须终止组包并返回错误；
- `parseServerMessage()` 建议新增长度参数版本，例如：

```c
uint8 *parseServerMessageSafe(uint8 *pack, uint16 pack_len, uint8 *msgType, uint8 *result);
```

旧函数可保留为兼容包装，但新调用路径必须使用安全版本。

---

### 1.8 `hw_tim1_pwm2_set_PWM_OUT()` 高频路径存在无条件日志

当前 PWM 输出函数中存在：

```c
printf("pwm=%d\r\n", pwm);
```

该函数可能在调光、温控、闭环控制中频繁调用，不能无条件打印。

**修复要求：**

- Release 固件必须关闭该打印；
- Debug 固件可通过独立宏打开；
- 不得在 PWM 高频路径打印完整日志。

---

## 2. 原有高风险问题仍然成立

补全文件后，以下原有结论保持不变，并且仍是优先修复对象。

---

### 2.1 主循环无条件喂狗，存在“假健康”

当前主循环在业务处理之前直接喂狗：

```c
while (1)
{
    watchdog_feed_dog();
    ...
}
```

这会导致：

- MQTT 状态机卡死仍然继续喂狗；
- OTA 状态异常仍然继续喂狗；
- BL0942 计量状态卡死仍然继续喂狗；
- 4G UART 接收队列持续溢出仍然继续喂狗；
- 主循环被日志 / Flash / 浮点计算拖慢仍然继续喂狗。

**修复要求：**

喂狗必须改为“健康门控喂狗”：

```c
if (mcu_health_is_ok())
{
    watchdog_feed_dog();
}
```

健康条件至少包括：

1. 主循环最大耗时未超过阈值；
2. 10ms tick 没有持续严重滞后；
3. MQTT AT 状态机没有超过最大 busy 时间；
4. MQTT 发布状态机没有超过最大 busy 时间；
5. OTA 状态机没有超过当前阶段最大时间；
6. 4G UART 队列没有持续溢出；
7. BL0942 计量状态机没有长期超时；
8. Flash 写入没有处于异常重复失败状态。

**注意：** OTA 擦写期间允许阶段性喂狗，但必须绑定 OTA 阶段健康判断，禁止无脑喂狗。

---

### 2.2 10ms 软件定时没有补偿机制

当前 `sys_tick_process()` 只在 `system_tick - _tick >= MODULE_TIMER_INTERVAL` 时补一次 10ms tick。如果主循环被阻塞 30ms / 100ms，只会补一次，导致所有 10ms 调度模块滞后。

**修复要求：**

采用有限补偿机制：

```c
#define SYS_TICK_MAX_CATCH_UP 3U

void sys_tick_process(void)
{
    u8 catch_count = 0;
    u32 now = system_tick;

    while ((now - _tick) >= MODULE_TIMER_INTERVAL && catch_count < SYS_TICK_MAX_CATCH_UP)
    {
        _tick += MODULE_TIMER_INTERVAL;
        sys_tick_cycle_handle();
        catch_count++;
    }

    if ((now - _tick) >= MODULE_TIMER_INTERVAL)
    {
        runtime_diag.tick_lag_count++;
        _tick = now;
    }
}
```

不得无限补偿，避免主循环被 timer 任务反向拖死。

---

### 2.3 MQTT 发布状态机没有严格等待 prompt 和发布确认

当前 MQTT 发布存在典型风险：

1. 发送 `AT+QMTPUBEX` 后固定延时；
2. 未严格等待 payload prompt；
3. 未等待 `+QMTPUBEX: 0,<msg_id>,0`；
4. QoS1 没有确认闭环；
5. 发布失败上层无法可靠感知；
6. 发布 busy 时存在上报丢失风险。

**修复要求：**

重写 MQTT 发布状态机，但不得修改平台协议字段：

```text
IDLE
  -> SEND_HEADER
  -> WAIT_PAYLOAD_PROMPT
  -> SEND_PAYLOAD
  -> WAIT_QMTPUBEX_ACK
  -> SUCCESS / FAIL
```

必须实现：

- 等待 `>` 或模块明确 payload 输入提示；
- 发送 payload 后等待 `+QMTPUBEX: 0,<msg_id>,0`；
- 超时失败必须释放状态机；
- 失败后允许有限重试；
- 发布失败要返回给 `mqtt_zk_protocol`；
- 关键响应、OTA 进度、巡检响应、心跳、周期上报必须分优先级排队；
- 不允许 busy 时直接静默丢弃关键响应。

---

### 2.4 蜂窝网络 AT 超时过短

当前 `send_AT_Command_machine()` 中 `waitCount` 基本按 20ms 递减一次。若 `QMTOPEN / QMTCONN / QMTSUB` 使用 50，则实际超时约 1 秒。

蜂窝网络中：

- `QMTOPEN` 可能需要几十秒；
- 网络波动时 `QMTCONN` 也不应按 1 秒失败；
- `QMTSUB` 应等待明确订阅确认。

**修复要求：**

定义清晰宏：

```c
#define NB_AT_TICK_MS                 20U
#define NB_QMTOPEN_TIMEOUT_MS         150000U
#define NB_QMTCONN_TIMEOUT_MS         30000U
#define NB_QMTSUB_TIMEOUT_MS          30000U
#define NB_QMTPUB_PROMPT_TIMEOUT_MS   5000U
#define NB_QMTPUB_ACK_TIMEOUT_MS      30000U
```

所有 AT 等待必须从“magic number waitCount”改成“毫秒宏换算”。

---

### 2.5 OTA 方向正确，但 Boot / APP / 校验必须闭环复核

当前 OTA 方向应保持：

- raw TCP / HTTP GET；
- HTTP 200 全量响应；
- 不依赖 `Content-Length`；
- 不依赖 Range；
- 不依赖 UFS 全量下载；
- 从固件头解析 size / checksum / device_type；
- 流式写入 OTA 备份区；
- 校验成功后触发 Boot 搬运。

**必须重点复核：**

1. APP Keil 工程是否定义 `APROM_OFFSET`；
2. APP 链接地址是否为 `0x08008000`；
3. `SCB->VTOR` 是否指向 APP 实际向量表；
4. APP 固件最高地址是否小于 `0x08024000`；
5. OTA 备份区是否为 `0x08024000 ~ 0x0803FFFF`；
6. APP 头字段位置是否为 `0x08008200 / 0x08008204 / 0x08008208`；
7. Boot 校验算法是否和 APP 侧 `get_checksum_status()` 一致；
8. `device_type` 是否固定为当前设备允许值；
9. 故意篡改 checksum / size / device_type 时 Boot 必须拒绝升级。

---

### 2.6 BL0942 计量采集存在浮点耗时和错误恢复不足

当前 BL0942 计量路径存在：

- `sqrtf / powf / double`；
- 计量换算耗时较高；
- 可能拉入较大的数学库；
- checksum 错误只打印；
- UART2 ORE / FE / NE 错误恢复不足；
- 计量状态机长期超时没有进入健康门控。

**修复要求：**

- 优先改为定点计算；
- 若暂时保留浮点，必须降低调用频率并统计耗时；
- 增加 `bl0942_checksum_error_count`；
- 增加 `bl0942_timeout_count`；
- 增加 USART2 错误回调恢复；
- 计量异常必须进入 `mcu_health_is_ok()` 判断。

---

### 2.7 日志输出影响时序与固件体积

当前风险包括：

- `APP_LOG_ENABLE` 默认打开；
- `printf` 被重定义到 DMA 打印；
- DMA 打印可能阻塞等待；
- MQTT payload 可能被完整打印；
- PWM 高频路径无条件打印；
- OTA / MQTT / BL0942 / Flash 路径日志过多。

**修复要求：**

Release 默认：

```c
#define APP_LOG_ENABLE      0
#define APP_OTA_LOG_ENABLE  1
#define APP_LOG_LEVEL       APP_LOG_LEVEL_ERROR
```

日志分级：

```c
LOGE / LOGW / LOGI / LOGD
```

要求：

- Release 只保留错误和关键 OTA 进度；
- 不打印完整 MQTT payload；
- 不在 ISR 打印；
- 不在高频 PWM / 计量路径打印；
- `dma_printf()` 忙时直接丢弃并计数，禁止阻塞主循环。

---

## 3. Codex 允许修改范围

Codex 只允许修改以下范围内文件：

### 3.1 允许修改

- `Core/Src/main.c`
- `Core/Src/sys_tick.c`
- `Core/Src/watchdog.c`
- `Core/Inc/watchdog.h`
- `Core/Src/LampProtocolLib/NbDriver.c`
- `Core/Src/LampProtocolLib/TcpClient.c`
- `Core/Src/LampProtocolLib/mqtt_zk_protocol.c`
- `Core/Inc/LampProtocolLib/mqtt_zk_protocol.h`
- `Core/Src/LampProtocolLib/ota.c`
- `Core/Inc/LampProtocolLib/ota_config.h`
- `Core/Src/sys_bl0942.c`
- `Core/Src/hw_uart2.c`
- `Core/Src/hw_uart3.c`
- `Core/Inc/common.h`
- `Core/Src/zk_property.c`
- `Core/Inc/zk_property.h`
- `Core/Src/zk_runtime_stats.c`
- `Core/Src/sys_serial_port.c`
- `Core/Inc/sys_serial_port.h`
- `Core/Src/hw_tim2.c`
- `Core/Inc/hw_tim2.h`
- `Core/Src/hw_tim1_pwm2.c`
- `Core/Src/Protocol.c`
- `Core/Inc/Protocol.h`
- Keil 工程文件，仅限：宏定义、map 输出、编译优化等级、是否生成 bin/hex。

### 3.2 禁止修改

未经明确确认，禁止修改：

- MQTT 平台协议字段；
- Topic 规则；
- 设备 IMEI / SN 生成规则；
- Flash 分区基地址；
- Boot 跳转地址；
- Boot 校验算法；
- 设备类型 `device_type`；
- 硬件引脚定义；
- UART 波特率；
- BL0942 硬件通信格式；
- OTA 服务器协议假设。

如果必须修改 Boot，必须先单独提交分析报告，并说明：

- 修改原因；
- 修改文件；
- 修改前后地址；
- 对 APP 的影响；
- J-Link 回退方案。

---

## 4. 执行阶段与验证要求

---

## 阶段 0：建立基线，不改代码

### 目标

确认当前工程真实状态，避免盲改。

### 必须执行

1. 建立 Git 分支：

```bash
git status
git checkout -b fix/mcu-timing-mqtt-ota-watchdog
```

2. 记录当前编译结果：

- 是否能编译通过；
- warning 数量；
- APP bin / hex 大小；
- Boot bin / hex 大小；
- map 文件中 ROM / RAM 占用；
- 是否定义 `APROM_OFFSET`；
- APP 链接基地址；
- Boot 链接基地址。

3. 保存基线产物：

```text
out/baseline/
  build_log.txt
  app.map
  app.hex
  app.bin
  boot.map
  boot.hex
  boot.bin
```

### 阶段 0 验收

- 不允许改代码；
- 输出基线报告；
- 若当前无法编译，先记录错误，不允许直接大改。

---

## 阶段 1：Boot / APP / Flash / 配置一致性检查

### 目标

先解决 OTA 和地址基础问题，防止后续实机烧录不可启动。

### 必须检查

1. APP 是否定义 `APROM_OFFSET`；
2. `APROM_OFFSET_ADDR` 是否为 `0x08008000`；
3. APP 链接地址是否为 `0x08008000`；
4. APP 固件是否小于 `0x08024000 - 0x08008000`；
5. APP 头字段是否在 `0x08008200 / 0x08008204 / 0x08008208`；
6. Boot 是否跳转 `0x08008000`；
7. Boot 校验是否跳过 checksum / size 字段；
8. OTA 备份区是否不会覆盖数据区；
9. `zk_property_flash_record_t` 和 `zk_runtime_flash_record_t` 是否无重叠。

### 必须修复

- `zk_property.h` 补齐 `zk_device_config_init()` 与 `zk_device_config_refresh_iccid()` 声明；
- 增加属性配置与运行时统计 Flash 记录的静态断言；
- 若 Keil 未定义 `APROM_OFFSET`，必须修复 Keil 宏或链接脚本；
- 不得改变原 Flash 分区，除非输出单独报告。

### 阶段 1 验收

- APP 编译地址正确；
- J-Link 读取 `0x08008000` 可见 APP 向量表；
- J-Link 读取 `0x08008200` 可见 checksum / size / device_type；
- Boot + APP 均能独立编译。

---

## 阶段 2：MCU 主循环时序与喂狗修复

### 目标

解决主循环假健康、10ms tick 滞后、阻塞无监控问题。

### 必须实现

新增运行时诊断结构，例如：

```c
typedef struct
{
    u32 main_loop_count;
    u32 main_loop_max_cost_ms;
    u32 tick_lag_count;
    u32 mqtt_busy_timeout_count;
    u32 mqtt_pub_timeout_count;
    u32 ota_stage_timeout_count;
    u32 uart1_queue_drop_snapshot;
    u32 bl0942_timeout_count;
    u32 bl0942_checksum_error_count;
    u32 log_drop_count;
} mcu_runtime_diag_t;
```

实现：

```c
boolean_en mcu_health_is_ok(void);
void mcu_runtime_diag_process(void);
```

修改主循环：

- 禁止 while 顶部无条件喂狗；
- 改为一轮业务处理后健康门控喂狗；
- OTA 擦写阶段允许局部喂狗，但必须检查 OTA 阶段状态。

修复 `sys_tick_process()`：

- 增加有限 catch-up；
- 记录 tick lag；
- 不允许无限补偿。

### 阶段 2 验收

使用 J-Link / RTT / 受控日志验证：

- 主循环持续运行；
- 10ms tick 不持续滞后；
- IWDG 不误复位；
- 人为制造 MQTT busy 超时后，健康状态能识别异常；
- 停止关键状态机时，不应继续假健康喂狗。

---

## 阶段 3：MQTT 发布订阅闭环修复

### 目标

让 MQTT 发布订阅从“能发 AT”变成“有确认闭环”。

### 必须实现

#### 3.1 AT 超时宏化

将所有 MQTT 关键 AT 超时改为毫秒宏：

```c
#define NB_AT_TICK_MS                 20U
#define NB_QMTOPEN_TIMEOUT_MS         150000U
#define NB_QMTCONN_TIMEOUT_MS         30000U
#define NB_QMTSUB_TIMEOUT_MS          30000U
#define NB_QMTPUB_PROMPT_TIMEOUT_MS   5000U
#define NB_QMTPUB_ACK_TIMEOUT_MS      30000U
```

#### 3.2 MQTT 发布状态机

必须改为：

```text
PUB_IDLE
PUB_SEND_HEADER
PUB_WAIT_PROMPT
PUB_SEND_PAYLOAD
PUB_WAIT_ACK
PUB_SUCCESS
PUB_FAIL
```

要求：

- Header 发送成功后等待 prompt；
- Payload 发送后等待 `+QMTPUBEX` ack；
- 成功后通知上层；
- 失败后通知上层；
- 超时释放状态；
- 不允许状态永久 busy；
- 不允许失败后静默丢弃关键响应。

#### 3.3 发布队列

至少实现小型发布队列：

```text
Priority 0: 控制回复 / 属性写回复 / OTA 错误
Priority 1: OTA 进度 / 巡检响应
Priority 2: 心跳 / 周期上报
```

如果 RAM 紧张，可以先实现 4~8 项队列，但必须保证：

- 队列满有计数；
- 关键消息不能被普通周期上报覆盖；
- OTA 期间不允许周期上报挤占 OTA 错误上报。

#### 3.4 订阅与接收解析

必须确认：

- `+QMTRECV` topic 被解析；
- payload 长度被验证；
- JSON 只从匹配 topic 处理；
- 不使用无边界 `{` 到 `}` 截取作为唯一逻辑；
- 收到下发控制后必须有响应上报。

### 阶段 3 验收

通过 MQTT 工具验证：

1. 设备上电后连接 MQTT；
2. 成功订阅平台下发 topic；
3. 平台下发属性读取，设备回复；
4. 平台下发属性写入，设备回复；
5. 平台下发开关 / 调光，设备执行并回复；
6. 人为断网后重连，设备重新订阅；
7. 连续 20 次发布，无 busy 卡死，无队列溢出；
8. `+QMTPUBEX` ack 成功率记录在测试报告中。

---

## 阶段 4：OTA 校验与实机闭环

### 目标

验证 OTA 流式下载、备份区写入、校验、Boot 搬运、APP 启动完整闭环。

### 禁止事项

- 禁止恢复 `QHTTPREADFILE`；
- 禁止使用 UFS 全量缓存；
- 禁止依赖 `Content-Length`；
- 禁止依赖 Range / 206；
- 禁止为了测试跳过 checksum / size / device_type 校验。

### 必须验证

#### 4.1 正常 OTA

流程：

1. J-Link 烧录 Boot；
2. J-Link 烧录当前 APP；
3. 设备正常连接 MQTT；
4. 下发 OTA URL；
5. 设备 raw TCP HTTP GET；
6. 接收 HTTP 200；
7. 从固件头解析 size / checksum / device_type；
8. 流式写入 OTA 备份区；
9. 校验备份区；
10. 设置升级标志；
11. 重启进入 Boot；
12. Boot 校验并搬运 APP；
13. 新 APP 启动；
14. MQTT 上报新版本 / OTA 成功。

#### 4.2 异常 OTA

必须分别测试：

- checksum 错误；
- size 错误；
- device_type 错误；
- 网络中断；
- HTTP 非 200；
- 固件超出 APP 区；
- 固件头缺失；
- 下载过程中 4G UART 队列溢出。

异常情况下：

- 不允许覆盖当前可运行 APP；
- 不允许 Boot 跳转到坏 APP；
- 不允许 IWDG 误复位循环；
- 必须 MQTT 上报失败原因，若 MQTT 不可用则保留本地错误码。

---

## 阶段 5：BL0942 计量采集与 UART2 恢复

### 目标

减少计量耗时、增加错误恢复、避免计量卡死影响主循环。

### 必须修复

1. 增加 USART2 错误回调：

```c
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        bl0942_uart_error_count++;
        HAL_UART_AbortReceive_IT(huart);
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        // 安全复位 BL0942 接收状态机
    }
}
```

2. 增加：

```c
bl0942_checksum_error_count
bl0942_timeout_count
bl0942_uart_error_count
```

3. 将 `sqrtf / powf / double` 优先改为定点计算；
4. 统一输入电压 / 输入电流 / 输入功率 / PF / 漏电流单位；
5. 计量数据上报前必须确认是否为最新数据；
6. 计量异常进入健康门控。

### 阶段 5 验收

- 连续采集 30 分钟；
- 无 UART2 ORE 卡死；
- checksum 错误计数不持续增长；
- MQTT 上报数值单位正确；
- 主循环最大耗时未因计量计算明显抖动；
- 固件体积较修复前不增加，最好下降。

---

## 阶段 6：日志输出与固件体积压缩

### 目标

减少时序阻塞与 ROM 占用。

### 必须修复

1. Release 默认关闭普通日志；
2. OTA 只保留关键进度和错误；
3. MQTT 不打印完整 payload；
4. PWM 高频路径不打印；
5. `dma_printf()` 改为非阻塞：忙则丢弃并计数；
6. 输出 map Top 20 大符号；
7. 检查是否仍拉入 `printf` 浮点、`sqrtf`、`powf`、`double` 相关库；
8. 检查 cJSON 静态池是否过大；
9. 保证 OTA 旧分支被宏裁剪。

### 阶段 6 验收

输出：

```text
固件体积对比：
Baseline APP bin: xxx bytes
Fixed APP bin:    xxx bytes
变化:             xxx bytes

Map Top 20 ROM symbols:
...

Map Top 20 RAM symbols:
...
```

APP 固件必须满足：

```text
APP_SIZE < 0x08024000 - 0x08008000
```

建议预留至少 4KB 安全余量。

---

## 阶段 7：`sys_serial_port` / `hw_tim2` / `Protocol` 历史遗留处理

### 目标

处理补全文件后确认的历史残留，不破坏主链路。

### 7.1 `sys_serial_port`

Codex 必须给出结论：

```text
当前产品是否需要 PC 串口 IAP / 离线编程器协议：是 / 否
是否有调用 sys_serial_port_data_in：是 / 否
是否有调用 sys_serial_port_process：是 / 否
```

若不需要：

- 使用宏显式禁用；
- 头文件声明保持一致；
- 不产生链接隐患。

若需要：

- 恢复 `sys_serial_port_data_in()`；
- 补齐 `sys_serial_port_process()`；
- 增加长度检查和状态超时；
- 验证不会与 4G OTA 冲突。

### 7.2 `hw_tim2`

Codex 必须给出结论：

```text
TIM2 当前是否被 DALI 使用：是 / 否
TIM2 当前是否被 LED 使用：是 / 否
TIM2 当前是否仍需初始化：是 / 否
```

修复要求：

- 不使用则不启动，优先级降级；
- 使用则恢复回调；
- `hw_tim2_start()` 防止 ARR 下溢；
- 不允许最高优先级空中断。

### 7.3 `Protocol.c`

修复要求：

- 组包失败必须返回错误；
- 解析必须长度安全；
- 不改变旧协议 ID；
- `MSG_TYPE_READ_RESPONSE == 0x84` 当前注释说明为平台要求，不得擅自改回 0x86。

---

## 5. J-Link 烧录与校验要求

当前设备已经连接 J-Link，Codex 必须使用 J-Link 完成烧录和校验。

### 5.1 生成 J-Link 脚本

Codex 需根据实际芯片和产物生成脚本，例如：

```text
si SWD
speed 4000
device STM32F103CB
r
h
loadfile out/boot.hex
loadfile out/app.hex
r
q
```

如果实际芯片为 HK32F103CCT6A，且 J-Link device 名称不同，Codex 必须使用当前 Keil / J-Link 可识别的芯片名，不得硬编码错误 device。

### 5.2 APP 校验脚本

如果产物为 bin：

```text
si SWD
speed 4000
device STM32F103CB
r
h
verifybin out/app.bin, 0x08008000
mem32 0x08008000, 8
mem32 0x08008200, 3
r
q
```

如果产物为 hex：

```text
si SWD
speed 4000
device STM32F103CB
r
h
loadfile out/app.hex
verifyfile out/app.hex
mem32 0x08008000, 8
mem32 0x08008200, 3
r
q
```

### 5.3 烧录注意事项

- 不得误擦除 Boot；
- 不得误擦除数据区，除非当前测试明确需要恢复出厂；
- 不得误擦除 OTA 备份区，除非 OTA 测试准备阶段要求；
- 每次烧录后必须 verify；
- 每次 verify 日志必须保存。

---

## 6. 最终逐项验证清单

Codex 执行完成后，必须逐项验证，不允许只说“编译通过”。

### 6.1 Boot / APP 启动验证

- [ ] J-Link 烧录 Boot 成功；
- [ ] J-Link 烧录 APP 成功；
- [ ] APP 向量表地址正确；
- [ ] 设备上电能启动 APP；
- [ ] 无 IWDG 反复复位；
- [ ] APP 版本能被读取 / 上报。

### 6.2 MQTT 发布订阅验证

- [ ] 4G 模块上电；
- [ ] 网络注册成功；
- [ ] MQTT open 成功；
- [ ] MQTT conn 成功；
- [ ] MQTT sub 成功；
- [ ] 下发属性读取，设备回复；
- [ ] 下发属性写入，设备回复；
- [ ] 下发开关，设备执行并回复；
- [ ] 下发调光，设备执行并回复；
- [ ] 连续发布 20 次，均收到 `+QMTPUBEX` 成功确认；
- [ ] 无发布状态机永久 busy。

### 6.3 MCU 业务逻辑验证

- [ ] 主循环持续运行；
- [ ] 10ms tick 无持续滞后；
- [ ] 温控逻辑正常；
- [ ] PWM 输出正常；
- [ ] 计划任务不被 MQTT / OTA 阻塞；
- [ ] 4G 重启状态机不阻塞主循环。

### 6.4 喂狗验证

- [ ] 正常业务下持续喂狗；
- [ ] MQTT busy 超时能被健康门控识别；
- [ ] OTA 长时间下载不误复位；
- [ ] Flash 擦写期间不误复位；
- [ ] 人为卡死关键状态机时不继续假健康喂狗。

### 6.5 OTA 验证

- [ ] 正常固件 OTA 成功；
- [ ] 错误 checksum 固件 OTA 失败且不覆盖 APP；
- [ ] 错误 size 固件 OTA 失败且不覆盖 APP；
- [ ] 错误 device_type 固件 OTA 失败且不覆盖 APP；
- [ ] 网络中断 OTA 失败且可恢复；
- [ ] OTA 成功后 Boot 搬运并启动新 APP；
- [ ] OTA 失败后旧 APP 仍能正常启动；
- [ ] OTA 过程不依赖 Content-Length；
- [ ] OTA 过程不依赖 Range / 206；
- [ ] OTA 过程不使用 QHTTPREADFILE / UFS 全量缓存。

### 6.6 计量采集验证

- [ ] BL0942 连续读取正常；
- [ ] 电压 / 电流 / 功率 / PF / 漏电流单位正确；
- [ ] UART2 无 ORE 卡死；
- [ ] checksum 错误不会导致状态机永久卡死；
- [ ] 计量异常能被统计；
- [ ] 计量计算不会明显拖慢主循环。

### 6.7 日志与固件体积验证

- [ ] Release 普通日志关闭；
- [ ] OTA 关键日志保留；
- [ ] PWM 高频日志关闭；
- [ ] MQTT payload 不完整打印；
- [ ] DMA 日志不阻塞主循环；
- [ ] 固件大小低于 APP 区限制；
- [ ] map 文件输出 ROM/RAM 差异。

---

## 7. Codex 最终必须输出的交付物

Codex 完成后必须输出：

```text
out/final_report/
  01_baseline_build_report.md
  02_static_issue_report.md
  03_patch_summary.md
  04_build_size_compare.md
  05_jlink_flash_verify_log.txt
  06_mqtt_pub_sub_test_log.md
  07_ota_test_log.md
  08_watchdog_timing_test_log.md
  09_bl0942_metering_test_log.md
  10_final_acceptance_checklist.md
```

`03_patch_summary.md` 必须包含：

- 修改了哪些文件；
- 每个文件为什么修改；
- 是否影响协议；
- 是否影响 Flash 分区；
- 是否影响 Boot；
- 是否影响硬件引脚；
- 如何回退。

---

## 8. 回退方案

必须保留三个可回退点：

1. **baseline**：原始可编译状态；
2. **after_boot_flash_check**：只完成地址 / 分区 / 编译修复；
3. **after_mqtt_watchdog_fix**：完成 MQTT 和喂狗但未动 OTA；
4. **after_ota_fix**：完成 OTA 验证。

每个阶段必须 Git commit：

```bash
git add .
git commit -m "fix: validate boot app flash layout"
git commit -m "fix: add watchdog health gate and tick lag diagnostics"
git commit -m "fix: make mqtt publish wait prompt and ack"
git commit -m "fix: validate ota stream and boot handoff"
git commit -m "fix: harden metering uart and reduce release logs"
```

禁止一次性大提交。

---

## 9. 当前最高优先级排序

Codex 必须按以下顺序执行：

1. **确认 APP 地址 / Boot 跳转 / OTA 校验一致性；**
2. **修复喂狗假健康；**
3. **修复 10ms tick 滞后与主循环耗时诊断；**
4. **修复 MQTT 发布 prompt / ack 闭环；**
5. **修复 MQTT 订阅 / 接收 topic 解析边界；**
6. **验证 OTA raw TCP 闭环；**
7. **修复 BL0942 UART2 错误恢复与浮点体积问题；**
8. **关闭 Release 高频日志；**
9. **处理 `sys_serial_port` / `hw_tim2` 历史残留；**
10. **输出 J-Link、MQTT、OTA、计量、喂狗完整验证报告。**

---

## 10. 禁止 Codex 使用的错误修复方式

严禁以下行为：

- 为了 MQTT 成功，把 QoS 改为 0 后不验证 ack；
- 为了 OTA 成功，跳过 checksum；
- 为了固件变小，删除业务功能；
- 为了不复位，继续无条件喂狗；
- 为了编译通过，注释掉错误代码不解释；
- 为了省事，把 Boot 和 APP 合并；
- 为了临时成功，改 Flash 分区；
- 为了避免日志阻塞，直接删除所有 OTA 错误日志；
- 为了通过测试，只测 MQTT 心跳，不测控制回复；
- 为了通过 OTA，只测下载，不测 Boot 搬运；
- 为了通过计量，只看串口有数据，不校验单位和错误恢复。

---

## 11. 最终验收标准

只有同时满足以下条件，才算任务完成：

1. Boot + APP 编译通过；
2. APP 链接地址正确；
3. J-Link 烧录和 verify 通过；
4. 设备上电正常执行业务逻辑；
5. MQTT 订阅成功；
6. MQTT 发布等待 prompt 和 ack；
7. 开关 / 调光 / 属性读写都有回复；
8. OTA 正常固件升级成功；
9. OTA 异常固件被拒绝；
10. 旧 APP 在 OTA 失败后仍可启动；
11. IWDG 不再无条件喂狗；
12. 主循环和 10ms tick 有诊断数据；
13. BL0942 计量连续运行稳定；
14. Release 日志不会阻塞主循环；
15. APP 固件大小低于 APP 区限制并保留余量；
16. 输出完整测试报告。

