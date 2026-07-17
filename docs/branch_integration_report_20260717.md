# 分支整合与验证报告（2026-07-17）

## 1. 盘点口径

- 盘点时间：2026-07-17，Asia/Shanghai。
- 基线仓库：`D:\keil_work\CAT1_Keil_Project\CAT1_Keil_Project`。
- Git 本身不保存“分支创建人”字段；下表创建人依据本地 reflog 的分支创建记录、分支首个独有提交作者和提交者综合推定。当前可见提交及 reflog 均为 `DONE.LCC <lizhangcai@donepower.com>`。
- 远端已执行 `git fetch --all --prune --tags` 后再盘点。

## 2. 分支清单与功能范围

| 分支 | 推定创建人 | 创建/最后提交时间 | 功能范围 | 盘点状态与策略 |
|---|---|---|---|---|
| `main` | DONE.LCC | 初始提交 2026-05-19；盘点基线最后提交 2026-07-16 15:21 | 当前稳定主线，包含 OTA、MQTT、计划任务、计量与体积优化 | 基线 `9fa8ccd`；相对 `origin/main` 领先 7 个提交 |
| `optimize/firmware-size-performance` | DONE.LCC | 2026-05-31 20:41 / 2026-07-02 09:09 | 固件体积、性能、日志、Flash/BL0942 安全与 OTA 优化 | 已完整包含于 `main`，保留原历史、不重复合并 |
| `fix/ota-rawtcp-jlink-e2e` | DONE.LCC | 2026-07-03 21:24 / 2026-07-04 08:53 | Raw TCP OTA、J-Link/MQTT 端到端验证及文档 | 已完整包含于 `main`，保留原历史、不重复合并 |
| `fix/mcu-timing-mqtt-ota-watchdog` | DONE.LCC | 2026-07-04 09:53 / 2026-07-09 08:29 | MCU 时序、MQTT、OTA、看门狗、计量与过温修正 | 已完整包含于 `main`，保留原历史、不重复合并 |
| `codex/mqtt-stats-clear` | DONE.LCC | 2026-07-16 14:16 / 2026-07-17 | MQTT 运行时/能耗清零后续与固件版本持久化权威化 | 独有变更规范化为提交 `11d2f62`，使用 `--no-ff` 合并 |
| `codex/full-parameter-calibration` | DONE.LCC | 2026-07-17 / 2026-07-17 | 全参数校准、曲线插值、双槽 Flash 持久化、MQTT 校准协议、台架工具和测试 | 独有变更规范化为提交 `ed37cb7`，使用 `--no-ff` 合并 |
| `codex/integrate-all-20260717` | DONE.LCC | 2026-07-17 / 2026-07-17 | 临时整合、测试基线恢复、逐分支构建与最终验证 | 完成后合并至 `main` 并删除 |

远端 `origin/fix/mcu-timing-mqtt-ota-watchdog`、`origin/optimize/firmware-size-performance` 均已被本地 `main` 包含；`origin/main` 在盘点时停留于 `6ee103e`。

## 3. 独立副本与历史资产

1. `CAT1_Keil_Project_mqtt_clear` 经 `git worktree list` 确认为同一仓库的链接工作树，并非独立克隆。其唯一未提交源码差异为 `APP_VERSION` 升至 5，以及配置从 Flash 加载后强制恢复当前镜像 `sver`；已整理到 `codex/mqtt-stats-clear`。
2. `Core.7z` 含 295 个文件。按 SHA-256 与活动工作树逐文件比较：290 个相同、5 个为较旧/不同版本、0 个仅归档中存在的文件，因此不存在需要反向覆盖活动源码的独有变更。
3. `docs.7z` 含 10 个文件：4 个与当前文档相同、6 个为较旧版本、0 个仅归档中存在的文件。
4. 旧 `stash@{2026-07-09}` 主要保存历史测试、构建输出和实机日志。测试源码已恢复、校准并重新纳入版本管理；大体量输出和日志不合入主线，使用归档标签保留可达性。
5. 仓库根目录的 `Core.zip` 属于重复源码快照；归档到仓库外历史目录后从活动主线移除，其内容仍可由预整合标签追溯。

## 4. 合并顺序与冲突规则

合并顺序如下：

1. 在临时整合分支恢复测试基线，并修复 Windows 下 `fcntl`/`termios` 导入和 `mkstemp` 文件描述符泄漏。
2. 合并底层 MQTT 固件身份修正 `codex/mqtt-stats-clear`。
3. 合并依赖 MQTT、Flash、PWM 和工程配置的 `codex/full-parameter-calibration`。
4. 构建 Release-MinSize 制品、执行镜像边界检查，再进入 `main`。

