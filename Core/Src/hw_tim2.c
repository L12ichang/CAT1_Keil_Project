/*************************************************************
程序功能：CAT.1智慧电源
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_tim2.h"
#include "sys_tick.h"
#include "hw_dali.h"
#include "app_led.h"


tim2_en tim2_user=TIM2_DALI;

TIM_HandleTypeDef htim2;

/* 执行完hw_tim2_start再开始计时到达hw_tim2_timeout_callback的 do soming延迟5us。函数已经补偿要补回来   */
#define DELAY_TIME      2


void TIM2_IRQHandler(void)
{
	//结束自己要做的事情
  	HAL_TIM_IRQHandler(&htim2);
}


void hw_tim2_timeout_callback(TIM_HandleTypeDef *htim)
{
    HAL_TIM_Base_Stop_IT(htim);
    //do soming
    
    //HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET); 
    switch (tim2_user)
    {
        case TIM2_DALI:
        {
         //   hw_dali_tx_handle();
        }
        break;
        case TIM2_LED:
        {
           // app_led_timeout();
        }
        break;
    }
}

/* 2us一个单位 */
void hw_tim2_start(u16 t)
{
    tim2_user = TIM2_DALI;
    TIM2->CNT = 0;
    TIM2->ARR = t-1-DELAY_TIME;    
    __HAL_TIM_CLEAR_FLAG(&htim2,TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim2);
}

/* 2us一个单位 */
void hw_tim2_start_led(u16 t)
{
    tim2_user = TIM2_LED;
    TIM2->CNT = 0;
    TIM2->ARR = t-1-DELAY_TIME;    
    __HAL_TIM_CLEAR_FLAG(&htim2,TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim2);
}


void hw_tim2_init(void)
{
    //TIM_ClockConfigTypeDef sClockSourceConfig = {0};
//    TIM_MasterConfigTypeDef sMasterConfig = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 144 - 1; // 
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xffff; // 计数器的最大数为10000-1，即1秒钟
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }


    HAL_TIM_RegisterCallback(&htim2, HAL_TIM_PERIOD_ELAPSED_CB_ID, hw_tim2_timeout_callback);

    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

   // HAL_TIM_Base_Start_IT(&htim2);



    

    
}




//void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
//{
//    if (htim == &htim2)
//    {
//       // sys_tick_cycle_handle();
//    }
//}




