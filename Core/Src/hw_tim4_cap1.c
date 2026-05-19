/*************************************************************
程序功能：输入捕捉，用于dali通信
开发环境：keil 5.37
芯片型号：STM32F103RBT6
开发人员：梁庆能
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/
#include "hw_tim4_cap1.h"
#include "hw_dali.h"
#include "sys_tick.h"

static u8 _timer=5;

TIM_HandleTypeDef htim4;
u16 capture_value_bak;
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    u16 t;
    if (htim->Instance == TIM4) 
    {
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) 
        {
            uint16_t capture_value = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
            t = capture_value-capture_value_bak;
            if(t>50)
            {
                if(htim->Instance->CCER&0x02)
                {
                    //下降沿捕获
                    htim->Instance->CCER = htim->Instance->CCER&0xFFFFFFFD;
                    //log_u32(1, capture_value-capture_value_bak);
                    if(_timer == 0)
                    {
                      //  hw_dali_rx(1, 0xffffffff);
                    }
                    else
                    {
                     //   hw_dali_rx(1, t);
                    }
                }
                else
                {
                    //上升沿捕获
                    htim->Instance->CCER = htim->Instance->CCER|0x02;
                    //log_u32(0, capture_value-capture_value_bak);
                    if(_timer == 0)
                    {
                       // hw_dali_rx(0, 0xffffffff);
                    }
                    else
                    {
                        //hw_dali_rx(0, t);
                    }
                }
                capture_value_bak = capture_value;
                
            }
            _timer = 5;

            // 处理输入捕获的计数器值

        }
    }
}

void HAL_TIM_IC_MspInit(TIM_HandleTypeDef* htim_ic)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(htim_ic->Instance==TIM4)
  {
  /* USER CODE BEGIN TIM4_MspInit 0 */

  /* USER CODE END TIM4_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_TIM4_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**TIM4 GPIO Configuration
    PB6     ------> TIM4_CH1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* TIM4 interrupt Init */
    HAL_NVIC_SetPriority(TIM4_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
  /* USER CODE BEGIN TIM4_MspInit 1 */

  /* USER CODE END TIM4_MspInit 1 */
  }

}

/**
* @brief TIM_IC MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param htim_ic: TIM_IC handle pointer
* @retval None
*/
void HAL_TIM_IC_MspDeInit(TIM_HandleTypeDef* htim_ic)
{
  if(htim_ic->Instance==TIM4)
  {
  /* USER CODE BEGIN TIM4_MspDeInit 0 */

  /* USER CODE END TIM4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM4_CLK_DISABLE();

    /**TIM4 GPIO Configuration
    PB6     ------> TIM4_CH1
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);

    /* TIM4 interrupt DeInit */
    HAL_NVIC_DisableIRQ(TIM4_IRQn);
  /* USER CODE BEGIN TIM4_MspDeInit 1 */

  /* USER CODE END TIM4_MspDeInit 1 */
  }

}
void hw_tim4_cap1_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
    }
}

void hw_tim4_cap1_stop(void)
{
    HAL_TIM_IC_Stop_IT(&htim4, TIM_CHANNEL_1);
}
void hw_tim4_cap1_start(void)
{
    if(HAL_GPIO_ReadPin(GPIOB ,GPIO_Pin_6) == GPIO_PIN_SET)   //key3
    {
        //上升沿捕获
        htim4.Instance->CCER = htim4.Instance->CCER|0x02;
    }
    else
    {
        //下降沿捕获
        htim4.Instance->CCER = htim4.Instance->CCER&0xFFFFFFFD;
    }
    HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);
}

void hw_tim4_cap1_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);


    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_IC_InitTypeDef sConfigIC = {0};
    
    /* USER CODE BEGIN TIM4_Init 1 */
    
    /* USER CODE END TIM4_Init 1 */
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 71;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 65535;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&htim4) != HAL_OK)
    {
      Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
    {
      Error_Handler();
    }
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0;
    if (HAL_TIM_IC_ConfigChannel(&htim4, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
    {
      Error_Handler();
    }
    /* TIM4 Start */
   // HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);


    
}
