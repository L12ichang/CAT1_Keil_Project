#ifndef SYS_BL0942_H
#define SYS_BL0942_H

#include "common.h"
#include "sys_bl0942_frame.h"

typedef struct
{
    u32 valid_frame_count;
    u32 ore_count;
    u32 fe_count;
    u32 ne_count;
    u32 timeout_count;
    u32 uart_error_count;
    u32 recovery_count;
    u32 recovery_fail_count;
    u32 last_valid_frame_tick;
    u32 bl_age_ms;
    u32 uart_error_code;
    u32 last_uart_error_code;
    u32 tx_start_fail_count;
    u32 rx_start_fail_count;
    u32 abort_fail_count;
    u32 hal_busy_count;
    u32 hal_timeout_count;
    u8 bl_fresh;
    u8 uart_g_state;
    u8 uart_rx_state;
    u8 recovery_state;
    u8 transfer_state;
    u8 buffer_index;
    u8 rx_length;
} sys_bl0942_diag_st;

extern void sys_bl0942_timer(void);
extern void sys_bl0942_process(void);
extern void sys_bl0942_init(void);
extern void sys_bl0942_power_on(void);
extern void sys_bl0942_power_off(void);
extern void sys_bl0942_power_down_save(void);
extern void sys_bl0942_energy_stats_clear(void);
extern u32 sys_bl0942_get_data_age_ms(u32 now_tick_ms);
extern boolean_en sys_bl0942_is_fresh(u32 now_tick_ms);
extern boolean_en sys_bl0942_get_diag(u32 now_tick_ms,
                                      sys_bl0942_diag_st *diag);
extern u32 bl0942_checksum_error_count;
extern u32 bl0942_timeout_count;
extern u32 bl0942_uart_error_count;
extern u32 bl0942_compat_frame_count;

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


