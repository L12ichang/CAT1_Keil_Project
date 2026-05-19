# 8008000 工程 OTA 改造开发文档

版本：v1.0  
日期：2026-05-18  
适用工程：`MDK-ARM-8008000`  
目标文件：`Core/Src/LampProtocolLib/ota.c`、`ota.h`、`mqtt_zk_protocol.c`、`NbDriver.c`

## 1. 文档目标

本文档用于指导当前 8008000 App 工程的 OTA 链路改造，目标是在不大幅复杂化系统、不重写 Bootloader、不引入 RTOS 的前提下，提高 OTA 的可靠性、可观测性和现场可恢复能力。

必须满足：

- OTA 过程中不因为长阻塞导致 IWDG 看门狗复位。
- OTA 过程中可向平台上报可理解的进度。
- OTA 下载、写入、校验、重启、Bootloader 搬运形成闭环。
- 网络波动、模组异常、串口异常、Flash 写失败时可以失败退出并恢复 MQTT。
- 失败不写升级标志，不破坏当前可运行 App，不扩大变砖风险。
- 保持当前 Flash 分区、App 起始地址和 Bootloader 约定不变。

本文档不是一次性重构全部 OTA 架构的方案。推荐采用分阶段落地：第一阶段做低风险强保护，第二阶段做恢复能力，第三阶段才考虑 AT 多业务并发。

## 2. 当前工程事实

### 2.1 Flash 分区

当前 8008000 App 逻辑分区如下：

| 区域 | 地址范围 | 用途 |
|---|---:|---|
| Bootloader | `0x08000000..0x08004FFF` | 独立 Boot 工程 |
| 参数主区 | `0x08005000..0x080067FF` | `sys_data` |
| 参数备份区 | `0x08006800..0x08007FFF` | `sys_data` 备份 |
| App 主程序区 | `0x08008000..0x08023FFF` | 8008000 App |
| OTA 备份区 | `0x08024000..0x0803FFFF` | 新固件暂存区 |

关键宏：

```c
#define APROM_STARTADDR      ((u32)0x08008000)
#define APROM_SAFE_ENDADDR   ((u32)0x08024000)
#define OTABAKROM_STARTADDR  ((u32)0x08024000)
#define OTABAKROM_ENDADDR    ((u32)0x0803FFFF)
```

约束：

- App 固件镜像不得越过 `0x08024000`。
- OTA 包必须能完整放入 `0x08024000..0x0803FFFF`。
- App 只负责下载到 OTA 备份区并写升级标志。
- 真正从 OTA 备份区搬运到 App 区由 Bootloader 完成。

### 2.2 当前 OTA 链路

当前 OTA 是两段式：

```text
平台下发 OTA URL
-> App 解析 URL，提取文件名
-> 关闭 MQTT
-> 4G 模组通过 HTTP 下载固件到 UFS
-> MCU 从 UFS 读取固件
-> MCU 按 512B 写入 OTA 备份区
-> 校验通过
-> sys_data.sn = 0xaa5555aa
-> NVIC_SystemReset()
-> Bootloader 搬运新 App
```

当前入口：

- `mqtt_zk_protocol.c` 处理 `SV="ota"`、`CT="W"`。
- `set_OTA_ENABLE()` 置 `OTA_ENABLE_state=1` 并启动 OTA。
- 主循环每圈调用 `_4G_OTA_machine()` 和 `mcu_copy_firmware_machine()`。

当前进度能力：

- 已有 `zk_publish_ota_progress(progress)`。
- 已有 `zk_publish_ota_error(err_code)`。
- 已有 pending 机制，MQTT 在线后可补发进度或错误。

当前主要问题：

- OTA 写 Flash 缺少完整边界保护。
- 字符串解析存在无长度 `while` 扫描风险。
- HTTP 下载、UFS 读取、Flash 写入、MQTT 进度上报耦合较强。
- 网络失败、模组失败、Flash 失败的错误码和恢复路径不统一。
- 进度节点是固定 30/50/90，不完全反映实际写入比例。
- 看门狗依赖主循环能及时返回，OTA 单步耗时缺少预算约束。

