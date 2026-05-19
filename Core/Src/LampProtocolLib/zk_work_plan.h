#ifndef ZK_WORK_PLAN_H_
#define ZK_WORK_PLAN_H_

#include "mqtt_zk_protocol.h"

#define ZK_PLAN_MAX_COUNT       8
#define ZK_PLAN_MAX_JOBS        2
#define ZK_PLAN_MAX_ACTIONS     6

void zk_work_plan_init(void);
void zk_work_plan_process(void);

#endif
