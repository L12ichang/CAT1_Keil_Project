/*************************************************************
程序功能：Cat.1机型掉电检测
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.5.4
*************************************************************/
#include "sys_pow_drop_check.h"
#include "sys_bl0942.h"
#include "zk_runtime_stats.h"

#define TIMEOUT 600      //掉电存储6秒间隔
#define POW_IDLE_STATE   0
#define POW_UP_STATE     1
#define POW_OFF_STATE    2


static u8 power_on;
static u8 power_on_state=POW_IDLE_STATE;
u8 power_down_flag;
static u16 power_downing_counter=0;
static u16 state_on_hold_time;
static u16 state_off_hold_time;
extern u16 ac_voltage_8209;

void sys_pow_drop_check_timer(void)
{
   //  软件滤波
    // if(  HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)==GPIO_PIN_SET)//硬件判定掉电
      if(  ac_voltage_8209>=70)//计量判定掉电
     {
        state_off_hold_time=0;
        if(++state_on_hold_time>10)
        {  state_on_hold_time=10;
            if(power_on==0)
            {
             printf("sys_pow_drop_check=HIGH\r\n");
             power_on=1;
            }
        }
     }
     else
     {
         state_on_hold_time=0;
         if(++state_off_hold_time>10)
         {
                state_off_hold_time=10;
                if(power_on==1)
                {  
                    power_on=0;
                    printf("sys_pow_drop_check= LOW\r\n" );
                }
        }
     }
     if(power_downing_counter>0)
     {
        --power_downing_counter;
     }
}


void sys_pow_drop_check_process(void)
{
      if(power_on_state==POW_IDLE_STATE)
      {
            if(power_on==1)
            {
               power_on_state=POW_UP_STATE;
            }
      }
     else if(power_on_state==POW_UP_STATE)
     {
           if( power_on==0)
           {                               
              
               printf("sys_pow_drop_check=掉电发生\r\n" );
               if(power_downing_counter==0)//在允许的时间内
               {
                   sys_bl0942_power_down_save();
                   (void)zk_runtime_stats_powerdown_checkpoint();
                   power_downing_counter= TIMEOUT;  //放置存储限制时间
          
                   printf("sys_pow_drop_check=存储\r\n" );

               }
               power_down_flag=1;//通知外部掉电发生
               power_on_state=POW_OFF_STATE;   
           }
     }
    
    else if( power_on_state==POW_OFF_STATE )//掉电续活
    {
        if(power_on==1)
        {
          power_on_state=POW_UP_STATE;
          printf("sys_pow_drop_check=掉电恢复\r\n" ); 
        }
    }

}


void sys_pow_drop_check_inint(void)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}



