# 嵌入式C代码编写规范

***

## 一、架构设计原则

### 1.1 模块化 + 分层设计

项目采用**模块化 + 三层架构**：

```
应用层 (Application)    →  breathing_light, flash_light, zk_work_plan...
系统层 (System)         →  sys_tick, sys_data, sys_pwm, sys_bl0942...
硬件层 (Hardware)       →  hw_uart1, hw_tim2, hw_flash, hw_gateway...
```

**调用规则：**

- ✅ 系统层调用硬件层
- ✅ 应用层调用系统层
- ❌ 应用层**禁止**直接调用硬件层（功能特别简单的除外，如 led\_on/led\_off）
- ❌ 硬件层**禁止**调用系统层和应用层

### 1.2 Keil 工程分组

Keil 工程中按模块分组管理，每组对应一个独立的功能模块，组名与模块名一致：

```
Project
├── App/          (应用层模块)
│   ├── breathing_light.c
│   ├── flash_light.c
│   └── ...
├── System/       (系统层模块)
│   ├── sys_tick.c
│   ├── sys_data.c
│   └── ...
└── Hardware/     (硬件层模块)
    ├── hw_uart1.c
    ├── hw_flash.c
    └── ...
```

***

## 二、文件组织规范

### 2.1 文件命名

| 层级  | 前缀     | 示例                                      |
| --- | ------ | --------------------------------------- |
| 硬件层 | `hw_`  | `hw_uart1.c`, `hw_tim2.c`, `hw_flash.c` |
| 系统层 | `sys_` | `sys_tick.c`, `sys_data.c`, `sys_pwm.c` |
| 应用层 | 无前缀    | `breathing_light.c`, `flash_light.c`    |
| 协议库 | 无前缀    | `zk_work_plan.c`, `json_protocol.c`     |

- **C文件名 = H文件名 = 函数名前缀**
- 一个模块对应一对 `.c` + `.h` 文件
- 文件名全小写，单词间用下划线分隔

### 2.2 公共头文件

#### type.h — 数据类型定义

所有与数据类型相关的宏定义、typedef 都放在此文件：

- 基本类型别名：`u8`, `u16`, `u32`, `u64`, `s8`, `s16`, `s32`, `s64`
- 布尔类型：`boolt`, `boolean_en`, `BOOL_FALSE`, `BOOL_TRUE`
- 位操作宏：`bitset8`, `bitclr8`, `bitcheck8` 等
- 大小端转换宏：`u16h`, `u16l`, `u32h`, `u32l` 等
- 通用常量：`NULL`, `TRUE`, `FALSE`, `HIGH_LEVEL`, `LOW_LEVEL`

#### common.h — 公共声明

**要求所有模块都 include**。包含：

- MCU 头文件引用
- `type.h` 引用
- 系统主频定义：`SYS_BASE_FREQUENCY_HZ`
- 全局公共常量、宏定义
- 跨模块使用的全局变量 `extern` 声明
- 公共函数 `extern` 声明

### 2.3 文件头注释模板

每个 `.c` 文件必须以文件头注释块开头：

```c
/*************************************************************
程序功能：模块功能简述
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
```

### 2.4 Include 顺序

每个 `.c` 文件的 include 顺序：

1. 自身的 `.h` 文件（第一个）
2. 通用头文件（`common.h` — 通常由自身 .h 已包含）
3. 依赖的其他模块头文件（按字母或依赖层级排序）

```c
#include "sys_tick.h"        // 自身头文件
#include "hw_tim2.h"         // 依赖的硬件层
#include "buzzer.h"          // 依赖的应用层
```

***

## 三、命名规范

### 3.1 类型命名

| 类型   | 后缀规范  | 示例                                                          |
| ---- | ----- | ----------------------------------------------------------- |
| 枚举   | `_en` | `boolean_en`, `flash_light_state_en`, `whole_comm_state_en` |
| 结构体  | `_st` | `sys_data_st`, `temp_protect_st`, `plan_st`                 |
| 函数指针 | 无固定后缀 | `function_table`                                            |

### 3.2 变量命名

| 作用域  | 命名规范          | 示例                                                      |
| ---- | ------------- | ------------------------------------------------------- |
| 全局变量 | 模块名\_描述       | `sys_data`, `breathing_light_state`                     |
| 静态变量 | `_` 前缀 + 小写描述 | `_tick_h`, `_flash_light_state`, `_timer`               |
| 局部变量 | 小写下划线         | `system_tick`, `tmp`, `h1`, `h2`                        |
| 常量/宏 | 全大写 + 下划线     | `SLOW_SPEED`, `MODULE_TIMER_INTERVAL`, `SYS_TICK_CYCLE` |

### 3.3 函数命名

```
模块前缀_动词_名词()
```

- 模块前缀：同文件名（如 `sys_tick_`, `flash_light_`, `hw_uart1_`）
- 每个模块必须有三个标准函数（不是绝对，视需求而定）：

