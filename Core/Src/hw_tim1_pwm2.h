#ifndef HW_TIM1_PWM2_H
#define HW_TIM1_PWM2_H

#include "common.h"


extern void hw_tim1_pwm2_init(void);
extern void hw_tim1_pwm2_set_on(void);
extern void hw_tim1_pwm2_set_off(void);
extern void hw_tim1_pwm2_set_PWM_OUT(u16 pwm);//pwmÊä³ö
extern u16 hw_tim1_pwm2_get_logical_pwm(void);
extern u16 hw_tim1_pwm2_get_ccr(void);
extern u8 hw_tim1_pwm2_get_oco_on(void);

#endif
