#ifndef SYS_CALIBRATION_SAFETY_H
#define SYS_CALIBRATION_SAFETY_H

#include "type.h"

/* 没有最坏映射、闭环渐升和新鲜有效反馈证据前，工厂直驱非零保持失败关闭。 */
#define SYS_CALIBRATION_FACTORY_DIRECT_PWM_ENABLED 0U

typedef enum
{
    SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL = 0,
    SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN,
    SYS_CALIBRATION_OUTPUT_SOURCE_CALIBRATION,
    SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT
} sys_calibration_output_source_en;

extern u16 sys_calibration_safety_limit_current_ma(u16 voltage_01v);
extern boolean_en sys_calibration_safety_limit_percent(
    u8 requested_percent,
    u16 voltage_01v,
    u16 requested_current_ma,
    u8 *safe_percent);
extern boolean_en sys_calibration_safety_is_absolute_overcurrent(u32 current_ma);
extern u16 sys_calibration_safety_arbitrate_pwm(
    u16 requested_pwm,
    sys_calibration_output_source_en source,
    boolean_en boot_inhibited,
    boolean_en emergency_stop,
    boolean_en feedback_fresh);

#endif
