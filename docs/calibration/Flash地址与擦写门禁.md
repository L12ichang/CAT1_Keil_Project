# 历史实现快照：旧校准 Flash 地址与共享页布局

> **状态：LEGACY / DEPRECATED / 不得作为新持久化布局依据**

本文件原来描述的是当前旧源码如何把系统参数、Property、Runtime、OTA Report、Calibration 和 Plan 切在 12KiB 数据区中。该布局存在多个逻辑记录共享同一 2KiB 物理擦除页的问题，只能作为现状证据。

## 1. 旧源码布局

旧实现包含：

- `0x08005000~0x08005800` 系统参数主区；
- `0x08005800~0x08006000` 内继续切 Property/Runtime/OTA/Calibration A；
- `0x08006000~0x08006800` Plan Main；
- `0x08006800~0x08007000` 系统参数 Backup；
- `0x08007000~0x08007800` 内继续切 Property/Runtime/OTA/Calibration B；
- `0x08007800~0x08008000` Plan Backup。

旧 Calibration A/B 各 1KiB，但并不是独立物理擦除页。`hw_flash_write_bytes_checked()` 实际仍需重写所在完整 2KiB 页，因此不同事务之间存在掉电耦合。

## 2. 目标新布局

新实施目标以 `docs/CAT1_50W校准固件基线与上位机对接方案.md` 为准：

```text
0x08005000~0x08005800  Config A
0x08005800~0x08006000  Config B
0x08006000~0x08006800  Calibration A
0x08006800~0x08007000  Calibration B
0x08007000~0x08007800  Runtime A
0x08007800~0x08008000  Runtime B
```

每个 Owner 独占一个 2KiB 物理擦除页。

## 3. 新 A/B 原则

```text
读取当前有效页
→ 在RAM构造完整新快照
→ 擦除非活动页
→ 写完整新记录
→ 回读/CRC
→ 最后写Commit
→ 切换Generation
```

禁止：

- 不相关事务共享一个物理擦除页；
- 在当前有效页原地局部修改；
- 主区写完后立即再写备区作为所谓“双备份”；
- Calibration 每个点都写 Flash。

## 4. 版本说明

旧源码中的 `SYS_CALIBRATION_STORAGE_FORMAT_VERSION=3` 只表示旧 Storage Record 实现。

目标新结构在逐 Byte 格式冻结前统一称为 `Target Calibration Record`，不得从本文件推导“Record V2/V3”目标版本。

## 5. 仍需实机证明

最终放行仍需要：

- Keil MAP/HEX 地址核对；
- 编译期对齐/不重叠断言；
- Config/Calibration/Runtime 分别做断电故障注入；
- Legacy 数据迁移；
- 审计旧 `0x0801E000~0x08020000` programmer 区不再作为持久化写区。
