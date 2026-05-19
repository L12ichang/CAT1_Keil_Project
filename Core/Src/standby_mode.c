/*************************************************************
程序功能：休眠模式
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "standby_mode.h"
#include "buzzer.h"
#include "charge.h"
#include "hw_led.h"
#include "hw_dali.h"
#include "hw_tim3_pwm2.h"
#include "app_state.h"



#define STANDBY_TIME          60   //这个时间内没有动作就进入休眠，单位 s
#define NO_STANDBY_TIME       500  //在此时间内不能进入休眠, 单位10ms

static u8 _timer = STANDBY_TIME;
extern u16 _timer_for_press;

u16 standby_timer = NO_STANDBY_TIME;
/************************************
功能描述：进入休眠
输入参数：无
输出返回：无
*************************************/
void standby_mode_action(void)
{

    HAL_NVIC_DisableIRQ(EXTI0_IRQn);
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    HAL_NVIC_DisableIRQ(TIM4_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);    
   // hw_led_all_off();
    DALI_OUT_DISABLE;
    TX_HIGH;  //io拉低
    //PDOWN_1;
    hw_tim3_pwm2_set_off();
    POWER_OFF(); //3.3V
  //  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);


}





/************************************
功能描述：进入间歇休眠
输入参数：无
输出返回：无
*************************************/
void standby_mode_interval_action(void)
{
#if 0
	GPIO_InitTypeDef GPIO_InitStructure;			    //定义一个GPIO_InitTypeDef类型的结构体
    u8 clkbak;    
    
    NVIC_DisableIRQ(PA_IRQn);
    //NVIC_DisableIRQ(PD_IRQn);
    NVIC_DisableIRQ(SysTick_IRQn);
    NVIC_DisableIRQ(TIMER2_IRQn);
    NVIC_DisableIRQ(TIMER3_IRQn);
    NVIC_DisableIRQ(UART0_IRQn);
    
    
    GPIO_PinAFConfig(CMSDK_PD, GPIO_PinSource6, GPIO_AFS_Primary);  
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;           
    GPIO_InitStructure.GPIO_Mode = GPIO_OType_PP;       
    GPIO_Init(CMSDK_PD, &GPIO_InitStructure);       
    GPIO_ResetBits(CMSDK_PD, GPIO_Pin_6);





    GPIO_PinAFConfig(CMSDK_PA, GPIO_PinSource2, GPIO_AFS_Primary);	
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;			
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;		
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;		
    GPIO_Init(CMSDK_PA, &GPIO_InitStructure);		    





    GPIO_ResetBits(CMSDK_PD, GPIO_Pin_3);
    GPIO_ResetBits(CMSDK_PB, GPIO_Pin_4);
    GPIO_ResetBits(CMSDK_PB, GPIO_Pin_5);

    
    wdt_init_for_standby();
    
    clkbak=CMSDK_SYSCON->SYSCLK;
    PWR_EnterSTANDBYMode(PWR_STANDBYEntry_WFI);

    CMSDK_SYSCON->SYSCLK&=~0x000000FF;
    CMSDK_SYSCON->SYSCLK|=clkbak;	

    wdt_init(32768*2); 

    uart1_init();
    led_red_off();
    gpio_interrupt_init();
    app_led_reflash();
    charge_reflash();

    
    NVIC_EnableIRQ(PA_IRQn);
    NVIC_EnableIRQ(SysTick_IRQn);
    NVIC_EnableIRQ(TIMER2_IRQn);
    NVIC_EnableIRQ(TIMER3_IRQn);
    NVIC_EnableIRQ(UART0_IRQn);

    //buzzer_sound(10,10,4);
#endif
}




/************************************
功能描述：定时器 10ms
输入参数：无
输出返回：无
*************************************/
void standby_mode_timer(void)
{
    if(charge_is_full() || charge_is_charging() /*|| (_timer_for_press>0 && _timer_for_press<6000)*/ || buzzer_is_finish()==BOOL_FALSE)
    {
        _timer = STANDBY_TIME;
        standby_timer = NO_STANDBY_TIME;
    }
    else
    {
        if(standby_timer > 0)
        {
            --standby_timer;
            _timer = STANDBY_TIME;
        }
        else
        {
            if(0)//(offline_program_state == OFFLINE_PROGRAM_STATE_IDLE)
            {
                //printf("standby\n");
                //standby_mode_interval_action();
                //printf("standby exit\n");
                if(_timer > 0)
                {
                    --_timer;
                    if(_timer == 0)
                    {
                       // printf("standby2\n");
                    //   standby_mode_action();//调试网关临时关闭
                       // printf("standby2 exit\n");
                       standby_timer = NO_STANDBY_TIME;
                    }
                }        
            }
            else
            {
                _timer = STANDBY_TIME;
            }
        }
    }
}

/************************************
功能描述：计时复位
输入参数：无
输出返回：无
*************************************/
void standby_mode_recount(void)
{
    //_timer = STANDBY_TIME;
}

void standby_mode_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();
#if HARDWARE_VERSION == HARDWARE_VERSION_1
    GPIO_InitStruct.Pin = GPIO_PIN_1;

#elif HARDWARE_VERSION == HARDWARE_VERSION_2
    GPIO_InitStruct.Pin = GPIO_PIN_2;

#endif 
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    POWER_OFF() ;
}

