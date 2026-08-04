
#ifndef SYS_PWM_H
#define SYS_PWM_H

#include "common.h"


extern    u8 net_entery_flag ;
extern void pwm_output(u8 persent)  ;
extern void sys_pwm_output(u8 persent);
void sys_pwm_fade_output(u8 oldpower, u8 newpower);

void sys_pwm_timer(void);
void sys_pwm_output_for_temp_protect(u8 persent);
void sys_pwm_output_on_fade(u8 persent);
void sys_pwm_process(void);
void sys_pwm_reload(void);
void sys_pwm_force_safe_off(void);

#endif
