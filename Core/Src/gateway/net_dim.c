/*************************************************************
程序功能：NET_DIM网络调光
开发环境：keil 5.36 c51 v9.60
芯片型号：STM32F103RCT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2023.5.6
*************************************************************/
#include "net_dim.h"
#include "sys_Vo_Io.h"
#include "hw_tim1_pwm2.h"
#include "sys_pwm.h"
#include "factory_user_data.h"
#include "json_protocol.h"
u8 dim_ready_flag;
u32 dim_level;
void dim_ready(void)
{
      dim_ready_flag=1;
    printf("dim_ready_flag\r\n");
}

void uart_diam_process(void)
{  
    u8 pwm;

    //前端下发调光命令==》
      if(dim_ready_flag)
      {  
                     dim_ready_flag=0;
                     net_entery_flag=1;
                     pwm=dim_level;
          
          
     extern u8 net_dim_to_protect;
             net_dim_to_protect=dim_level;

               
           if( dim_level>0 )
           {   
               if(pwm<5)
               {
                 pwm=0;
               }
               else
               {
                 printf("dim_level=%d\n",dim_level);
               }
            }
            else
            {
             printf("dim_level=%d\n",dim_level);
             pwm=0;
            }

            dim_bak_to_low_acin=pwm;
            sys_pwm_output(pwm);
            
        }
 }
