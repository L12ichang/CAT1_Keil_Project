# 一体化电源电流校准——Codex 执行文档

> **历史文档，已废弃，不得作为本次量产校准实现依据。** 本文仍保留 21 点、旧协议命名和历史 MID=4/补偿假设。2026-08-20 起只以 `done/cat1-product-profile-cal-context-20260817` 上的 `docs/CAT1_50W校准固件基线与上位机对接方案.md` 和 `docs/CAT1_50W固件与上位机联合审核清单.md` 为目标规范；优先级见 `docs/CAT1_50W文档口径说明.md`。本文中的 `profileCrc` 绑定 SET_OUTCUR/HWMAX、21 点和旧 payload 设计均不得继续引用。

> 文档状态：历史废弃，仅用于追溯旧 21 点方案；不得作为协议或数据结构实施基线。
> 适用工程：`CAT1_Keil_Project/Core`  
> 适用对象：内置 4G 模块与 MCU 的单路一体化电源，MCU 直接输出 PWM 控制恒流电源  
> 明确排除：DTU → UART → 外部数字电源、从机地址、数字电源寄存器协议、多通道选择  
> 校准点：0%、5%……100%，共 21 点

## 1. 目标、边界与总体架构

本项目要让工厂上位机通过设备现有中科 MQTT 通道，对同一台设备内 MCU 的 PWM 输出进行闭环标定。电子负载/功率计是输出电流的标准源；MCU 自身 ADC 电流值只用于诊断、稳定性辅助和保护，不能替代标准仪表。

```text
上位机校准软件
├─ MQTT：发送校准动作、接收 ACK/状态
├─ 电子负载/功率计：读取标准电流
├─ 搜索算法：寻找每个目标电流对应的逻辑 PWM
└─ 生产记录：保存原始数据、曲线、CRC、设备身份和结果
                  │ 4G / 现有 MQTT Topic 与中科消息外层
                  ▼
一体化电源
├─ 内置 4G 模块
├─ MQTT 中科协议解析
├─ 校准会话、安全状态机、RAM pending 曲线
├─ 21 点查表与插值
├─ 温度/电压/电流/短路等保护（始终有效）
└─ MCU PWM → OCO/恒流输出
```

实施原则：

1. 不新增 UART 二级协议，不把本设备抽象成数字电源从机。
2. MQTT 原有 `SV/CT/ID/DT` 外层和 Topic 保持兼容；在 `DT` 下扩展独立 `Calibration` 业务节点。
3. 协议操作“逻辑 PWM”，不直接写 TIM CCR。逻辑 PWM 的最终范围以工程常量和硬件极性核对结果为准。
4. 上位机负责标准仪表、搜索和曲线生成；设备负责安全执行、会话管理、曲线校验、临时预览和可靠持久化。
5. 所有过程非阻塞；校准期间 MQTT、看门狗和保护任务必须继续运行。

## 2. 当前工程事实与开工前确认

### 2.1 已从源码确认

当前网络调光主链路为：

```text
Core/Src/LampProtocolLib/mqtt_zk_protocol.c
  zk_apply_brightness()
    → dim_level = brightness
    → dim_ready()
Core/Src/gateway/net_dim.c
  uart_diam_process()
    → 旧 MID==4 减 3 补偿
    → oco_on()/oco_off()
    → sys_pwm_output()
Core/Src/sys_pwm.c
  pwm_output()
    → 温度/电压限制
    → percent × SET_OUTCUR × PWM_USEFUL_RANGE /
      (HWMAX_OUTCUR × 100)
    → hw_tim1_pwm2_set_PWM_OUT()
Core/Src/hw_tim1_pwm2.c
  → OCO 开关
  → CCR = pwm + OP_PWM_OFFSET
```

当前实现还存在以下重要事实：

- `sys_pwm.c` 中 `PWM_OUT_MAX` 为 1000，`PWM_USEFUL_RANGE = 1000 - OP_PWM_OFFSET`。
- `hw_tim1_pwm2.c` 的 TIM1 周期为 999，PWM2 模式，比较值写入时增加 `OP_PWM_OFFSET`。
- `logicalPwm == 0` 时硬件层会调用 `oco_off()`；不能只依赖 CCR 表达关断。
- `net_dim.c` 对 `MID == 4` 且部分亮度范围执行 `dim_level - 3` 旧补偿。
- `sys_pwm.c` 先执行多项保护限制，再换算 PWM；`sys_Vo_Io.c` 及其他保护流程会调用 `sys_pwm_reload()` 或直接影响输出。
- `sys_Vo_Io.c` 已有 `Vo_value`（0.1 V）、`Io_value`（mA）、`Po_value`（0.1 W）；ADC 原始量的准确导出接口尚未冻结。
- `main.c` 主循环包含网络、ADC、保护、`uart_diam_process()`、`sys_pwm_process()`、JSON 处理与看门狗。
- `sys_data_store()` 当前同时写主/备数据区；`zk_property.c` 也已有独立主/备 Flash 记录。校准区不能未经布局核对直接复用或覆盖它们。

### 2.2 必须在编码前定位确认

以下项目不能凭文档假设，由 Codex 在工程中搜索、记录结论并再落代码：

