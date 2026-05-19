
#ifndef SYS_TEMP_OVER_PROTECT_H
#define SYS_TEMP_OVER_PROTECT_H

#include "common.h"
#include "sys_data.h"
typedef enum
{
    SYS_TEMP_OVER_PROTECT_STATE_IDLE,
    SYS_TEMP_OVER_PROTECT_STATE_OVER,

}sys_temp_over_protect_state_en;

extern sys_temp_over_protect_state_en sys_temp_over_protect_state;
extern u8 driver_temperarure_warn;
void sys_temp_over_protect_timer(void);
void sys_temp_over_protect_process(void);
void sys_temp_over_protect_init(void);
void sys_temp_over_protect_recovery_to_idle(void);
boolean_en low_temp_detect_is_low(u16* out, u16 in);
boolean_en temp_detect_is_over(u16* out, u16 in);

void sys_temp_low_protect_process(void);

#endif