| 函数  | 命名              | 调用上下文                        | 职责          |
| --- | --------------- | ---------------------------- | ----------- |
| 初始化 | `xxx_init()`    | 初始化阶段                        | 初始化硬件、变量、状态 |
| 定时器 | `xxx_timer()`   | `sys_tick_process()` 中，每10ms | 递减计数器、周期性检查 |
| 主循环 | `xxx_process()` | `while(1)` 主循环中              | 状态机处理、业务逻辑  |

**额外命名约定：**

- 获取值：`xxx_get_yyy()` — 如 `sys_tick_get_tick()`
- 设置值：`xxx_set_yyy()`
- 检查状态：`xxx_check_yyy()`
- ISR回调：`xxx_cycle_handle()`

***

## 四、代码格式规范

### 4.1 缩进与空格

- **缩进：4个空格**（不使用 Tab 字符）
- 运算符两侧加空格：`a = b + c;`
- 逗号后加空格：`func(a, b, c);`
- 关键字后加空格：`if (`, `for (`, `while (`
- 函数名与括号之间无空格：`func(a, b);`

### 4.2 大括号风格

```c
// 函数：左大括号另起一行
void flash_light_init(void)
{
    _flash_light_state = sys_data.flash_light;
}

// 控制语句：左大括号与语句同行（或另起一行，保持一致）
if (condition)
{
    do_something();
}
else
{
    do_other();
}

// switch-case：case 内的代码块加大括号
switch(state)
{
    case STATE_A:
    {
        do_a();
    }
    break;
    case STATE_B:
    {
        do_b();
    }
    break;
}
```

### 4.3 空行规范

- 函数体开头无空行（第一行紧跟 `{` 后）
- 函数之间空2行
- 逻辑块之间空1行
- 文件末尾保留1个空行

***

## 五、主程序结构规范

```c
int main(void)
{
    // ========== 外设初始化 (HAL) ==========
    HAL_Init();
    SystemClock_Config();
    watchdog_init();
    MX_GPIO_Init();

    // ========== 硬件层初始化 ==========
    hw_uart3_init();
    hw_uart2_init();
    hw_uart1_init();
    hw_tim2_init();

    // ========== 系统层初始化 ==========
    sys_tick_init();
    sys_data_load();
    sys_bl0942_init();

    // ========== 应用层初始化 ==========
    zk_work_plan_init();

    // ========== 开中断 ==========
    __enable_irq();

    // ========== 主循环 ==========
    while (1)
    {
        watchdog_feed_dog();    // ⚠️ 第一行必须是喂狗，整个程序只允许在此处喂狗

        // 各模块 process 函数
        sys_tick_process();
        hw_gateway_process();
        adc_process();
        // ...
    }
}
```

### ⚠️ 喂狗规则（极其重要）

- **喂狗只允许在主循环 while(1) 的第一行**
- **严禁在定时器中断、其他中断中喂狗**
- 看门狗超时时间必须大于主循环最坏情况执行时间

***

## 六、模块编写规范

### 6.1 模块模板结构

每个模块 `.c` 文件按以下顺序组织：

```c
/* 1. 文件头注释 */
/*************************************************************
程序功能：xxx
...
*************************************************************/

/* 2. Include 区域 */
#include "module.h"           // 自身头文件
#include "dependency1.h"      // 依赖
#include "dependency2.h"

/* 3. 宏定义（模块私有） */
#define MODULE_CONSTANT  value

/* 4. 类型定义（模块私有） */
typedef enum { ... } module_state_en;

/* 5. 静态变量 */
static u16 _timer;
static module_state_en _state;

/* 6. 函数实现 */
// 6.1 辅助/私有函数 (static)
static void helper_func(void) { ... }

// 6.2 公开函数
void module_init(void) { ... }
void module_timer(void) { ... }
void module_process(void) { ... }
```

### 6.2 头文件模板

```c
#ifndef MODULE_NAME_H       // 注意：双下划线格式
#define MODULE_NAME_H

#include "common.h"

/* 公开类型定义 */
typedef enum { ... } xxx_en;
typedef struct { ... } xxx_st;

/* 公开变量 extern 声明 */
extern xxx_st module_data;

/* 函数声明 — 带完整注释 */
/************************************
功能描述：xxx
输入参数：xxx
输出返回：xxx
*************************************/
extern void module_init(void);

extern void module_timer(void);

extern void module_process(void);

#endif
```

### 6.3 函数注释规范

**每个函数必须有注释块：**

```c
/************************************
功能描述：读取32位系统节拍的值，每一步是 1/SYS_BASE_FREQUENCY_HZ 秒
输入参数：无
输出返回：32位系统节拍值
注意：调用时不能关闭中断，不能在中断里执行。
*************************************/
u32 sys_tick_get_tick(void)
{
    // ...
}
```

**注释块格式要点：**

- 使用 `/************************************/` 包裹（非 `/*****/`）
- 必须包含三个字段：
  - `功能描述：`（必填）
  - `输入参数：`（无参数写"无"）
  - `输出返回：`（无返回值写"无"）
- 可选字段：`注意：`、`参考：`

### 6.4 行内注释规范

