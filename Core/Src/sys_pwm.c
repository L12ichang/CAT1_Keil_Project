
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
#include "sys_calibration_snapshot.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_safety.h"
#include "sys_calibration_service.h"
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

#if LEGACY_APP_PROCESS_ENABLE
extern u32 fac_en_timer;
extern u8  fa_test_EN;
#else
u32 fac_en_timer;
u8  fa_test_EN;
#endif


 void pwm_output(u8 persent)   //硬件输出
{
     u16 pwm;
     u32 pwm_value;
     u16 pwm_current_reference_ma = (u16)SET_OUTCUR;
     u8 requested_percent = persent;
#if APP_PWM_DEBUG_ENABLE
     u8 init_persent = persent;
#endif

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

      /* Runtime SET_OUTCUR is writable, but may never exceed the profile envelope. */
      if (sys_product_profile_runtime_matches(
              MID, OUTPUT_CUR_SENSOR, HWMAX_OUTCUR) == BOOL_TRUE &&
          factory_user_validate_runtime_current(
              BOUND_OUTPUT_VOLTAGE_01V, SET_OUTCUR) ==
              SYS_PRODUCT_CURRENT_VALID &&
          sys_calibration_service_runtime_context_matches_voltage(
              BOUND_OUTPUT_VOLTAGE_01V) == BOOL_TRUE)
      {
          u8 safe_percent;
          u8 calibrated_percent;

          if (Error_1_OL != 0U || Error_Out_LV != 0U ||
              Error_3_OV != 0U || Error_4_LV != 0U)
          {
              persent = 0U;
          }
          else if (persent > 0U &&
                   sys_calibration_safety_limit_percent(
                       persent, (u16)Vo_value, (u16)SET_OUTCUR,
                       &safe_percent) == BOOL_TRUE)
          {
              persent = safe_percent;
          }
          else if (persent > 0U)
          {
              persent = 0U;
          }
          if (persent > 0U)
          {
              if (sys_calibration_service_correct_output_percent(
                      persent, (u16)SET_OUTCUR,
                      &calibrated_percent) == BOOL_TRUE &&
                  sys_product_profile_compute_i100_ma(
                      sys_product_profile_current(),
                      BOUND_OUTPUT_VOLTAGE_01V,
                      &pwm_current_reference_ma) == BOOL_TRUE)
              {
                  /* The table level was produced with the profile I100 PWM
                     scale; do not multiply by the lower SET a second time. */
                  persent = calibrated_percent;
              }
              else
              {
                  persent = 0U;
              }
          }
      }
      else if (persent > 0U)
      {
          persent = 0U;
      }

      /* 直驱没有最坏映射和新鲜反馈证明，非零请求在量纲换算前失败关闭。 */
      if (fa_test_EN != 0U && persent > 0U &&
          SYS_CALIBRATION_FACTORY_DIRECT_PWM_ENABLED == 0U)
      {
          persent = 0U;
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
             if (sys_product_profile_scale_percent_to_pwm(
                     sys_product_profile_current(),
                     BOUND_OUTPUT_VOLTAGE_01V, persent,
                     PWM_USEFUL_RANGE, &pwm) == BOOL_TRUE)
             {
                 pwm_value = pwm;
             }
             else
             {
                 pwm_value = 0U;
             }
         }
      }
      else   //产测模式
      {
        if (SYS_CALIBRATION_FACTORY_DIRECT_PWM_ENABLED != 0U)
        {
            /* 未来开启前必须补齐最坏映射、渐升和反馈新鲜度门禁。 */
            pwm_value = ((u32)persent * (u32)PWM_USEFUL_RANGE) / 100U;
            PWM_DBG("[PWM] fac_test mode -> pwm_value=%lu\r\n", pwm_value);
        }
        else
        {
            pwm_value = 0U;
            PWM_DBG("[PWM] fac_test nonzero output is safety-gated\r\n");
        }
      }

    pwm_value = sys_calibration_safety_arbitrate_pwm(
        (u16)pwm_value,
        (fa_test_EN != 0U) ? SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT :
                             SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
        sys_calibration_service_is_boot_inhibited(),
        (Error_1_OL != 0U || Error_Out_LV != 0U ||
         Error_3_OV != 0U || Error_4_LV != 0U) ? BOOL_TRUE : BOOL_FALSE,
        BOOL_FALSE);

    /* PWM 调试日志：可观察完整计算过程与电子负载对比 */
    PWM_DBG("====== PWM Calc ======\r\n");
    PWM_DBG("  brightness     = %u %%\r\n", init_persent);
    PWM_DBG("  eff_persent    = %u %%\r\n", persent);
    PWM_DBG("  SET_OUTCUR     = %u mA\r\n", (u16)SET_OUTCUR);
    PWM_DBG("  PWM_REF_CUR    = %u mA\r\n", pwm_current_reference_ma);
    PWM_DBG("  HWMAX_OUTCUR   = %u mA\r\n", (u16)HWMAX_OUTCUR);
    PWM_DBG("  PWM_OFFSET     = %u\r\n", (u16)PWM_OFFSET);
    PWM_DBG("  PWM_USEFUL_RNG = %u\r\n", (u16)PWM_USEFUL_RANGE);
    PWM_DBG("  PWM_OUT_MAX    = %u\r\n", (u16)PWM_OUT_MAX);