1. 中科协议各 `CT` 值、请求/应答 Topic、`ID` 回显和 ACK 发送入口；需在工程中定位确认。
2. MQTT 单包最大长度、cJSON 堆/栈预算，以及 7 点分片是否满足真实上限；需在工程中定位确认。
3. `sys_tick_get_tick()` 的单位、溢出语义及可用于超时的接口；需在工程中定位确认。
4. OTA、严重故障、当前保护码、温度值和输出使能状态的统一读取 API；需在工程中定位确认。
5. ADC 电压/电流原始值的变量可见性及一致快照方法；需在工程中定位确认。
6. Flash 芯片实际容量、页大小、Bootloader/OTA/参数区边界、链接映像空闲页；需结合 `.map` 与地址定义确认。
7. `OP_PWM_OFFSET` 的有效范围、PWM2 极性和 0/1000 对应的真实输出方向；必须用示波器和小功率实测确认。
8. 是否允许校准 100% 对应逻辑 PWM 达到 `1000 - OP_PWM_OFFSET`，以及保护允许的最大输出；需硬件负责人确认。
9. 工厂参数变更入口全集，以便在 `SET_OUTCUR/HWMAX_OUTCUR/OP_PWM_OFFSET/MID/...` 变化时使旧曲线失效；需在工程中定位确认。

## 3. 正常调光与校准控制方式

### 3.1 统一输出模型

改造后所有业务亮度应遵循：

```text
目标亮度 0..100
  → 校准锁检查
  → 有效曲线：21 点插值；无有效曲线：旧线性公式
  → 所有安全限制
  → 唯一逻辑 PWM 硬件入口
  → OP_PWM_OFFSET/极性/CCR/OCO
```

校准直接 PWM 路径为：

```text
Calibration.setPwm(logicalPwm)
  → 会话、状态、范围检查
  → 安全限制（不可绕过）
  → 唯一逻辑 PWM 硬件入口
```

直接 PWM 路径绕过“亮度→曲线/旧公式”和渐变，但不得绕过 OCO 关断、PWM 上限、过温、过流、短路、输入异常和看门狗。若保护层当前只接受“百分比”，需重构为能够约束逻辑 PWM，或在统一输出仲裁器中强制关断/限幅；具体复用符号需在工程中定位确认。

### 3.2 控制仲裁优先级

从高到低固定为：

1. 严重故障/硬件保护：强制关闭。
2. 校准超时、断会话、abort/exit/commit 完成：强制关闭。
3. 校准直接 PWM 或临时曲线验证。
4. 正常网络/离线计划/上电控制。

校准锁有效时，普通调光命令不得改变物理输出，可返回业务忙错误；离线计划、上电软启动、PWM reload 也不能覆盖校准输出。退出校准后保持关闭，等待下一条正常控制命令，不恢复校准前功率。

## 4. 21 点校准目标和职责

点位索引固定：`percent = index × 5`，`index = 0..20`。不得接受任意百分比写入曲线。

目标电流：

```c
target_ma = ((uint32_t)rated_current_ma * percent + 50U) / 100U;
```

上位机负责设备身份校验、标准仪表连接、21 点搜索、稳定判断主判据、生成曲线、临时验证、生产记录和 commit 决策。MCU 负责执行安全 PWM、返回状态、管理 `sessionId/seq`、接收 pending 曲线、检查完整性/单调性/CRC/profile、临时应用及 Flash 提交。

MCU 的 `Io_value` 不作为“达标”的权威值。若未来增加 MCU 电流采样通道的增益/偏置校准，应作为独立版本化字段和独立流程，不与 21 点输出曲线混淆。

## 5. MQTT 协议扩展

### 5.1 外层和通用规则

沿用工程已有外层，示例中的 `SV/CT` 仅表示沿用现值，不冻结具体取值：

```json
{
  "SV": "<沿用现有值>",
  "CT": "<沿用现有请求类型>",
  "ID": "<现有消息ID>",
  "DT": {
    "Calibration": {
      "action": "readInfo",
      "seq": 100
    }
  }
}
```

通用要求：

- 字段名、大小写和数值单位一经联调冻结不得变化。
- 整数不得用浮点 JSON 表达；CRC 建议使用无符号十进制，日志可另显示十六进制。
- 除 `readInfo` 外，会话内动作必须携带 `sessionId` 与严格递增 `seq`。
- 响应回显原 `ID`，并在 `DT` 下使用 `CalibrationAck`、`CalibrationStatus`、`CalibrationCurveStatus` 或 `CalibrationInfo`。
- 不返回中文错误文本；UI 依据错误码本地化。
- 未识别字段可忽略，但缺失必填字段必须拒绝。类型错误、溢出、负数、数组过长一律拒绝。

### 5.2 幂等机制

`sessionId` 推荐为最大 32 字节 ASCII，设备内部可保存完整值或保存 CRC32/哈希并处理碰撞风险。`seq` 为 `uint32_t`，会话中从 1 递增。

设备至少缓存最近一次已完成命令的：会话标识、`seq`、action、result 和响应关键字段。

```text
新 session + enter：创建会话
当前 session，seq > last_seq：按状态机处理
当前 session，seq == last_seq 且 action/参数摘要一致：重发上次 ACK，不重复执行
当前 session，seq == last_seq 但参数不同：INVALID_SEQ
当前 session，seq < last_seq：STALE_SEQ
不同 session 的会话动作：INVALID_SESSION
```

