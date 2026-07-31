# CAT1_Keil_Project 状态机重构设计方案

> 文档阶段：第一阶段——改造思维导图  
> 项目类型：CAT.1 一体化电源  
> 硬件边界：板载 MCU + 板载 CAT.1/4G 模块 + 板载 BL0942 + 本机 PWM/保护电路  
> 不包含：DTU、UART2 外部数字电源链路、半双工单线控制协议

---

## 1. 重构总目标

本次重构不是重新开发全部业务功能，而是在保持现有产品能力、Boot/APP 分区、通信协议和 Flash 数据兼容的前提下，重新整理状态机边界、调度入口和资源所有权。

```mermaid
mindmap
  root((CAT1一体化电源状态机重构))
    重构原则
      不改变项目硬件边界
      不引入DTU架构
      保持裸机超级循环
      保持Boot与APP校验机制
      保持中科MQTT协议兼容
      保持Flash数据兼容
      先收口再优化
    核心目标
      状态机职责唯一
      主循环入口清晰
      共享资源统一仲裁
      控制优先级明确
      异常恢复路径统一
      阻塞时间可监控
      模块可独立测试
    保留能力
      CAT1联网
      MQTT登录与心跳
      属性上报与控制
      BL0942计量
      PWM调光
      电流校准
      计量校准
      OTA升级
      RTC工作计划
      温度电压过流保护
      掉电保存
      看门狗诊断
```

---

## 2. 目标软件分层思维导图

```mermaid
mindmap
  root((目标软件架构))
    应用调度层
      app_scheduler
        固定任务顺序
        周期预算
        任务耗时统计
        健康状态汇总
      app_event_queue
        网络事件
        控制事件
        OTA事件
        校准事件
        保护事件
    通信服务层
      modem_service
        UART1接收队列
        AT事务状态机
        模块开关机
        网络注册
        MQTT传输连接
        断线恢复
      mqtt_session_service
        业务登录
        心跳
        周期上报
        告警上报
        请求响应
        PUBACK跟踪
      protocol_service
        JSON解析
        设备身份校验
        命令分发
        参数校验
        响应封装
      ota_service
        OTA命令确认
        Modem独占
        HTTP或Raw TCP下载
        Flash分片写入
        镜像校验
        Boot升级标志
    电源控制层
      power_control_service
        网络调光请求
        计划调光请求
        离线调光请求
        校准调光请求
        最终输出仲裁
      pwm_driver
        逻辑百分比
        校准曲线映射
        占空比输出
        渐变控制
      safety_service
        Flash故障锁存
        强制关断
        过流保护
        高温保护
        低温限制
        输入过压欠压
    计量服务层
      bl0942_driver
        帧接收
        校验
        原始数据解析
      meter_service
        工程量换算
        统一运行快照
        数据有效期
        电能累计
        检查点持久化
      adc_service
        输出采样
        温度采样
        电源状态采样
    业务功能层
      calibration_service
        21点PWM曲线
        计量系数
        Session与Seq
        临时应用
        正式提交
      plan_service
        RTC时间
        固定计划
        日出日落
        优先级匹配
      alarm_service
        故障判定
        告警去抖
        告警队列
        恢复事件
    基础设施层
      storage_service
        参数主备区
        CRC校验
        原子提交
        Flash错误管理
      time_service
        SysTick
        RTC
        软件定时器
      watchdog_service
        主循环耗时
        Tick延迟
        UART丢包
        MQTT超时
        BL0942异常
```

---

## 3. 状态机归属重构思维导图

当前问题不是状态机数量本身，而是同一个业务状态被多个模块重复表达。重构后每类状态只能有一个权威来源。

