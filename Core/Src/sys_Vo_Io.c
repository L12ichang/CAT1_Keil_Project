/*************************************************************
程序功能：驱动输出/电源保护及报警
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_Vo_Io.h"
#include "Portable.h"
#include "sys_data.h"
#include "adc.h"
#include "sys_pwm.h"
#include "factory_user_data.h"
#include "ntc.h"


     
     
     
#define DIM_STATE_0   0
#define DIM_STATE_1   1
#define DIM_STATE_WAIT   2
static u8  dim_state=DIM_STATE_1;
static u16  dim_state_waitime = 0;
static u16  VO_voltage=0;
static u32  VO_voltage_sum=0;
static u16  VO_voltage_avg=0;
extern u16  ac_voltage_8209;   //交流电的电压，单位 V
 u32 Vo_value;
#define  OUTPUT_VOLTAGE  Vo_value
#define  INPUT_VOLTAGE   ac_voltage_8209   
 u32 Io_value;
 u32 Po_value;
static u32 vo_io_sample_tick;
u8 _dim;
u8 dim_bak_to_low_acin=100;//最大100
typedef enum 
{
   OUT_VO_IDLE,
   OUT_VO_LOW,
   OUT_VO_LOW_WAIT_H,
} out_vo_state_en;

out_vo_state_en  out_vo_state=OUT_VO_IDLE;

static u16  power_time_out=120;//1.2S
static u8   ready_flag=0;
static u16  time_count;
static u8   error_output_count=0;
 u8  error_flag_byte=0;

u8  Error_0_linght;//闪灯
u8  Error_1_OL;//输出过载
u8  Error_Out_LV;//输出低压
u8  Error_3_OV;//输入过压
u8  Error_4_LV;//输入欠压
u16 timer_ad=1000;
u16 printf_timer=0;
static u16 warn_check=3000;//开机30S 后开始报警检测
   extern u8 online;
static u8 offline_hlaf_load=0;


#define  HALFLOAD_ENABLE     0


#define OUTPUT_STATE_IDLE           0
#define OUTPUT_STAE_SHORT_DELAY     1
#define OUTPUT_STATE_SHORT          2
static u8 output_state=OUTPUT_STATE_IDLE;
static  u8 out_ov_protect;
static  u8 out_short_protect;
static u16 counter_delay;
extern u8 set_percent;
extern u8 pwm_on;
#define OUTPUT_SHORT_THRESHOLD  200 //短路判定电压20V 单位0.1V
#define OUTPUT_OV_THRESHOLD     560*1.2        //56V 1.2倍
typedef enum
{
	OUT_OV_STATE_IDLE=0,
	OUT_OV_STATE_PRE,
	OUT_OV_STATE_OV
}  output_ov_state_en;

output_ov_state_en output_ov_state=OUT_OV_STATE_IDLE;
static u8 output_check_start=0;
static u16 output_check_tiemout=100;
static u16 output_check_counter=0;
static u16 dc_power;   // 直流功率A，   单位 0.1W

u16 pow_on_delay=1000;
u16 protect_check_delay=1000;







void voio_timer(void)
{
    
    if(power_time_out>0 && _dim > 0)
    {
        --power_time_out;
        if(power_time_out<=80)//前200ms不采
        {
            if( power_time_out%10==0)
            {
                ready_flag=1;
            }
        }
    }
    if(timer_ad>0)
    {
     --timer_ad;
    }
  if( ++printf_timer>10 )
  {
      printf_timer=0;
  //  printf("ac=%d\n", ac_voltage_8209);
  }
  if(warn_check>0)
  {
  --warn_check;
  // printf("warn_check=%d\n", warn_check);
  }
    
}

u16 output_samp_res;//输出电流采样电阻

void load_Output_samp_res(void)    
{
    output_samp_res=sys_data.fa_Parambuf[5];
   
}

//---------------------------------------------------------



/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/


void alarm_and_protection_timer(void)
{
    if(pow_on_delay>0) 
    {
       --pow_on_delay;
	}



   if(counter_delay)
   	{
	   --counter_delay;
   	}


	if(protect_check_delay)
	{
	   --protect_check_delay;
	}

    if( output_check_start)
    {
       if(  output_check_tiemout)
       {
         --output_check_tiemout;
		 if(OUTPUT_VOLTAGE >OUTPUT_OV_THRESHOLD&&dc_power>50)   //功率大于5W（空载功率小于5W）
		 {
		    ++output_check_counter;
		 }
		 
		 


	   }


	}
	

}




/************************************
功能描述：电源输出是否处于短路状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/
boolean_en Out_short_detect_is_short(u16* out, u16 in)   //直流PLC
{
    if( out_short_protect)      
    {
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}

void Out_short_detect_fsm(void)                     //待测试
{
    //
   if(1)//parameter1.Output_short.enable
    {

		    switch( output_state)
		    {
		    case OUTPUT_STATE_IDLE:
				
			  if(INPUT_VOLTAGE>700 && pwm_on&&Error_4_LV==0/*&&ov_protect==0*/&&set_percent>0)//在调亮时且输入电压大于70V且欠压过压调灭保护未发生才能判断有短路成立if(INPUT_VOLTAGE>700 && pwm_on&&low_protect==0&&ov_protect==0&&set_percent>0)
			  	{ 
				     if(OUTPUT_VOLTAGE <= OUTPUT_SHORT_THRESHOLD) //小于20V
					  {
						   out_short_protect=0;
						  
						   counter_delay=300;
						
					       output_state=OUTPUT_STAE_SHORT_DELAY;
					  }
			  	}
			
				break;
		     case OUTPUT_STAE_SHORT_DELAY:
			 	if(counter_delay>0)
			 	{
				 	 if(OUTPUT_VOLTAGE > OUTPUT_SHORT_THRESHOLD+5||Error_4_LV||Error_3_OV)//只要有保护调灭的都退出
				 	 {
					     out_short_protect=0;
				    	 output_state=OUTPUT_STATE_IDLE;
					 }
				 }
				
				else if(OUTPUT_VOLTAGE <= OUTPUT_SHORT_THRESHOLD&&INPUT_VOLTAGE>700 && pwm_on&&Error_3_OV==0&&Error_4_LV==0&&set_percent>0)
			 	{
			 	   out_short_protect=1;
				    
	               output_state=OUTPUT_STATE_SHORT;

				}

				break;
			  case OUTPUT_STATE_SHORT:
			  	 if(OUTPUT_VOLTAGE > OUTPUT_SHORT_THRESHOLD+5)
			 	 {
				     out_short_protect=0;

			    	 output_state=OUTPUT_STATE_IDLE;;
				 }

				break;
		  }
	
    }
}