`commit` 重试必须返回第一次提交结果，绝不再次擦写 Flash。只有成功 `exit/abort` 或超时清理后才能接受新 `enter`。`seq` 回绕不在同一会话内支持。

### 5.3 通用 ACK

```json
{
  "SV": "<沿用>",
  "CT": "<沿用现有应答类型>",
  "ID": "<回显请求ID>",
  "DT": {
    "CalibrationAck": {
      "action": "setPwm",
      "sessionId": "CAL-20260713-0001",
      "seq": 12,
      "result": 0,
      "state": "DIRECT_TEST",
      "detail": 0
    }
  }
}
```

`detail` 是可选的数值上下文，不改变 `result` 语义。

## 6. 完整动作定义

### 6.1 `enter`

请求：

```json
{"DT":{"Calibration":{"action":"enter","sessionId":"CAL-20260713-0001","seq":1,"profileCrc":38152,"timeoutSec":30}}}
```

`timeoutSec` 允许范围建议 10..300，默认及最大值最终由安全评审冻结；不允许上位机关闭超时。设备检查无 OTA、无活动校准、无严重故障、profile 匹配，然后停止渐变/计划覆盖、输出归零、清 pending，进入 `READY`。

成功 ACK 需包含：`state`、设备计算的 `profileCrc`、`pwmLogicalMax`、`pointCount=21`、`curveState`。任何检查失败均保持输出关闭或原安全状态，不创建半会话。

### 6.2 `setPwm`

```json
{"DT":{"Calibration":{"action":"setPwm","sessionId":"CAL-20260713-0001","seq":12,"pointIndex":4,"targetPercent":20,"logicalPwm":128}}}
```

校验 `pointIndex <= 20`、`targetPercent == pointIndex*5`、`logicalPwm <= pwmLogicalMax`。0% 只允许 `logicalPwm=0`。设备立即重置该点状态时间，执行安全 PWM，响应实际接受的 `logicalPwm`、`outputEnabled`、`protectCode` 和 `state=DIRECT_TEST`。保护导致限幅或关断时必须明确返回 `PROTECT_ACTIVE`，不得假装已应用请求值。

### 6.3 `readStatus`

请求：

```json
{"DT":{"Calibration":{"action":"readStatus","sessionId":"CAL-20260713-0001","seq":13}}}
```

响应：

```json
{
  "DT":{"CalibrationStatus":{
    "sessionId":"CAL-20260713-0001","seq":13,"result":0,
    "state":"DIRECT_TEST","pointIndex":4,"targetPercent":20,
    "logicalPwm":128,"compareValue":128,"outputEnabled":1,
    "adcVoltageRaw":832,"adcCurrentRaw":417,
    "voltage01V":440,"currentMa":338,"power01W":149,
    "temperatureC":42,"protectCode":0,"sampleAgeMs":12,
    "curveState":"EMPTY","lastError":0
  }}
}
```

`compareValue`、ADC 原始量、温度和 `sampleAgeMs` 的来源需在工程中定位确认。无法可靠提供的字段第一版应删除或置入能力位，不能填假数据。读取状态会刷新会话通信超时，但不会改变 PWM。

### 6.4 `writeCurveChunk`

建议固定 3 个分片，每片最多 7 点；最后为索引 14..20。若真实 MQTT 限制允许也不得无界数组。

```json
{"DT":{"Calibration":{"action":"writeCurveChunk","sessionId":"CAL-20260713-0001","seq":50,"curveVersion":1,"profileCrc":38152,"curveCrc":22741,"startIndex":0,"values":[0,31,66,96,128,159,190]}}}
```

所有分片的 `curveVersion/profileCrc/curveCrc` 必须一致；`startIndex + values.length <= 21`。写入 RAM 并置 received bitmap，相同索引写相同值可幂等接受，写不同值须先明确支持“替换 pending”策略；第一版建议返回 `CHUNK_CONFLICT`，由上位机 abort 后重来。此动作绝不写 Flash。

ACK 返回 `startIndex/receivedCount/receivedBitmap/totalReceived`。21 位 bitmap 用 `uint32_t`。

### 6.5 `readCurveStatus`

```json
{"DT":{"Calibration":{"action":"readCurveStatus","sessionId":"CAL-20260713-0001","seq":53}}}
```

响应包含 `pointCount=21`、`receivedCount`、`missingBitmap`、`profileCrc`、`curveCrc`、`curveState`、`curveValid`。只有 21 点齐全且结构校验、CRC 校验通过时 `curveValid=1`。

### 6.6 `applyTemporary`

```json
{"DT":{"Calibration":{"action":"applyTemporary","sessionId":"CAL-20260713-0001","seq":60,"curveCrc":22741}}}
```

设备再次验证 profile、21 点、0% 点、范围、单调性和 CRC；成功后 pending 曲线成为 RAM 预览曲线，`curveState=TEMP_APPLIED`，但保持输出关闭。验证可新增 `setPercent` 动作会扩大协议；第一版可约定 `setPwm` 仍只用于直接 PWM 搜索，而临时曲线验证复用现有正常亮度请求但由校准锁特殊接收。更推荐在实现评审时新增明确的 `setTestPercent`，避免普通控制绕过锁；是否新增需在协议冻结会议确认。

### 6.7 `commit`