## 3. 总体改造原则

### 3.1 不优先做 HTTP 和 MQTT 真并发

当前软件架构是单 AT 命令通道：

- `send_AT_Command_machine()` 一次只等待一个 AT 命令响应。
- MQTT、HTTP、UFS 共用 `usartRecvQueue`、`stringBuf`、`recvLength`。
- HTTP URC、MQTT URC、UFS 读文件响应会混在同一串口流中。

因此第一阶段不做 HTTP 下载和 MQTT 上报真并发。推荐采用“分片下载 + 间歇上报”：

```text
下载/写入一个大分片
-> 临时恢复 MQTT
-> 上报进度
-> 再关闭 MQTT
-> 继续下一分片
```

这比真并发可靠，改动小，适合当前工程。

### 3.2 OTA 状态机必须短步快返

所有 OTA 状态机函数必须满足：

- 单次调用尽量小于 `20ms`。
- Flash 写入以 `512B` 或一个 Flash page 为上限。
- 不允许在状态里等待长时间响应。
- 等待模组响应必须用 tick 超时，不用阻塞 delay。
- 每个状态执行后返回主循环，让主循环继续喂狗。

### 3.3 失败不写升级标志

只有同时满足以下条件，才允许写：

```c
sys_data.sn = 0xaa5555aa;
sys_data_store();
```

条件：

- 下载总长度合法。
- 写入地址未越界。
- OTA 备份区内容和期望长度一致。
- 固件头合法。
- 固件整体 CRC/checksum 通过。
- 向量表 MSP 和 Reset_Handler 合法。

任何失败都不得写 `0xaa5555aa`。

## 4. 推荐目标流程

### 4.1 主流程

```text
OTA_IDLE
  等待平台 OTA 命令

OTA_ACCEPTED
  回复 ER=0，保存 URL 和文件名

OTA_PREPARE
  初始化上下文，清错误码，检查大小和地址预算

OTA_CLOSE_MQTT
  关闭 MQTT，避免 HTTP 和 MQTT 响应交织

OTA_HTTP_CONFIG
  配置 QHTTPCFG

OTA_HTTP_SET_URL
  AT+QHTTPURL=<len>,30
  发送 URL

OTA_HTTP_DELETE_OLD_FILE
  AT+QFDEL="*"

OTA_HTTP_DOWNLOAD_CHUNK
  AT+QHTTPGETEX=80,<offset>,20480

OTA_HTTP_SAVE_TO_UFS
  AT+QHTTPREADFILE="UFS:<firm_name>",80

OTA_UFS_PREPARE_READ
  AT+QFLST / AT+QFDWL / AT+QFOPEN

OTA_UFS_READ_512
  AT+QFSEEK=1,<offset>,0
  AT+QFREAD=1,512

OTA_FLASH_WRITE_512
  写入 OTABAKROM_STARTADDR + written_size
  写后读回校验

OTA_REPORT_PROGRESS
  必要时短暂切 MQTT 上报

OTA_VERIFY
  校验 OTA 备份区完整性

OTA_MARK_PENDING
  sys_data.sn = 0xaa5555aa
  sys_data_store()

OTA_REPORT_100
  尽力上报 100%

OTA_REBOOT_TO_BOOT
  iap_jump2boot()
```

### 4.2 失败流程

```text
任意状态失败
  -> OTA_FAIL
  -> 关闭 HTTP/UFS 文件句柄
  -> 清 OTA_ENABLE / OTA_ENABLE_state
  -> 恢复 MQTT
  -> 上报 CT="E" 错误码
  -> 保持旧 App 继续运行
```

失败处理原则：

- 不复位。
- 不写升级标志。
- 不擦 App 区。
- 不清除仍可用于诊断的错误信息。
- MQTT 恢复失败也不阻塞，挂起错误上报。

