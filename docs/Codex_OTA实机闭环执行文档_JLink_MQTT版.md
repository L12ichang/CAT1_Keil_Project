# Codex 执行文档：OTA 可靠升级改造 + J-Link 烧录 + MQTT 实机闭环验证

> 适用工程：当前 CAT.1/4G MQTT 项目，上传代码包 `Core(4).rar` 中的 `Core/Src/LampProtocolLib/ota.c`、`mqtt_zk_protocol.c`、`NbDriver.c`、`watchdog.c`、`hw_flash.c` 等。
> 当前现场条件：电脑已连接 J-Link；设备已接 J-Link；允许 Codex 自动编译、烧录、复位、发送 MQTT OTA 指令、抓日志、验证 OTA 结果。
> 当前服务器限制：不支持 Range 分片；不会返回 HTTP 206；GET 响应不稳定或不携带 `Content-Length`；4G 模块 HTTP/UFS 全量缓存路径会失败，日志出现过类似 `+QHTTPREADFILE: 729`。

---

## 0. 最终目标

Codex 要完成的是 **实机可验证的 OTA 闭环**，不是只做静态代码修改。

最终必须达到：

1. APP 通过 MQTT 收到 OTA 指令。
2. OTA 不再依赖 `QHTTPREADFILE` / UFS 全量保存。
3. OTA 不依赖服务器返回 `Content-Length`。
4. OTA 不依赖服务器支持 `Range` / `206 Partial Content`。
5. 设备使用 raw TCP 主动发 HTTP GET。
6. MCU 通过 `AT+QIRD` 小块读取 HTTP body。
7. MCU 将固件流式写入 OTA 备份区 `OTABAKROM_STARTADDR`。
8. 写入完成后校验固件头、固件长度、checksum、device_type。
9. 校验成功后写升级标志并跳转/复位到 Boot。
10. Boot 搬运备份区固件到 APP 区。
11. APP 新版本启动成功，MQTT 可再次上线并上报版本。
12. 整个过程中不能因为喂狗不足、UART 中断阻塞、Flash 写入阻塞、日志过量导致失败。

---

## 1. 当前代码关键事实

### 1.1 当前 OTA 入口已经通，不要重构 MQTT 入口

`mqtt_zk_protocol.c` 中已经有 OTA 指令处理入口：

- Topic 规则：
  - OTA 下发：`MS/<IMEI>/pcp2dev`
  - OTA 上报：`MS/<IMEI>/dev2pcp`
- OTA 服务字段：`SV = "ota"`
- OTA 写命令：`CT = "W"`
- OTA 指令 URL 字段：`DT.url`

代码位置：

```c
// Core/Src/LampProtocolLib/mqtt_zk_protocol.c
#define ZK_TOPIC_PREFIX "MS"
#define ZK_SV_OTA       "ota"
#define ZK_CT_WRITE     "W"
#define ZK_CT_PROGRESS  "P"
#define ZK_CT_ERROR     "E"
```

`zk_handle_ota_message()` 已经会解析：

```c
url = cJSON_GetObjectItem(dt, "url");
zk_ota_set_url(url->valuestring);
strncpy(firm_name_buffer, OTA_LOCAL_FIRMWARE_NAME, 255);
OTA_LOGI("cmd received id=%s url=%s local=%s\r\n", ...);
set_OTA_ENABLE();
```

因此 Codex 不要先大改 MQTT 协议。只需要补齐自动化测试脚本和 OTA 闭环。

---

### 1.2 当前 OTA 失败主因不是“没收到命令”

现场日志中已经出现过：

```text
[OTA][I] cmd received id=... url=... local=cat1.bin
```

这表示：

- 平台/MQTT 指令已经进入设备。
- JSON 解析基本成功。
- OTA 状态机已经被触发。

所以问题集中在：

1. 下载路径。
2. Flash 写入路径。
3. 固件校验。
4. 写升级标志。
5. 跳 Boot。
6. Boot 搬运后的新 APP 启动。

---

### 1.3 当前 OTA 代码仍有调试宏，导致不会真正升级

`Core/Src/LampProtocolLib/ota.c` 当前宏定义：

```c
#define OTA_DEBUG_DOWNLOAD_ONLY           1U
#define OTA_DISABLE_FAIL_RESET            1U
#define OTA_DEBUG_CLEAR_ALL_UFS           1U
#define OTA_RAW_TCP_STREAM_DEBUG          1U
#define OTA_STREAM_TO_BACKUP_DEBUG        1U
#define OTA_STREAM_ALLOW_RAW_BIN_TEST     1U
```

这些宏会造成几个严重问题：

- `OTA_DEBUG_DOWNLOAD_ONLY=1`：跳过旧的 MCU copy / boot 跳转链路。
- `OTA_STREAM_TO_BACKUP_DEBUG=1`：stream verify 成功后只打印 `debug stream: skip boot/reset`，不会写升级标志，不会跳 Boot。
- `OTA_STREAM_ALLOW_RAW_BIN_TEST=1`：允许无合法固件头的 raw bin 被判成功，生产不可接受。
- `OTA_DEBUG_CLEAR_ALL_UFS=1`：调试清理 UFS，不应出现在生产 OTA。

当前 `CONNECT_OTA_AT_STREAM_VERIFY` 逻辑中：

```c
if (ota_stream_verify_backup() == BOOL_TRUE)
{
    OTA_LOGI("debug stream: skip boot/reset\r\n");
    ota_connect_state=CONNECT_OTA_AT_QHTTPREADFILE_STROE_FINISH;
    MCU_OTA_state=MCU_OTA_STATE_IDLE;
    set_gateway_state_idle();
    changea_to_MQTT_modle();
    break;
}
```

