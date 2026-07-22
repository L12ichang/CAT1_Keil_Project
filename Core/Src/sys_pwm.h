#ifndef SYS_PWM_H
#define SYS_PWM_H

#include "common.h"
#include "current_cal_curve.h"

typedef enum
{
    SYS_PWM_SOURCE_INTERNAL = 0,
    SYS_PWM_SOURCE_NETWORK,
    SYS_PWM_SOURCE_OFFLINE
} sys_pwm_source_en;

typedef struct
{
    u8 requested_percent;
    u8 effective_percent;
    u16 requested_logical_pwm;
    u16 applied_logical_pwm;
    u16 compare_value;
    u16 protect_code;
    boolean_en output_enabled;
    boolean_en calibration_locked;
    boolean_en limited;
} sys_pwm_status_t;

extern u8 net_entery_flag;
extern volatile u8 set_percent;

void pwm_output(u8 percent);
void sys_pwm_output(u8 percent);
void sys_pwm_output_network(u8 percent);
void sys_pwm_output_offline(u8 percent);
void sys_pwm_fade_output(u8 oldpower, u8 newpower);
void sys_pwm_timer(void);
void sys_pwm_output_for_temp_protect(u8 percent);
void sys_pwm_output_on_fade(u8 percent);
void sys_pwm_process(void);
void sys_pwm_reload(void);
void sys_pwm_release_and_reload(void);

void sys_pwm_calibration_lock(void);
void sys_pwm_calibration_unlock(void);
boolean_en sys_pwm_calibration_is_locked(void);
boolean_en sys_pwm_calibration_set_direct(u16 logical_pwm);
boolean_en sys_pwm_calibration_set_percent(const current_cal_curve_t *curve, u8 percent);
boolean_en sys_pwm_calibration_safety_ready(void);
void sys_pwm_force_off(void);
void sys_pwm_get_status(sys_pwm_status_t *status);

#endif