```json
{"DT":{"Calibration":{"action":"commit","sessionId":"CAL-20260713-0001","seq":80,"profileCrc":38152,"curveCrc":22741}}}
```

仅允许从 `TEMP_APPLIED` 提交。设备先归零，再全量复验，写非活动 Flash 槽、回读验证、切换有效序列，随后将 RAM active 指向新曲线。成功后 `curveState=VALID`、`state=COMMITTED`、输出保持关闭。ACK 返回 `curveVersion/curveCrc/profileCrc/stored=1`。写入或回读失败不得破坏旧有效槽。

### 6.8 `abort`

```json
{"DT":{"Calibration":{"action":"abort","sessionId":"CAL-20260713-0001","seq":90,"reason":3}}}
```

任何会话活动状态均可 abort：立即关闭输出、停止直接 PWM、清 pending/预览、恢复原 active 曲线、释放校准锁并进入 `IDLE`。不写 Flash，不恢复先前亮度。

### 6.9 `exit`

```json
{"DT":{"Calibration":{"action":"exit","sessionId":"CAL-20260713-0001","seq":91}}}
```

仅成功提交后允许正常 exit。关闭输出、释放校准锁、保留新 active 曲线、进入 `IDLE`。未提交时必须要求 `abort`，避免含糊地保留 pending。

### 6.10 `readInfo`

可在无会话状态使用：

```json
{"DT":{"Calibration":{"action":"readInfo","seq":100}}}
```

```json
{"DT":{"CalibrationInfo":{"seq":100,"result":0,"curveState":"VALID","curveVersion":1,"pointCount":21,"profileCrc":38152,"curveCrc":22741,"setOutCurrentMa":2700,"hwMaxOutCurrentMa":4700,"opPwmOffset":0,"pwmLogicalMax":1000}}}
```

建议再返回 `capabilities` 位图和存储序列号，具体字段在协议冻结时确定。

## 7. 错误码与状态机

```c
typedef enum {
    CAL_OK = 0,
    CAL_INVALID_ACTION = 1,
    CAL_INVALID_PARAM = 2,
    CAL_BUSY = 3,
    CAL_INVALID_SESSION = 4,
    CAL_DUPLICATE_SEQ = 5,
    CAL_INVALID_STATE = 6,
    CAL_PROFILE_MISMATCH = 7,
    CAL_PWM_OUT_OF_RANGE = 8,
    CAL_PROTECT_ACTIVE = 9,
    CAL_CURVE_INCOMPLETE = 10,
    CAL_CURVE_NOT_MONOTONIC = 11,
    CAL_CURVE_CRC_ERROR = 12,
    CAL_FLASH_WRITE_ERROR = 13,
    CAL_FLASH_VERIFY_ERROR = 14,
    CAL_TIMEOUT = 15,
    CAL_INTERNAL_ERROR = 16,
    CAL_STALE_SEQ = 17,
    CAL_SEQ_CONFLICT = 18,
    CAL_CHUNK_CONFLICT = 19,
    CAL_OTA_ACTIVE = 20,
    CAL_OUTPUT_NOT_STABLE = 21
} cal_result_t;
```

设备状态：

```text
IDLE --enter--> READY --setPwm/readStatus循环--> DIRECT_TEST
 READY/DIRECT_TEST --writeCurveChunk--> CURVE_RECEIVING
 CURVE_RECEIVING --21点齐全--> CURVE_PENDING
 CURVE_PENDING --applyTemporary--> TEMP_APPLIED
 TEMP_APPLIED --commit--> COMMITTING --> COMMITTED --exit--> IDLE
 任意会话状态 --abort/超时/严重故障--> ABORTING --> IDLE
```

`COMMITTING` 中只允许重复查询/重发相同 commit，普通新动作返回 BUSY。Flash 操作若是同步短操作，也必须在写前关断输出并满足看门狗时限；若可能超时则拆成非阻塞步骤。

## 8. 关键 C 数据结构（建议，不假定现有符号）

```c
#define CAL_POINT_COUNT       21U
#define CAL_PWM_SCALE_MAX     1000U
#define CAL_SESSION_ID_MAX    32U

typedef enum {
    CAL_STATE_IDLE = 0,
    CAL_STATE_READY,
    CAL_STATE_DIRECT_TEST,
    CAL_STATE_CURVE_RECEIVING,
    CAL_STATE_CURVE_PENDING,
    CAL_STATE_TEMP_APPLIED,
    CAL_STATE_COMMITTING,
    CAL_STATE_COMMITTED
} cal_state_t;

typedef struct {
    uint16_t version;
    uint16_t point_count;
    uint32_t profile_crc;
    uint32_t curve_crc;
    uint16_t logical_pwm[CAL_POINT_COUNT];
} cal_curve_t;

typedef struct {
    cal_state_t state;
    char session_id[CAL_SESSION_ID_MAX + 1U];
    uint32_t last_seq;
    uint32_t last_activity_tick;
    uint32_t received_bitmap;
    uint8_t point_index;
    uint8_t target_percent;
    uint16_t direct_pwm;
    uint8_t lock_active;
    uint8_t temporary_active;
    cal_curve_t pending;
} cal_context_t;
```

持久化记录建议：

```c
typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t record_size;
    uint32_t sequence;
    uint32_t commit_state;
    cal_curve_t curve;
    uint32_t record_crc;
} cal_flash_record_t;
```

