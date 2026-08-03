#ifndef ZK_RUNTIME_STATS_H_
#define ZK_RUNTIME_STATS_H_

#include "type.h"
#include "cJSON.h"

/* 运行时统计：初始化（从Flash加载历史数据） */
void zk_runtime_stats_init(void);

/* 运行时统计：非阻塞周期处理（秒级累加），需在main loop中周期性调用 */
void zk_runtime_counter_process(void);

/* Clear RunTm and LightTm counters, then persist zero. */
boolean_en zk_runtime_stats_clear(void);

/* 以下访问函数供协议层构建JSON上报时使用 */

/** 获取本次上电累计运行秒数 */
uint32 zk_runtime_get_boot_run_seconds(void);

/** 获取本次上电累计亮灯秒数 */
uint32 zk_runtime_get_boot_light_seconds(void);

/** 获取历史总运行秒数（含本次） */
uint32 zk_runtime_get_total_run_seconds(void);

/** 获取历史总亮灯秒数（含本次） */
uint32 zk_runtime_get_total_light_seconds(void);

#endif
