/*************************************************************
程序功能：固定长度系统事件队列及分级满队列策略
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.30
*************************************************************/
#include "sys_event.h"

#define SYS_EVENT_QUEUE_CAPACITY    ((u16)16U)
#define SYS_EVENT_U32_MAX           ((u32)0xFFFFFFFFUL)

typedef enum
{
    SYS_EVENT_POLICY_LOW = 0,
    SYS_EVENT_POLICY_NORMAL,
    SYS_EVENT_POLICY_COALESCIBLE,
    SYS_EVENT_POLICY_CRITICAL
} sys_event_policy_en;

static sys_event_st _queue[SYS_EVENT_QUEUE_CAPACITY];
static u16 _queue_count;
static sys_event_stats_st _stats;
static sys_event_dispatcher_fn _dispatcher;

/************************************
功能描述：进入事件队列临界区并保存原中断状态
输入参数：无
输出返回：进入前的 PRIMASK
*************************************/
static u32 sys_event_enter_critical(void)
{
    u32 primask;

    primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}


/************************************
功能描述：按进入前状态退出事件队列临界区
输入参数：primask 进入临界区前的 PRIMASK
输出返回：无
*************************************/
static void sys_event_exit_critical(u32 primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}


/************************************
功能描述：对无符号统计值执行饱和加一
输入参数：value 待更新统计地址
输出返回：无
*************************************/
static void sys_event_increment_saturated(u32 *value)
{
    if (*value < SYS_EVENT_U32_MAX)
    {
        (*value)++;
    }
}


/************************************
功能描述：返回指定事件类型的固定队列策略
输入参数：type 事件类型
输出返回：事件队列策略
*************************************/
static sys_event_policy_en sys_event_get_policy(sys_event_type_en type)
{
    switch (type)
    {
        case SYS_EVENT_POWER_FAIL:
        case SYS_EVENT_PROTECTION_CHANGED:
        case SYS_EVENT_MQTT_DISCONNECTED:
        case SYS_EVENT_STORAGE_FAILED:
        case SYS_EVENT_OTA_FINISHED:
        {
            return SYS_EVENT_POLICY_CRITICAL;
        }

        case SYS_EVENT_MODEM_READY:
        case SYS_EVENT_NETWORK_REGISTERED:
        case SYS_EVENT_NETWORK_LOST:
        case SYS_EVENT_MQTT_CONNECTED:
        case SYS_EVENT_MQTT_SUBSCRIBED:
        case SYS_EVENT_MEASUREMENT_UPDATED:
        case SYS_EVENT_CONTROL_APPLIED:
        case SYS_EVENT_CALIBRATION_CHANGED:
        {
            return SYS_EVENT_POLICY_COALESCIBLE;
        }

        case SYS_EVENT_DIAGNOSTIC:
        {
            return SYS_EVENT_POLICY_LOW;
        }

        default:
        {
            return SYS_EVENT_POLICY_NORMAL;
        }
    }
}


/************************************
功能描述：判断事件是否属于只保留最新状态的可合并类型
输入参数：type 事件类型
输出返回：可合并返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
static boolean_en sys_event_is_coalescible(sys_event_type_en type)
{
    switch (type)
    {
        case SYS_EVENT_MODEM_READY:
        case SYS_EVENT_NETWORK_REGISTERED:
        case SYS_EVENT_NETWORK_LOST:
        case SYS_EVENT_MQTT_CONNECTED:
        case SYS_EVENT_MQTT_SUBSCRIBED:
        case SYS_EVENT_MQTT_DISCONNECTED:
        case SYS_EVENT_MEASUREMENT_UPDATED:
        case SYS_EVENT_PROTECTION_CHANGED:
        case SYS_EVENT_CONTROL_APPLIED:
        case SYS_EVENT_CALIBRATION_CHANGED:
        {
            return BOOL_TRUE;
        }

        default:
        {
            return BOOL_FALSE;
        }
    }
}


/************************************
功能描述：删除队列指定位置并保持事件先后顺序
输入参数：index 待删除事件索引
输出返回：无
*************************************/
static void sys_event_remove_at(u16 index)
{
    u16 move_index;

    for (move_index = index; (move_index + 1U) < _queue_count; move_index++)
    {
        _queue[move_index] = _queue[move_index + 1U];
    }
    if (_queue_count > 0U)
    {
        _queue_count--;
    }
}