结构体落 Flash 前须固定字节序、对齐和版本迁移策略；建议显式序列化，至少用静态断言检查大小。不要直接把结构插入 `factory_user_buff[128]` 中间。

## 9. 21 点上位机校准算法

### 9.1 准备和稳定判定

每次测量顺序：设置 PWM → 等最小建立时间 → 周期读取标准仪表 → 取稳定窗口。建议初值（实机调优后冻结）：

- 最小建立时间 300~500 ms；
- 采样间隔 50~100 ms；
- 窗口 8~16 个样本；
- 最大稳定等待 3 s；
- 稳定条件：窗口 `max-min <= max(5mA, average×0.5%)`，且线性趋势斜率不超阈值；
- 同时要求 MCU 无保护，温度未超过校准上限，实际 PWM 未被限幅。

标准仪表自身量程、滤波和更新率必须写入工站配置。稳定条件连续满足两个窗口更稳妥。

### 9.2 容差

```text
tolerance_ma = max(10mA, target_ma × 0.5%)
```

低电流段若仪表分辨率不足，可按产品规格单独定义绝对容差，但必须记录配置版本，不得在算法中隐藏。达标：`abs(reference_ma-target_ma) <= tolerance_ma`。

### 9.3 搜索策略

PWM 与电流总体单调时使用“渐进探测 + 二分搜索”：

1. 点 0 特殊处理，见下节。
2. 当前点下界为上一已确认点 PWM；为保证严格上升可用 `prev+1`，若硬件允许平台区则曲线校验采用不下降，但产品标准应优先要求严格上升。
3. 上界为 `pwmLogicalMax`。初值优先用线性估计 `target/rated × estimated_full_pwm`，并限制在上下界。
4. 初次测量若明显不足/过高，按较大步长渐进扩展，快速形成夹逼区间。
5. 有一低一高两个稳定测量点后执行整数二分。
6. 若中点等于边界或 PWM 分辨率耗尽，选择误差最小且满足安全、单调条件的候选；仍超容差则该点失败。
7. 每点限制最大迭代（建议 12~16）、稳定超时和总耗时。
8. 每次测量都保存 PWM、电流、温度、时间、稳定统计和保护状态，不能只保存最终值。

伪代码：

```text
for index in 0..20:
  target = rated_current * (index*5) / 100
  if index == 0: calibrate_zero(); continue
  low = curve[index-1] + monotonic_step
  high = pwmLogicalMax
  candidate = clamp(estimate(target), low, high)
  best = none
  repeat up to maxIterations:
    setPwm(index, candidate)
    sample = waitStandardMeterStable()
    status = readStatus()
    fail if protection/timeout/not-applied
    best = lowerError(best, sample)
    if abs(sample.current-target) <= tolerance: accept candidate; break
    if sample.current < target: low = candidate + 1
    else: high = candidate - 1
    fail if low > high and best outside tolerance
    candidate = low + (high-low)/2
```

### 9.4 0% 特殊处理

0% 固定 `logicalPwm[0]=0`，不做二分搜索。设备必须关 OCO。上位机等待关断稳定后验证标准电流低于产品定义的关断泄漏阈值；超阈值直接判硬件/关断失败，不能通过写非零 PWM “校准零点”。

### 9.5 边界和异常

- 100% 在最大 PWM 仍达不到目标：校准失败，不能保存越界值。
- 很小 PWM 已超过 5% 目标：若 PWM 分辨率无法满足容差，失败并记录分辨率限制。
- 测得电流明显非单调、突跳或保护动作：停止输出并失败，不得继续搜索。
- 标准仪表断连/超量程/返回旧样本：abort。
- 温升超过工站阈值：归零冷却；是否恢复同一会话需工站规则明确，默认 abort 更安全。
- 上位机 MQTT 断线：设备超时自动归零和清 pending。

## 10. 曲线、CRC 与插值

### 10.1 曲线校验

```c
static bool cal_curve_validate(const cal_curve_t *c, uint16_t pwm_max)
{
    uint32_t i;
    if (c == NULL || c->point_count != 21U || c->logical_pwm[0] != 0U)
        return false;
    for (i = 0; i < 21U; ++i) {
        if (c->logical_pwm[i] > pwm_max) return false;
        if (i > 0U && c->logical_pwm[i] <= c->logical_pwm[i-1U]) return false;
    }
    return true;
}
```

若实机证明相邻目标共享同一 PWM 是不可避免的，才将严格递增改为不下降，并同步修改验收标准。不能按 CCR 的物理增减判断单调性，统一定义“逻辑 PWM 越大，期望输出电流越大”。

### 10.2 `profileCrc`

profile 必须采用固定序列化顺序、固定宽度和固定字节序，至少绑定：产品系列/型号（SID/MID）、驱动/硬件版本、`SET_OUTCUR`、`HWMAX_OUTCUR`、`OUTPUT_CUR_SENSOR`、`OP_PWM_OFFSET`、PWM 周期/频率、逻辑最大值以及任何改变电流环关系的参数。字段实际来源需在工程中定位确认。

建议 CRC32（例如 CRC-32/ISO-HDLC）统一用于 profile、curve 和记录；若为了复用当前 `crc16_modbus` 采用 CRC16，必须在协议写清算法、多项式、初值、反射、字节序和测试向量。对话示例中的 38152/22741 仅为示例，不是固定结果。