## 5. OTA 上下文设计

新增一个集中上下文，避免大量全局变量散落：

```c
typedef enum {
    OTA_STAGE_IDLE = 0,
    OTA_STAGE_PREPARE,
    OTA_STAGE_CLOSE_MQTT,
    OTA_STAGE_HTTP_CONFIG,
    OTA_STAGE_HTTP_SET_URL,
    OTA_STAGE_HTTP_DELETE_OLD_FILE,
    OTA_STAGE_HTTP_DOWNLOAD_CHUNK,
    OTA_STAGE_HTTP_SAVE_TO_UFS,
    OTA_STAGE_UFS_OPEN,
    OTA_STAGE_UFS_SEEK,
    OTA_STAGE_UFS_READ,
    OTA_STAGE_FLASH_WRITE,
    OTA_STAGE_REPORT_PROGRESS,
    OTA_STAGE_VERIFY,
    OTA_STAGE_MARK_PENDING,
    OTA_STAGE_REPORT_100,
    OTA_STAGE_REBOOT,
    OTA_STAGE_FAIL_RECOVER
} ota_stage_t;

typedef struct {
    ota_stage_t stage;
    uint32 start_tick;
    uint32 stage_tick;
    uint32 last_progress_tick;
    uint32 firmware_size;
    uint32 downloaded_size;
    uint32 written_size;
    uint32 current_chunk_offset;
    uint32 current_chunk_size;
    uint32 expected_crc32;
    uint32 running_crc32;
    uint16 server_xor;
    uint16 chunk_xor;
    uint8 retry_count;
    uint8 chunk_retry_count;
    uint8 last_progress;
    uint8 error_code;
    uint8 mqtt_closed;
    uint8 ufs_opened;
    char url[192];
    char file_name[64];
} ota_context_t;
```

第一阶段可以不一次性替换所有旧变量，但新逻辑应向该结构收敛。

## 6. 进度上报设计

### 6.1 进度区间

建议按实际阶段映射：

| 进度 | 含义 |
|---:|---|
| 0 | 已收到 OTA 命令，准备开始 |
| 5 | URL 合法，OTA 上下文已初始化 |
| 10 | MQTT 已关闭，HTTP 配置开始 |
| 20 | 模组 HTTP 下载已开始 |
| 30 | 第一个大分片下载并写入成功 |
| 30-90 | 根据 `written_size / firmware_size` 线性计算 |
| 95 | OTA 备份区校验通过 |
| 98 | 升级标志写入成功 |
| 100 | 即将重启进入 Bootloader |

计算公式：

```c
static uint8 ota_calc_progress(const ota_context_t *ctx)
{
    uint32 p;

    if (ctx->firmware_size == 0) {
        return ctx->last_progress;
    }

    p = 30 + (ctx->written_size * 60) / ctx->firmware_size;
    if (p > 90) {
        p = 90;
    }
    if (p < ctx->last_progress) {
        p = ctx->last_progress;
    }
    return (uint8)p;
}
```

### 6.2 上报节流

进度上报不能太频繁。建议满足任一条件才上报：

- 进度增加不少于 `10%`。
- 完成一个 `20KB` 大分片。
- 距离上次进度上报超过 `15s`。
- 到达关键节点：`0/10/30/90/95/98/100`。

### 6.3 上报失败策略

`zk_publish_ota_progress()` 已支持 pending。OTA 不应等待进度上报成功：

```text
上报成功 -> 继续 OTA
上报失败 -> 记录 pending -> 继续 OTA
```

在临时恢复 MQTT 上报时，最多等待一个小窗口，例如 `3s`。超时后继续 OTA，不把上报失败当作升级失败。

### 6.4 是否需要恢复 MQTT 上报

第一阶段建议采用当前思路：OTA 下载/写入期间关闭 MQTT，在大分片边界短暂恢复 MQTT 上报。

