# 单片机 Flash / RAM 内存分配详图

> **当前工程**：`MDK-ARM-8008000`，输出名 `cat1`
> **芯片型号**：HK32F103CCT6A（Keil 中配置为兼容的 STM32F103RC），256KB Flash + 48KB RAM
> **宏定义**：`USE_HAL_DRIVER, STM32F103xB, APROM_OFFSET`
> **链接器脚本**：`MDK-ARM-8008000/out/cat1.sct`
> **实测大小**：ROM 47.3KB / RAM 14.7KB（2026-05-16 map 文件）

---

## 一、Flash 分区（256KB / 0x08000000 ~ 0x0803FFFF）

下图为当前实际使用的 Flash 布局。分区边界由 `sys_data.h` 中的地址宏定义，链接器 scatter 文件负责将应用程序代码放置在 `0x08008000` 起始的 224KB 区域内。

```
0x08000000  ┌──────────────────────────────┐
            │  Bootloader (20KB)           │  BOOTROM_STARTADDR
            │  由独立的 bootloader 工程管理  │
0x08005000  ├──────────────────────────────┤
            │  系统参数区 (6KB)             │  DATAROM_STARTADDR
            │  sys_data_st (408B)          │  ← 所有持久化属性存储于此
            │  由 sys_data_store() 写入     │
0x08006800  ├──────────────────────────────┤
            │  系统参数备份区 (6KB)          │  BAKDATAROM_STARTADDR
            │  sys_data_st 的完整副本       │
0x08008000  ├──────────────────────────────┤  ← APROM_STARTADDR（应用程序入口）
            │  prog_checksum  @ +0x200     │  4B，供 bootloader 校验
            │  prog_length    @ +0x204     │  4B
            │  device_type    @ +0x208     │  2B
            │                              │
            │  应用程序代码 (~48KB 已用)     │  scatter 文件分配上限 224KB
            │  （逻辑设计预留 112KB）        │  实际代码结束于 ~0x08013CC4
            │                              │
            │  ─── 空闲区 ───               │  ~66KB 未使用
            │                              │
0x08024000  ├──────────────────────────────┤  ← OTABAKROM_STARTADDR（逻辑边界）
            │                              │    APROM_SAFE_ENDADDR
            │  OTA 固件备份区 (112KB)       │
            │  固件升级时暂存新固件          │  由 ota.c 的 flash_store() 写入
            │  传输粒度 = 512B/包           │
            │                              │
0x0803FFFF  └──────────────────────────────┘  ← 芯片物理尾部
```

### 关键地址宏定义（定义于 `sys_data.h`）

| 宏 | 地址 | 大小 | 状态 |
|---|---|---|---|
| `BOOTROM_STARTADDR` | `0x08000000` | 20 KB | bootloader 独占（独立工程） |
| `DATAROM_STARTADDR` | `0x08005000` | 6 KB | **活跃** — 系统参数主区 |
| `BAKDATAROM_STARTADDR` | `0x08006800` | 6 KB | **活跃** — 系统参数备份区 |
| `APROM_STARTADDR` | `0x08008000` | 112 KB (逻辑) / 224 KB (链接器) | **活跃** — 应用程序 |
| `APROM_SAFE_ENDADDR` | `0x08024000` | — | 应用程序安全区逻辑边界 |
| `OTABAKROM_STARTADDR` | `0x08024000` | 112 KB | **活跃** — OTA 备份区 |
| `OTABAKROM_ENDADDR` | `0x0803FFFF` | — | Flash 尾部 |

> **链接器 vs 逻辑分区的差异**：scatter 文件给 app 分配了 224KB（`0x08008000 ~ 0x08040000`），但逻辑设计只用了前 112KB（到 `0x08024000`），后半留给 OTA 备份。当前 app 实际仅 48KB，远未触及逻辑边界。

---

## 二、`sys_data_st` 结构体布局（408 字节）

这是**所有持久化属性的唯一存储结构**。运行时在 RAM 中有全局副本 `sys_data`，写入时整结构体通过 `sys_data_store()` 同时写入主区（`0x08005000`）和备份区（`0x08006800`）。