### 10.3 `curveCrc`

对 `curveVersion + pointCount + 21个uint16逻辑PWM` 的规范字节流计算，不包含 JSON 文本、不包含结构体填充、不包含 `curveCrc` 自身。上位机和 MCU 用至少三个公共测试向量联调。

### 10.4 正常插值

```c
uint16_t cal_map_percent(const cal_curve_t *c, uint8_t percent)
{
    uint8_t lo, rem;
    uint32_t a, b;
    if (percent == 0U) return 0U;
    if (percent >= 100U) return c->logical_pwm[20];
    lo = percent / 5U;
    rem = percent % 5U;
    a = c->logical_pwm[lo];
    b = c->logical_pwm[lo + 1U];
    return (uint16_t)(a + ((b - a) * rem + 2U) / 5U);
}
```

中间乘法用 32 位。曲线无效或 profile 不匹配时回退当前旧线性公式。0% 始终走硬关断。

## 11. Flash 存储策略

使用独立 A/B 槽，不逐点写 Flash。地址必须在检查 `.map`、Bootloader、OTA 和现有数据页后写入 `flash_address_assignment.h` 或独立配置文件；本文件不指定虚构地址。

提交顺序：

1. 找到当前有效槽和最高 sequence。
2. 选择非活动槽并擦除。
3. 写入 `WRITING` 记录（需适配 Flash 位翻转规则）。
4. 写完整 payload 与 record CRC。
5. 回读并验证 magic/version/size/profile/curve/record CRC。
6. 原子形成 `VALID` 标志；具体办法需结合页擦写特性确认。
7. RAM active 切换到新记录；旧槽保留作为掉电回滚。

启动时分别验证 A/B，选择 sequence 更新者。两槽都坏时记录无效，使用旧线性公式启动，不能砖化。sequence 比较需考虑回绕，或规定生产生命周期内不回绕。

注意：当前 `sys_data_store()` 是主/备均写，不等于原子 A/B 切换；不能直接把 `factory_user_buff` 改完后调用一次就宣称具备掉电安全。建议新增独立 `current_cal_storage.c/.h`，复用底层 `hw_flash` 读写，但自行做记录验证和切换。

## 12. 旧 PWM 补偿与兼容

当前 `net_dim.c` 的 `MID==4`、特定亮度减 3，以及 `OP_PWM_OFFSET` 属于两类不同补偿：

- `OP_PWM_OFFSET` 是硬件逻辑 PWM→CCR 的底层偏置，profile 必须绑定；曲线值不含该偏置。
- `dim_level-3` 是业务亮度补丁。有效新曲线下必须禁止，否则会二次校准；无有效曲线且旧型号需要时暂时保留。

推荐：

```c
if (calibration_curve_is_usable()) {
    logical_pwm = calibration_map_percent(percent);
} else {
    legacy_percent = legacy_brightness_compensate(percent);
    logical_pwm = legacy_percent_to_pwm(legacy_percent);
}
```

同时搜索全工程的 `pwm_output`、`sys_pwm_output`、`hw_tim1_pwm2_set_PWM_OUT`、`__HAL_TIM_SET_COMPARE`、`oco_on/off` 和 `sys_pwm_reload` 调用者，建立白名单。任何能绕过统一仲裁器的业务入口都要迁移或明确只允许保护层调用。

## 13. 模块拆分与工程修改要求

### 13.1 新增模块

建议新增（最终目录遵循工程现有风格）：

- `current_calibration.c/.h`：状态机、会话、超时、锁、direct PWM、pending/active/preview。
- `current_cal_curve.c/.h`：21 点校验、CRC 输入序列化、插值、profile 匹配。
- `current_cal_storage.c/.h`：独立 A/B Flash、加载、提交、回读。
- `zk_calibration_property.c/.h`：cJSON 严格解析、动作分发、响应构造，不持有业务状态。

### 13.2 现有文件

`mqtt_zk_protocol.c`：在现有消息分发和 ACK 体系中识别 `DT.Calibration`；复用现有发送队列/ID 回显；禁止在这里计算曲线或写 Flash。具体路由入口需在工程中定位确认。

`zk_property.c`：若它是当前 `DT` 属性路由入口，则只调用 `zk_calibration_property_handle()`；不要把状态机堆进去。当前已处理 `Factory`，校准节点必须与工厂参数写入解耦。

`sys_pwm.c`：拆出“旧百分比换算”和“统一逻辑 PWM 应用”；正常模式优先有效曲线，校准 direct 模式绕过百分比换算；保护仍在最终输出前生效。停止渐变 API 和校准锁查询 API 需按现有代码实现。

`hw_tim1_pwm2.c`：只保留范围限制、OCO、偏置、极性和 CCR；增加可读回实际逻辑 PWM/compare 的接口（名称按工程风格确定）。重点修正/确认 `pwm=0` 时仍写 `OP_PWM_OFFSET` 是否符合真实硬关断，因为 OCO 已关闭但状态上报应一致。

`sys_Vo_Io.c/.h`：提供一次性状态快照 API，避免跨变量读取不一致；暴露原始 ADC 必须通过 getter/快照，不直接在协议层 extern 私有变量。保留现有测量换算，21 点结果以标准仪表为准。