这就是当前“下载看似成功但不会真正 OTA”的直接代码原因之一。

---

### 1.4 Flash 分区要求至少 256KB Flash

当前分区来自 `Core/Src/sys_data.h`：

```c
#define BOOTROM_STARTADDR       (u32)0x8000000
#define DATAROM_STARTADDR       (u32)0x8005000
#define BAKDATAROM_STARTADDR    (u32)0x8006800
#define APROM_STARTADDR         (u32)0x8008000
#define APROM_SAFE_ENDADDR      (u32)0x8024000
#define OTABAKROM_STARTADDR     (u32)0x8024000
#define OTABAKROM_ENDADDR       (u32)0x803FFFF
```

含义：

| 区域       |                        地址 |  大小 |
| ---------- | --------------------------: | ----: |
| Boot       | `0x08000000 ~ 0x08004FFF` |  20KB |
| 参数区     | `0x08005000 ~ 0x08007FFF` |  12KB |
| APP 区     | `0x08008000 ~ 0x08023FFF` | 112KB |
| OTA 备份区 | `0x08024000 ~ 0x0803FFFF` | 112KB |

因此实际 MCU 必须至少 256KB Flash。若实物是 `STM32F103CBT6 / HK32F103CBT6A` 128KB，则 `0x08024000` 已经超出 Flash，双区 OTA 物理上不成立。

Codex 必须先确认实际芯片/Keil Target/Linker/Map 是否为 256KB 或更大。不能在 128KB 芯片上继续调当前双区 OTA。

---

### 1.5 当前 `APROM_OFFSET` 必须核对

`Core/Src/common.h` 中：

```c
#ifdef APROM_OFFSET
#define APROM_OFFSET_ADDR 0x08008000
#else
#define APROM_OFFSET_ADDR 0x08000000
#endif
```

而 OTA/Boot 分区实际要求 APP 运行地址是 `0x08008000`。

Codex 必须核对 Keil 工程的 C/C++ Define 中是否有：

```text
APROM_OFFSET
```

同时核对 APP 工程 IROM 起始地址是否为：

```text
0x08008000
```

如果没有，必须修复 Keil 工程配置，否则 APP 固件头、向量表、Boot 跳转都会错。

---

### 1.6 当前看门狗约 2 秒超时，OTA 期间必须持续喂狗

`Core/Src/watchdog.c` 当前配置：

```c
hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
hiwdg.Init.Reload = 312;
```

注释中写的是约 2 秒超时。OTA 擦 Flash、写 Flash、等待 AT、读取 QIRD 时不能超过这个时间不喂狗。

当前 `ota.c` 已有：

```c
static void ota_feed_watchdog_if_enabled(void)
{
    watchdog_feed_dog();
}
```

Codex 需要保留并加强这个策略，不能简单删除。

---

## 2. 技术路线：固定采用 raw TCP + HTTP GET + QIRD 流式写备份区

### 2.1 禁止继续使用的路径

禁止把以下路径作为最终方案：

```text
AT+QHTTPREADFILE="UFS:cat1.bin",...
AT+QFREAD
AT+QFDWL
模块 UFS 全量保存固件
HTTP Range 分片下载
依赖 206 Partial Content
依赖 Content-Length 才开始写 Flash
```

原因：

1. 服务器不支持 206。
2. 服务器 GET 不稳定提供 Content-Length。
3. 模块全量缓存/UFS 路径出现过 `729` 内存分配失败。
4. 固件约 85KB，但模块内部 HTTP 栈最大缓存能力不稳定，不能假设一定能装下。
5. MCU 更可控，应该直接流式接收并写入备份区。

---

### 2.2 允许使用的路径

最终只允许：

```text
MQTT OTA 指令
  ↓
关闭/暂停 MQTT AT 链路
  ↓
AT+QICLOSE=0
  ↓
AT+QIOPEN=1,0,"TCP",host,port,0,0
  ↓
AT+QISEND=0
  ↓
发送 HTTP GET /xxx.bin HTTP/1.1
Host: host:port
Accept-Encoding: identity
Connection: close
  ↓
循环 AT+QIRD=0,512
  ↓
解析 HTTP Header
  ↓
解析 HTTP Body
  ↓
按页缓存，安全窗口写入 OTA 备份区
  ↓
校验固件头 + checksum
  ↓
写升级标志
  ↓
跳 Boot / NVIC_SystemReset
  ↓
Boot 搬运 OTA 备份区到 APP 区
  ↓
新 APP 启动并 MQTT 上线
```

---

### 2.3 不依赖 Content-Length 的规则

HTTP Header 中如果有 `Content-Length`：

- 只能作为辅助信息。
- 必须和固件头中的 size 一致。
- 不一致则失败。

HTTP Header 中如果没有 `Content-Length`：

- 从固件头读取 size。
- 固件头字段来自 APP 固件偏移：

```c
#define ADDR_CHECKSUM_OFFSET 0x200
#define ADDR_SIZE_OFFSET     0x204
#define ADDR_TYPE_OFFSET     0x208
```

固件头必须满足：

```text
checksum != 0x12345678
size     != 0x89ABCDEF
size > ADDR_TYPE_OFFSET + 2
size <= APP区容量
size <= OTA备份区容量
size % 4 == 0
设备类型 device_type 与 Boot/APP 约定一致
```