| 偏移 | 字段 | 大小 | 读写属性 | 说明 |
|------|------|------|---------|------|
| 0 | `sn` | 4B (u32) | 系统/O | 序列号；OTA 完成后置 `0xAA5555AA` 标记新固件待搬运 |
| 4 | `checksum1` | 4B (u32) | 系统 | 校验和 1 |
| 8 | `firmware_len` | 4B (u32) | OTA | 固件长度 |
| 12 | `bak_verson` | 4B (u32) | OTA | 备份版本号 |
| 16 | `mac` | 4B (u32) | 系统 | MAC 地址，默认 `0x80` |
| 20 | `ota_enable` | 4B (u32) | OTA | OTA 使能标志 |
| 24 | **`fa_Parambuf[128]`** | 128B | **属性** | **工厂参数区**（详见第四节） |
| 152 | `couter` | 4B (u32) | 系统 | 计数器 |
| 156 | **`ac_EnergyP`** | 4B (u32) | **属性** | **累积电能 (Wh)**，`0xFFFFFFFF` 时归零 |
| 160 | **`lamp_power`** | 1B (u8) | **属性** | **灯功率百分比 0-100** |
| 161 | `day` | 1B (u8) | 属性 | 统计天数 |
| 162 | **`today_Energy`** | 2B (u16) | **属性** | **今日电量 (0.1Wh)** |
| 164 | **`all_plan`** | 156B | **计划** | **定时计划区 — 7 条计划**（详见第三节） |
| 320 | **`temp_protect`** | 5B | **属性** | **温度保护配置**（详见下方） |
| 328 | **`setcur`** | 4B (u32) | **属性** | **设定电流 (mA)** |
| 332 | **`hwmaxcur`** | 4B (u32) | **属性** | **硬件最大电流 (mA)** |
| 336 | **`openid[33]`** | 33B | **属性** | **MQTT 登录 OpenID** |
| 369 | **`token[33]`** | 33B | **属性** | **MQTT 登录 Token** |
| 402 | `usedata_sn` | 2B (u16) | 系统 | 内部序列号 |
| 404 | `checksum` | 2B (u16) | 系统 | 结构体校验和（累加和算法，`0xA5` 起始防全零） |
| 406 | (padding) | 2B | — | 对齐到 4 字节边界 |

**温度保护子结构 `temp_protect_st`（5 字节）**：

| 偏移 (相对) | 字段 | 大小 | 说明 |
|---|---|---|---|
| 0 | `enable` | 1B (u8) | 过温保护使能，默认 `BOOL_TRUE` |
| 1 | `temp_protect` | 1B (s8) | 过温保护阈值，默认 `100` |
| 2 | `power` | 1B (u8) | 过温时功率 |
| 3 | `temp_recovery` | 1B (s8) | 温度恢复阈值 |
| 4 | `power_recovery` | 1B (u8) | 恢复后功率 |

---

## 三、定时计划区 `all_plan_st`（156 字节）

支持 **7 条独立定时计划**，运行时由主循环中的 `zk_work_plan_process()` 每周期轮询执行（`main.c:245`）。

```
all_plan_st (156 字节):
├── total: u8 (1B)              — 已配置的计划总数
├── plan[0..6]: plan_st × 7     — 7 条计划，每条 22 字节
│   ├── year: u8          (1B)  — 年
│   ├── mon: u8           (1B)  — 月
│   ├── day: u8           (1B)  — 日
│   ├── week: u8          (1B)  — 星期
│   ├── hour: u8          (1B)  — 时
│   ├── min: u8           (1B)  — 分
│   ├── plan_type: u8     (1B)  — 计划类型（位域: type[2bit] + on_off[1bit]）
│   ├── keep_time: u16    (2B)  — 持续时间
│   ├── keep_power: u8    (1B)  — 持续功率
│   ├── standby_time: u16 (2B)  — 待机时间
│   ├── standby_power: u8 (1B)  — 待机功率
│   ├── env_lux: u16      (2B)  — 环境照度阈值
│   ├── flag_plan: u8     (1B)  — 定时计划生效标志 ✅
│   └── end_time: u16     (2B)  — 结束时间
└── active_index: u8 (1B)       — 当前活跃计划索引
```

```c
typedef struct {
    u8 type:2;    // 计划类型 (2 bit)
    u8 on_off:1;  // 开关状态 (1 bit)
} plan_type_st;
```

**计划执行流程**：
1. MQTT 下发计划 JSON → `Json_Protocol.c` / `mqtt_zk_protocol.c` 解析
2. 写入 `sys_data.all_plan`
3. `sys_data_store()` 持久化到 Flash（`0x08005000` + `0x08006800` 双副本）
4. 主循环 `zk_work_plan_process()` 每周期检查 `flag_plan` 和时间条件，匹配则执行

---

## 四、工厂参数区 `fa_Parambuf[128]` 详解

