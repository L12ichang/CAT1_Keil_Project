/*************************************************************
程序功能：输出电压可调节
开发环境：keil 5.37
芯片型号：STM32F103RCT6
开发人员：
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/
#include "sys_dim_voltage_out.h"
#include "hw_tim3_pwm2.h"


#define MAP_SIZE            19

/*
PWM VOLTAGE
0	15.86
60	18.17
120	19.8
180	21.26
240	22.42
300	23.21
360	23.77
420	24.22
480	24.56
540	24.89
600	25.21
660	25.46
720	25.73
1080	26.94
1440	27.82
1800	28.37
2160	28.95
2520	29.55
2880	30.16

*/


const u16 voltage_map[MAP_SIZE][2]=
{
    {0,   1586},
    {60,  1817},
    {120, 1980},
    {180, 2126},
    {240, 2242},
    {300, 2321},
    {360, 2377},
    {420, 2422},
    {480, 2456},
    {540, 2489},
    {600, 2521},
    {660, 2546},
    {720, 2573},
    {1080, 2694},
    {1440, 2782},
    {1800, 2837},
    {2126, 2895},
    {2520, 2955},
    {2880, 3016},
};


//voltage 单位10mV
u16 sys_dim_voltage_out_voltage_to_pwm(u16 voltage)
{
    u8 i;
    u16 pwm = 0;
    if(voltage <= voltage_map[0][1])
    {
        //voltage = voltage_map[0][1];
        return(voltage_map[0][0]);
    }
    else if(voltage >= voltage_map[MAP_SIZE-1][1])
    {
        //voltage = voltage_map[MAP_SIZE-1][1];
        return(voltage_map[MAP_SIZE-1][0]);
    }
    for(i=0; i<MAP_SIZE-1; i++)
    {
        if(voltage >= voltage_map[i][1] && voltage < voltage_map[i+1][1])
        {
            pwm = voltage_map[i][0]+(u32)(voltage-voltage_map[i][1])*(voltage_map[i+1][0]-voltage_map[i][0])/(voltage_map[i+1][1]-voltage_map[i][1]);
            break;
        }
    }
    return (pwm);
}

//voltage 单位10mV
void sys_dim_voltage_out_set_voltage(u16 voltage)
{
    u16 pwm = sys_dim_voltage_out_voltage_to_pwm(voltage);
    hw_tim3_pwm2_set_on(pwm);
}