`Connection: close` 场景下，如果没有 `Content-Length`，也不能依赖 socket close 判断大小，而是以固件头 size 为准。达到 size 后停止写入并进入 verify。

---

## 3. Codex 必须修改的代码项

### 3.1 建立 OTA 编译配置头文件

新增或整理：

```text
Core/Src/LampProtocolLib/ota_config.h
```

建议内容：

```c
#ifndef OTA_CONFIG_H_
#define OTA_CONFIG_H_

/* 生产版本固定 raw TCP，不使用模块 UFS 全量缓存 */
#define OTA_USE_RAW_TCP_STREAM              1U
#define OTA_USE_QHTTPREADFILE_UFS           0U

/* 禁止下载成功后只停在 debug 状态 */
#define OTA_DEBUG_DOWNLOAD_ONLY             0U
#define OTA_STREAM_TO_BACKUP_DEBUG          0U
#define OTA_STREAM_ALLOW_RAW_BIN_TEST       0U
#define OTA_DEBUG_CLEAR_ALL_UFS             0U

/* 日志控制 */
#define OTA_LOG_LEVEL_ERROR                 1U
#define OTA_LOG_LEVEL_WARN                  1U
#define OTA_LOG_LEVEL_INFO                  1U
#define OTA_LOG_LEVEL_DEBUG                 0U
#define OTA_RAW_HEX_LOG_ENABLE              0U

/* QIRD 小块读取，优先稳定，不追速度 */
#define OTA_RAW_TCP_QIRD_LEN                512U

/* 超时配置 */
#define OTA_RAW_TCP_OPEN_TIMEOUT_MS         150000U
#define OTA_RAW_TCP_SEND_TIMEOUT_MS         20000U
#define OTA_RAW_TCP_IDLE_TIMEOUT_MS         60000U
#define OTA_RAW_TCP_TOTAL_TIMEOUT_MS        330000U

#endif
```

要求：

- 不允许在 `ota.c` 顶部散落大量调试宏。
- 生产配置和调试配置必须可读、可控。
- 默认编译必须是可 OTA 升级版本。

---

### 3.2 修复 `CONNECT_OTA_AT_STREAM_VERIFY` 成功后的闭环

当前成功后只是：

```c
OTA_LOGI("debug stream: skip boot/reset\r\n");
changea_to_MQTT_modle();
```

必须改为：

```c
case CONNECT_OTA_AT_STREAM_VERIFY:
    if (ota_stream_verify_backup() == BOOL_TRUE)
    {
        OTA_LOGI("stream verify ok: mark upgrade and jump boot size=%u\r\n", ota_stream_expected_size);

        /* 可选：先上报 100%，但不能为了上报阻塞 OTA */
        (void)zk_publish_ota_progress(100U);

        sys_data.sn = 0xaa5555aa;     /* 与 Boot 约定的新固件标志 */
        sys_data_store();             /* 这里必须喂狗，避免参数区写入触发 IWDG */
        ota_feed_watchdog_if_enabled();

        HAL_Delay(50);
        ota_feed_watchdog_if_enabled();

        iap_jump2boot();              /* 内部 NVIC_SystemReset 或跳 Boot */
        break;
    }

    OTA_LOGE("stream verify failed: no boot jump\r\n");
    (void)zk_publish_ota_error(OTA_ERR_VERIFY_FAILED);
    ota_start_http_stop_cleanup("stream_verify_failed");
    break;
```

注意：

- 需要包含 `sys_data.h`、`for_iap.h`、`mqtt_zk_protocol.h`。
- 写升级标志之前必须确保备份区校验通过。
- 校验失败绝不能跳 Boot。
- 不允许 raw bin 测试文件进入升级闭环。

---

### 3.3 修复 checksum 校验边界

当前 `get_checksum_status()` 存在两个隐患：

```c
if((sum==(u32)0x12345678) || (size<(u32)58*2048 && sum == user_frash_checksum(size/4)))
```

问题：

1. `sum == 0x12345678` 是占位值，不应视为成功。
2. APP 区实际是 56 页，不是 58 页。
3. size 边界应使用分区常量，不应写死 `58*2048`。

Codex 必须改为：

```c
static boolean_en ota_image_size_valid(u32 size_bytes)
{
    if (size_bytes < OTA_STREAM_HEADER_MIN_LEN) return BOOL_FALSE;
    if (size_bytes > OTA_STREAM_APP_MAX_SIZE) return BOOL_FALSE;
    if (size_bytes > OTA_STREAM_BACKUP_CAPACITY) return BOOL_FALSE;
    if ((size_bytes % 4U) != 0U) return BOOL_FALSE;
    return BOOL_TRUE;
}

boolean_en get_checksum_status(void)
{
    u32 expected_sum;
    u32 size_bytes;
    u32 calc_sum;

    expected_sum = *((__IO u32 *)(OTABAKROM_STARTADDR + ADDR_CHECKSUM_OFFSET));
    size_bytes   = *((__IO u32 *)(OTABAKROM_STARTADDR + ADDR_SIZE_OFFSET)) & 0x00FFFFFFU;

    if (expected_sum == 0x12345678U || size_bytes == 0x89ABCDEFU)
    {
        return BOOL_FALSE;
    }
    if (ota_image_size_valid(size_bytes) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    calc_sum = user_frash_checksum(size_bytes / 4U);
    return (expected_sum == calc_sum) ? BOOL_TRUE : BOOL_FALSE;
}
```

同时：