```mermaid
mindmap
  root((状态机唯一归属))
    Modem状态
      modem_service唯一维护
      POWER_OFF
      BOOTING
      AT_READY
      SIM_READY
      NETWORK_REGISTERING
      NETWORK_REGISTERED
      MQTT_CONNECTING
      MQTT_CONNECTED
      RECOVERING
      OTA_EXCLUSIVE
    AT事务状态
      at_transaction唯一维护
      IDLE
      SEND
      WAIT_RESPONSE
      RETRY
      COMPLETE
      FAILED
    MQTT业务状态
      mqtt_session_service唯一维护
      OFFLINE
      LOGIN_PENDING
      LOGIN_WAIT_ACK
      ONLINE
      SESSION_RECOVERING
    MQTT发布状态
      mqtt_publisher唯一维护
      IDLE
      SEND_HEADER
      WAIT_PROMPT
      SEND_PAYLOAD
      WAIT_ACK
      SUCCESS
      FAILED
    OTA状态
      ota_service唯一维护
      IDLE
      PRECHECK
      ACK_PENDING
      MODEM_LOCKING
      HTTP_CONNECTING
      HEADER_PARSING
      BODY_RECEIVING
      FLASH_WRITING
      IMAGE_VERIFYING
      READY_TO_REBOOT
      FAILED
    校准状态
      calibration_service唯一维护
      IDLE
      SESSION_READY
      DIRECT_TEST
      CURVE_RECEIVING
      CURVE_PENDING
      TEMP_APPLIED
      COMMITTING
      COMMITTED
      METER_CALIBRATING
      ABORTING
    电源输出状态
      power_control_service唯一维护
      OUTPUT_OFF
      OUTPUT_NORMAL
      OUTPUT_LIMITED
      OUTPUT_CALIBRATION
      OUTPUT_FORCE_OFF
      OUTPUT_FAULT_LATCHED
    保护状态
      safety_service唯一维护
      NORMAL
      WARNING
      DERATING
      SHUTDOWN
      LATCHED_FAULT
    计划状态
      plan_service唯一维护
      DISABLED
      WAIT_TIME_VALID
      ACTIVE
      PAUSED_BY_CALIBRATION
      ACTION_APPLIED
```

---

## 4. 主循环重构思维导图

重构后 `main.c` 只负责硬件初始化和调用统一调度器，不再直接了解每个业务状态机内部细节。

```mermaid
flowchart TD
    A[main初始化] --> B[storage_service_init]
    B --> C[safety_service_init]
    C --> D[meter_service_init]
    D --> E[power_control_init]
    E --> F[modem_service_init]
    F --> G[mqtt_session_init]
    G --> H[ota_service_init]
    H --> I[calibration_service_init]
    I --> J[plan_service_init]
    J --> K[app_scheduler_init]
    K --> L{while 1}

    L --> M[watchdog_loop_begin]
    M --> N[app_scheduler_run_fast_tasks]
    N --> O[app_scheduler_run_periodic_tasks]
    O --> P[app_event_dispatch]
    P --> Q[app_health_collect]
    Q --> R[watchdog_loop_end]
    R --> L
```

统一调度器内部：

```mermaid
mindmap
  root((app_scheduler))
    高频任务
      UART1接收消费
      AT事务推进
      Modem状态推进
      MQTT发布推进
      BL0942帧接收
      安全保护采样
    10ms任务
      软件定时器
      PWM渐变
      ADC处理
      温度处理
    100ms任务
      Modem健康检查
      输出状态仲裁
      告警去抖
      校准状态推进
    1s任务
      MQTT业务会话
      RTC处理
      工作计划匹配
      运行统计
    低频任务
      周期数据上报
      电能检查点保存
      Flash延迟提交
      诊断信息汇总
    调度约束
      单任务执行预算
      禁止无限循环等待
      禁止业务层HAL_Delay
      Flash操作分段
      超预算计数
```

---

## 5. 通信状态机改造思维导图

```mermaid
flowchart TD
    A[UART1接收中断] --> B[环形缓冲区]
    B --> C[modem_rx_parser]
    C --> D{数据类型}
    D -->|AT响应| E[at_transaction]
    D -->|MQTT URC| F[mqtt_transport]
    D -->|网络URC| G[modem_service]
    D -->|OTA数据| H[ota_transport]
    D -->|未知行| I[diagnostic_log]

    E --> J[统一事件队列]
    F --> J
    G --> J
    H --> J
    J --> K[mqtt_session/protocol/ota]
```

目标：

```mermaid
mindmap
  root((通信改造目标))
    UART1所有权
      接收中断只入队
      解析器统一消费
      发送由仲裁器控制
      不允许多个模块直接抢占
    AT事务
      单事务模型
      明确期待响应
      明确超时
      明确重试上限
      明确终止错误
    MQTT连接
      传输连接独立
      业务登录独立
      在线状态唯一
    发布队列
      高优先级告警
      OTA结果
      控制响应
      心跳
      周期上报
      队列满策略
    恢复策略
      MQTT快速恢复
      网络重新注册
      CFUN重启
      硬件复位
      恢复次数统计
```

---

## 6. 电源输出与保护仲裁思维导图

所有功能只提交“期望输出”，最终 PWM 只能由 `power_control_service` 一个模块决定。

```mermaid
flowchart TD
    A[网络控制请求] --> G[power_control_service]
    B[工作计划请求] --> G
    C[离线策略请求] --> G
    D[校准请求] --> G
    E[临时恢复请求] --> G
    F[本地默认状态] --> G

    H[Flash故障] --> I[safety_service]
    J[过流] --> I
    K[高低温] --> I
    L[输入过压欠压] --> I
    M[掉电] --> I

    I --> G
    G --> N[输出优先级仲裁]
    N --> O[亮度百分比]
    O --> P[21点校准曲线]
    P --> Q[PWM逻辑值]
    Q --> R[TIM1硬件输出]
```