```c
// 单行注释使用双斜杠（推荐）

/* 多行重要说明使用块注释 */

// ⚠️ 注释写在代码上方，不要写在行尾（特殊情况例外）
// 例如：ADC 校准
HAL_ADCEx_Calibration_Start(&hadc1);

// ❌ 避免这种：HAL_ADCEx_Calibration_Start(&hadc1);  // ADC校准

// 宏定义注释使用 /* ... */
#define MODULE_TIMER_INTERVAL    ((u32)10000*SYS_TOTAL_TICK_PER_US)  /* 10ms调用一次 */
```

***

## 七、关键设计规范

### 7.1 禁止死等（No Busy-Waiting）

每个函数处理完必须**立即退出**，不得做死循环等待。

```c
// ❌ 错误 — 死等
void wait_for_ready(void)
{
    while (!(REG & FLAG_READY));  // 死等！
}

// ✅ 正确 — 状态机
void check_ready(void)
{
    if (_state == WAITING && (REG & FLAG_READY))
    {
        _state = READY;
    }
}
```

**唯一例外：** `sys_tick_delay()` 用于微秒级精确延时（不得超过看门狗超时值）。

### 7.2 中断中接收数据必须先入队

```c
// ❌ 错误 — 在 ISR 中直接处理数据
void ISR_Handler(void)
{
    u8 dat = UART->DR;
    process_data(dat);  // 禁止！
}

// ✅ 正确 — 入队，主循环取用
void ISR_Handler(void)
{
    u8 dat = UART->DR;
    u32_queue_in(dat);  // 放入队列
}
// 主循环中：
void module_process(void)
{
    u8 dat;
    if (u32_queue_out(&dat))
    {
        process_data(dat);  // 安全处理
    }
}
```

### 7.3 状态机处理复杂逻辑

复杂逻辑必须采用**状态机**方式实现：

```c
typedef enum
{
    STATE_IDLE,
    STATE_UP,
    STATE_DOWN
} breathing_light_state_en;

void breathing_light(void)
{
    switch (breathing_light_state)
    {
        case STATE_IDLE:
        {
            if (condition) { breathing_light_state = STATE_UP; }
        }
        break;
        case STATE_UP:
        {
            // ...
        }
        break;
        case STATE_DOWN:
        {
            // ...
        }
        break;
    }
}
```

### 7.4 sys\_data 数据存储规范

- 添加新参数：在 `sys_data_st` 结构体中 `checksum` 字段**之前**添加
- 预设值：在 `sys_data_default()` 函数中设置
- 写入前检查与已有值是否相同（避免频繁擦写 Flash）
- 关键数据必须有主/备双区存储和校验

### 7.5 宏定义规范

```c
// 多语句宏使用 do-while(0) 包裹
#define MACRO_FUNC(a, b)  \
    do                     \
    {                      \
        func1(a);          \
        func2(b);          \
    } while (0)

// 宏参数用括号保护
#define MAX(x, y)  (((x) > (y)) ? (x) : (y))

// 条件编译用 #if，不用 #ifdef（便于判断值）
#if APP_LOG_ENABLE
    // ...
#endif
```

***

## 八、常见问题与禁止事项

| 类别    | ❌ 禁止               | ✅ 应该                   |
| ----- | ------------------ | ---------------------- |
| 喂狗    | 在中断中喂狗             | 只在 main() while(1) 第一行 |
| 死等    | 函数内 while(flag) 死等 | 使用状态机 + process() 分步   |
| ISR   | ISR 中直接处理业务逻辑      | 入队，主循环取用               |
| ISR   | ISR 中调用 printf     | 主循环中打印                 |
| Flash | 频繁写入 Flash         | 先比较再决定是否需要写入           |
| 层次    | 应用层直接调 HAL 库       | 通过系统层/硬件层封装            |
| 注释    | 函数无注释              | 每个函数必须有功能描述/输入/输出注释    |
| 缩进    | Tab + 空格混用         | 统一4空格                  |

***

## 九、审查检查清单

在代码提交前，请逐项确认：

- [ ] 文件头注释块完整（功能、环境、芯片、人员、日期）
- [ ] 每个函数有标准注释块（功能描述、输入参数、输出返回）
- [ ] 模块文件命名符合规范（hw\_/sys\_/无前缀）
- [ ] 函数命名使用模块前缀
- [ ] include 守卫使用 `__XXX_H` 双下划线格式
- [ ] while(1) 第一行是 watchdog\_feed\_dog()
- [ ] 无死等循环（sys\_tick\_delay 除外）
- [ ] ISR 中无业务逻辑调用
- [ ] 全局变量用 extern 在 .h 中声明，在 .c 中定义
- [ ] 静态变量使用 `_` 前缀
- [ ] 枚举类型以 `_en` 结尾
- [ ] 结构体类型以 `_st` 结尾
- [ ] 缩进为4空格，无 Tab 字符
- [ ] 无未使用的变量/函数（消除编译警告）
- [ ] 常量使用 `#define` 或 `const`，避免魔法数字

