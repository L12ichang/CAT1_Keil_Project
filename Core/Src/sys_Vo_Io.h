#ifndef  _SYS_VO_IO_H
#define    _SYS_VO_IO_H
#include "common.h"

extern u8  Error_0_linght;//闪灯
extern u8  Error_1_OL;//输出过载
extern u8  Error_Out_LV;//输出低压
extern u8  Error_3_OV;//输入过压
extern u8  Error_4_LV;//输入欠压

extern u32 Vo_value;   //单位0.1V 
extern u32 Io_value;   //单位 mA
extern u32 Io_protection_value; /* OCO Raw经保守默认换算，仅供硬保护 */
extern u32 Po_value;   //单位0.1W
extern u8 dim_bak_to_low_acin;
extern void voio_timer(void);
extern u8  error_flag_byte;
extern void error_report_process(void);

extern boolean_en DC_low_voltage_detect_is_low(u16* out, u16 in);
boolean_en High_voltage_detect_is_high(u16* out, u16 in) ;

#endif
