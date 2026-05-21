#include "zk_alarm.h"
#include "common.h"
#include "mqtt_zk_protocol.h"
#include "sys_Vo_Io.h"
#include "sys_bl0942.h"
#include "danger_current_check.h"
#include "sys_temp_over_protect.h"
#include "sys_pow_drop_check.h"
#include "ntc.h"
#include "factory_user_data.h"

/* dangeo_out 定义于 danger_current_check.c */
extern u32 dangeo_out;

#define ZK_ALARM_POWER_DOWN_INDEX 7U

/* ========== 告警状态结构 ========== */
typedef struct
{
    uint16 alm_id;          /* 告警ID，对应协议定义的 ZK_ALARM_xxx */
    u8 pending;             /* 是否有待上报的状态变化 */
    u8 pending_status;      /* 待上报的状态值：0=恢复，1=告警 */
    u8 reported;            /* 最近已上报的状态值 */
    u8 one_shot;            /* 是否一次性告警（上报后不复位状态） */
    uint32 value;           /* 当前采样值 */
    uint32 threshold;       /* 告警阈值 */
} zk_alarm_state_t;

/* 告警状态表：每个告警类型的实时状态 */
static zk_alarm_state_t zk_alarm_states[] =
{
    {ZK_ALARM_OVER_VOLTAGE, 0, 0, 0, 0, 0, 3200},
    {ZK_ALARM_UNDER_VOLTAGE, 0, 0, 0, 0, 0, 800},
    {ZK_ALARM_OVER_CURRENT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_UNDER_CURRENT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_LIGHT_ON_FAIL, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_LEAK_CURRENT, 0, 0, 0, 0, 0, 30},
    {ZK_ALARM_DEVICE_FAULT, 0, 0, 0, 0, 0, 0},
    {ZK_ALARM_POWER_DOWN, 0, 0, 0, 1, 0, 70},
};
#define ZK_ALARM_STATE_COUNT (sizeof(zk_alarm_states) / sizeof(zk_alarm_states[0]))

/* ========== 内部辅助函数 ========== */

/* 根据额定电流计算过流百分比阈值 */
static uint32 zk_alarm_current_threshold(uint32 percent)
{
    if (SET_OUTCUR == 0)
    {
        return 0;
    }
    return ((uint32)SET_OUTCUR * percent) / 100U;
}

/* 获取NTC温度值（单位℃），无效时返回0 */
static uint32 zk_alarm_temperature_value(void)
{
    signed short temp;

    temp = Ntctemp.Ntctemp;
    if (temp <= 0)
    {
        return 0;
    }
    return (uint32)((temp + 5) / 10);
}

/* 获取过温保护阈值，关闭时返回0 */
static uint32 zk_alarm_temperature_threshold(void)
{
    if (INNRE_TEMP_PRO <= 0)
    {
        return 0;
    }
    return (uint32)INNRE_TEMP_PRO;
}

/* 更新单路告警的去抖状态机：
 *  active=1表示告警条件成立，active=0表示恢复
 *  仅在状态切换时标记 pending，避免重复上报 */
static void zk_alarm_update_level(zk_alarm_state_t *alarm, u8 active, uint32 value, uint32 threshold)
{
    if (alarm == NULL)
    {
        return;
    }

    alarm->value = value;
    alarm->threshold = threshold;

    if (active)
    {
        if (alarm->pending && alarm->pending_status == 0)
        {
            alarm->pending = 0;
        }
        if (alarm->reported == 0)
        {
            alarm->pending = 1;
            alarm->pending_status = 1;
        }
    }
    else
    {
        if (alarm->pending && alarm->pending_status != 0)
        {
            alarm->pending = 0;
        }
        if (alarm->reported != 0)
        {
            alarm->pending = 1;
            alarm->pending_status = 0;
        }
    }
}

/* 掉电告警：检测到掉电标志且尚未触发时，立即标记 pending */
static void zk_alarm_update_power_down(void)
{
    zk_alarm_state_t *alarm;

    alarm = &zk_alarm_states[ZK_ALARM_POWER_DOWN_INDEX];
    alarm->value = ac_voltage_8209;
    alarm->threshold = 70;
    if (power_down_flag != 0 && alarm->pending == 0)
    {
        alarm->pending = 1;
        alarm->pending_status = 1;
    }
}

/* 采集所有告警源的实时状态，更新告警状态机 */
static void zk_alarm_collect_sources(void)
{
    zk_alarm_update_level(&zk_alarm_states[0], Error_3_OV ? 1 : 0, ac_voltage_8209, 3200);
    zk_alarm_update_level(&zk_alarm_states[1], Error_4_LV ? 1 : 0, ac_voltage_8209, 800);
    zk_alarm_update_level(&zk_alarm_states[2], Error_1_OL ? 1 : 0, Io_value, zk_alarm_current_threshold(150));
    zk_alarm_update_level(&zk_alarm_states[3], Error_Out_LV ? 1 : 0, Io_value, zk_alarm_current_threshold(80));
    zk_alarm_update_level(&zk_alarm_states[4], Error_0_linght ? 1 : 0, Po_value, 0);
    zk_alarm_update_level(&zk_alarm_states[5], danger_current_warn ? 1 : 0, dangeo_out, 30);
    zk_alarm_update_level(&zk_alarm_states[6], driver_temperarure_warn ? 1 : 0,
                          zk_alarm_temperature_value(),
                          zk_alarm_temperature_threshold());
    zk_alarm_update_power_down();
}

/* 遍历告警表，发布第一条 pending 的告警（一次只发一条，防止多轮拥塞） */
static boolean_en zk_alarm_publish_pending(void)
{
    uint32 i;
    u8 status;

    for (i = 0; i < ZK_ALARM_STATE_COUNT; ++i)
    {
        if (zk_alarm_states[i].pending == 0)
        {
            continue;
        }

        status = zk_alarm_states[i].pending_status ? 1 : 0;
        if (zk_publish_alarm_report(zk_alarm_states[i].alm_id,
                                    status,
                                    zk_alarm_states[i].value,
                                    zk_alarm_states[i].threshold) == 0)
        {
            zk_alarm_states[i].pending = 0;
            if (zk_alarm_states[i].one_shot && status != 0)
            {
                power_down_flag = 0;
                zk_alarm_states[i].reported = 0;
            }
            else
            {
                zk_alarm_states[i].reported = status;
            }
        }
        return BOOL_TRUE;
    }

    return BOOL_FALSE;
}

/* ========== 公共API ========== */

/* 复位所有告警状态（会话重置/初始启动时调用） */
void zk_alarm_reset_states(void)
{
    uint32 i;

    for (i = 0; i < ZK_ALARM_STATE_COUNT; ++i)
    {
        zk_alarm_states[i].pending = 0;
        zk_alarm_states[i].pending_status = 0;
        zk_alarm_states[i].reported = 0;
        zk_alarm_states[i].value = 0;
    }
}

/* 告警处理主入口：采集告警源 → 发布待上报告警
 *  返回 BOOL_TRUE 表示本轮已处理告警，调用方应暂缓其他非紧急任务 */
boolean_en zk_alarm_process(void)
{
    zk_alarm_collect_sources();
    return zk_alarm_publish_pending();
}