- `user_frash_checksum()` 必须明确入参单位是 word count，不是 byte count。
- 建议重命名为 `ota_calc_sum32_words()`，降低误用概率。

---

### 3.4 增加 device_type 校验

当前 `for_iap.c` 写入：

```c
const u16 device_type __attribute__((section(".ARM.__at_0x8008208"))) = 0x0003;
```

Codex 必须确认 Boot 期望 device_type 是否也是 `0x0003`。

在 APP OTA verify 中增加：

```c
#define OTA_EXPECTED_DEVICE_TYPE 0x0003U

if (ota_stream_header_device_type != OTA_EXPECTED_DEVICE_TYPE)
{
    OTA_LOGE("download failed: device type mismatch got=0x%04x exp=0x%04x\r\n",
             ota_stream_header_device_type, OTA_EXPECTED_DEVICE_TYPE);
    return BOOL_FALSE;
}
```

如果 Boot 工程里定义的是其他值，以 Boot 为准统一，不要只改 APP。

---

### 3.5 彻底禁用 QHTTPREADFILE / UFS 路径

Codex 要求：

- `OTA_USE_QHTTPREADFILE_UFS=0` 时，相关状态不编译或不可达。
- 即使 raw TCP 失败，也不允许自动 fallback 到 `QHTTPREADFILE`。
- `+QHTTPREADFILE: 729` 只能作为诊断日志，不作为重试路径。

可保留代码作为注释/诊断，但生产状态机不能进入：

```c
CONNECT_OTA_AT_QHTTPREADFILE
CONNECT_OTA_AT_QHTTPREADFILE_STROE_WAIT
CONNECT_OTA_AT_QHTTPREADFILE_QFLST_DIAG
```

---

### 3.6 Flash 写入必须维持“QIRD_DATA 不写 Flash”原则

当前已有保护：

```c
static boolean_en ota_stream_programming_blocked(void)
{
    return (ota_connect_state == CONNECT_OTA_AT_RAW_QIRD_DATA) ? BOOL_TRUE : BOOL_FALSE;
}
```

要求继续保留，并加强：

- `CONNECT_OTA_AT_RAW_QIRD_DATA` 中只能做：
  - 从 `usartRecvQueue` 取字节；
  - HTTP 状态机解析；
  - 写 RAM page buffer；
  - 计数和轻量喂狗。
- Flash erase/program 只能发生在：
  - OTA 开始前擦备份区；
  - `QIRD_TRAILER` 收到 OK 后；
  - Verify 前 flush 尾页；
  - 非 UART 高速接收窗口。

禁止在 UART ISR 中写 Flash。
禁止在 `dequeue()` 数据循环里写 Flash。

---

### 3.7 Flash 驱动安全要求

当前 `hw_flash_write_bytes()` 是参数区通用写法，会：

1. 读整页到 `temp_buf_byte`。
2. 修改局部数据。
3. 擦页。
4. 整页写回。

该函数不适合 OTA 主数据流。OTA 应继续使用专用的：

```c
ota_stream_erase_backup_area()
ota_stream_program_bytes()
ota_stream_verify_programmed_bytes()
```

Codex 不要把 OTA 改回 `hw_flash_write_bytes()`。

同时检查 HAL Flash PageAddress 参数：

- 当前部分代码有 `PageAddress = addr / FLASH_PAGE_SIZE` 的写法。
- STM32 HAL F1 通常要求 `PageAddress` 是页起始地址，不是页号。
- OTA 专用擦除中目前使用的是 `erase_init.PageAddress = addr`，更合理。
- Codex 必须用实际 HAL 库确认，避免参数区擦错页。

---

### 3.8 日志策略

生产 OTA 日志只允许输出关键事件：

```text
[OTA][I] cmd received id=... url=... local=cat1.bin
[OTA][I] raw tcp url ok
[OTA][I] raw tcp open ok
[OTA][I] raw http header done status=200 chunked=... clp=... cl=...
[OTA][I] stream image header checksum=... size=... type=...
[OTA][I] stream progress rx=8192 known=1 exp=...
[OTA][I] stream body complete rx=... programmed=...
[OTA][I] STREAM DOWNLOAD VERIFY SUCCESS...
[OTA][I] stream verify ok: mark upgrade and jump boot
[BOOT][I] upgrade copy start...
[BOOT][I] upgrade copy ok...
[APP][I] app boot version=...
```

禁止：

- 每字节打印。
- 每个 QIRD 包打印完整 HEX。
- 大量 `printf_buf()` 打印 Flash 内容。
- OTA 期间持续打印无关 BL0942、DALI、巡检、ADC 日志。

建议：

```c
#define APP_HEX_LOG_ENABLE 0
#define APP_PWM_DEBUG_ENABLE 0
#define OTA_RAW_HEX_LOG_ENABLE 0
```

---

## 4. 自动化实机验证要求

Codex 当前可以使用 J-Link 和 MQTT 工具，所以必须做实机闭环。不要只停在“编译通过”。

### 4.1 Codex 允许使用的本机工具

优先顺序：

1. Codex 内置技能/工具：J-Link、MQTT、串口/日志、PowerShell、文件系统、Git。
2. 命令行工具：
   - `JLink.exe`
   - `JFlash.exe`
   - `UV4.exe`
   - `mosquitto_pub.exe`
   - `mosquitto_sub.exe`
   - `python + paho-mqtt`
   - `python + pyserial`
3. 如果没有 mosquitto，则 Codex 自己生成 Python MQTT 脚本。