/************************************
功能描述：查找队列中最早的指定策略事件
输入参数：policy 待查找策略，index 接收事件索引
输出返回：找到返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
static boolean_en sys_event_find_policy(sys_event_policy_en policy, u16 *index)
{
    u16 queue_index;

    for (queue_index = 0U; queue_index < _queue_count; queue_index++)
    {
        if (sys_event_get_policy(_queue[queue_index].type) == policy)
        {
            *index = queue_index;
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}


/************************************
功能描述：查找队列中最早的可合并且非关键状态事件
输入参数：index 接收事件索引
输出返回：找到返回 BOOL_TRUE，否则返回 BOOL_FALSE
注意：该查找禁止将关键事件作为非关键事件的淘汰候选。
*************************************/
static boolean_en sys_event_find_noncritical_coalescible(u16 *index)
{
    u16 queue_index;

    for (queue_index = 0U; queue_index < _queue_count; queue_index++)
    {
        if ((sys_event_get_policy(_queue[queue_index].type) !=
             SYS_EVENT_POLICY_CRITICAL) &&
            (sys_event_is_coalescible(_queue[queue_index].type) == BOOL_TRUE))
        {
            *index = queue_index;
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}


/************************************
功能描述：合并队列中已有的同类型状态事件
输入参数：event 最新事件值
输出返回：完成合并返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
static boolean_en sys_event_try_coalesce(const sys_event_st *event)
{
    u16 queue_index;

    if (sys_event_is_coalescible(event->type) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }

    for (queue_index = 0U; queue_index < _queue_count; queue_index++)
    {
        if (_queue[queue_index].type == event->type)
        {
            _queue[queue_index] = *event;
            sys_event_increment_saturated(&_stats.coalesced_count);
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}


/************************************
功能描述：初始化固定长度系统事件队列
输入参数：无
输出返回：无
*************************************/
void sys_event_init(void)
{
    u32 primask;

    primask = sys_event_enter_critical();
    memset(_queue, 0, sizeof(_queue));
    memset(&_stats, 0, sizeof(_stats));
    _queue_count = 0U;
    _dispatcher = NULL;
    sys_event_exit_critical(primask);
}


/************************************
功能描述：按事件等级、合并和溢出策略提交值拷贝事件
输入参数：event 待提交事件
输出返回：成功入队或合并返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
boolean_en sys_event_post(const sys_event_st *event)
{
    sys_event_policy_en policy;
    u32 primask;
    u16 evict_index;

    if ((event == NULL) || (event->type <= SYS_EVENT_NONE) ||
        (event->type >= SYS_EVENT_TYPE_COUNT))
    {
        return BOOL_FALSE;
    }

    primask = sys_event_enter_critical();
    if (sys_event_try_coalesce(event) == BOOL_TRUE)
    {
        sys_event_exit_critical(primask);
        return BOOL_TRUE;
    }

    policy = sys_event_get_policy(event->type);
    if (_queue_count >= SYS_EVENT_QUEUE_CAPACITY)
    {
        if (policy == SYS_EVENT_POLICY_CRITICAL)
        {
            if (sys_event_find_policy(SYS_EVENT_POLICY_LOW,
                                      &evict_index) == BOOL_TRUE)
            {
                sys_event_remove_at(evict_index);
                sys_event_increment_saturated(&_stats.evicted_count);
            }
            else if (sys_event_find_noncritical_coalescible(
                         &evict_index) == BOOL_TRUE)
            {
                sys_event_remove_at(evict_index);
                sys_event_increment_saturated(&_stats.evicted_count);
            }
        }
        else if ((policy == SYS_EVENT_POLICY_COALESCIBLE) &&
                 (sys_event_find_noncritical_coalescible(
                      &evict_index) == BOOL_TRUE))
        {
            sys_event_remove_at(evict_index);
            sys_event_increment_saturated(&_stats.evicted_count);
        }
        else if ((policy == SYS_EVENT_POLICY_NORMAL) &&
                 (sys_event_find_policy(SYS_EVENT_POLICY_LOW,
                                        &evict_index) == BOOL_TRUE))
        {
            sys_event_remove_at(evict_index);
            sys_event_increment_saturated(&_stats.evicted_count);
        }

        if (_queue_count >= SYS_EVENT_QUEUE_CAPACITY)
        {
            sys_event_increment_saturated(&_stats.dropped_count);
            if (policy == SYS_EVENT_POLICY_CRITICAL)
            {
                _stats.critical_overflow_latched = BOOL_TRUE;
                sys_event_increment_saturated(&_stats.critical_overflow_count);
            }
            sys_event_exit_critical(primask);
            return BOOL_FALSE;
        }
    }

    _queue[_queue_count] = *event;
    _queue_count++;
    sys_event_increment_saturated(&_stats.posted_count);
    if (_queue_count > _stats.high_watermark)
    {
        _stats.high_watermark = _queue_count;
    }
    _stats.queued_count = _queue_count;
    sys_event_exit_critical(primask);
    return BOOL_TRUE;
}


/************************************
功能描述：从队列取出最早事件
输入参数：event 接收事件的稳定存储地址
输出返回：成功取出返回 BOOL_TRUE，队列为空或参数无效返回 BOOL_FALSE
*************************************/
boolean_en sys_event_get(sys_event_st *event)
{
    u32 primask;

    if (event == NULL)
    {
        return BOOL_FALSE;
    }

    primask = sys_event_enter_critical();
    if (_queue_count == 0U)
    {
        sys_event_exit_critical(primask);
        return BOOL_FALSE;
    }

    *event = _queue[0];
    sys_event_remove_at(0U);
    _stats.queued_count = _queue_count;
    sys_event_exit_critical(primask);
    return BOOL_TRUE;
}


/************************************
功能描述：注册主循环事件分发函数
输入参数：dispatcher 分发函数，传入 NULL 可停止分发
输出返回：无
*************************************/
void sys_event_set_dispatcher(sys_event_dispatcher_fn dispatcher)
{
    u32 primask;

    primask = sys_event_enter_critical();
    _dispatcher = dispatcher;
    sys_event_exit_critical(primask);
}


/************************************
功能描述：主循环最多分发一个系统事件
输入参数：无
输出返回：无
*************************************/
void sys_event_process(void)
{
    sys_event_dispatcher_fn dispatcher;
    sys_event_st event;
    u32 primask;

    primask = sys_event_enter_critical();
    dispatcher = _dispatcher;
    sys_event_exit_critical(primask);
    if (dispatcher == NULL)
    {
        return;
    }

    if (sys_event_get(&event) == BOOL_TRUE)
    {
        dispatcher(&event);
        sys_event_increment_saturated(&_stats.dispatched_count);
    }
}


/************************************
功能描述：复制事件队列诊断统计
输入参数：stats 接收统计的稳定存储地址
输出返回：无
*************************************/
void sys_event_get_stats(sys_event_stats_st *stats)
{
    u32 primask;

    if (stats == NULL)
    {
        return;
    }

    primask = sys_event_enter_critical();
    *stats = _stats;
    sys_event_exit_critical(primask);
}


/************************************
功能描述：清除已处理的关键事件溢出锁存
输入参数：无
输出返回：无
*************************************/
void sys_event_clear_critical_overflow(void)
{
    u32 primask;

    primask = sys_event_enter_critical();
    _stats.critical_overflow_latched = BOOL_FALSE;
    sys_event_exit_critical(primask);
}
