/*************************************************************
程序功能：工厂参数
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/

#include "factory_user_data.h"
#include "sys_data.h"
#include "sys_pwm.h"
 u8  factory_user_buff[128];

u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;

void fac_128_data_default(void)
{

    if( MID==0xff||MID==0x00)
    {
        MID=4;//默认150W     //1: 60W   2: 75W   3:100W   4:150W  5: 200W  6: 240W
    }
    if( OUTPUT_CUR_SENSOR==0xffff||OUTPUT_CUR_SENSOR==0x00)
    {
      OUTPUT_CUR_SENSOR=30;//默认30毫欧
    }
    if(OP_PWM_OFFSET==0xffff||OP_PWM_OFFSET==0x00)
    {
    
      OP_PWM_OFFSET=0x00;
    
    }
      if(SET_OUTCUR==0xffff||SET_OUTCUR==0x00)
    {
    
      SET_OUTCUR=2700;
    
    }
      if(HWMAX_OUTCUR==0xffff||HWMAX_OUTCUR==0x00)
    {
    
      HWMAX_OUTCUR=4700;
    
    }
    if( INNRE_TEMP_PRO_EN==0xff)
    {
      INNRE_TEMP_PRO_EN =1; //默认使能
    }
    if( INNRE_TEMP_PRO==-1)
    {
      INNRE_TEMP_PRO =85; //默认85度
    }
     if(CX==0xff||CX==0x00 ) 
    {
        CX=0x44; //默认0.68uf  放大100倍
    }
    if(SCHEDULE_SIZE>7)
    {
       SCHEDULE_SIZE=7;//默认单日最大调光动作数
    }

}



/************************************
功能描述：加载flash里的工厂参数和用户参数到 factory_user_buff
输入参数：无
输出返回：无
*************************************/
void factory_user_load_data(void)
{      //两字节以上要大小端转换
       memcpy(factory_user_buff, sys_data.fa_Parambuf, 128);
       u16h(SET_OUTCUR_temp)  =        (*((u8*)(factory_user_buff+0x10))); 
       u16l(SET_OUTCUR_temp)  =        (*((u8*)(factory_user_buff+0x11))); 
       u16h(HWMAX_OUTCUR_temp)=        (*((u8*)(factory_user_buff+0x12)));
       u16l(HWMAX_OUTCUR_temp)=        (*((u8*)(factory_user_buff+0x13)));
       u16h(OUTPUT_CUR_SENSOR_temp)=   (*((u8*)(factory_user_buff+0x14)));
       u16l(OUTPUT_CUR_SENSOR_temp)=   (*((u8*)(factory_user_buff+0x15)));
       u16h(OP_PWM_OFFSET_temp)    =   (*((u8*)(factory_user_buff+0x16)));
       u16l(OP_PWM_OFFSET_temp)    =   (*((u8*)(factory_user_buff+0x17)));
   
       fac_128_data_default();//检查参数合法性,不合法设为默认值

 
    

     
#ifdef DEBUG_PRINTF
    printf("Protocol_version=%d\n", (u16)Protocol_version);
    printf("色温=%d\n" , (u16)Config3.TintingTemperatureValid);

    
    printf("kx+b使能= %d\n", (u16)Formula_k_H.enable);
    printf("k= %d\n", (u16)kx_b_k);
    printf("b= %d\n", (u16)kx_b_b);

    printf("寿命预警= %d\n", (u16)ExtFuncEnable2.LIFE_ALARM);
    printf("寿命预警时间= %d 0.5kh\n", (u16)LifeAlarmTime);
    printf("报警时间= %d 分\n", (u16)LifeAlarmPeriod);
    printf("报警次数= %d\n", (u16)LifeAlarmTimes);

#endif



    


}