Codex 每一步都要把命令、日志、结果保存到：

```text
tools/ota_test/logs/
```

---

### 4.2 Git 安全要求

Codex 开始前必须执行：

```bash
git status
git branch --show-current
git checkout -b fix/ota-rawtcp-jlink-e2e
```

要求：

- 每个阶段小提交。
- 烧录前必须有一次 commit。
- 禁止在未提交状态下做大面积重构。
- 发现设备起不来时能快速 `git reset --hard <last-good>` 回退。

推荐提交节点：

```text
commit 1: add ota config and remove debug-only upgrade path
commit 2: harden raw tcp stream ota verify and boot jump
commit 3: add ota firmware packing tool
commit 4: add jlink/mqtt e2e test scripts
commit 5: validated on hardware with logs
```

---

### 4.3 烧录安全要求

Codex 可以烧录，但必须遵守：

1. 禁止修改 Option Bytes。
2. 禁止修改 RDP/读保护。
3. 禁止执行全片擦除，除非明确需要恢复设备且已有备份。
4. 优先使用 `loadfile xxx.hex`，让 J-Link 按 Hex 地址写入。
5. 如果需要烧 Boot 和 APP，分两次 load，并 verify。
6. 烧录后必须 reset + go。

J-Link 命令模板：

```text
device STM32F103CB
if SWD
speed 4000
connect
r
h
loadfile <APP_HEX_OR_BOOT_HEX>
verifyfile <APP_HEX_OR_BOOT_HEX>
r
g
exit
```

如果实际芯片是 HK32 而 J-Link 没有对应型号，Codex 可尝试：

```text
device STM32F103CB
```

或根据项目现有 J-Link 配置选择同内核/同 Flash 容量型号。

---

### 4.4 编译要求

Codex 要自动定位 Keil 工程：

```powershell
Get-ChildItem -Recurse -Filter *.uvprojx
```

使用 Keil 命令行编译：

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -b "<project>.uvprojx" -j0 -o "build_log.txt"
```

如果 Keil 路径不同，自动搜索：

```powershell
Get-ChildItem "C:\" -Recurse -Filter UV4.exe -ErrorAction SilentlyContinue
```

编译后必须检查：

- 0 Error。
- APP `.map` 中 IROM 起始地址为 `0x08008000`。
- APP `.bin` 大小 <= `APROM_SAFE_ENDADDR - APROM_STARTADDR`。
- OTA 包大小 <= `OTABAKROM_ENDADDR - OTABAKROM_STARTADDR + 1`。
- 固件头地址 `0x200/0x204/0x208` 已正确写入。

---

## 5. OTA 固件打包工具要求

Codex 需要新增：

```text
tools/ota_test/pack_ota_firmware.py
```

功能：

1. 输入 Keil 生成的 APP bin。
2. 确认 bin 对应 APP 起始地址 `0x08008000`。
3. 按 4 字节对齐补 `0xFF`。
4. 写入：
   - `0x200`：checksum。
   - `0x204`：size，低 24 位为字节数。
   - `0x208`：device_type。
5. checksum 算法必须与 `user_frash_checksum(size/4)` 一致：
   - 按 32-bit word 读取。
   - 跳过 `ADDR_CHECKSUM_OFFSET` 所在 word。
   - 跳过 `ADDR_SIZE_OFFSET` 所在 word。
   - 对每个 word 的 4 个字节求和。
6. 输出：
   - `cat1.bin`
   - `cat1_ota_info.json`

伪代码：

```python
def sum32_word_bytes(word):
    return ((word >> 24) & 0xff) + ((word >> 16) & 0xff) + ((word >> 8) & 0xff) + (word & 0xff)

def calc_checksum(data):
    total = 0
    word_count = len(data) // 4
    for i in range(word_count):
        off = i * 4
        if off == 0x200 or off == 0x204:
            continue
        word = int.from_bytes(data[off:off+4], "little")
        total = (total + sum32_word_bytes(word)) & 0xffffffff
    return total
```

注意：

- 不允许使用 raw 未打包 bin 直接 OTA。
- 不允许 `checksum=0x12345678` 的测试固件进入 OTA。
- 不允许 `size=0x89abcdef` 的测试固件进入 OTA。

---

## 6. MQTT OTA 指令自动化

Codex 需要新增：

```text
tools/ota_test/mqtt_ota_publish.py
```

基础指令格式：

```json
{
  "SN": "<IMEI>",
  "TM": "2026-07-03 12:00:00",
  "SV": "ota",
  "ID": "156783",
  "CT": "W",
  "DT": {
    "url": "http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032"
  }
}
```

发布 Topic：

```text
MS/<IMEI>/pcp2dev
```

订阅 Topic：

```text
MS/<IMEI>/dev2pcp
```

Codex 需要从代码或日志确认 IMEI。若无法自动读取，则使用启动日志中的 IMEI 或从设备 MQTT 登录信息中提取。

Python 脚本参数：

```powershell
python tools/ota_test/mqtt_ota_publish.py `
  --host 47.120.15.220 `
  --port 1883 `
  --imei <IMEI> `
  --url "http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032" `
  --id 156783 `
  --wait-progress `
  --timeout 600
```

脚本必须：

1. 订阅 `MS/<IMEI>/dev2pcp`。
2. 发布 OTA 指令到 `MS/<IMEI>/pcp2dev`。
3. 等待：
   - `ER=0` 响应；
   - `SV=ota, CT=P, progress=...`；
   - `SV=ota, CT=E` 错误；
   - 设备重启后重新上线/心跳。
