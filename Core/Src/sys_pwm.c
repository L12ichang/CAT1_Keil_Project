
/*************************************************************
程序功能：PWM输出
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "sys_pwm.h"

#include "sys_data.h"
#include "sys_temp_over_protect.h"
#include "hw_tim1_pwm2.h"
#include "sys_Vo_Io.h"
#include "factory_user_data.h"
#define TIMEOUT_MAX      200
#define PWM_OUT_MAX      1000
#define PWM_OFFSET   (u16)(OP_PWM_OFFSET)  //由于光耦的延迟问题增加3%输出
#define PWM_USEFUL_RANGE    (u16)(PWM_OUT_MAX-PWM_OFFSET)  //

#if APP_PWM_DEBUG_ENABLE
#define PWM_DBG(...) printf(__VA_ARGS__)
#else
#define PWM_DBG(...) do {} while (0)
#endif

//#define SET_OUTCUR     sys_data.setcur
//#define HWMAX_OUTCUR   sys_data.hwmaxcur

#define  hw_set_pwm(pwm)     hw_tim1_pwm2_set_PWM_OUT(pwm)//+++++++
u8 reload;
u16 set_percent;
static u8 _timer = TIMEOUT_MAX;
static boolean_en _fade = BOOL_FALSE;
static u8 power_old; //第一次入网的时候才渐变，后面的不渐变，因为调光0-100，没有101 
static u8 power_new;
static u8 power_current;

extern u32 fac_en_timer;
extern u8  fa_test_EN; 


 void pwm_output(u8 persent)   //硬件输出
{
     u16 pwm;
     u32 pwm_value;
     u8 init_persent = persent;

      set_percent = persent;
      if( low_temp_detect_is_low(&pwm, persent) == BOOL_TRUE)
      {printf("low_temp_detect_\n");
        if(pwm<persent)
        {
            persent = pwm; //温度保护取最低那个输出
        }

      }
      if( DC_low_voltage_detect_is_low(&pwm,persent)==BOOL_TRUE)
      { printf("DC_low_voltage_detec\n");
         if(pwm<persent)
        {
            persent = pwm; //温度保护和低压保护，取最低那个输出
        }

      }
        if(High_voltage_detect_is_high(&pwm, persent) == BOOL_TRUE)
        {
            printf("high_voltage\n");
              //                log_u16(1,pwm);
            if(pwm<persent)
            {
                persent = pwm; //温度保护和低压保护，取最低那个输出
            }
        }
          if(temp_detect_is_over(&pwm, persent) == BOOL_TRUE)
        {
             printf("high_temp\n");
              //                log_u16(1,pwm);
             printf("persent=%d\n",persent);
             printf("tempPWM=%d\n",pwm);
            if(pwm<persent)
            {
                persent = pwm; //温度保护和低压保护，取最低那个输出

            }
        }

      if(fa_test_EN==0)
      {
         if(HWMAX_OUTCUR == 0)
         {
             pwm_value = 0;
             PWM_DBG("[PWM] HWMAX_OUTCUR=0 -> pwm_value=0\r\n");
         }
         else
         {
             pwm_value = ((u32)persent * (u32)SET_OUTCUR * (u32)PWM_USEFUL_RANGE) / ((u32)HWMAX_OUTCUR * 100U);
         }
      }
      else   //产测模式
      {
        pwm_value = ((u32)persent * (u32)PWM_USEFUL_RANGE) / 100U;
        PWM_DBG("[PWM] fac_test mode -> pwm_value=%lu\r\n", pwm_value);
      }

    /* PWM 调试日志：可观察完整计算过程与电子负载对比 */
    PWM_DBG("====== PWM Calc ======\r\n");
    PWM_DBG("  brightness     = %u %%\r\n", init_persent);
    PWM_DBG("  eff_persent    = %u %%\r\n", persent);
    PWM_DBG("  SET_OUTCUR     = %u mA\r\n", (u16)SET_OUTCUR);
    PWM_DBG("  HWMAX_OUTCUR   = %u mA\r\n", (u16)HWMAX_OUTCUR);
    PWM_DBG("  PWM_OFFSET     = %u\r\n", (u16)PWM_OFFSET);
    PWM_DBG("  PWM_USEFUL_RNG = %u\r\n", (u16)PWM_USEFUL_RANGE);
    PWM_DBG("  PWM_OUT_MAX    = %u\r\n", (u16)PWM_OUT_MAX);
    if (HWMAX_OUTCUR > 0 && fa_test_EN == 0)
    {
        u32 numerator   = (u32)persent * (u32)SET_OUTCUR * (u32)PWM_USEFUL_RANGE;
        u32 denominator = (u32)HWMAX_OUTCUR * 100U;
        PWM_DBG("  numerator      = %lu\r\n", numerator);
        PWM_DBG("  denominator    = %lu\r\n", denominator);
        PWM_DBG("  ratio(SET/HW)  = %lu / 1000\r\n",
                ((u32)SET_OUTCUR * 1000U) / (u32)HWMAX_OUTCUR);
    }
    PWM_DBG("  -> pwm_value   = %lu / %u\r\n", pwm_value, (u16)PWM_OUT_MAX);
    PWM_DBG("  -> duty        = %lu.%lu %%\r\n",
            (pwm_value * 100U) / PWM_OUT_MAX,
            ((pwm_value * 1000U) / PWM_OUT_MAX) % 10U);
    PWM_DBG("======================\r\n");

    hw_set_pwm((u16)pwm_value);
}