```text
完成 20KB 大分片
-> OTA_ENABLE = 0
-> _4G_configModule_star_from_onestate(CONNECT_CONFIG_AT_qmtping)
-> MQTT 登录完成
-> zk_publish_ota_progress(progress)
-> 等 200ms 让发布命令发出
-> AT+QMTCLOSE=0
-> OTA_ENABLE = 1
-> 继续下一片
```

注意：进度上报失败不应重新开始 OTA。

## 7. 看门狗与阻塞规避

### 7.1 看门狗基本策略

当前看门狗约 2 秒超时，主循环第一行喂狗。OTA 改造必须保证主循环持续返回。

禁止：

- 在 OTA 状态里等待 HTTP 完成。
- 在 OTA 状态里连续写完整固件。
- 在 OTA 状态里使用无超时 `while`。
- 在中断里喂狗掩盖主循环卡死。
- 在死循环中喂狗。

允许：

- 每圈主循环统一喂狗。
- OTA 每次只推进一个短步骤。
- Flash 写一个 512B 包后立即返回。
- 长等待用 tick 超时状态。

### 7.2 单步耗时预算

| 操作 | 单步预算 | 处理方式 |
|---|---:|---|
| AT 命令发送 | < 5ms | 发送后返回，等待状态机收响应 |
| 等待 AT 响应 | 0ms 阻塞 | 每 20ms 检查一次 |
| UFS 读取 512B | < 1 个状态周期 | 读到数据后交给写入状态 |
| Flash 写 512B | 建议 < 100ms | 写后返回主循环 |
| Flash 整包校验 | 分段执行 | 每次校验 1KB 或 2KB |
| 进度上报等待 | 最多 3s | 超时继续 OTA |

### 7.3 改造点

所有类似逻辑必须改成带长度和超时：

```c
while (*p != '\r') {
    ...
}
```

改为：

```c
static boolean_en ota_parse_u32_until(const char *buf,
                                      uint16 len,
                                      uint16 *index,
                                      char end,
                                      uint32 *out)
{
    uint32 value = 0;
    uint16 i = *index;

    while (i < len && buf[i] != end) {
        if (buf[i] < '0' || buf[i] > '9') {
            return BOOL_FALSE;
        }
        value = value * 10U + (uint32)(buf[i] - '0');
        i++;
    }
    if (i >= len || buf[i] != end) {
        return BOOL_FALSE;
    }
    *index = i + 1U;
    *out = value;
    return BOOL_TRUE;
}
```

## 8. 网络波动处理

### 8.1 重试策略

| 失败点 | 重试对象 | 次数 | 失败后 |
|---|---|---:|---|
| MQTT 关闭失败 | `AT+QMTCLOSE` | 2 | 继续 HTTP 配置，记录警告 |
| HTTP URL 设置失败 | 当前 URL 设置 | 3 | OTA_FAIL |
| HTTP GET 失败 | 当前 20KB 大分片 | 3 | OTA_FAIL |
| QHTTPREADFILE 失败 | 当前 UFS 保存 | 3 | OTA_FAIL |
| QFLST/QFDWL 失败 | 当前 UFS 查询 | 3 | OTA_FAIL |
| QFREAD 失败 | 当前 512B 小片 | 3 | 重开 UFS 文件再试 |
| Flash 写失败 | 当前 Flash page | 2 | OTA_FAIL |
| 进度上报失败 | 当前进度 | 不阻塞 | pending |

### 8.2 断点续传策略

第一阶段不建议做掉电续传，避免复杂化。先做运行时断点重试：

- `server_big_pick_counter` 表示当前 20KB 大片。
- `pfile` 表示 UFS 文件内 512B 小片偏移。
- `save_byete_counter` 表示 OTA 备份区已写字节数。

HTTP 当前大分片失败，只重试当前大分片，不清空前面已写数据。

第二阶段可增加持久化 resume record：