`gateway/net_dim.c`：校准锁期间不执行普通调光；有效曲线时跳过 MID 旧补偿。长期建议将补偿迁入 `sys_pwm.c` 的 legacy 分支，避免入口处分叉。

`main.c`：初始化时先加载校准 A/B 记录并校验 profile；主循环增加 `current_calibration_process()`，位置需保证网络消息后及时处理、保护持续运行、`sys_pwm_process()` 不覆盖 direct 模式。建议顺序：网络/JSON解析 → 校准 process → ADC/保护 → 统一输出 process；实际调度依赖当前函数行为，需审查后调整。

`flash_address_assignment.h`、Keil 工程文件：分配独立页面并把新增 `.c` 加入正确编译组。检查 map 无重叠，记录 Flash/RAM 增量。

### 13.3 主循环伪代码

```c
for (;;) {
    watchdog_loop_begin();
    network_process();
    json_process();
    adc_and_measurement_process();
    protection_process();
    current_calibration_process();
    normal_dimming_process();      /* 内部受 calibration lock 仲裁 */
    unified_pwm_process();
    watchdog_loop_end();
}
```

不要照抄伪代码替换现有循环；应保持现有 4G、OTA、时钟、报警和计划任务，并只插入必要接口。

## 14. 超时和安全保护

使用无符号 tick 差：`(uint32_t)(now - last) >= timeout`，天然处理回绕。任何有效会话命令可刷新通信超时；无效 session/旧 seq 不刷新。`setPwm` 后还可设单次高功率最大驻留时间，即使上位机持续 `readStatus` 也不能无限维持 100% 测试；建议 30 s 后要求重新 setPwm 或归零，数值需安全评审。

必须保持有效：看门狗、过温、过流、短路、输入过欠压、输出异常、ADC 异常、PWM 最大限制、OTA 互斥。出现严重保护：立即 OCO off、状态记故障、清预览、保留旧 Flash 曲线并终止会话。校准不得修改保护阈值。

`enter` 与 OTA、工厂参数写入、Flash 提交互斥。校准中收到 OTA 请求应拒绝 OTA 或先安全 abort 后按现有产品策略处理；策略需在工程中定位确认并冻结。

## 15. 实机闭环步骤

1. 上位机连接 MQTT、电子负载/功率计，读取设备身份、Factory 和 `readInfo`。
2. 计算并比对 profileCrc，确认型号、额定电流、固件版本、仪表量程。
3. `enter`，确认设备归零、OCO 关闭、无故障。
4. 验证 0% 泄漏电流。
5. 对 5%..100% 逐点渐进/二分搜索，每次 `setPwm` 后等待标准仪表稳定并读取 `readStatus`。
6. 保存完整原始样本，形成 21 点严格单调曲线。
7. 本地重新校验范围、单调性和误差，计算 curveCrc。
8. 三次 `writeCurveChunk`，随后 `readCurveStatus` 核对 bitmap/CRC。
9. `applyTemporary`，设备保持关断。
10. 使用临时曲线复测全部 21 点；至少抽检不足以作为首版量产验收。
11. 全点合格后 `commit`，等待回读成功 ACK。
12. `exit`，确认输出保持关闭。
13. 设备重启，再次 `readInfo`；复测 0/5/50/100% 或产线规定点，确认掉电加载。
14. 上位机保存设备 SN/ICCID、profile、曲线、CRC、固件版本、仪表 ID/校准日期、环境温度、原始样本和最终判定。

## 16. 测试用例

### 16.1 协议与状态机

- 每个动作的成功、缺字段、错类型、负数、溢出、未知字段/动作。
- 错 session、seq 相等重发、相等但参数冲突、旧 seq、乱序、seq 最大值。
- 各非法状态转移：未 enter setPwm、未齐 apply、未 preview commit、未 commit exit。
- 21 位分片重复、交叠、越界、缺片、冲突、CRC 不同。
- commit ACK 丢失后原请求重发，不增加 Flash sequence/擦写次数。

### 16.2 算法与映射

- 0、1、4、5、6、52、99、100% 插值与边界。
- 0% 硬关断；100% 不越最大 PWM。
- 曲线缺点、首点非零、相等点、递减点、越界点、CRC 错、profile 错。
- 搜索达到容差、分辨率不足、非单调负载、仪表旧数据、稳定超时。

### 16.3 安全和并发

- 校准中普通 MQTT 调光、离线计划、上电软启动、`sys_pwm_reload()` 不覆盖输出。
- 校准中 OTA、Factory 写入、过温、短路、过流、输入过欠压。
- MQTT 断线/4G 重启/上位机崩溃后在超时内关断。
- 连续 `enter/abort` 100 次无状态残留；高功率驻留超时有效。
- 看门狗持续喂养，主循环最大耗时满足约束。

### 16.4 Flash 与掉电

- 空白 Flash、仅 A 有效、仅 B 有效、两槽有效选择最新、两槽损坏回退 legacy。
- 擦除前、写 payload 中、写 CRC 中、标有效前后各阶段掉电注入。
- 50 次完整提交，sequence、CRC 和旧槽回滚正确。
- profile 参数改变后旧曲线不再使用；恢复原 profile 是否重新启用需产品策略冻结，建议仍需重新校准或以 generation 防止误复用。

