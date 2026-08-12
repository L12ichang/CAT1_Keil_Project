#include "offline_Time_controlled_dimming.h"
#include "sys_temp_over_protect.h"
#include "sys_data.h"
#include "sys_aip1302.h"
#include "factory_user_data.h"
#include "sys_pwm.h"

#define TYPE_TIMER      0
   extern u8 online;
boolean_en power_status = BOOL_TRUE; //开灯
boolean_en power_status_bak = BOOL_TRUE; //开灯
//static boolean_en flag_one_date = BOOL_FALSE;//真为有RTC效数据
boolean_en flag_plan_pause = BOOL_FALSE;//时控中止（由离线决定）

u8 current_brightness_level = 0;

static u16 power_on_deylay=6000*10;//开机10分钟后才能开始是否离线运行
static u8 online_timeout=0;
void offline_timer(void)
{
      if(power_on_deylay>0)
      {
         --power_on_deylay;
          if(power_on_deylay==0)
          {
            online_timeout=1;    //上线待做清零
          }
      }
}

void Work_offline_dimming_process(void)  //离线模式与服务器日同步
{    
   //离线日重复调光
    if( DAY_LOOP_EN==1 && online==0&&online_timeout )//日循环使能并且离线，开机n秒后  
   {
        if(sys_temp_over_protect_state == SYS_TEMP_OVER_PROTECT_STATE_IDLE)
        {             
            if((apprtc_RtcTime.day>=STAR_TIME.day&& apprtc_RtcTime.mon>=STAR_TIME.mon&&apprtc_RtcTime.year>=STAR_TIME.year) &&(apprtc_RtcTime.day<= END_TIME.day&& apprtc_RtcTime.mon<=END_TIME.mon&&apprtc_RtcTime.year<=END_TIME.year))//年月日在设置范围
            {  
                for (int i = 0; i < SCHEDULE_SIZE; i++)
                {
                   if (apprtc_RtcTime.hour == SEVER_TIMER_DIM[i].hour && apprtc_RtcTime.min ==SEVER_TIMER_DIM[i].min ) //时间相等执行动作
                   {  
                       // 调整到对应的亮度级别
                       sys_pwm_output(SEVER_TIMER_DIM[i].dim_lever);//调光输出
                       current_brightness_level = SEVER_TIMER_DIM[i].dim_lever; // 更新当前亮度级别
                       break;
                    }
                }
          }
              
        }
            
   }     
    

}



































