
#ifndef HW_TIM2_H
#define HW_TIM2_H

#include "common.h"


typedef enum 
{
    TIM2_DALI=0,
    TIM2_LED,    
}tim2_en;


extern tim2_en tim2_user;

extern void hw_tim2_init(void);

extern void hw_tim2_start(u16 t);

extern void hw_tim2_start_led(u16 t);

#endif






