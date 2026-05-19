/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   This file contains all the function prototypes for
  *          the adc.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_hal.h"




typedef enum
{
    ADC_CH08_NTC=0,
    ADC_CH09_VO,
    ADC_CH04_VO,
    ADC_CH06_VO,   
    ADC_CH_MAX
}adc_channel_en;
/*
    PB0     ------> ADC1_IN8   //温度
    PB1     ------> ADC1_IN9   //Vo
    PA4     ------> ADC1_IN4   //漏电流
    PA6     ------> ADC1_IN6   //Io
*/
extern	uint32_t ADC_Value1,ADC_Value2,ADC_Value3,ADC_Value4;  // 用于保存ADC的值 
extern	uint16_t adc_buf[4];
extern uint32_t adc_average[ADC_CH_MAX];
extern void adc_process(void);
extern void adc_process_timer(void);
/* USER CODE B



EGIN Includes */

/* USER CODE END Includes */

extern ADC_HandleTypeDef hadc1;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_ADC1_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

