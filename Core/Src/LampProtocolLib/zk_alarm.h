#ifndef ZK_ALARM_H_
#define ZK_ALARM_H_

#include "type.h"

/* 告警系统：采集传感器状态、管理告警去抖与上报时机 */

/** 复位所有告警状态（在会话重置时调用） */
void zk_alarm_reset_states(void);

/** 告警处理：采集告警源，发布待上报的告警
 *  返回 BOOL_TRUE 表示已处理告警（调用方应避免本轮继续执行其他任务） */
boolean_en zk_alarm_process(void);

#endif