4. 输出 JSON 日志到：

```text
tools/ota_test/logs/mqtt_ota_<timestamp>.jsonl
```

---

## 7. J-Link 自动化验证脚本

Codex 需要新增：

```text
tools/ota_test/jlink_flash.ps1
tools/ota_test/jlink_reset.ps1
tools/ota_test/jlink_read_flash.ps1
```

### 7.1 烧录 APP

`jlink_flash.ps1`：

```powershell
param(
  [string]$JLinkExe = "C:\Program Files\SEGGER\JLink\JLink.exe",
  [string]$Device = "STM32F103CB",
  [string]$Image
)

$script = @"
device $Device
if SWD
speed 4000
connect
r
h
loadfile $Image
verifyfile $Image
r
g
exit
"@

$scriptPath = "tools\ota_test\logs\jlink_flash.jlink"
$script | Out-File -Encoding ascii $scriptPath
& $JLinkExe -CommanderScript $scriptPath | Tee-Object "tools\ota_test\logs\jlink_flash.log"
```

### 7.2 读取备份区/APP 区片段

用于 OTA 前后验证：

```text
savebin tools/ota_test/logs/ota_backup_head.bin,0x08024000,0x400
savebin tools/ota_test/logs/app_head.bin,0x08008000,0x400
```

验证点：

- OTA 下载完成后，`0x08024000 + 0x200` 处不是 `0x12345678`。
- Boot 搬运完成后，`0x08008000 + 0x200` 与 OTA 包 checksum 一致。
- APP 区版本号变化。

---

## 8. 实机验证流程

### 阶段 A：静态检查

Codex 执行：

```powershell
git status
Get-ChildItem -Recurse -Filter *.uvprojx
Select-String -Path .\**\*.c,.\**\*.h -Pattern "OTA_DEBUG_DOWNLOAD_ONLY|OTA_STREAM_TO_BACKUP_DEBUG|QHTTPREADFILE|OTABAKROM_STARTADDR|APROM_STARTADDR|APP_VERSION"
```

必须输出检查报告：

```text
tools/ota_test/logs/static_check.md
```

报告必须包含：

- Keil 工程路径。
- APP IROM 起始地址。
- APP IROM 大小。
- 是否定义 `APROM_OFFSET`。
- OTA 备份区起止地址。
- 当前芯片型号/目标型号。
- 当前固件大小。
- 是否还存在 debug-only OTA 宏。

---

### 阶段 B：编译 APP

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -b "<APP>.uvprojx" -j0 -o "tools\ota_test\logs\build_app.log"
```

通过标准：

```text
0 Error(s), 0 Warning(s) 或仅允许已有非 OTA 相关 Warning
```

如果 Warning 与 OTA 修改有关，必须修复。

---

### 阶段 C：生成 OTA 包

```powershell
python tools/ota_test/pack_ota_firmware.py `
  --input "<build_output_app.bin>" `
  --output "tools/ota_test/out/cat1.bin" `
  --device-type 0x0003 `
  --max-size 114688
```

输出检查：

```powershell
python tools/ota_test/inspect_ota_bin.py tools/ota_test/out/cat1.bin
```

必须显示：

```text
checksum != 0x12345678
size <= 114688
device_type = 0x0003
size % 4 == 0
```

---

### 阶段 D：本地/服务器文件检查

Codex 需要确认 OTA URL 可下载：

```powershell
curl.exe -v "<OTA_URL>" -o tools\ota_test\logs\server_cat1.bin
```

不能要求服务器返回 `Content-Length`。但如果返回了，记录下来。

禁止失败条件：

- HTTP 状态不是 200。
- 文件小于固件头最小长度。
- 下载文件头 checksum/size/device_type 无效。

---

### 阶段 E：J-Link 烧录基线固件

先烧当前稳定 APP 或上一版本 APP，确保设备能上线。

```powershell
powershell tools/ota_test/jlink_flash.ps1 -Image "<baseline_app.hex>"
```

烧录后检查：

1. 设备重启。
2. 4G 模块上线。
3. MQTT 有心跳或登录。
4. 当前版本为旧版本。

---

### 阶段 F：MQTT 触发 OTA

```powershell
python tools/ota_test/mqtt_ota_publish.py `
  --host 47.120.15.220 `
  --port 1883 `
  --imei <IMEI> `
  --url "<OTA_URL>" `
  --id 156783 `
  --timeout 600
```

必须保存：

```text
tools/ota_test/logs/mqtt_ota_<timestamp>.jsonl
tools/ota_test/logs/serial_or_rtt_<timestamp>.log
```

OTA 过程中关键日志必须出现：

```text
[OTA][I] cmd received id=... url=...
[OTA][I] raw tcp mode: bypass module HTTP stack
[OTA][I] raw tcp open ok
[OTA][I] raw tcp qisend ...
[OTA][I] raw http header done status=200 ...
[OTA][I] stream image header checksum=... size=... type=...
[OTA][I] stream progress rx=...
[OTA][I] stream body complete rx=...
[OTA][I] STREAM DOWNLOAD VERIFY SUCCESS
[OTA][I] stream verify ok: mark upgrade and jump boot
```

失败日志中不能再出现：

```text
+QHTTPREADFILE: 729
module fs save result err=729
DEBUG DOWNLOAD ONLY SUCCESS
skip mcu flash write in debug version
debug stream: skip boot/reset
STREAM RAW OK ... boot off
```

---

### 阶段 G：Boot 搬运和新 APP 验证

OTA 成功后设备应重启。Codex 通过以下任一方式验证：

1. MQTT 新版本上线。
2. J-Link 读取 APP 区固件头。
3. 日志显示 APP_VERSION 已变化。
4. MQTT 查询设备信息，`sver` 或相关版本字段变化。

J-Link 读取验证：

```text
savebin tools/ota_test/logs/app_after_ota_head.bin,0x08008000,0x400
```

使用 Python 对比：

```powershell
python tools/ota_test/compare_app_header.py `
  --ota tools/ota_test/out/cat1.bin `
  --flash tools/ota_test/logs/app_after_ota_head.bin