预识别重叠文件为 `Core/Src/common.h` 和 `Core/Src/LampProtocolLib/zk_property.c`。处理原则为：保留 `APP_VERSION=5` 与镜像 `sver` 权威化，同时保留校准模块引用、校准期间工厂参数写入保护及校准曲线失效逻辑。实际合并由 `ort` 自动完成，无文本冲突、无人工冲突提交。

两个新分支均只有一个内聚提交，故不再 squash；使用 `--no-ff` 生成清晰的合并节点。既有分支的完整历史已在 `main` 中，不做重复 cherry-pick 或 squash。

## 5. 验证记录

| 阶段 | Keil `program` 全量构建 | 软件测试 | 镜像/体积 |
|---|---|---|---|
| 盘点基线 `main@9fa8ccd` | 0 Error / 0 Warning；Code 88086，RO 5026，RW 1692，ZI 33684 | 恢复历史测试后发现 83 项中 6 项为过期断言或 Windows 兼容问题 | APP 93320 bytes，分区未越界 |
| 测试基线修复后 | 不改变固件源码 | 98/98 通过 | 不适用 |
| 合并 MQTT 修正后 | 0 Error / 0 Warning；Code 88086，RO 5026，RW 1692，ZI 33684 | 98/98 通过 | APP 93320 bytes |
| 合并全参数校准后 | 0 Error / 0 Warning；Code 95874，RO 5142，RW 1764，ZI 33884 | 基线 98/98 + 校准 17/17，共 115/115 通过 | APP 101256 bytes；范围 `0x08008000..0x08020B87`，小于 `0x08024000` |
| `Release-MinSize` | 0 Error / 0 Warning；Code 77334，RO 2402，RW 1692，ZI 32932 | 与上述同源测试集 | APP 79976 bytes；范围 `0x08008000..0x0801B867`，低于 88 KiB ROM 上限 |
| 最终 `main` | 两个目标均为 0 Error / 0 Warning；尺寸与上述对应目标一致 | 基线/工具契约 99/99 + 校准 17/17，共 116/116 通过 | program 与 Release-MinSize 镜像检查均通过；合并镜像结构逐字节校验通过 |

`tools/check_keil_app_image.py` 对 program 与 Release-MinSize 产物均通过：设备类型 `0x0003`、程序长度与校验和一致、APP 基址和安全边界一致。

## 6. 当前验证边界

- 本轮完成了可在本机自动执行的软件单元/契约测试、Keil 两个目标的全量构建、镜像头、校验和、Flash 分区和 ROM 体积验证。
- 已通过 USB 识别到 SEGGER J-Link，并以只读 `verifybin` 对 `0x08008000` 处的 Release-MinSize 制品执行核验；J-Link 成功连接 Cortex-M3，目标电压约 3.27 V，但首个字节即发现板上固件 `0x28` 与当前制品 `0x40` 不一致。该操作未擦除、未烧写 Flash，核验后设备已复位并恢复运行。
- 上述实机核验同时发现原 `jlink_verify.ps1` 会在 Commander 日志报告失败时错误返回成功；已在 `fa2cc30` 中修正为同时检查进程退出码和日志中的 `Verify successful.`，并补充契约测试。修正后的脚本已用同一固件不匹配场景验证，能够正确向调用方传播失败。
- 当前没有本任务可用的 MQTT 凭据和限流安全台架条件，因此未自动发布 OTA、未改变实机 Flash，也未执行带载电流/温升/长稳性能测试。历史验收脚本还引用 2026-07-10 的固定制品尺寸和现场日志，不能作为本次新校准固件的实时通过证据。
- 上线前仍需在限流安全台架执行：J-Link 烧录回读、启动/登录、调光全范围、断电恢复、校准双槽容错、计量精度、温升、长稳和受保护 OTA。未取得这些实机证据前，本报告只证明软件构建和离线验证通过。

## 7. 最终收尾状态

- 整合分支的已验证节点标记为 `integration/validated-20260717`；盘点前主线和历史 stash 分别标记为 `archive/main-pre-integration-20260717`、`archive/pre-size-optimization-stash-20260709`，保证删除冗余引用后仍可追溯。
- GitHub 默认分支已由 `optimize/firmware-size-performance` 调整为 `main`；远端冗余开发分支均已删除，`origin/HEAD` 指向 `refs/heads/main`。
- 本地临时工作树、已合并开发分支和旧 stash 均已清理；最终本地分支和远端跟踪分支各仅保留 `main`。
- 独立压缩副本已移至仓库外 `D:\keil_work\CAT1_Keil_Project\archives\20260717`，由其中的 `README.md` 记录文件哈希、大小和差异结论。