```c
typedef struct {
    uint32 magic;
    uint32 version;
    uint32 firmware_size;
    uint32 written_size;
    uint32 expected_crc32;
    uint32 current_chunk_index;
    uint32 checksum;
} ota_resume_record_t;
```

但这会新增参数区写入次数，第一阶段不建议默认启用。

### 8.3 网络恢复原则

网络失败后：

1. 停止当前 HTTP/UFS 状态。
2. 尽力关闭文件：`AT+QFCLOSE=1`。
3. 清 `OTA_ENABLE` 和 `OTA_ENABLE_state`。
4. 调用 `changea_to_MQTT_modle()` 或等价流程恢复 MQTT。
5. 上报 `CT="E"` 错误。
6. 不复位。

## 9. Flash 写入保护

### 9.1 必须增加硬边界检查

写 Flash 前必须检查：

```c
#define OTA_BACKUP_SIZE   (OTABAKROM_ENDADDR - OTABAKROM_STARTADDR + 1U)
#define APP_MAX_SIZE      (APROM_SAFE_ENDADDR - APROM_STARTADDR)

static boolean_en ota_flash_range_valid(uint32 offset, uint32 len)
{
    if (len == 0U) {
        return BOOL_FALSE;
    }
    if (offset > OTA_BACKUP_SIZE) {
        return BOOL_FALSE;
    }
    if (len > OTA_BACKUP_SIZE - offset) {
        return BOOL_FALSE;
    }
    if (offset + len > APP_MAX_SIZE) {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
```

写入函数：

```c
static boolean_en ota_write_backup(uint32 offset, uint8 *buf, uint32 len)
{
    uint32 addr;

    if (ota_flash_range_valid(offset, len) == BOOL_FALSE) {
        ota_fail(OTA_ERR_FLASH_RANGE);
        return BOOL_FALSE;
    }

    addr = OTABAKROM_STARTADDR + offset;
    flash_store(buf, (u16)len, addr);

    if (memcmp((void *)addr, buf, len) != 0) {
        ota_fail(OTA_ERR_FLASH_VERIFY);
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
```

### 9.2 固件头校验

写完后至少校验：

- `firmware_size > 0`
- `firmware_size <= APP_MAX_SIZE`
- `firmware_size <= OTA_BACKUP_SIZE`
- `prog_length` 合法
- `prog_checksum` 合法
- MSP 初值在 SRAM 范围，例如 `0x20000000..0x2000BFFF`
- Reset_Handler 在 App 范围，例如 `0x08008000..0x08023FFF`

向量表校验示例：

```c
static boolean_en ota_vector_valid(void)
{
    uint32 msp = *((volatile uint32 *)OTABAKROM_STARTADDR);
    uint32 reset = *((volatile uint32 *)(OTABAKROM_STARTADDR + 4U));

    if (msp < 0x20000000U || msp > 0x2000C000U) {
        return BOOL_FALSE;
    }
    if (reset < APROM_STARTADDR || reset >= APROM_SAFE_ENDADDR) {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
```

### 9.3 校验算法建议

当前有 XOR 和固件内部 checksum。建议分阶段：

第一阶段：

- 保留现有 XOR。
- 保留现有 `prog_checksum/prog_length`。
- 增加长度、地址、向量表校验。

第二阶段：

- 增加 CRC32。
- 平台 OTA 命令携带 `size` 和 `crc32`。
- App 写完后对 OTA 备份区按 `size` 计算 CRC32。

第三阶段：

- 固件包增加 header：`magic/version/target/size/crc32/build_id`。
- Bootloader 和 App 共用同一套包格式。

## 10. OTA 失败处理

### 10.1 错误码建议