```

通过标准：

```text
APP header checksum == OTA bin checksum
APP header size == OTA bin size
APP device_type == OTA bin device_type
```

---

## 9. 强制测试用例

Codex 至少跑完以下用例。每个用例都要有日志。

### TC01：正常 OTA

条件：

- URL 返回 HTTP 200。
- 可能没有 Content-Length。
- 固件头合法。

预期：

- raw TCP 下载成功。
- 备份区校验成功。
- 写升级标志。
- 跳 Boot。
- 新 APP 启动。

---

### TC02：无 Content-Length

条件：

- 服务器响应不包含 Content-Length。
- 固件头合法。

预期：

- 日志出现：`stream size unknown: parse header`。
- 从固件头解析 size。
- OTA 成功。

---

### TC03：错误固件头

条件：

- 使用未打包 raw bin 或故意写 `checksum=0x12345678`。

预期：

- OTA 拒绝。
- 不写升级标志。
- 不跳 Boot。
- MQTT 上报 OTA error。

---

### TC04：device_type 不匹配

条件：

- OTA 包 device_type 改为错误值。

预期：

- OTA 拒绝。
- 日志显示 `device type mismatch`。
- 设备继续运行旧 APP。

---

### TC05：网络中断/下载超时

条件：

- URL 不可达或下载中断。

预期：

- raw TCP timeout。
- 不写升级标志。
- 不跳 Boot。
- MQTT 恢复后能继续工作。

---

### TC06：复位恢复

条件：

- OTA 下载过程中手动 reset 或 J-Link reset。

预期：

- 如果未写升级标志，Boot 不搬运。
- 旧 APP 能继续启动。
- 下一次 OTA 可重新开始。

---

### TC07：固件接近上限

条件：

- OTA 包大小接近 112KB，但不超过 `APROM_SAFE_ENDADDR - APROM_STARTADDR`。

预期：

- 边界检查通过。
- 不越界写入。
- 超过上限时必须拒绝。

---

### TC08：看门狗稳定性

条件：

- OTA 下载全过程。

预期：

- 不出现 IWDG 异常复位。
- 如果发生复位，需要通过 reset reason 或日志定位在什么阶段超过喂狗窗口。

---

## 10. Codex 需要输出的交付物

Codex 完成后必须输出：

```text
1. 修改后的源码
2. Keil 编译日志
3. APP map 文件摘要
4. OTA 打包脚本
5. OTA 包 inspect 结果
6. J-Link 烧录日志
7. MQTT OTA 下发日志
8. OTA 过程设备日志
9. OTA 后 APP 区头部读取结果
10. 测试用例结果表
11. Git commit hash
```

推荐目录：

```text
tools/ota_test/
  pack_ota_firmware.py
  inspect_ota_bin.py
  mqtt_ota_publish.py
  jlink_flash.ps1
  jlink_reset.ps1
  compare_app_header.py
  out/
    cat1.bin
    cat1_ota_info.json
  logs/
    build_app.log
    jlink_flash.log
    mqtt_ota_*.jsonl
    ota_device_*.log
    static_check.md
    test_report.md
```

---

## 11. 最小通过标准

只有同时满足以下条件，才算 OTA 问题解决：

| 项目           | 标准                                 |
| -------------- | ------------------------------------ |
| 编译           | 0 Error                              |
| 固件大小       | APP 和 OTA 包不超过分区上限          |
| 下载路径       | raw TCP + QIRD，不进入 QHTTPREADFILE |
| Content-Length | 没有也能升级                         |
| 206 分片       | 不需要 206                           |
| 备份区写入     | 无越界，无 Flash verify mismatch     |
| 校验           | checksum、size、device_type 全部通过 |
| 看门狗         | OTA 全过程不因 IWDG 复位失败         |
| 中断           | UART 接收无持续 overflow/drop        |
| 日志           | 无 HEX 洪泛，无 debug-only 成功假象  |
| Boot           | 写升级标志并跳 Boot                  |
| 新 APP         | OTA 后新版本启动并 MQTT 在线         |
| 回退           | 校验失败/中断失败时保留旧 APP        |

---

## 12. 给 Codex 的最终执行提示词

把下面整段直接交给 Codex：

```text
你现在接管一个 STM32F103/HK32F103 CAT.1 MQTT 工程的 OTA 修复任务。电脑已连接 J-Link，设备已连接 J-Link，允许你自动编译、烧录、复位、通过 MQTT 工具发送 OTA 指令并验证实机结果。

当前服务器限制：不支持 Range 分片，不返回 206，GET 响应不稳定携带 Content-Length。当前 OTA 失败日志曾出现 QHTTPREADFILE 729。禁止继续依赖模块 QHTTPREADFILE/UFS 全量保存，禁止依赖 Content-Length，禁止把分片下载作为当前方案。

