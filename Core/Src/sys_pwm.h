
#ifndef SYS_PWM_H
#define SYS_PWM_H

#include "common.h"


extern u8 net_entery_flag;
extern void pwm_output(u8 percent);
extern void sys_pwm_output(u8 percent);
void sys_pwm_normal_output(u8 percent);
void sys_pwm_fade_output(u8 oldpower, u8 newpower);

void sys_pwm_timer(void);
void sys_pwm_output_for_temp_protect(u8 percent);
void sys_pwm_output_on_fade(u8 percent);
void sys_pwm_process(void);
void sys_pwm_reload(void);
void sys_pwm_force_safe_off(void);
boolean_en sys_pwm_calibration_set_level(u16 level, u16 *actual_pwm);
boolean_en sys_pwm_calibration_set_output(u8 percent, u16 *actual_pwm);
boolean_en sys_pwm_calibration_set_direct_pwm(
    u16 level,
    u16 logical_pwm,
    u16 *actual_pwm);

#endif
