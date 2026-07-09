#ifndef SYS_BL0942_H
#define SYS_BL0942_H

#include "common.h"

extern void sys_bl0942_timer(void);
extern void sys_bl0942_process(void);
extern void sys_bl0942_init(void);
extern void sys_bl0942_power_on(void);
extern void sys_bl0942_power_off(void);
extern void sys_bl0942_power_down_save(void);
extern u32 bl0942_checksum_error_count;
extern u32 bl0942_timeout_count;
extern u32 bl0942_uart_error_count;

extern u16  ac_voltage_8209;  //交流电的电压，单位 0.1V
extern u16  Z_ac_current;      //休正容性无功电流后的值 单位mA
//extern u16  ac_current;      //交流电的电流，单位 mA
extern u16  ac_freq;          //交流电的频率单位0.01HZ
extern u16  ac_powerpa;       //有功功率0.01W
extern u16  ac_powerq;        //无功功率
extern u32  energy_this_time; //当前能耗
extern u32 total_power_this_time;
extern u8  ac_pf;


#endif