工厂参数存储在 `sys_data_st.fa_Parambuf[128]`（偏移 24），随 `sys_data` 一起写入 Flash `0x08005000`。

**启动加载流程**：`sys_data_load()` → `factory_user_load_data()` → `memcpy(factory_user_buff, sys_data.fa_Parambuf, 128)` → 对各字段做大小端转换 → `fac_128_data_default()` 校验合法性。

| 偏移 | 字段 | 类型 | 说明 | 默认值 |
|------|------|------|------|--------|
| 0x04 | `SID` | u8 | 产品系列 | — |
| 0x05 | `MID` | u8 | 产品型号 (1=60W, 2=75W, 3=100W, **4=150W**, 5=200W, 6=240W) | 4 |
| 0x06 | `DRV_VERSION` | u8 | 驱动器版本 | — |
| 0x07 | `Protocol_version` | u8 | 协议版本 (0=旧二进制, 1=新 JSON/MQTT) | — |
| 0x10-11 | `SET_OUTCUR` | u16 | 额定电流（需大小端转换） | 2700 mA |
| 0x12-13 | `HWMAX_OUTCUR` | u16 | 硬件最大电流 | 4700 mA |
| 0x14-15 | `OUTPUT_CUR_SENSOR` | u16 | 输出电流传感器阻值（毫欧） | 30 |
| 0x16-17 | `OP_PWM_OFFSET` | u16 | 光耦延迟补偿（千分之一） | 0 |
| 0x18 | `INNRE_TEMP_PRO_EN` | u8 | 过温使能 | 1 (使能) |
| 0x19 | `INNRE_TEMP_PRO` | s8 | 过温保护值（℃） | 85 |
| 0x1E | `CX` | u8 | 电容值（uF × 100） | 0x44 (0.68uF) |
| 0x30-36 | `STAR_TIME` | 7B | 离线调光起始时间 | — |
| 0x37-3D | `END_TIME` | 7B | 离线调光结束时间 | — |
| 0x3E | `DAY_LOOP_EN` | u8 | 日循环调光使能 | — |
| 0x3F | `SCHEDULE_SIZE` | u8 | 单日调光动作数（上限 7） | 7 |
| 0x40+ | `SEVER_TIMER_DIM` | N×3B | 定时调光动作数组 `{hour, min, dim_lever}` | — |

---

## 五、OTA 固件升级流程

1. 新固件通过 MQTT 以 512 字节/包（`PICK_SIZE`）传输，每 20480 字节为一个服务端包（`SERVER_PICK_SIZE`）
2. 固件数据通过 `flash_store()` 逐包写入 `OTABAKROM_STARTADDR`（`0x08024000`）
3. 下载完成后，`sys_data.sn = 0xAA5555AA`，标记"新固件就绪"
4. 调用 `iap_jump2boot()`（`for_iap.c`）→ `NVIC_SystemReset()` 跳转 bootloader
5. Bootloader 执行：
   - 检查 `sys_data.sn == 0xAA5555AA`
   - 验证 OTA 备份区固件的校验和、长度、设备类型
   - 验证通过后将固件从 `0x08024000` 搬运到 `0x08008000`
   - 清零 `sys_data.sn`，跳转到新应用程序

### 应用程序固定地址常量（定义于 `for_iap.c`）

| 绝对地址 | 常量 | 大小 | 说明 |
|---|---|---|---|
| `0x08008200` | `prog_checksum` | 4B (u32) | 程序校验和 |
| `0x08008204` | `prog_length` | 4B (u32) | 程序长度 |
| `0x08008208` | `device_type` | 2B (u16) | 设备类型 |

这些常量在编译时通过 `__attribute__((section(...)))` 放置在 APROM 起始 + 0x200 处，供 bootloader 读取验证。

---

## 六、Flash 写入机制（属性持久化）

### 当前实际使用路径

```
属性变更（MQTT / 本地逻辑）
  → 修改 RAM 中的 sys_data 字段
  → sys_data_store()
    → struct_data_checksum_make()   // 计算累加和校验
    → data_store_data() → hw_flash_write_bytes(0x08005000, ...)  // 主区
    → data_store_data() → hw_flash_write_bytes(0x08006800, ...)  // 备份区
```

### 容错读取路径

```
sys_data_load()
  → data_load_data(0x08005000)           // 读主区
  → struct_data_check()                  // 校验
     ├─ 通过 → factory_user_load_data()  // 使用主区数据
     └─ 失败 → data_load_data(0x08006800) // 读备份区
              → struct_data_check()
                 ├─ 通过 → factory_user_load_data()
                 └─ 失败 → sys_data_default()  // 两区都坏，用默认值
```