### 16.5 回归

- 未校准设备原有开关、调光、渐变、计划、报警、OTA、上报不退化。
- MID 4 旧设备无曲线时补偿保持；有曲线时不二次补偿。
- `Vo_value/Io_value/Po_value` 上报单位保持兼容。

## 17. 验收标准

1. 协议文档、字段表、状态转移、CRC 测试向量经上位机/固件双方签字冻结。
2. 21 点全部达到产品规定容差；0% 满足关断泄漏规格；曲线严格单调且不越界。
3. 临时预览不写 Flash，commit 每次会话最多一次持久化，重发幂等。
4. 任意通信中断或严重故障能在规定时间内关断，保护与看门狗不被校准绕过。
5. 掉电注入不丢失最后一个已确认有效版本；损坏记录能回退 legacy。
6. 重启后 profile/curve CRC 一致，查表生效；参数不匹配时自动失效。
7. 无曲线旧设备功能回归通过；有效曲线不叠加 `dim_level-3`。
8. Keil 全量编译零 error；新增 warning 为零；map 无 Flash 重叠，RAM/Flash 增量有记录。
9. 上位机生产记录可追溯到设备、固件、profile、仪表和全部原始样本。

## 18. Codex 执行顺序

### 阶段 A：只读审计与协议冻结

1. 建立调用图和所有 PWM/OCO/Flash/保护入口清单。
2. 定位 MQTT 路由、ACK、Topic、包长和 JSON 内存约束。
3. 输出 Flash 地图，确认独立 A/B 页。
4. 确认 tick、OTA、保护、温度、ADC snapshot API。
5. 把所有“需在工程中定位确认”项形成决策表；未确认项不得硬编码。
6. 与上位机冻结 JSON、状态机、CRC 算法和测试向量。

### 阶段 B：纯逻辑和存储

1. 实现 curve/profile 序列化、CRC、校验、插值及主机可运行单元测试。
2. 实现 A/B 加载/提交和掉电模拟测试。
3. 实现会话状态机、幂等缓存和超时，不接真实 PWM。

### 阶段 C：输出仲裁

1. 重构 `sys_pwm.c` 为 legacy/curve/direct 三条输入、一个安全硬件出口。
2. 迁移或隔离 `net_dim.c` 旧补偿。
3. 审计所有旁路调用，确保校准锁和保护优先级。
4. 用示波器确认逻辑 PWM、CCR、偏置、极性和 0% OCO。

### 阶段 D：协议接入

1. 增加严格 cJSON 解析和响应构造。
2. 接入现有消息分发、ID 回显、发送队列。
3. 做协议 fuzz/边界/重发测试。

### 阶段 E：台架闭环与回归

1. 先限流、小功率验证 setPwm/关断/超时/保护。
2. 跑 21 点算法、预览、commit、重启验证。
3. 做掉电注入、断网、保护和旧设备回归。
4. 整理构建日志、map、测试报告和生产工站说明。

每阶段单独提交，禁止把协议、PWM 重构和 Flash 大改混成一个不可审查提交。

## 19. 禁止事项

- 禁止引入 DTU、从机地址或数字电源 UART 功能码。
- 禁止 MQTT/cJSON 层直接写 CCR、OCO 或 Flash。
- 禁止上位机直接发送 CCR；只允许规范化逻辑 PWM。
- 禁止逐点写 Flash、分片即写 Flash、commit 重试重复擦写。
- 禁止校准模式绕过保护、关闭看门狗或用长阻塞延时等待稳定。
- 禁止用 MCU `Io_value` 冒充标准仪表电流。
- 禁止有效曲线叠加 MID 4 的 `dim_level-3` 旧补偿。
- 禁止未经 map 核对占用 Flash 地址或扩展 `factory_user_buff[128]`。
- 禁止无 CRC/profile/版本的裸 21 点数组持久化。
- 禁止 abort/exit/超时后自动恢复校准前高功率输出。
- 禁止返回伪造的 ADC、温度、保护或 compare 数据；无法提供就修改能力声明。
- 禁止只测试 happy path 后量产。

## 20. 交付清单

1. 冻结版协议文档、JSON Schema/字段表、错误码、状态图、CRC 测试向量。
2. PWM 调用图、Flash 地址图、保护/并发审计表和确认项决策记录。
3. 新增校准状态机、曲线、存储、协议模块源码及头文件。
4. 对 `mqtt_zk_protocol.c`、`zk_property.c`、`sys_pwm.c`、`hw_tim1_pwm2.c`、`sys_Vo_Io.c`、`net_dim.c`、`main.c` 和 Keil 工程的受控修改。
5. 单元测试/主机测试、协议测试、台架 21 点记录、掉电与安全测试、回归报告。
6. Keil 构建产物、map 差异、RAM/Flash 增量、warning 清单。
7. 上位机算法说明、工站配置模板、生产记录格式和操作 SOP。
8. 实机验收记录：设备身份、固件版本、profileCrc、curveCrc、21 点目标/实测/PWM/误差、仪表信息和签字结论。

---

本文件是 Codex 的实施约束，不等同于允许直接批量修改代码。正式编码前必须先完成第 2.2 节确认和协议冻结；实现过程中如源码事实与本文建议冲突，以实测和代码审计结果为依据更新设计，并留下决策记录。