优先级：

```mermaid
mindmap
  root((最终输出优先级))
    第1级 硬故障锁存
      Flash参数损坏
      持久化失败
      不可恢复硬件故障
      结果为强制关断
    第2级 紧急保护
      严重过流
      关断温度
      严重输入异常
      掉电过程
    第3级 校准独占
      校准PWM
      输出最长持续时间
      禁止计划覆盖
      禁止网络控制覆盖
    第4级 降额保护
      高温半亮
      低温限幅
      输入电压限幅
    第5级 正常业务请求
      网络控制
      工作计划
      离线策略
      临时恢复
    第6级 默认输出
      上电默认值
      无有效请求时输出
```

---

## 7. BL0942计量状态机改造思维导图

```mermaid
mindmap
  root((meter_service))
    数据采集
      BL0942周期请求
      UART帧接收
      帧超时
      校验和
      连续错误统计
    数据换算
      原始电压
      原始电流
      原始功率
      频率
      功率因数
      校准系数
    运行快照
      时间戳
      有效标志
      数据年龄
      最近错误
      协议只读快照
    电能累计
      CF计数增量
      回绕处理
      异常跳变过滤
      累计电能
    持久化
      RAM持续累计
      定期检查点
      掉电立即保存
      主备槽位
      CRC和序号
    故障联动
      参数非法
      持久化失败
      数据长期失效
      通知safety_service
```

---

## 8. OTA与校准互斥思维导图

```mermaid
flowchart TD
    A[资源管理器 resource_lock] --> B[MODEM_EXCLUSIVE]
    A --> C[FLASH_WRITE]
    A --> D[PWM_EXCLUSIVE]
    A --> E[PLAN_PAUSE]

    F[OTA] --> B
    F --> C
    G[电流校准] --> C
    G --> D
    G --> E
    H[计量校准] --> C
    H --> D
    I[参数保存] --> C
    J[掉电保存] --> C
```

```mermaid
mindmap
  root((资源互斥规则))
    OTA
      独占Modem
      独占OTA Flash区
      禁止进入校准
      暂停普通MQTT业务
      保持保护任务运行
    电流校准
      独占PWM
      暂停计划任务
      拒绝普通调光
      禁止启动OTA
      允许保护强制关断
    计量校准
      独占校准参数区
      允许BL0942持续采样
      禁止并发参数提交
    Flash提交
      串行执行
      中断保护最小化
      写后回读
      失败通知安全层
    掉电保存
      最高Flash写入优先级
      放弃非关键延迟提交
      保存必要运行数据
```

---

## 9. 事件驱动改造思维导图

模块之间不再直接跨层调用内部函数，而是通过事件和命令接口协作。

```mermaid
mindmap
  root((系统事件))
    Modem事件
      MODEM_READY
      NETWORK_REGISTERED
      MQTT_CONNECTED
      MQTT_DISCONNECTED
      MODEM_RESET
    MQTT事件
      LOGIN_ACK
      PUBACK
      MESSAGE_RECEIVED
      SESSION_TIMEOUT
    控制事件
      BRIGHTNESS_REQUEST
      PROPERTY_WRITE
      RESTORE_REQUEST
      REBOOT_REQUEST
    计量事件
      METER_SNAPSHOT_READY
      METER_TIMEOUT
      ENERGY_CHECKPOINT_DUE
    保护事件
      TEMP_DERATE
      TEMP_SHUTDOWN
      OVERCURRENT
      INPUT_VOLTAGE_FAULT
      FLASH_FAULT
    OTA事件
      OTA_REQUEST
      OTA_PROGRESS
      OTA_VERIFY_OK
      OTA_FAILED
    校准事件
      CAL_ENTER
      CAL_POINT_RECEIVED
      CAL_APPLY
      CAL_COMMIT
      CAL_ABORT
      CAL_TIMEOUT
    计划事件
      PLAN_ACTION_DUE
      PLAN_PAUSE
      PLAN_RESUME
```

接口原则：

```text
上层向下层发命令
下层向上层发事件
同层之间不直接修改对方内部状态
全局变量逐步收口到模块Context
```

---

## 10. 文件结构改造思维导图

```mermaid
mindmap
  root((建议目录结构))
    Core
      App
        app_main.c
        app_scheduler.c
        app_event.c
        app_health.c
      Services
        modem_service.c
        mqtt_session_service.c
        protocol_service.c
        ota_service.c
        power_control_service.c
        safety_service.c
        meter_service.c
        calibration_service.c
        plan_service.c
        storage_service.c
      Drivers
        drv_uart1.c
        drv_bl0942.c
        drv_pwm.c
        drv_adc.c
        drv_flash.c
        drv_rtc.c
        drv_watchdog.c
      Compat
        legacy_gateway_compat.c
        legacy_parameter_adapter.c
      Config
        app_config.h
        product_config.h
        flash_layout.h
```