/************************************
功能描述：电源输出是否处于过压状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/
boolean_en Out_voltage_detect_is_high(u16* out, u16 in)   //直流PLC        输出过压保护
{
    if( out_ov_protect)       //  && parameter1.Output_ov.enable 移植更改
    {
        *out = ((u32)in*50)/100; //半载
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}




void Out_voltage_detect_fsm(void)                      //直流PLC    输出过压报警，
{
    if(1)//(parameter1.Output_ov.enable)
    {
        switch(output_ov_state)
        {
	      case OUT_OV_STATE_IDLE :
			if(OUTPUT_VOLTAGE > OUTPUT_OV_THRESHOLD && dc_power>50)   //额定输出电压不大于170V  功率要大于5W
			 {
			     output_check_start=1;//开始识别空载引起的过压
			     output_check_tiemout=100;//用1秒的时间识别空载
				 output_check_counter=0;
			     output_ov_state =OUT_OV_STATE_PRE; 
			 }

		  	break;
          case OUT_OV_STATE_PRE:

		   if(output_check_tiemout>0)
		   {
              if(OUTPUT_VOLTAGE >1700&&dc_power<50)  //防止输出空载误判为输出过压,,电压高，电流很小作为空载
              	{
				   out_ov_protect = 0;
				   output_check_start=0;    //
				   output_check_tiemout=0;  //
				   output_check_counter=0;
				   output_ov_state =OUT_OV_STATE_IDLE; 
				   break;
			    }
		   
	       }
			else if (output_check_tiemout==0)
			{
				if(output_check_counter>95)
				 {
				   out_ov_protect = 1;
					sys_pwm_reload();
				  output_ov_state =OUT_OV_STATE_OV; 

				}
				else 
				{
				   out_ov_protect = 0;
					
				  output_ov_state =OUT_OV_STATE_IDLE; 
				}
				output_check_start=0;	 //
				output_check_tiemout=0;  //
				output_check_counter=0;
			}

		  	break;
			case OUT_OV_STATE_OV:

			    if(OUTPUT_VOLTAGE <OUTPUT_OV_THRESHOLD-50&& dc_power>50)
			      {
					   out_ov_protect = 0;
					  
					   sys_pwm_reload();
				       output_ov_state =OUT_OV_STATE_IDLE; 
				  }

			break;


        }








    }
}






/************************************
功能描述：电源输出是否处于空载状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/
boolean_en Out_noload_detect_is_true(void)   //直流PLC        输出过压保护
{
    if( out_ov_protect)       
    {
       
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}




void Out_noload_detect_fsm(void)                      //从直流PLC移植    输出空载报警，未完成
{
    if(1)//(parameter1.Output_ov.enable)
    {
        switch(output_ov_state)
        {
	      case OUT_OV_STATE_IDLE :
			if(OUTPUT_VOLTAGE > OUTPUT_OV_THRESHOLD && dc_power>50)   //额定输出电压不大于170V  功率要大于5W
			 {
			     output_check_start=1;//开始识别空载引起的过压
			     output_check_tiemout=100;//用1秒的时间识别空载
				 output_check_counter=0;
			     output_ov_state =OUT_OV_STATE_PRE; 
			 }

		  	break;
          case OUT_OV_STATE_PRE:

		   if(output_check_tiemout>0)
		   {
              if(OUTPUT_VOLTAGE >1700&&dc_power<50)  //防止输出空载误判为输出过压,,电压高，电流很小作为空载
              {
				   out_ov_protect = 0;
				   output_check_start=0;    //
				   output_check_tiemout=0;  //
				   output_check_counter=0;
				   output_ov_state =OUT_OV_STATE_IDLE; 
				   break;
			   }
		   
	       }
			else if (output_check_tiemout==0)
			{
			   if(output_check_counter>95)
			    {
				   out_ov_protect = 1;
				   sys_pwm_reload();
				   output_ov_state =OUT_OV_STATE_OV; 

				}
				else 
				{
				   out_ov_protect = 0;
					
				  output_ov_state =OUT_OV_STATE_IDLE; 
				}
				output_check_start=0;	 //
				output_check_tiemout=0;  //
				output_check_counter=0;
			}

		  	break;
			case OUT_OV_STATE_OV:

			    if(OUTPUT_VOLTAGE <OUTPUT_OV_THRESHOLD-50&& dc_power>50)
			      {
                       out_ov_protect = 0;
                       sys_pwm_reload();
                       output_ov_state =OUT_OV_STATE_IDLE; 
				  }

			break;


        }








    }
}






//--------------------------------------------------------

/************************************
功能描述： 故障报告
输入参数：无
输出返回：无
*************************************/
void error_report_process(void)
{ 

    if(timer_ad==0)
    {
               timer_ad=1;
          //   printf(" OUTPUT_CUR_SENSOR=%d\n",OUTPUT_CUR_SENSOR);
               Vo_value= ((u32)ADC_Value2*53U)/100U;   //  电压单位0.1V   参考电压3.3V    39K+0.75K ，上报时会除以100按0.1V为单位上报  电压检测偏大0.001  //*((float)1-0.01)
               if(OUTPUT_CUR_SENSOR == 0)
               {
                   Io_value = 0;
               }
               else
               {
                   Io_value = ((u32)ADC_Value4*100000U)/((u32)OUTPUT_CUR_SENSOR*834U);      //  电流单位mA     参考电压3.3V    50毫欧/8.3333倍    100W以下
               }

  
        
        
              /* if(MID==3) //100W 输入功率和电流偏大 的处理
               {
                   if(Vo_value>25)
                   {
                      Vo_value-=5;
                   }
                   else if(Vo_value>10)
                   {
                     Vo_value-=10;
                   }
                   if(Io_value>10)
                   {
                     Io_value-=15;
                   }

                }

                
                if(MID==2) //75W 输入功率和电流偏大 的处理
                {
                   if(Vo_value>25)
                   {
                      Vo_value-=5;
                   }
                   else if(Vo_value>10)
                   {
                     Vo_value-=10;
                   }
                }*/
               Po_value = ((u32)Vo_value*Io_value)/1000U;   //单位0.1W
               vo_io_sample_tick = Timer_GetTickCount();
        
//             printf(" ADC_Value4=%d\n",ADC_Value4);
//             printf(" Vo_value=%dV\n",Vo_value/10);
//             printf(" Io_value=%dmA\n",Io_value);
//             printf(" Po_value=%dW\n",Po_value);

            

    }




    
    //以下功能是调灭停止采电压，调亮延迟5秒采集电压
    if(dim_state==DIM_STATE_1)
    {
        if(_dim==0)
        {
          dim_state=DIM_STATE_0;
        }
    }
    else if(dim_state==DIM_STATE_0)
    {
        if(_dim>0)
        {
            dim_state_waitime=2000;//延时5秒
            dim_state=DIM_STATE_WAIT;
        }
    }
   else if(dim_state==DIM_STATE_WAIT)
    {
       if(_dim>0) 
       {
           if(dim_state_waitime==0)
           {
              dim_state=DIM_STATE_1;
           }
       }
       else
       {
         dim_state=DIM_STATE_0;
       }
  
    }
     //以上功能是调灭停止采集电压，调亮延迟5秒采集电压使能
    
    
    
       if(dim_state==DIM_STATE_1)
       {  
         // if(flag_adc_vs3)
          {
        //   flag_adc_vs3=0;

           
             VO_voltage =Vo_value;//(u16)((float)ADC_Value2*39.75/0.75/100); 
             printf(" VO_voltage=%dmV\n",VO_voltage);
          }
      }
       if(ready_flag&&power_time_out>0&&power_time_out<=100)//250
       {  
           
                 ready_flag=0; 

//                printf(" VO1=%d\n",VO_voltage);
                  VO_voltage_sum+=VO_voltage;
//                printf(" count=%d\n",count); 
//                printf(" SUM=%d\n",VO_voltage_sum);
       }
       else if(power_time_out==0)
       {
           
         VO_voltage_avg=(u32)(VO_voltage_sum/8);
  
       }
   

     if(_dim > 0 && power_time_out== 0 )//&& dim_off_flag == 0
     {
              switch (out_vo_state)  //闪灯判断
              {
                  case OUT_VO_IDLE :
                      if(VO_voltage<VO_voltage_avg*0.8)
                      { 
                          time_count=500;//5秒的检测周期
                          out_vo_state=OUT_VO_LOW;
                      }
                      break;
                  case OUT_VO_LOW:
                      if(time_count<=470 && VO_voltage<VO_voltage_avg*0.8)  //要大于300mS不然调灭误报
                      {
                        out_vo_state=OUT_VO_LOW_WAIT_H;
                      }
                      else if(time_count>490 && VO_voltage>VO_voltage_avg*0.8)//小于100mS 丢弃
                      {   
                           time_count=0;
                           out_vo_state=OUT_VO_IDLE;
                      }
                      break;
                  case OUT_VO_LOW_WAIT_H:
                      if(time_count>1)
                      {
//                         printf("VO_voltage=%d\n",VO_voltage); 
                          if(VO_voltage>VO_voltage_avg*0.8)
                          { 

                              if(error_output_count<255)
                              {
                                ++error_output_count;
                              }
                              Error_0_linght=1;//闪灯上报
                              time_count=0;
                              out_vo_state=OUT_VO_IDLE;
                          
                          }
                      }
                      else if(time_count==0)                     
                      {
                          if(VO_voltage>VO_voltage_avg*0.8)//0.8
                          {
                               out_vo_state=OUT_VO_IDLE;
                          }

                      }
                      break;

              }
        }
  //报警保护
        if(warn_check==0)   //开机延时到开始检测，防止计量芯片等还没正常启动  
        {   
            if(HALFLOAD_ENABLE)
            {
                  if(online==1)
                  {
                      if(offline_hlaf_load==1)
                      {
                          offline_hlaf_load=0;
                          pwm_output(dim_bak_to_low_acin);//后续用 sys_pwm_reload();  处理
                      }
                  }
                  else
                  {
                      if(offline_hlaf_load==0)
                      {
                          offline_hlaf_load=1;
                          pwm_output(dim_bak_to_low_acin/2);
                      } 
                      
                  }
              }
         //输入欠压保护   低于70V硬件关机
               if(ac_voltage_8209<800)  //单位0.1V 
               {
               //printf("ac=%d\n", ac_voltage_8209);
                 if(  Error_4_LV==0)
                 {
                    Error_4_LV=1;//输入欠压 170V
                    sys_pwm_reload();  //pwm_output(dim_bak_to_low_acin/2);
                    printf("Error_4_LV=%d\n", Error_4_LV);
                 }
               } 
                if(ac_voltage_8209>850)  //dan位0.1V 
               {
                  if( Error_4_LV==1) 
                  {
                  // printf("ac=%d\n", ac_voltage_8209);
                 
                    Error_4_LV=0;//输入欠压 恢复
                    sys_pwm_reload();  // pwm_output(dim_bak_to_low_acin);
                    printf("Error_4_LV=%d\n", Error_4_LV);
                  }
               } 
               
               //掉电
               
               
               
               
               
               
           //输入过压保护       
               
               
               if(ac_voltage_8209>3200)   //320确保330V关
               {
                   if( Error_3_OV==0)
                   {
                      printf("ac=%d\n", ac_voltage_8209);
                      Error_3_OV=1;//
                      sys_pwm_reload();// pwm_output(0);
                   }
               } 
               if(ac_voltage_8209<3100)  //310确保305V恢复
               {
                   if(Error_3_OV)
                   {
                      printf("ac=%d\n", ac_voltage_8209);
                      Error_3_OV=0;//          //恢复报警
                      sys_pwm_reload();//   pwm_output(dim_bak_to_low_acin);
                   }
                 
               } 
               
             //输出功率保护    
             /*  
               if(power_on)
               {
                   if(Po_value>1800)        //单位0.1W
                   {
                         Error_1_OL=1;
                   }
                   if(Po_value<1800-200)     //单位0.1W
                   {
                         Error_1_OL=0;         //恢复报警
                   }
               
               
                  if(   dim_bak_to_low_acin>0 ) 
                   {
                       if(Vo_value<200)      //单位0.1V  输出低压
                       {
                          Error_Out_LV=1;
                       }
                        if(Vo_value>230)//单位0.1V  恢复低压报警
                       {
                          Error_Out_LV=0;
                       }
                   }
                }
               */
         }
     //合并报警字节/备用，后台不用解析
       if(Error_0_linght)//闪灯
       {
          error_flag_byte|=0x01;
       }
       else
       {
         error_flag_byte&=~0x01;
       }
       
       if(Error_1_OL)//输出过载
       {
        
          error_flag_byte|=0x02;
       }
       else
       {
         error_flag_byte&=~0x02;
       }
       
       if(Error_Out_LV)    // 输出低压
       {
           
        error_flag_byte|=0x04;
       }
       else
       {
         error_flag_byte&=~0x04;
       }
       
        if(Error_3_OV)//输入过压
       {
          error_flag_byte|=0x08;
       }
       else
       {
         error_flag_byte&=~0x08;
       }
       
        if(Error_4_LV)//输入欠压
       {
          error_flag_byte|=0x10;
       }
       else
       {
         error_flag_byte&=~0x10;
       }
}

