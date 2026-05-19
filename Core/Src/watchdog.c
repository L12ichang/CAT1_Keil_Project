/*************************************************************
程序功能：看门狗
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/

#include "watchdog.h"


IWDG_HandleTypeDef hiwdg;




void watchdog_feed_dog(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}


void watchdog_init(void)
{
    /* IWDG initialization */
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256; // 设置预分频器，使看门狗时钟为40kHz, 256分频。
    hiwdg.Init.Reload = 312; // 设置超时时间为2秒312
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) 
    {
        Error_Handler();
    }    
}



