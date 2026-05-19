/*************************************************************
程序功能：CAT.1智慧电源ONCO
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_4g_io.h"
#define PWR_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET)
#define PWR_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET)
#define RESET_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET)
#define RESET_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET)

/************************************
功能描述：pwr打开
输入参数：无
输出返回：无
*************************************/
void pwr_on(void)
{
   PWR_ON();
}

/************************************
功能描述：pwr关闭
输入参数：无
输出返回：无
*************************************/
void pwr_off(void)
{
   PWR_OFF();
}
/************************************
功能描述：reset
输入参数：无
输出返回：无
*************************************/
void reset_on(void)
{
   RESET_ON();//复位状态
}

/************************************
功能描述：reset
输入参数：无
输出返回：无
*************************************/
void reset_off(void)
{
   RESET_OFF();//工作状态
}

/************************************
功能描述：初始化
输入参数：无
输出返回：无
*************************************/
void hw_4g_io_init(void)
{ 
  //4模块启动器，PWR=1，RESET=0，DTR=0，
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PB9     ------> PWR
    PB8     ------> DTR
    PB4     ------> 4G_RESET//PB4复位会产生高电平
    PB3 /  PC15  ------> 4G_RESET
    */
    
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    RESET_OFF(); 
}

