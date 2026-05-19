#ifndef SYS_POW_DROP_CHECK_H
#define SYS_POW_DROP_CHECK_H
#include "common.h"


extern u8 power_down_flag;
extern void sys_pow_drop_check_timer(void);
extern void sys_pow_drop_check_process(void);
extern void sys_pow_drop_check_inint(void);



#endif











