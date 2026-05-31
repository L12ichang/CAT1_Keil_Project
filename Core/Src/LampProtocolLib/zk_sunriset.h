#ifndef ZK_SUNRISET_H_
#define ZK_SUNRISET_H_

#include "common.h"

/*
 * 日出日落计算模块
 * 基于 NOAA 天文算法，根据设备经纬度和时区计算当日日出日落时间。
 * STM32F103 浮点运算（无 FPU）可接受，仅在查询或每日更新时调用。
 * 内部按日期缓存结果，同一天重复调用直接返回缓存值。
 */

/* 获取当日日出日落时间（本地时间，单位：分钟，范围 0~1439）
 *   返回 0=成功, -1=参数无效, -2=极昼/极夜, -3=RTC未就绪 */
int zk_sunriset_get(int *sr_minute, int *ss_minute);

#endif
