# STM32F103/HK32F103 固件烧录指南

## 问题背景

此前使用 J-Link 的 `loadbin` 命令直接烧录固件时，出现设备无法启动的现象。
经 J-Link 调试诊断发现：APP 区存在已被擦除但未重新写入的 Flash 页（内容为 `0xFFFFFFFF`），
CPU 执行到此处遇到未定义指令，触发 HardFault 异常，CPU 陷入 HardFault 死循环。

IWDG（独立看门狗）在 APP 启动时被使能（超时约 2 秒），因此在 HardFault 后 IWDG 触发复位，
设备重新进入 Bootloader → 跳转 APP → 再次 HardFault → IWDG 复位，形成死循环。

## 根本原因

### J-Link `loadbin` 的扇区擦除策略

J-Link 的 `loadbin` 命令并非简单地将文件写入 Flash，而是采用"智能比较 + 按需扇区擦除"策略：

1. **比较**：读取目标 Flash 区域内容，与 .bin 文件逐字节比较
2. **识别**：定位存在差异的 Flash 扇区（Sector）
3. **擦除**：擦除有差异的整个扇区
4. **编程**：仅写入差异部分

### 扇区大小不匹配

J-Link 对 STM32F103CB 芯片的扇区（Sector）定义大小为 **2KB**。
而 STM32F103CB（中容量/128KB）的硬件 Flash 页（Page）大小实际为 **1KB**。

| 芯片型号 | 硬件 Flash 页大小 | J-Link 扇区定义 |
|----------|-------------------|-----------------|
| STM32F103CB (128KB) | 1KB | 2KB |

### 问题复现流程

```
初始状态：  Page 22 (0x0800D800) = 有效代码 ✓
            Page 23 (0x0800DC00) = 0xFFFFFFFF（已擦除，缺失数据）

1. 执行 loadbin xxx.bin 0x08008000
2. J-Link 发现 0x0800DC00 区域有差异 → 需要擦除该扇区
3. J-Link 擦除扇区 [0x0800D800 - 0x0800DFFF]（2KB）
   ├─ Page 22 (0x0800D800): 被擦除为 0xFF（原有的有效代码被销毁！）
   └─ Page 23 (0x0800DC00): 被擦除为 0xFF
4. J-Link 重新写入差异数据：
   ├─ Page 22: 不写入（因为与 bin 文件一致...但已被误擦除）
   └─ Page 23: 写入正确数据 ✓

结果状态：  Page 22 (0x0800D800) = 0xFFFFFFFF（被破坏！）
            Page 23 (0x0800DC00) = 有效代码 ✓

APP 执行到 0x0800D800 → HardFault → IWDG 复位 → 循环
```

这就是为什么每次 `loadbin` 烧录后，总有某个 1KB 区域缺少数据——
J-Link 擦除了 2KB 扇区，但只回写了其中 1KB。

## 解决方案

### 正确烧录流程（J-Link Commander）

使用 **先全量擦除目标区域，再写入固件** 的方式，避免扇区边界问题：

``` jlink
device STM32F103CB
si SWD
speed 1000
connect
halt

// 步骤1：全量擦除 APP 区（不要用扇区编号，直接用地址范围）
// 注意：只擦除APP主分区，保留0x08024000之后的OTA备份区
erase 0x08008000, 0x08023FFF

// 步骤2：写入固件（此时所有扇区已擦除，loadbin 只需编程，无需擦除）
loadbin firmware.bin, 0x08008000

// 步骤3：验证烧录结果
verifybin firmware.bin, 0x08008000

// 步骤4：确认 Boot 区未被影响
mem32 0x08000000 2

r
g
exit
```

### 烧录完成后

1. **断电重启**（关闭设备电源 ≥30 秒后重新上电）—— 掉电复位可以彻底清除 IWDG
2. 确认设备正常启动、PWM 输出正常

### 为什么不能用 `loadbin` 直接烧录

| 方式 | 风险 |
|------|------|
| `loadbin xxx.bin 0x08008000`（直接） | J-Link 按 2KB 扇区部分擦除，可能破坏相邻有效数据 |
| `erase 0x08008000, 0x08023FFF` + `loadbin` | 先全量擦除APP主分区后完整写入，保留OTA备份区 |

### 注意事项

- **仅擦除 APP 区**：地址范围 `0x08008000 - 0x08023FFF`，不触及 Boot 区（`0x08000000`）、参数区（`0x08005000`）和 OTA 备份区（`0x08024000`）
- **分区布局参考**：
  ```
  0x08000000 ~ 0x08004FFF  Bootloader (20KB)
  0x08005000 ~ 0x08007FFF  参数/数据区 (12KB)
  0x08008000 ~ 0x08023FFF  APP 区 (112KB)
  0x08024000 ~ 0x0803FFFF  OTA 备份区 (112KB)
  ```
- **每次烧录后务必 `verifybin`** 确认数据正确
- **烧录后必须断电重启**：系统复位（J-Link `r` 命令）不会清除 IWDG 状态

## 备选方案

如果需要在 Keil MDK 中配置自动烧录脚本：

1. 在 Keil 的 Options → Utilities → Configure Flash Menu Command 中
2. 使用 J-Link 命令行参数：`-CommanderScript flash_app.jlink`
3. 将上述烧录脚本保存为 `flash_app.jlink`，便于批量操作

## 版本信息

- J-Link 版本: V9.32
- 芯片型号: STM32F103CB / HK32F103CCT6A
- 更新日期: 2026-05-13
