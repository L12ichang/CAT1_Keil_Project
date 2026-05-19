/*************************************************************
程序功能：Cat.1机型漏电检测
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.5.4
*************************************************************/
#include "danger_current_check.h"
#include "adc.h"
#define TIMEOUT  1000


u8 danger_current_warn=0;
static u32 max_current=0;
static u32 max_current_tmp=0;
 u32 dangeo_out=0;
static u16 counter=0;
static u8 flag_ready=0;
static u16 Periods_of_time=TIMEOUT;


extern u8 online;

void danger_current_timer(void)
{
       if(online) 
       {
           if( Periods_of_time==0)//时间到开始检测
           {   
                  if( ++counter<20)  //间隔读取20次耗时20mS
                  {  
                      if(ADC_Value3>max_current&&ADC_Value3<3300)//去除过冲值3300。后续还需要降低此值，干扰可能进来
                        {
                             max_current_tmp  = adc_buf[2]/20*0.717;//adc_buf[2];//  运放放大了20倍
                            log_u32(1, max_current_tmp);
                            if(max_current_tmp>max_current)
                            {
                                max_current=max_current_tmp;
                            
                            }
                        }
                  }
                  else
                  { 
                        counter=0;
                        dangeo_out   = max_current;
                        printf("danger==%d\r\n",dangeo_out);
                       // printf("danger_current=%dmA \r\n",(u16)((float)(dangeo_out)/4095*116));    // adc_buf[2]/4095* 3300/20/1.414
                        flag_ready=1;      
                  }
            }
              
             if(Periods_of_time  >0 )   
             {   
                -- Periods_of_time;
             }
       }
}

void danger_current_check_process(void)
{
    if (flag_ready)
    {   
         flag_ready=0; 
         max_current=0;
         Periods_of_time=TIMEOUT;//重置检测间隔时间
         if(  danger_current_warn==0 )
         {
             if(dangeo_out> 30)       //漏电电流阀值
            {
               printf("danger=============");
               danger_current_warn=1;  
            }
            else if(dangeo_out<25)
            {
               danger_current_warn=0;  
            
            }
         }
    }
}