#if APP_PWM_DEBUG_ENABLE
    if (HWMAX_OUTCUR > 0 && fa_test_EN == 0)
    {
        u32 numerator   = (u32)persent * (u32)pwm_current_reference_ma *
                          (u32)PWM_USEFUL_RANGE;
        u32 denominator = (u32)HWMAX_OUTCUR * 100U;
        PWM_DBG("  numerator      = %lu\r\n", numerator);
        PWM_DBG("  denominator    = %lu\r\n", denominator);
        PWM_DBG("  ratio(SET/HW)  = %lu / 1000\r\n",
                ((u32)SET_OUTCUR * 1000U) / (u32)HWMAX_OUTCUR);
    }
#endif
    PWM_DBG("  -> pwm_value   = %lu / %u\r\n", pwm_value, (u16)PWM_OUT_MAX);
    PWM_DBG("  -> duty        = %lu.%lu %%\r\n",
            (pwm_value * 100U) / PWM_OUT_MAX,
            ((pwm_value * 1000U) / PWM_OUT_MAX) % 10U);
    PWM_DBG("======================\r\n");

    sys_calibration_snapshot_prepare_pwm((u16)requested_percent, (u16)persent);
    hw_set_pwm((u16)pwm_value);
    sys_calibration_snapshot_publish_pwm(HAL_GetTick(),
                                         hw_tim1_pwm2_get_logical_pwm(),
                                         hw_tim1_pwm2_get_ccr(),
                                         hw_tim1_pwm2_get_oco_on(),
                                         SYS_CALIBRATION_PWM_SAMPLE_VALID);
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

void sys_pwm_force_safe_off(void)
{
    _fade = BOOL_FALSE;
    power_old = 0U;
    power_new = 0U;
    power_current = 0U;
    set_percent = 0U;
    sys_calibration_snapshot_prepare_pwm(0U, 0U);
    hw_tim1_pwm2_set_PWM_OUT(0U);
    sys_calibration_snapshot_publish_pwm(HAL_GetTick(),
                                         hw_tim1_pwm2_get_logical_pwm(),
                                         hw_tim1_pwm2_get_ccr(),
                                         hw_tim1_pwm2_get_oco_on(),
                                         SYS_CALIBRATION_PWM_SAMPLE_VALID);
}

boolean_en sys_pwm_calibration_set_level(u16 level)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_adc_snapshot_st adc;
    sys_calibration_context_st context;
    u16 calibration_max_current_ma;
    u16 pwm;
    u8 requested_percent;
    u8 safe_percent;
    u32 pwm_value;
    u32 feedback_voltage_01v;

    if (level > 200U)
    {
        return BOOL_FALSE;
    }
    requested_percent = (u8)(level / 2U);
    if (requested_percent == 0U)
    {
        sys_pwm_force_safe_off();
        return BOOL_TRUE;
    }
    if (sys_calibration_snapshot_read_adc(&adc) != BOOL_TRUE)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    feedback_voltage_01v = (((u32)adc.vout_raw * 3300U) / 4095U) * 53U / 100U;
    if (sys_product_profile_runtime_matches(
            MID, OUTPUT_CUR_SENSOR, HWMAX_OUTCUR) != BOOL_TRUE ||
        sys_calibration_service_get_context(&context) != BOOL_TRUE ||
        sys_product_profile_compute_i100_ma(
            profile, context.calibration_voltage_01v,
            &calibration_max_current_ma) != BOOL_TRUE ||
        sys_calibration_service_runtime_context_matches_voltage(
            BOUND_OUTPUT_VOLTAGE_01V) != BOOL_TRUE ||
        Error_1_OL != 0U || Error_Out_LV != 0U ||
        Error_3_OV != 0U || Error_4_LV != 0U ||
        adc.valid_flags == 0U || (HAL_GetTick() - adc.tick_ms) > 500U ||
        sys_calibration_safety_limit_percent(
            requested_percent, (u16)feedback_voltage_01v,
            calibration_max_current_ma,
            &safe_percent) != BOOL_TRUE || safe_percent == 0U ||
        HWMAX_OUTCUR == 0U)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    if (sys_product_profile_scale_percent_to_pwm(
            profile, context.calibration_voltage_01v, safe_percent,
            PWM_USEFUL_RANGE, &pwm) != BOOL_TRUE)
    {
        sys_pwm_force_safe_off();
        return BOOL_FALSE;
    }
    pwm_value = pwm;
    sys_calibration_snapshot_prepare_pwm(requested_percent, safe_percent);
    hw_tim1_pwm2_set_calibration_PWM_OUT((u16)pwm_value);
    sys_calibration_snapshot_publish_pwm(HAL_GetTick(),
                                         hw_tim1_pwm2_get_logical_pwm(),
                                         hw_tim1_pwm2_get_ccr(),
                                         hw_tim1_pwm2_get_oco_on(),
                                         SYS_CALIBRATION_PWM_SAMPLE_VALID);
    return (hw_tim1_pwm2_get_logical_pwm() == (u16)pwm_value) ?
           BOOL_TRUE : BOOL_FALSE;
}