| 错误码 | 含义 | 处理 |
|---:|---|---|
| 10 | URL 为空或格式非法 | 拒绝 OTA，立即回复错误 |
| 11 | 文件名提取失败 | 拒绝 OTA |
| 12 | 设备 OTA 忙 | 回复 busy |
| 20 | MQTT 关闭失败 | 记录警告，继续或重试 |
| 21 | HTTP 配置失败 | 失败恢复 MQTT |
| 22 | HTTP URL 设置失败 | 失败恢复 MQTT |
| 23 | HTTP 下载超时 | 重试当前大分片 |
| 24 | HTTP 下载失败 | 重试后失败 |
| 30 | UFS 文件不存在 | 失败恢复 MQTT |
| 31 | UFS 打开失败 | 重试后失败 |
| 32 | UFS 读取失败 | 重读当前 512B |
| 40 | OTA 写入越界 | 立即失败，不再写 Flash |
| 41 | Flash 写失败 | 重试当前页 |
| 42 | Flash 读回校验失败 | 失败恢复 MQTT |
| 50 | 传输 XOR 校验失败 | 失败恢复 MQTT |
| 51 | 固件 checksum 失败 | 失败恢复 MQTT |
| 52 | 固件向量表非法 | 失败恢复 MQTT |
| 53 | 固件长度超过 App 上限 | 拒绝 OTA |
| 60 | 写升级标志失败 | 不重启，恢复 MQTT |
| 70 | OTA 状态超时 | 失败恢复 MQTT |

### 10.2 失败清理

失败时统一调用：

```c
static void ota_fail(uint8 err)
{
    ota_ctx.error_code = err;
    ota_ctx.stage = OTA_STAGE_FAIL_RECOVER;
}
```

`OTA_STAGE_FAIL_RECOVER` 做：

```text
1. 尽力关闭 UFS 文件
2. 清 OTA_ENABLE
3. 清 OTA_ENABLE_state
4. 恢复 MQTT 状态机
5. zk_publish_ota_error(error_code)
6. 记录失败原因到日志
7. 回到 OTA_IDLE
```

失败时不要：

- 不调用 `iap_jump2boot()`。
- 不写 `sys_data.sn = 0xaa5555aa`。
- 不擦 App 区。
- 不无限重启。

## 11. 预防 Boot 后异常

当前 Bootloader 行为不在 App 工程内，App 侧必须只交给 Bootloader 一个“干净、完整、可校验”的 OTA 备份区。

App 侧建议：

- 写升级标志前确认 OTA 备份区合法。
- 写升级标志后立即 `sys_data_store()`。
- `sys_data_store()` 后读回参数区确认 `sn == 0xaa5555aa`。
- 重启前尽力上报 `100%`。
- 重启前等待 MQTT 发布窗口最多 `500ms`，不要无限等。

App 启动后已有逻辑：

```c
if (sys_data.sn == 0xaa5555aa) {
    sys_data.sn = 0;
    sys_data_store();
}
```

建议启动后增加一次升级结果上报：

```text
若检测到上一轮 OTA pending 已被 Bootloader 消费
-> 上报 OTA success / current version
```

第一阶段可以只打印日志，第二阶段再做平台闭环。

## 12. HTTP 与 MQTT 并发评估

### 12.1 第一阶段结论

不建议实现 HTTP 下载和 MQTT 上报真并发。

原因：

- 当前只有一个 AT 命令状态机。
- 当前只有一个串口接收队列和一个 `stringBuf`。
- HTTP 和 MQTT URC 交织会导致解析复杂度显著上升。
- 真并发需要 AT 仲裁器和 URC 路由器，属于中到大改。

### 12.2 可接受替代方案

采用“分片间歇上报”：

```text
HTTP 下载和 MCU 写入期间：MQTT 关闭
完成大分片后：短暂恢复 MQTT
上报后：再关闭 MQTT
继续 OTA
```

这样能满足进度上报，又不会引入 URC 并发解析复杂度。

### 12.3 第二阶段可选真并发架构

如果实机确认模组支持 HTTP 和 MQTT 同 PDP 并发，且确实需要实时上报，可新增：

