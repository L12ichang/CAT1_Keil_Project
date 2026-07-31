#ifndef __SYS_EVENT_H__
#define __SYS_EVENT_H__

#include "common.h"

typedef enum
{
    SYS_EVENT_NONE = 0,
    SYS_EVENT_MODEM_READY,
    SYS_EVENT_MODEM_NO_RESPONSE,
    SYS_EVENT_NETWORK_REGISTERED,
    SYS_EVENT_NETWORK_LOST,
    SYS_EVENT_MQTT_CONNECTED,
    SYS_EVENT_MQTT_SUBSCRIBED,
    SYS_EVENT_MQTT_DISCONNECTED,
    SYS_EVENT_MQTT_PUBACK,
    SYS_EVENT_MQTT_PUBLISH_FAILED,
    SYS_EVENT_MQTT_MESSAGE,
    SYS_EVENT_MEASUREMENT_UPDATED,
    SYS_EVENT_PROTECTION_CHANGED,
    SYS_EVENT_CONTROL_APPLIED,
    SYS_EVENT_POWER_FAIL,
    SYS_EVENT_STORAGE_FAILED,
    SYS_EVENT_OTA_FINISHED,
    SYS_EVENT_CALIBRATION_CHANGED,
    SYS_EVENT_DIAGNOSTIC,
    SYS_EVENT_TYPE_COUNT
} sys_event_type_en;

typedef struct
{
    sys_event_type_en type;
    u16 source_id;
    u16 data_id;
    u32 timestamp_ms;
    u32 value;
} sys_event_st;

typedef struct
{
    u32 posted_count;
    u32 dispatched_count;
    u32 coalesced_count;
    u32 evicted_count;
    u32 dropped_count;
    u32 critical_overflow_count;
    u16 high_watermark;
    u16 queued_count;
    boolean_en critical_overflow_latched;
} sys_event_stats_st;

typedef void (*sys_event_dispatcher_fn)(const sys_event_st *event);

/************************************
功能描述：初始化固定长度系统事件队列
输入参数：无
输出返回：无
*************************************/
extern void sys_event_init(void);

/************************************
功能描述：按事件等级、合并和溢出策略提交值拷贝事件
输入参数：event 待提交事件
输出返回：成功入队或合并返回 BOOL_TRUE，否则返回 BOOL_FALSE
*************************************/
extern boolean_en sys_event_post(const sys_event_st *event);

/************************************
功能描述：从队列取出最早事件
输入参数：event 接收事件的稳定存储地址
输出返回：成功取出返回 BOOL_TRUE，队列为空或参数无效返回 BOOL_FALSE
*************************************/
extern boolean_en sys_event_get(sys_event_st *event);

/************************************
功能描述：注册主循环事件分发函数
输入参数：dispatcher 分发函数，传入 NULL 可停止分发
输出返回：无
*************************************/
extern void sys_event_set_dispatcher(sys_event_dispatcher_fn dispatcher);

/************************************
功能描述：主循环最多分发一个系统事件
输入参数：无
输出返回：无
*************************************/
extern void sys_event_process(void);

/************************************
功能描述：复制事件队列诊断统计
输入参数：stats 接收统计的稳定存储地址
输出返回：无
*************************************/
extern void sys_event_get_stats(sys_event_stats_st *stats);

/************************************
功能描述：清除已处理的关键事件溢出锁存
输入参数：无
输出返回：无
*************************************/
extern void sys_event_clear_critical_overflow(void);

#endif