### Flash 底层约束

- **页大小**：`FLASH_PAGE_SIZE = 0x800`（2KB）
- **写入前需擦除整页**，`temp_buf_byte[2048]` 用作页缓冲区（定义于 `hw_flash.c`）
- **写入函数**：`hw_flash_write_bytes()` → HAL 库 `HAL_FLASH_Program()`，半字（16bit）编程
- **系统数据大小**：408 字节，远小于一个 Flash 页

---

## 七、RAM 分配（48KB / 0x20000000 ~ 0x2000BFFF）

链接器分配：`RW_IRAM1 0x20000000 0x0000C000`（48KB），实测使用 ~15KB。

```
0x20000000  ┌──────────────────────────────────┐
            │  全局 / 静态变量区  (~12KB)        │
            │  ├─ rx_buffer[540]               │  串口 DMA 接收缓冲
            │  ├─ tx_buffer[540]               │  串口 DMA 发送缓冲
            │  ├─ temp_buf_byte[2048]          │  Flash 页操作缓冲（2KB）
            │  ├─ sys_data (408B)              │  系统参数全局副本
            │  ├─ factory_user_buff[128]       │  工厂参数 RAM 副本
            │  ├─ OTA 相关缓冲                  │  firm_name_buffer[256]
            │  │                               │  common_send_buff[256]
            │  ├─ cJSON / JSON 解析缓冲         │  协议栈动态解析
            │  └─ HAL 库 / 各模块静态变量        │
            ├──────────────────────────────────┤
            │  Heap: 0x200 (512B)              │  malloc 动态分配
            │  定义于 startup_stm32f103xe.s    │
            ├──────────────────────────────────┤
            │  Stack: 0x800 (2KB)              │  函数调用栈
            │  定义于 startup_stm32f103xe.s    │
0x2000BFFF  └──────────────────────────────────┘
```

| 指标 | 值 | 占比 |
|---|---|---|
| RAM 总量 | 48 KB | 100% |
| 实际 RW + ZI | 14.7 KB | 31% |
| Stack | 2 KB | 4% |
| Heap | 0.5 KB | 1% |
| 剩余可用 | ~30.8 KB | 64% |

---

## 八、主循环运行时模块一览

以下为 `main.c` 的 `while(1)` 中实际运行的处理函数（截至当前代码）：

| 函数 | 用途 | 状态 |
|---|---|---|
| `watchdog_feed_dog()` | 喂狗 | ✅ 运行 |
| `sys_pwm_fade_output()` | 软启动渐亮 | ✅ 运行（仅首次） |
| `hw_uart3_process()` | 4G 模组串口处理 | ✅ 运行 |
| `sys_tick_process()` | 系统时基 | ✅ 运行 |
| `hw_gateway_process()` | 4G 模组状态机 | ✅ 运行 |
| `uart_diam_process()` | DALI 调光 | ✅ 运行 |
| `adc_process()` | ADC 采集 | ✅ 运行 |
| `sys_temp_over_protect_process()` | 过温保护 | ✅ 运行 |
| `tcpClientProcess()` | MQTT TCP 客户端 | ✅ 运行（OTA 期间暂停） |
| `resetNbModule_machine()` | 4G 模组复位状态机 | ✅ 运行 |
| `_4G_configModule_machine()` | 4G 模组配置 | ✅ 运行 |
| `send_AT_Command_machine()` | AT 命令发送 | ✅ 运行 |
| `nbSendTcpData_sm()` | TCP 数据发送状态机 | ✅ 运行 |
| `sys_bl0942_process()` | 电能计量芯片 | ✅ 运行 |
| `_4G_OTA_machine()` | 4G 模组自身 OTA | ✅ 运行 |
| `mcu_copy_firmware_machine()` | MCU 固件搬运状态机 | ✅ 运行 |
| `sys_aip1302_process()` | RTC 时钟芯片 | ✅ 运行（初始化后） |
| `sys_pow_drop_check_process()` | 掉电检测 | ✅ 运行 |
| `danger_current_check_process()` | 过流保护 | ✅ 运行 |
| `error_report_process()` | 错误上报 | ✅ 运行 |
| `sys_pwm_process()` | PWM 调光处理 | ✅ 运行 |
| `sys_temp_low_protect_process()` | 低温保护 | ✅ 运行 |
| **`zk_work_plan_process()`** | **ZK 定时计划执行** | ✅ **运行** |
| **`json_process()`** | **ZK JSON 协议处理** | ✅ **运行** |
| `appProcess()` | 旧二进制协议 | ❌ 已注释 |
| `app_activate_process()` | 旧 HTTP 激活 | ❌ 已注释 |
| `http_congfig_fsm()` | 旧 HTTP 配置 | ❌ 已注释 |
| `http_post_fsm()` | 旧 HTTP 上报 | ❌ 已注释 |

