/*************************************************************
程序功能：CAT.1智慧电源PWM 输出CCO 
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_tim1_pwm2.h"
#include "oco.h"
#include "factory_user_data.h"
#include "sys_calibration_service.h"
TIM_HandleTypeDef htim1;

u8 pwm_on;
static u16 _pwm_logical_output;

#define TIM1_PWM2_CYCLE     3600

#define PWM_CYCLE       370 //2.7k

void hw_tim1_pwm2_set_on(void)
{
   // TIM1->CCR2 = PWM_CYCLE/2;
}

void hw_tim1_pwm2_set_off(void)
{
  //  TIM1->CCR2 = 0xffffffff;
}

#define PWM_MAX        1000
#define PWM_OFFSET   OP_PWM_OFFSET //由于光耦的延迟问题增加3%输出
#define PWM_USEFUL_RANGE    (u16)(PWM_MAX-PWM_OFFSET)  //

void hw_tim1_pwm2_set_PWM_OUT(u16 pwm)//pwm输出
{
    /* 最后一道硬件出口门禁：先归零，再决定是否允许OCO导通。 */
    if (pwm > 0U && sys_calibration_service_is_boot_inhibited() == BOOL_TRUE)
    {
        pwm = 0U;
    }
    if(pwm>1000)
    {
     pwm=1000;
    }
    if(pwm>0)        
    {
      oco_on();
            pwm_on = 1;
    }
    else
    {
     oco_off();
          pwm_on = 0;
    }

   _pwm_logical_output = pwm;
    
#if APP_PWM_DEBUG_ENABLE
   printf("pwm=%d\r\n",pwm);
#endif
   __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pwm+PWM_OFFSET);  //负逻辑

}

u16 hw_tim1_pwm2_get_logical_pwm(void)
{
    return _pwm_logical_output;
}

u16 hw_tim1_pwm2_get_ccr(void)
{
    return (u16)__HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_1);
}

u8 hw_tim1_pwm2_get_oco_on(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) ? 1U : 0U;
}

void hw_tim1_pwm2_init(void)
{
    /* USER CODE BEGIN TIM1_Init 0 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    __HAL_RCC_TIM1_CLK_ENABLE();
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 71;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 999;//1000 1KHZ 2000 500HZ;// htim1.Init.Period = 370;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
    {
      Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
    {
      Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM2;
    sConfigOC.Pulse = 500;//150;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
      Error_Handler();
    }
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
     __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);  //0%输出

}

