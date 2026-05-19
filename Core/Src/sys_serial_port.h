
#ifndef SYS_SERIAL_PORT_H
#define SYS_SERIAL_PORT_H

#include "common.h"

typedef enum
{
    FROM_PC_RX_STATE_NONE,
    FROM_PC_RX_STATE_PPP,
    FROM_PC_RX_STATE_PROGRAMMER
}from_pc_rx_state_en;

extern from_pc_rx_state_en from_pc_rx_state;

extern void sys_serial_port_data_in(u8 * dat, u16 length);

/************************************
功能描述：主循环调用
输入参数：无
输出返回：无
*************************************/
extern void sys_serial_port_process(void);


/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/
extern void sys_serial_port_timer(void);



/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
extern void sys_serial_port_init(void);


extern void sys_serial_set_whole_comm_state(whole_comm_state_en state);

#endif