void sys_pwm_timer(void)
{
    u32 tmp;
    
    if(_fade)
    {
        if(_timer < TIMEOUT_MAX)
        {
            ++_timer;
            if(power_new > power_old)
            {
                tmp = power_new - power_old;
                power_current = power_old + tmp*_timer/TIMEOUT_MAX;
                pwm_output(power_current);
            }
            else
            {
                tmp = power_old - power_new;
                power_current = power_old - tmp*_timer/TIMEOUT_MAX;
                pwm_output(power_current);
            }
        }
        else
        {
            _fade = BOOL_FALSE;
            power_old = power_new;
            pwm_output(power_new);
        }
    }
    
    
    if(fac_en_timer>0)
    {
       --fac_en_timer;
        if(fac_en_timer==0&&fa_test_EN!=0)
        {
         // fa_test_EN=0;        //产测超时退出
          //  sys_pwm_reload();    //刷新PWM
        }
    }
}

void sys_pwm_fade_output(u8 oldpower, u8 newpower) //本文被调    启动渐变
{
    power_current = oldpower;
    power_old = oldpower;
    power_new = newpower;
    pwm_output(oldpower);
    if(power_old != power_new)
    {
        _timer = 0;
        _fade = BOOL_TRUE;
    }
}

// persent 0-100
u8 net_entery_flag=0 ;
u8 has_entery_at_first=0;

void sys_pwm_output(u8 persent)   
{
    if(persent == 0)
    {
        _fade = BOOL_FALSE;
        power_old = persent;
        pwm_output(persent);   // hw_set_pwm((u32)PWM_USEFUL_RANGE*persent/100);        
    }
    else
    {
        if(_fade)
        {
            sys_pwm_fade_output(power_current, persent);
        }
        else
        {
           // if(power_old == 0)  //第一次入网的时候才渐变，后面的不渐变，因为调光0-100，没有101 net_entery && has_entery_at_first==0
            if( net_entery_flag && has_entery_at_first==0)
            { 
                has_entery_at_first=1;// 第一次入网进来要渐变
                printf("fade_first_____");
                sys_pwm_fade_output(power_current, persent);//入网进来时，要把半载改为服务器的值
                
            }
            else
            {                
                power_old = persent;
                printf("netdimpersent=%d\n",persent); 
                pwm_output(persent);        
            }
        }
    }
    //sys_temp_over_protect_recovery_to_idle();
}

// persent 0-100
void sys_pwm_output_for_temp_protect(u8 persent)    //未调用
{
    power_old = persent;
    _fade = BOOL_FALSE;
    pwm_output(persent);          
}
void sys_pwm_output_on_fade(u8 persent)             //未调用
{
    power_old = persent;
    _fade = BOOL_FALSE;
    pwm_output(persent);        
}

void sys_pwm_reload(void)
{
     reload = 1;
	 printf(" reload1\r\n");
}


void sys_pwm_process(void)
{
  
    if(reload==1)
    {
      reload=0;
        printf(" reload1persent=%d\n",set_percent);  
      sys_pwm_output( set_percent );
    }
    
}