第一阶段不要求一次性移动全部文件。应先在现有目录内建立新接口，再逐步迁移实现，避免 Keil 工程文件和编译路径一次性大改。

---

## 11. 分阶段改造路线思维导图

```mermaid
mindmap
  root((重构实施阶段))
    第0阶段 建立基线
      固定当前可编译版本
      保存map文件
      保存固件大小
      记录设备联网日志
      记录MQTT测试结果
      记录BL0942数据
      记录PWM和保护行为
    第1阶段 调度收口
      新建app_scheduler
      main只调用调度器
      保持原函数执行顺序
      增加任务耗时统计
      不改变业务逻辑
    第2阶段 通信收口
      UART1统一接收队列
      AT事务单一入口
      Modem状态唯一
      MQTT在线状态唯一
      隔离旧gateway状态
    第3阶段 控制收口
      建立power_control_service
      所有亮度请求走统一接口
      保护优先级集中
      删除直接改PWM变量路径
    第4阶段 计量收口
      BL0942驱动与运行快照分层
      协议只读取快照
      电能持久化独立
      故障统一上报安全层
    第5阶段 OTA与校准资源化
      resource_lock
      Flash写入仲裁
      Modem独占仲裁
      PWM独占仲裁
    第6阶段 清理历史代码
      删除多驱动扫描
      删除无效gateway状态
      删除空json_process
      删除重复在线标志
      统一命名
    第7阶段 回归与固化
      Keil全量编译
      J-Link实机测试
      MQTT闭环测试
      OTA闭环测试
      校准闭环测试
      掉电与看门狗测试
      更新正式架构文档
```

---

## 12. 重构验收思维导图

```mermaid
mindmap
  root((重构验收标准))
    编译
      Keil零错误
      无新增警告
      Boot与APP链接地址不变
      固件体积可接受
    联网
      上电自动联网
      弱网自动恢复
      MQTT重新登录
      Topic订阅完整
      无UART资源冲突
    控制
      开关调光正常
      计划控制正常
      临时恢复正常
      校准期间普通控制被拒绝
      保护可覆盖一切输出请求
    计量
      BL0942持续采样
      快照时效正确
      电压电流功率正常
      电能累计连续
      掉电保存可靠
    OTA
      普通MQTT正确暂停
      下载写入不阻塞保护
      CRC与镜像头校验正确
      Boot迁移成功
      失败可恢复联网
    稳定性
      主循环无长阻塞
      看门狗不误复位
      异常时能主动复位
      运行统计可查询
      连续运行测试通过
    代码质量
      每类状态唯一归属
      全局变量显著减少
      旧Gateway语义清除
      模块接口可单测
```

---

## 13. 第一阶段结论

本次状态机重构的核心不是增加更多状态，而是完成以下五个收口：

```text
主循环入口收口到 app_scheduler
4G状态收口到 modem_service
MQTT业务状态收口到 mqtt_session_service
所有PWM请求收口到 power_control_service
所有保护结果收口到 safety_service
```

最终目标架构：

```mermaid
flowchart LR
    CLOUD[云平台] <-->|MQTT| CAT1[板载CAT1模块]
    CAT1 <-->|UART1 AT/URC| MODEM[modem_service]
    MODEM --> MQTT[mqtt_session_service]
    MQTT --> PROTOCOL[protocol_service]
    PROTOCOL --> POWER[power_control_service]
    PROTOCOL --> OTA[ota_service]
    PROTOCOL --> CAL[calibration_service]
    PROTOCOL --> PLAN[plan_service]

    BL[板载BL0942] --> METER[meter_service]
    ADC[ADC与温度] --> SAFE[safety_service]
    METER --> SAFE
    CAL --> POWER
    PLAN --> POWER
    SAFE --> POWER
    POWER --> PWM[TIM1 PWM输出]

    SCHED[app_scheduler] --> MODEM
    SCHED --> MQTT
    SCHED --> OTA
    SCHED --> CAL
    SCHED --> PLAN
    SCHED --> METER
    SCHED --> SAFE
    SCHED --> POWER
```

后续详细设计应在这张思维导图基础上，继续展开：

1. 每个状态机的正式状态定义与转换条件；
2. 模块Context结构体；
3. 事件类型与队列结构；
4. 各模块公开API；
5. 原文件到新模块的迁移映射；
6. 分阶段Codex执行任务；
7. 每阶段Keil编译和J-Link实机测试用例。