你的目标是把 OTA 修成 raw TCP + HTTP GET + AT+QIRD 小块读取 + MCU 流式写 OTA 备份区 + 校验 + 写升级标志 + 跳 Boot + 新 APP 启动 的完整闭环。

请按以下步骤执行：

1. 先执行 git status，并创建分支 fix/ota-rawtcp-jlink-e2e。
2. 定位 Keil 工程、APP 工程、Boot 工程、当前 Target、IROM 地址、C/C++ Define，确认 APP 起始地址必须是 0x08008000，并确认 APROM_OFFSET 是否定义。
3. 确认实际芯片 Flash 容量是否支持当前 0x08000000~0x0803FFFF 的 256KB 双区 OTA。若实际只有 128KB，立即停止并报告，不要继续烧录双区 OTA。
4. 修改 Core/Src/LampProtocolLib/ota.c，把 OTA 调试宏收敛到 ota_config.h，生产默认：OTA_DEBUG_DOWNLOAD_ONLY=0，OTA_STREAM_TO_BACKUP_DEBUG=0，OTA_STREAM_ALLOW_RAW_BIN_TEST=0，OTA_DEBUG_CLEAR_ALL_UFS=0，OTA_USE_QHTTPREADFILE_UFS=0，OTA_USE_RAW_TCP_STREAM=1。
5. 保留 raw TCP + QIRD 状态机，彻底禁用 QHTTPREADFILE/UFS 生产路径。raw TCP HTTP GET 必须使用 Connection: close 和 Accept-Encoding: identity。
6. OTA 不得依赖 Content-Length。如果有 Content-Length，只作为辅助并与固件头 size 对比。没有 Content-Length 时，必须从固件头 0x204 读取 size。
7. 修复 STREAM_VERIFY 成功后的逻辑：校验通过后写 sys_data.sn=0xaa5555aa，sys_data_store()，喂狗，延迟极短时间，再 iap_jump2boot()。不能再出现 debug stream: skip boot/reset。
8. 修复 get_checksum_status：占位 checksum=0x12345678 或 size=0x89abcdef 必须失败；size 边界必须用 APROM_SAFE_ENDADDR-APROM_STARTADDR 和 OTABAKROM_ENDADDR-OTABAKROM_STARTADDR+1，不允许写死 58*2048。
9. 增加 device_type 校验，APP 与 Boot 必须一致，当前代码中 APP device_type 是 0x0003，需核对 Boot。
10. Flash 写入期间必须考虑 IWDG。当前 watchdog 约 2 秒，擦备份区每页、写 Flash 每 256B 或每页、QIRD 循环、AT 等待循环都必须喂狗。
11. 保持 UART 中断接收可用。CONNECT_OTA_AT_RAW_QIRD_DATA 状态中禁止执行 Flash erase/program，只允许取队列字节、HTTP 解析、写 RAM page buffer、轻量喂狗。Flash 写入只能在 QIRD trailer OK 后或 verify 前安全窗口执行。
12. 控制日志：默认关闭 HEX dump 和每字节打印，只保留 OTA 状态、错误码、rx/programmed/expected、checksum、跳 Boot 结果。生产日志不能洪泛。
13. 新增 tools/ota_test/pack_ota_firmware.py，用于给 APP bin 写入 0x200 checksum、0x204 size、0x208 device_type，checksum 算法必须与当前 user_frash_checksum 兼容。
14. 新增 tools/ota_test/mqtt_ota_publish.py，用于向 MS/<IMEI>/pcp2dev 发布 OTA 指令，并订阅 MS/<IMEI>/dev2pcp 等待响应、进度、错误和重启后的上线。
15. 新增 J-Link PowerShell 脚本，支持烧录、复位、读取 APP/OTA 头部。禁止修改 Option Bytes，禁止修改 RDP，禁止无必要全片擦除。
16. 编译通过后，先提交一次 git commit，再使用 J-Link 烧录基线固件，确认设备上线。
17. 生成 cat1.bin OTA 包，确认 checksum、size、device_type 合法。
18. 通过 MQTT 发送 OTA 指令，等待并记录完整日志。
19. OTA 完成后用 J-Link 读取 0x08008000+0x200/0x204/0x208，确认 APP 区已被 Boot 搬运为新固件。
20. 跑完正常 OTA、无 Content-Length、错误固件头、device_type 不匹配、网络中断、下载中复位、固件接近上限、看门狗稳定性这些测试用例。
21. 最终输出 test_report.md，包含命令、日志路径、结果、失败原因和 commit hash。

不要只修改代码不验证。这个任务必须以 J-Link 烧录 + MQTT OTA 下发 + 实机新 APP 启动成功作为完成标准。
```

---

## 13. 当前风险提醒

1. 如果实物是 128KB Flash，当前双区 OTA 必定失败，必须换 256KB/512KB 芯片或改成外部存储/模块存储/单区高风险 OTA。
2. 如果 Boot 工程没有一起提供，Codex 只能验证 APP 写备份区和跳 Boot，不能保证 Boot 搬运逻辑正确。需要在实际完整项目根目录中处理 Boot。
3. 如果 MQTT broker 需要账号密码，Codex 要复用设备协议中的 IMEI 密码规则或使用现有测试工具配置。
4. 如果没有串口日志，Codex 应通过 MQTT 上报、J-Link 读 Flash、J-Link reset、版本查询组合验证，不要强依赖串口。
5. 如果 OTA URL 指向的文件不是经过 `pack_ota_firmware.py` 处理后的固件，必须先更新服务器文件，否则设备应拒绝升级。

```

```
