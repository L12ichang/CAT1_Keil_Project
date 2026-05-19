/*************************************************************
程序功能：收到串口数据，自动判别是IAP协议还是离线编程器协议
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/
#include "sys_serial_port.h"
#include "programmer_protocol.h"
#define PROGRAMMER_RX(dat)           programmer_rx(dat)  

from_pc_rx_state_en from_pc_rx_state = FROM_PC_RX_STATE_NONE;
whole_comm_state_en whole_comm_state = WHOLE_COMM_STATE_IDLE;

#define TIMEOUT_IAP   500   
#define TIMEOUT       100   
static u16 _timer_for_packet_rx = 0;


/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/
void sys_serial_port_timer(void)
{
    if(_timer_for_packet_rx > 0)
    {
        --_timer_for_packet_rx;
        if(_timer_for_packet_rx == 0)
        {
            from_pc_rx_state = FROM_PC_RX_STATE_NONE;
            sys_serial_set_whole_comm_state(WHOLE_COMM_STATE_IDLE);
        }
    }
}


void sys_serial_set_whole_comm_state(whole_comm_state_en state)
{
//    if(state != whole_comm_state)
//    {
//        printf("whole_comm_state=%d\n", (u16)state);
//    }
    
    whole_comm_state = state;
}
/*
void sys_serial_port_data_in(u8 * dat, u16 length)
{
    u16 i;
    if(OFFLINE_PROGRAM_IS_IDLE)
    {
        for (i=0; i<length; i++)
        {
            if(!(whole_comm_state == WHOLE_COMM_STATE_DALI_RXING))
            {
                sys_serial_set_whole_comm_state(WHOLE_COMM_STATE_UART_RXING);
                if(from_pc_rx_state == FROM_PC_RX_STATE_NONE)
                {
                    if(dat[i] == 0x7e)
                    {                
                        _timer_for_packet_rx = TIMEOUT_IAP;
                       PPP_DECODE(dat+i, 1);
                        from_pc_rx_state = FROM_PC_RX_STATE_PPP;
                    }
                    else
                    {
                        _timer_for_packet_rx = TIMEOUT;
                       PROGRAMMER_RX(dat[i]);                
                        from_pc_rx_state = FROM_PC_RX_STATE_PROGRAMMER;
                    }
                }
                else if(from_pc_rx_state == FROM_PC_RX_STATE_PPP)
                {
                    _timer_for_packet_rx = TIMEOUT_IAP;
                    PPP_DECODE(dat+i, 1);
                }
                else if(from_pc_rx_state == FROM_PC_RX_STATE_PROGRAMMER)
                {
                    _timer_for_packet_rx = TIMEOUT;
                    PROGRAMMER_RX(dat[i]);
                }
            }  
            else
            {
                //printf("whole_comm_state error = %d\n", (u16)whole_comm_state);
            }            
        }
    }
}


*/

/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
void sys_serial_port_init(void)
{
    
}