---

## 九、已废弃 / 不再使用的 Flash 区域

以下区域定义于源码但不参与当前 cat1 MQTT 编译：

| 文件 | 地址 | 大小 | 原用途 | 废弃原因 |
|---|---|---|---|---|
| `flash_allocation.h` | `0x0801E000` | 8 KB | 编程器/工厂/用户数据分区 | `programmer_protocol.c` 已从工程移除 |
| `flash_allocation.h` | `0x0801E000` | 1 KB | UserBlock 数据 | 同上 |
| `flash_allocation.h` | `0x0801E400` | 6 KB | 工厂+用户参数（8槽×768B） | 工厂参数已合并到 `sys_data.fa_Parambuf` |
| `flash_allocation.h` | `0x0801FC00` | 1 KB | 编程器参数 | 同上 |

`sys_data.h` 顶部注释中的 **STM32F103CBT6 (128KB) 分区图**也是历史遗留，当前 256KB 芯片不使用该布局。

> 这些宏定义仍存在于源码中（`flash_allocation.h` 被 `hw_flash.c` include），但 `WRITE_START_ADDR` 仅在注释代码中引用，不影响链接和运行。

---

## 十、关键源文件索引

| 文件 | 与内存分配的关系 |
|---|---|
| `Core/Src/sys_data.h` | 芯片分区图注释、地址宏、`sys_data_st` 结构体（408B）、`all_plan_st`、`temp_protect_st` |
| `Core/Src/sys_data.c` | `sys_data_load()` 加载、`sys_data_store()` 持久化、校验和算法、`sys_data_default()` |
| `Core/Src/factory_user_data.h` | 工厂参数字段宏（`SID`, `MID`, `SET_OUTCUR` 等，映射到 `fa_Parambuf` 偏移） |
| `Core/Src/factory_user_data.c` | `factory_user_load_data()` 从 `sys_data.fa_Parambuf` 加载并转字节序、`fac_128_data_default()` |
| `Core/Src/common.h` | `APROM_OFFSET_ADDR` 逻辑、`RX_BUFF_LENGTH`/`TX_BUFF_LENGTH` 缓冲区大小 |
| `Core/Src/flash_allocation.h` | ⚠️ 废弃 — 旧编程器数据分区地址 |
| `Core/Src/hw_flash.h` | `FLASH_ADDR_SYS_DATA` / `FLASH_ADDR_SYS_DATA_BACKUP` 定义 |
| `Core/Src/hw_flash.c` | Flash 底层擦/写/读，`temp_buf_byte[2048]` 页缓冲 |
| `Core/Src/data_backup.c` | 双扇区冗余算法（序列号+CRC16），当前 `#if 0` 中，主路径用简单双写 |
| `Core/Src/for_iap.c` | `prog_checksum/length/type` 绝对地址常量、`iap_jump2boot()` |
| `Core/Src/LampProtocolLib/ota.c` | OTA 状态机，固件写入 `OTABAKROM_STARTADDR` |
| `Core/Src/LampProtocolLib/zk_work_plan.c` | ZK 协议定时工作计划执行引擎 |
| `Core/Src/LampProtocolLib/mqtt_zk_protocol.c` | MQTT ZK 协议、属性下发/上报 |
| `Core/Src/LampProtocolLib/Json_Protocol.c` | JSON 协议解析，属性读写 |
| `Core/Src/offline_Time_controlled_dimming.c` | 离线时间控制调光（基于 RTC，字段在 `fa_Parambuf` 0x30-0x3F） |
| `Core/Src/main.c` | 主程序入口、模块初始化序列、主循环 |
| `MDK-ARM-8008000/out/cat1.sct` | **权威** — 链接器 scatter 文件，定义实际 Flash/RAM 分配 |
| `MDK-ARM-8008000/startup_stm32f103xe.s` | Stack=2KB, Heap=512B |
| `MDK-ARM-8008000/project.uvprojx` | Keil 工程文件，定义芯片型号、宏、编译文件列表 |
