/*************************************************************
程序功能：蜂鸣器, 最佳频率 2.7K
开发环境：keil 5.37
芯片型号：STM32F103RBT6
开发人员：梁庆能
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/


#include "buzzer.h"
//#include "timer2.h"
//#include "stm32f0xx_tim.h"
#include "hw_tim4_pwm2.h"
#include "hw_tim1_pwm2.h"

//#define SET_BUZZER_TOGGLE       BEEP_0;

#if HARDWARE_VERSION == HARDWARE_VERSION_1
#define SET_BUZZER_ON        hw_tim4_pwm2_set_on();     // 启动TIM4通道2输出
#define SET_BUZZER_OFF       hw_tim4_pwm2_set_off();    // 不启动TIM4通道2输出
#elif HARDWARE_VERSION == HARDWARE_VERSION_2
#define SET_BUZZER_ON      //  hw_tim1_pwm2_set_on();     // 启动TIM4通道2输出
#define SET_BUZZER_OFF       hw_tim1_pwm2_set_off();    // 不启动TIM4通道2输出
#endif


#define TIMEOUT             0xff
#define CONTINUE            0xff

static u8  _beepcnt;
static u8  _beepon_tmr;
static u8  _beepoff_tmr;
static u8  _beepon_duration;
static u8  _beepoff_duration;


/************************************
功能描述：蜂鸣器PWM发生器
输入参数：无
输出返回：无
*************************************/
void pwm_generator(void)
{
//    SET_BUZZER_TOGGLE;        
}


boolean_en buzzer_is_finish(void)
{
    if(_beepcnt == 0 && _beepoff_tmr == TIMEOUT && _beepon_tmr == TIMEOUT)
    {
        return BOOL_TRUE;
    }
    else
    {
        return BOOL_FALSE;
    }
}


/************************************
功能描述：每定时 10ms 执行一次
输入参数：无
输出返回：无
*************************************/
void buzzer_timer(void)
{
    if (_beepon_tmr != TIMEOUT)
    {
        _beepon_tmr--;
        if (_beepon_tmr == TIMEOUT)
        {
                SET_BUZZER_OFF;      
            if (_beepcnt != 0)
            {
                _beepoff_tmr = _beepoff_duration;
            }
        }
    }
    if (_beepoff_tmr != TIMEOUT)
    {
        _beepoff_tmr--;
        if (_beepoff_tmr == TIMEOUT)
        {
            if (_beepcnt != 0)
            {
                if (_beepcnt != CONTINUE)
                {
                  _beepcnt--;
                };
                SET_BUZZER_ON;          
                _beepon_tmr = _beepon_duration;
            }
        }
    }
} 


/************************************
功能描述：蜂鸣器发声
输入参数：ontime 响的时间， offtime 关的时间，cnt重复的次数 单位是10ms
输出返回：无
*************************************/
void  buzzer_sound(u8 ontime, u8 offtime, u8 cnt)
{
    if ((ontime != 0) && (cnt != 0) && _beepcnt==0)
    {
        SET_BUZZER_ON;        
        if (offtime != 0)
        {      
            if (cnt != CONTINUE)
            {
                _beepcnt = cnt - 1;
            }
            else
            {
                _beepcnt = CONTINUE;
            }
            _beepon_duration = ontime;
            _beepoff_duration = offtime;
            _beepoff_tmr = TIMEOUT;
            _beepon_tmr = ontime;
        }
    }
}


/************************************
功能描述：模块的初始化
输入参数：无
输出返回：无
*************************************/
void buzzer_init(void)
{
//    buzzer_gpio_init();
    _beepon_tmr = TIMEOUT;
    _beepoff_tmr = TIMEOUT;
    _beepcnt = 0;
    SET_BUZZER_OFF;  
}


#if 1
/************************************
功能描述：停止蜂鸣器叫声
输入参数：无
输出返回：无
*************************************/
void buzzer_stop(void)
{
    _beepoff_tmr = TIMEOUT;
    _beepon_tmr = TIMEOUT;
    _beepcnt = 0;
    SET_BUZZER_OFF;        
}
#endif