```text
AT Arbiter
  - MQTT command queue
  - HTTP command queue
  - UFS command queue
  - priority command queue

URC Router
  - +QMTRECV    -> MQTT RX
  - +QMTPUBEX   -> MQTT TX ACK
  - +QMTSTAT    -> MQTT link lost
  - +QHTTPGET   -> HTTP download
  - +QHTTPREADFILE -> HTTP save result
  - +QFREAD     -> UFS read data
  - ERROR       -> active command failure
```

该方案不建议放入当前第一阶段。

## 13. 分阶段开发计划

### 阶段 1：低风险可靠性补强

目标：不改总体架构，不做真并发，只封住风险。

改动：

- OTA 写入前增加地址和长度硬边界检查。
- 替换 OTA 中无长度 `while` 解析。
- 每个 OTA 状态增加超时。
- 失败统一走 `ota_fail()`。
- 进度上报改为节流，不阻塞 OTA。
- 失败不上升级标志。
- 校验增加向量表合法性。

预计改动量：中小。  
影响底层：Flash 写入保护和 OTA 状态机，不改 Bootloader。

### 阶段 2：网络波动和恢复能力

目标：当前分片失败可以重试，不必整包重来。

改动：

- 当前 20KB 大分片重试。
- 当前 512B 小片重读。
- Flash 当前 page 重写。
- MQTT 恢复失败挂起错误上报。
- 增加 OTA 失败错误码上报。
- 增加升级结果上报。

预计改动量：中。  
影响底层：不改分区，不改 Bootloader。

### 阶段 3：包格式和断点续传

目标：更强现场恢复能力。

改动：

- 平台 OTA 指令增加 `size/crc32/version`。
- App 支持 CRC32。
- 可选保存 `ota_resume_record_t`。
- 可选掉电后继续下载。

预计改动量：中到大。  
影响底层：参数区新增记录，需要谨慎。

### 阶段 4：AT 多业务并发

目标：HTTP 下载期间 MQTT 保持在线。

改动：

- 新增 AT 命令仲裁器。
- 新增 URC 路由器。
- 拆分 MQTT、HTTP、UFS 接收缓存。
- 所有业务按事件驱动处理。

预计改动量：大。  
影响底层：通信架构级修改。当前不建议优先做。

## 14. 当前文件改造建议

### 14.1 `ota.h`

新增：

- OTA 主状态枚举。
- OTA 错误码枚举。
- OTA 上下文声明。
- `ota_start_from_url()`。
- `ota_process()`。
- `ota_fail()`。
- `ota_is_busy()`。

保留旧接口：

- `_4G_OTA_machine()`
- `mcu_copy_firmware_machine()`
- `OTA_STROE_MCU()`

第一阶段可以在旧接口内部转调新逻辑，避免大面积改调用点。

### 14.2 `ota.c`

优先改：

- 所有 `sprintf(common_send_buff, ...)` 改为 `snprintf`。
- 所有 URL 和文件名操作做长度限制。
- 写 Flash 前调用 `ota_flash_range_valid()`。
- `OTA_STROE_MCU()` 写后读回校验。
- `get_checksum_status_XOR()` 前检查 `firmware_total_size`。
- `get_checksum_status()` 前检查 `size`。
- `mcu_copy_firmware_machine()` 增加状态超时。

### 14.3 `mqtt_zk_protocol.c`

保持：

- URL 校验。
- 文件名提取。
- `zk_publish_ota_progress()`。
- `zk_publish_ota_error()`。
- pending 上报机制。

建议增加：

- OTA accepted 时立即上报 `progress=0`。
- OTA busy 时返回 `ER=12`。
- OTA 失败时使用统一错误码。
- OTA 成功启动后保存 `zk_last_ota_id`，直到升级结果闭环。

### 14.4 `NbDriver.c`

第一阶段不做真并发。只需确保：

- OTA 模式下普通 MQTT `+QMTRECV` 不参与业务处理。
- `send_AT_Command_machine()` 有明确超时结果。
- AT 命令失败能让 OTA 状态机识别，而不是默认为成功。

