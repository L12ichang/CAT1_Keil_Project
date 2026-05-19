/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
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
/* Includes ------------------------------------------------------------------*/
#include "adc.h"
#include "stm32f1xx_hal.h"
#include "common.h"
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;





//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++加滤波+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


#define HW_ADC_TOTAL           4096
#define HW_ADC_REMAIN4BIT      16

/*

Y(n)=αX(n) + (1-α)Y(n-1)
式中:a=滤波系数;X(n)=本次采样值;Y(n-1)=上次滤波输出值;Y(n)=本次滤波输出值。

*/

//alfa不要少于0.01，否则alfa越小静差越大

#define alfa1     0.01     //波系数a (0.0-1.0)
#define alfa2     0.02     //波系数a (0.0-1.0)
#define alfa3     0.02     //波系数a (0.0-1.0)
#define alfa4     0.02     //波系数a (0.0-1.0)
#define alfa5     0.05     //波系数a (0.0-1.0)
#define alfa6     0.50     //波系数a (0.0-1.0)


#define Q15_ALFA1        (u32)(alfa1*65536)
#define Q15_1_ALFA1      (u32)((1-alfa1)*65536)
#define Q15_ALFA2        (u32)(alfa2*65536)
#define Q15_1_ALFA2      (u32)((1-alfa2)*65536)
#define Q15_ALFA3        (u32)(alfa3*65536)
#define Q15_1_ALFA3      (u32)((1-alfa3)*65536)
#define Q15_ALFA4        (u32)(alfa4*65536)
#define Q15_1_ALFA4      (u32)((1-alfa4)*65536)
#define Q15_ALFA5        (u32)(alfa5*65536)
#define Q15_1_ALFA5      (u32)((1-alfa5)*65536)
#define Q15_ALFA6        (u32)(alfa6*65536)
#define Q15_1_ALFA6      (u32)((1-alfa6)*65536)

static const u32 Q15_ALFA[ADC_CH_MAX] = 
{
 Q15_ALFA1,Q15_ALFA1,Q15_ALFA6,Q15_ALFA1
};

static const u32 Q15_1_ALFA[ADC_CH_MAX] = 
{
    Q15_1_ALFA1,Q15_1_ALFA1,Q15_1_ALFA6,Q15_1_ALFA1
};



static u32 _old_data[ADC_CH_MAX];

void first_order_filter_set_old_data(adc_channel_en ch, u16 old)
{
    _old_data[ch] = (u32)old*16;//原移植方案
      // _old_data[ch] = (u32)old*3300/4095;
}

/************************************
功能描述：一阶滤波，电压调光使用
输入参数：滤波前的AD值
输出返回：滤波后的值
*************************************/
u16 first_order_filter(adc_channel_en ch, u16 new_data)
{
    u32 tmp;

    tmp = Q15_ALFA[ch]*new_data + (Q15_1_ALFA[ch]*_old_data[ch]+HW_ADC_REMAIN4BIT/2)/HW_ADC_REMAIN4BIT;  //old data放大了16位，运算完缩小16倍
    _old_data[ch] = (tmp+HW_ADC_TOTAL/2)/HW_ADC_TOTAL; //adc是12位，还有4位（16倍）用来放大数据
 
    return (u32h(tmp));  
    
}



u16 vdda;  //单位mV
extern u8 jjj;
static boolean_en _first_take_adc = BOOL_TRUE;




//++++++++++++++++++++++++++++++++++++++++++++++++++++++加滤波++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++





uint16_t adc_timer=0;
uint32_t adc_average[ADC_CH_MAX];
u8 adc_stat=1;
// 1.定义ADC存的数组 应该声明16位变量，因为CubeMX设置ADC_DMA的时候设置为half world
	uint16_t adc_buf[4];
	uint32_t ADC_Value1,ADC_Value2,ADC_Value3,ADC_Value4;  // 用于保存ADC的值 
   








/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 4;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
   /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
     /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */
//	HAL_ADC_Start_DMA((ADC_HandleTypeDef*)&hadc1,  (uint32_t*) adc_buf, (uint32_t) 4);  //因为启用了3个ADC通道，所以length为4
  /* USER CODE END ADC1_Init 2 */

}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */
    /* ADC1 clock enable */
    __HAL_RCC_ADC1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC1 GPIO Configuration

    PB0     ------> ADC1_IN8   //温度
    PB1     ------> ADC1_IN9   //Vo
    PA4     ------> ADC1_IN4   //漏电流
    PA6     ------> ADC1_IN6   //Io
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
      
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* ADC1 DMA Init */
    /* ADC1 Init */
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc1);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{

  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC1_CLK_DISABLE();

    /**ADC1 GPIO Configuration

    PB0     ------> ADC1_IN8   //温度
    PB1     ------> ADC1_IN9   //Vo
    PA4     ------> ADC1_IN4   //漏电流
    PA6     ------> ADC1_IN6   //Io
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0|GPIO_PIN_1);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4|GPIO_PIN_6);
    /* ADC1 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */


void adc_process_timer(void)
{  
    ++adc_timer;
}

void adc_process(void)
{
    
    if(adc_stat)
    {       
        adc_stat=0;
		HAL_ADC_Start_DMA((ADC_HandleTypeDef*)&hadc1,  (uint32_t*) adc_buf, (uint32_t) 4);  
    }	  
        

        adc_channel_en i;        
        if(_first_take_adc == BOOL_TRUE)
        {
            _first_take_adc = BOOL_FALSE;
             for(i=ADC_CH08_NTC; i<ADC_CH_MAX; i++)
             {
                first_order_filter_set_old_data(i, adc_buf[i]);
             }
        }
        for(i=ADC_CH08_NTC; i<ADC_CH_MAX; i++)
        {
             adc_average[i] = first_order_filter(i,adc_buf[i]);
            
        }  
        ADC_Value1 = ((float)adc_average[ADC_CH08_NTC]/4095)*3300;  //滤波后的实际电压,单位是mV 
		ADC_Value2 = ((float)adc_average[ADC_CH09_VO]/4095)*3300;  
        ADC_Value3 = ((float)adc_average[ADC_CH04_VO]/4095)*3300;  
        ADC_Value4 = ((float)adc_average[ADC_CH06_VO]/4095)*3300;
        
        if(adc_timer>100)
        { 
          adc_timer=0;   
           
     		// 4.滤波后的实际电压
            /*
          printf("adc_buf[2]=%dmA \r\n",(u16)((float)(adc_buf[2])/4095*116));    // adc_buf[2]/4095* 3300/20/1.414   
          printf("adc_buf[2]=%dmV \r\n",(u16)((float)adc_buf[2]/4095*3300));       
          printf("Vo  =%dmV \r\n", (u16)((float)ADC_Value2*39.75/0.75));      //Vo    
          printf("ADC_Value3=%dmV \r\n",(u16)ADC_Value3);
          printf("Io=%dmA \r\n",(u16)((float)ADC_Value4/30/8.3*1000)); //Io
           */
            /*
                u16     VO_voltage = (u16)((float)ADC_Value2*39.75/0.75); //参考电压3.3V  电压单位mV
                u16     Io_value = (u16)((float)ADC_Value4/30/8.3*1000);  //参考电压3.3V  电流单位mA
                u16     Po_value =(u16)((float)VO_voltage*Io_value/1000000);
                 printf(" VO_voltage=%dmV\n",VO_voltage);
                 printf(" Io_value=%dmA\n",Io_value);
                 printf(" Po_value=%dW\n",Po_value);
            */
 
    /*
        printf("adc_buf[0]=%dmV \r\n",adc_buf[0]);
        printf("adc_buf[1]=%dmV \r\n",adc_buf[1]);
        printf("adc_buf[2]=%dmV \r\n",adc_buf[2]);
        printf("adc_buf[3]=%dmV \r\n",adc_buf[3]); 
        
        printf("ADC_Value1=%dmV \r\n",ADC_Value1);
        printf("ADC_Value2=%dmV \r\n",ADC_Value2);
       
        printf("ADC_Value4=%dmV \r\n",ADC_Value4);
        */
     // printf(" ac_voltage_8209=%d\r\n",ac_voltage_8209);   //交流电的电压，单位 0.1V
     // printf(" ac_current=%d\r\n",Z_ac_current);   // 交流电的电流，单位 mA   
        
      }  
        
        
        
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++       
        
        
        extern  int8_t  flag_adc_ntc1; 
        flag_adc_ntc1=1;
        extern void  NtcTempCalc1(void);
        NtcTempCalc1();
  
 }
























/* USER CODE END 1 */