boolean_en sys_vo_io_get_snapshot(sys_vo_io_snapshot_t *snapshot)
{
    u32 now;
    u32 sample_tick;
    u32 primask;
    u8 i;

    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < 4U; ++i)
    {
        snapshot->adc_raw[i] = adc_average[i];
    }
    snapshot->adc_voltage_mv = ADC_Value2;
    snapshot->adc_current_mv = ADC_Value4;
    snapshot->output_voltage_01v = Vo_value;
    snapshot->output_current_ma = Io_value;
    snapshot->output_power_01w = Po_value;
    snapshot->temperature_01c = Ntctemp.Ntctemp;
    snapshot->protect_code = error_flag_byte;
    sample_tick = vo_io_sample_tick;
    if (primask == 0U)
    {
        __enable_irq();
    }
    now = Timer_GetTickCount();
    snapshot->sample_age_ms = now - sample_tick;
    if (sample_tick == 0U || snapshot->sample_age_ms > 2000U)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}



/************************************
功能描述：电源输入是否处于低压状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/
boolean_en DC_low_voltage_detect_is_low(u16* out, u16 in)
{
    if( Error_4_LV)       
    {
        *out = ((u32)in*50)/100; //半载
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}

/************************************
功能描述：电源输入是否处于高压状态
输入参数：无
输出返回：是返回 BOOL_TRUE, 没有低压返回 BOOL_FALSE
*************************************/

boolean_en High_voltage_detect_is_high(u16* out, u16 in)   
{
    if( Error_3_OV)      
    {
        *out = 0;   //关机保护
        return(BOOL_TRUE);
    }
    else
    {
        return(BOOL_FALSE);
    }
}