第二阶段再考虑拆分 AT 命令结果和 URC 路由。

## 15. 测试验收方案

### 15.1 桌面静态检查

- 编译前运行 `git diff --check`。
- 检查所有 `sprintf` 是否替换为 `snprintf`。
- 搜索 OTA 中是否仍存在无边界解析：

```sh
rg -n "while \\(\\*.*!=|sprintf\\(" Core/Src/LampProtocolLib/ota.c
```

- 检查 App 镜像不越过 `0x08024000`：

```sh
python3 tools/check_keil_app_image.py --project MDK-ARM-8008000/project.uvprojx --skip-freshness
```

### 15.2 实机基础用例

| 用例 | 预期 |
|---|---|
| 正常 URL OTA | 进度上报，校验成功，重启进 Bootloader |
| 非 http URL | 回复错误，不进入 OTA |
| OTA 过程中再次下发 OTA | 返回 busy |
| HTTP 服务器断开 | 重试，失败后恢复 MQTT |
| 下载中弱网 | 当前分片重试，不复位 |
| 固件超大 | 拒绝写入，错误上报 |
| 固件 CRC 错 | 不写升级标志，恢复 MQTT |
| Flash 写入校验失败 | 不重启，错误上报 |
| 进度上报失败 | OTA 继续，pending 补报 |
| OTA 期间运行 30 分钟 | 无 IWDG 复位 |

### 15.3 看门狗验证

验证方法：

- 串口打印 OTA 状态和 tick。
- 每个状态切换记录 `stage_tick`。
- 统计单状态最大停留时间。
- 读取 RCC reset flag，确认无 IWDG reset。

验收标准：

- OTA 期间主循环持续运行。
- 单步 Flash 写入不会超过看门狗窗口。
- 模组长等待期间主循环仍持续喂狗。
- 网络异常时不会反复复位。

## 16. 发布门禁

发布 OTA 固件前必须满足：

- `cat1.bin` 小于 App 分区容量。
- 写入范围不越过 `0x08024000`。
- OTA 包能放入 `0x08024000..0x0803FFFF`。
- Bootloader 需要的 `prog_checksum/prog_length/device_type` 未破坏。
- 正常 OTA 成功。
- 错误 OTA 不写升级标志。
- OTA 失败后 MQTT 能恢复。
- 24 小时运行无异常复位。

## 17. 推荐优先级

| 优先级 | 事项 | 原因 |
|---|---|---|
| P0 | Flash 边界检查 | 防止写穿 OTA 备份区 |
| P0 | 解析带长度 | 防止异常响应导致死循环或越界 |
| P0 | 状态超时和失败出口 | 防止卡死后看门狗复位 |
| P0 | 失败不写升级标志 | 防止 Bootloader 搬运坏固件 |
| P0 | 向量表校验 | 防止跳入非法 App |
| P1 | 分片重试 | 应对弱网 |
| P1 | 进度节流和 pending | 提高平台可观测性 |
| P1 | 错误码上报 | 方便现场定位 |
| P2 | CRC32 | 提高完整性校验强度 |
| P2 | 升级结果上报 | 形成平台闭环 |
| P3 | HTTP/MQTT 真并发 | 复杂度高，后置 |

## 18. 最小可落地结论

当前项目不需要一开始做大重构。最可行的 OTA 改造路线是：

```text
保留现有两段式 OTA
+ 写入硬边界
+ 带长度解析
+ 状态超时
+ 当前分片重试
+ 进度节流上报
+ 失败统一恢复 MQTT
+ 校验通过才写 0xaa5555aa
```

这样能在当前主循环、当前 AT 状态机、当前 Bootloader 约定下提高可靠性，且不会把系统复杂度推到不可控。

HTTP 和 MQTT 真并发可以作为后续增强，但必须在实机确认模组支持并完成 AT 仲裁器/URC 路由器后再做，不建议作为当前 OTA 可靠性改造的第一步。
