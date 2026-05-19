/*************************************************************
程序功能：CAT.1智慧电源OCO
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "oco.h"
#define OCO_ON()   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET)
#define OCO_OFF()  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)
#define ONCO_ON()    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET)
#define ONCO_OFF()   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET)

/************************************
功能描述：oco打开
输入参数：无
输出返回：无
*************************************/
void oco_on(void)
{
   OCO_ON();
}


/************************************
功能描述：oco关闭
输入参数：无
输出返回：无
*************************************/
void oco_off(void)
{
   OCO_OFF();
}


/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
void oco_init(void)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PC13     ------> OCO
    PC14     ------> ONCO
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);  
    //初始调灭配置
    OCO_OFF();    
    ONCO_ON();   
}

