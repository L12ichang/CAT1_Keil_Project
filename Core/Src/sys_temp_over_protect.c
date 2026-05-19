/*************************************************************
程序功能：CAT.1智慧电源过温保护
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_temp_over_protect.h"
#include "sys_data.h"
#include "ntc.h"
#include "sys_pwm.h"
#include "offline_Time_controlled_dimming.h"
#include "factory_user_data.h"

u8 driver_temperarure_warn=0;
static u16 _timer = 1000;//100以内启动异常
static u16 _timer2 = 1000;//400以内启动异常低温（开机从温度从-39开始上升）
static u8 _timer_sec = 0;
static u16 _timer_for_down_power;
sys_temp_over_protect_state_en sys_temp_over_protect_state = SYS_TEMP_OVER_PROTECT_STATE_IDLE;

static  u16 _timer_for_lowtemp = 18000; //开机3分钟内执行低温保护
static u8 low_temp_protect;
#define THE_TIME_10SEC     10 //10秒
#define THE_TIME_10MIN     600 
#define THE_TIME_20MIN     1200 
#define LOW_TEMP_PROTECT    -35

u16 time_test=100;
void sys_temp_over_protect_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
    }
        if(_timer2 > 0)
    {
        --_timer2;
    }
    ++_timer_sec;
    if(_timer_sec == 100)
    {
        _timer_sec = 0;
        if(_timer_for_down_power > 0)
        {
            --_timer_for_down_power;
        }
    }
    
        if(_timer_for_lowtemp > 0)
    {
        --_timer_for_lowtemp;
    }
   if( time_test)
   {
   -- time_test;
   }
    
    
}


/************************************
功能描述：电源输入是否处于过温状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/
boolean_en temp_detect_is_over(u16* out, u16 in)
{
    if( driver_temperarure_warn&&INNRE_TEMP_PRO_EN)       
    {
       
        *out = ((u32)in*sys_data.lamp_power)/100; 
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}
u8 net_dim_to_protect;

void sys_temp_over_protect_process(void)
{
    u8 temp = 0;


   if(INNRE_TEMP_PRO_EN)
    {
        if(Ntctemp.Ntctemp>0)
        {
            temp = (Ntctemp.Ntctemp+5)/10;
            if(time_test==0)
            {
//                 printf ("Ntctemp =%d\r\n",temp );
//                 printf ("INNRE_TEMP_PRO =%d\r\n",INNRE_TEMP_PRO );
//                 printf ("MID4 =%d\r\n",MID );
//                 printf ("sys_data.lamp_power =%d\r\n",sys_data.lamp_power );
             
                time_test=100;
            }
        }
        
        if(_timer == 0)
        {
            switch (sys_temp_over_protect_state)
            {
                case SYS_TEMP_OVER_PROTECT_STATE_IDLE:
                {
                    if(temp >=INNRE_TEMP_PRO)
                    {
                        if(_timer_for_down_power == 0)
                        {
                            sys_temp_over_protect_state = SYS_TEMP_OVER_PROTECT_STATE_OVER;
                           // _timer_for_down_power = THE_TIME_10MIN;//调试临时关闭
                               _timer_for_down_power = THE_TIME_10SEC;
                               driver_temperarure_warn=1;//过温标志
                            _timer_sec = 0;
                           //sys_data.lamp_power =  70;

                          //  sys_data.lamp_power =100-(u8)(temp-sys_data.temp_protect.temp_protect+1)*10;//每升高一度降10%
                           //   sys_data.lamp_power =100-(u8)(temp-INNRE_TEMP_PRO+1)*10;//每升高一度降10%
                            printf ("sys_data.lamp_power =%d\r\n",sys_data.lamp_power );
                            if(sys_data.lamp_power > 0)
                            {                
                                power_status = BOOL_TRUE;
                            }
                            else
                            {                
                                power_status = BOOL_FALSE;
                            }
                        
                           sys_pwm_reload();     //   sys_pwm_output_for_temp_protect(net_dim_to_protect*sys_data.lamp_power/100 ); 
                            
                        }
                    }
                    else
                    {
                        _timer_for_down_power = THE_TIME_10SEC;
                        _timer_sec = 0;
                    }
                }
                break;
                case SYS_TEMP_OVER_PROTECT_STATE_OVER:
                {
                    if(temp >=INNRE_TEMP_PRO)
                    {
                        if(_timer_for_down_power == 0)
                        {
                                // sys_temp_over_protect_state = SYS_TEMP_OVER_PROTECT_STATE_OVER_STEP1;//不改变转态
                             
                                // _timer_for_down_power = THE_TIME_20MIN;//调试临时关闭
                               _timer_for_down_power = THE_TIME_10SEC;
                                _timer_sec = 0;
                                //sys_data.lamp_power =  50;
                               if(temp -INNRE_TEMP_PRO>10)//温度继续增加到10度则直接电流降到0
                                {
                                    printf("-------sys_data.temp_protect.temp_protect=%d\r\n",sys_data.temp_protect.temp_protect);
                                    driver_temperarure_warn=1;//过温标志
                                    sys_data.lamp_power =0;
                                }                           
                                else if(temp -INNRE_TEMP_PRO>=5) //超过50%电流降到50%为止
                                {
                                 driver_temperarure_warn=1;//过温标志
                                 sys_data.lamp_power =50;
                                }

                                else if(temp -INNRE_TEMP_PRO>=0&&temp -INNRE_TEMP_PRO<5)  //每升高一度降10%
                                {
                                 // sys_data.lamp_power =100-(u8)(temp-sys_data.temp_protect.temp_protect+1)*10;
                                    sys_data.lamp_power =100-(u8)(temp-INNRE_TEMP_PRO+1)*10;
                                }
                                
                                if(sys_data.lamp_power > 0)//温度保护时
                                {                
                                    power_status = BOOL_TRUE;
                                }
                                else
                                {                
                                    power_status = BOOL_FALSE;
                                }
                               
                            
                                sys_pwm_reload();//  sys_pwm_output_for_temp_protect(net_dim_to_protect*sys_data.lamp_power/100 );    
                          }
                    }
                    else
                    {
                      
                        
                     // _timer_for_down_power = THE_TIME_10MIN;//调试临时关闭
                        _timer_for_down_power = THE_TIME_10SEC;
                        _timer_sec = 0;
                        sys_data.lamp_power =100;//要恢复到当前实际调光值
          
                        sys_pwm_reload();// sys_pwm_output_for_temp_protect(net_dim_to_protect*sys_data.lamp_power/100 );    
                        sys_temp_over_protect_state =SYS_TEMP_OVER_PROTECT_STATE_IDLE;//只有保护和静默状态，没有区间状态
                        driver_temperarure_warn=0;//过温报警自解除解除
                    }
                }
                break;

            }
        }
    }
}


/************************************
功能描述：电源输入是否处于低温状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低温返回 BOOL_FALSE
*************************************/
boolean_en low_temp_detect_is_low(u16* out, u16 in)
{
    if( low_temp_protect)       
    {
        *out = ((u16)in*50)/100; //半载
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}

void sys_temp_low_protect_process(void)
{
      s16 temp;
        //低温保护
    if(0)//低温保护与软启动冲突，另外产品部新规划取消低温保护，所以此模块不开放不维护
    {
        if(_timer2==0)//开机延时时间到使能
        {
             temp = (Ntctemp.Ntctemp+5)/10;
            
            if(_timer_for_lowtemp > 0)
            {
                if(low_temp_protect)
                {
                    if(temp > LOW_TEMP_PROTECT+5)
                    {
                        printf("解除低温保护\n");
                        low_temp_protect = 0;
                        _timer_for_lowtemp = 0;
                        sys_pwm_reload();
                    }
                }
                else
                {
                    if(temp <= LOW_TEMP_PROTECT)
                    {
                        printf("低温保护\n");
                        low_temp_protect = 1;
                        sys_pwm_reload();
                    }
                }
            }
            else
            {
                if(low_temp_protect)
                {
                    low_temp_protect = 0;
                    printf("2低温保护\n");
                    sys_pwm_reload();
                }
            }
        }
        else
        {
            low_temp_protect = 0;
         //   flag_ready = 1;
        }


    }
}

void sys_temp_over_protect_init(void)
{
    
}

