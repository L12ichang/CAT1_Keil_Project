/*************************************************************
程序功能：CAT.1智慧电源
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_tim4_pwm2.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_tim.h"
#include "stm32f1xx_hal_tim_ex.h"

/* 定义TIM句柄 */
#define PWM_CYCLE       370 //2.7k

#if 0
void hw_tim4_pwm2_set_on(void)
{
    TIM4->CCR2 = PWM_CYCLE/2;
}

void hw_tim4_pwm2_set_off(void)
{
    TIM4->CCR2 = 0xffffffff;
}

void hw_tim4_pwm2_init(void)
{
    TIM_HandleTypeDef htim4 = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};


    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);



    
    /* TIM4外设时钟使能 */
    __HAL_RCC_TIM4_CLK_ENABLE();
    
    /* 初始化TIM4基本参数 */
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 72 - 1;           // TIM4时钟频率为72MHz
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = PWM_CYCLE - 1;           // PWM频率为2.7KHz
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
    {
        Error_Handler();
    }
    
    /* 配置TIM4为PWM模式2 */
    sConfigOC.OCMode = TIM_OCMODE_PWM2;
    sConfigOC.Pulse = 0xffffffff;             // 占空比为50%
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
      Error_Handler();
    }
    
    /* 配置TIM4主模式 */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
    {
      Error_Handler();
    }
    
    /* 启动PWM输出 */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
}
#else
//与输入捕获兼容
#if HARDWARE_VERSION == HARDWARE_VERSION_1
void hw_tim4_pwm2_set_on(void)
{
    u32 tmpcr1 = TIM4->CR1;
    TIM4->ARR = PWM_CYCLE - 1;           // PWM频率为2.7KHz
    TIM4->CCR2 = PWM_CYCLE/2;
    MODIFY_REG(tmpcr1, TIM_CR1_ARPE, TIM_AUTORELOAD_PRELOAD_ENABLE);    
    TIM4->CR1 = tmpcr1;
}

void hw_tim4_pwm2_set_off(void)
{
    u32 tmpcr1 = TIM4->CR1;
    TIM4->ARR = 0xffff;         
    TIM4->CCR2 = 0xffffffff;
    MODIFY_REG(tmpcr1, TIM_CR1_ARPE, TIM_AUTORELOAD_PRELOAD_DISABLE);    
    TIM4->CR1 = tmpcr1;
}
#endif

void hw_tim4_pwm2_init(void)
{
    TIM_HandleTypeDef htim4 = {0};



    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);



    
    /* TIM4外设时钟使能 */
    __HAL_RCC_TIM4_CLK_ENABLE();
    
    /* 初始化TIM4基本参数 */
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 72 - 1;           // TIM4时钟频率为72MHz
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    // htim4.Init.Period = PWM_CYCLE - 1;           // PWM频率为2.7KHz
    htim4.Init.Period = 0xffff;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
    {
        Error_Handler();
    }
#if HARDWARE_VERSION == HARDWARE_VERSION_1
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};		
    /* 配置TIM4为PWM模式2 */
    sConfigOC.OCMode = TIM_OCMODE_PWM2;
    sConfigOC.Pulse = 0xffffffff;             // 占空比为0%
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
      Error_Handler();
    }
    
    /* 配置TIM4主模式 */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
    {
      Error_Handler();
    }
    
    /* 启动PWM输出 */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
    #endif
}
#endif
