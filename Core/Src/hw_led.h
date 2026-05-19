
#ifndef _HW_LED_H_
#define _HW_LED_H_
#include "common.h"

typedef enum
{
    LED_WORKING=0,
    LED_NFC,  
    LED_DOWN_COMM,
    LED_UP_COMM,
    LED_3IN1,
    LED_485,
    LED_TOTAL
}led_index_en;

extern void hw_led_timer(void);
extern void hw_led_init(void);

//time 闪的时间，单位100ms. state_back闪完后恢复的状态
extern void hw_led_flash(led_index_en i, u16 time, boolean_en state_back);
extern void hw_led_all_off(void);
extern void hw_led_set_state(led_index_en i, boolean_en state);
extern void hw_led_toggle(led_index_en i);

#endif


