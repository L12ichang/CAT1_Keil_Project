/*************************************************************
程序功能：CAT.1智慧电源
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "charge.h"


typedef struct
{
    u8 charging:1;
    u8 full:1;
}charge_status_st;


typedef union
{
    charge_status_st status;
    u8 byte;
}charge_status_un;


charge_status_un charge_status;


void Sems_filter(u8 in, u8 *out)
{
  static u8 _inBackup;
  static u8 _cnt = 0;
  if (in == _inBackup)
  {
    if (_cnt < 5)
    {
      ++_cnt;
    }
    else
    {
      *out = in;
    }
  }
  else
  {
    _inBackup = in;
    _cnt = 0;
  }
}


/************************************
功能描述：定时器 10 ms执行一次
输入参数：无
输出返回：无
*************************************/
void charge_timer(void)
{
    u8 tmp=0;
    if(HAL_GPIO_ReadPin(GPIOB, GPIO_Pin_15) == GPIO_PIN_RESET) //charging
    {
        tmp |= 1;
    }
    if(HAL_GPIO_ReadPin(GPIOC, GPIO_Pin_6) == GPIO_PIN_RESET)//full
    {
        tmp |= 2;
    }
    Sems_filter(tmp, &charge_status.byte);
}


/************************************
功能描述：不滤波，立刻更新
输入参数：无
输出返回：无
*************************************/
void charge_reflash(void)
{
    u8 tmp=0;
    if(HAL_GPIO_ReadPin(GPIOB, GPIO_Pin_15) == GPIO_PIN_RESET) //charging
    {
        tmp |= 1;
    }
    if(HAL_GPIO_ReadPin(GPIOC, GPIO_Pin_6) == GPIO_PIN_RESET)//full
    {
        tmp |= 2;
    }
    charge_status.byte = tmp;
}

/************************************
功能描述：正在充电吗
输入参数：无
输出返回：是返回BOOL_TRUE， 否则返回 BOOL_FALSE
*************************************/
boolean_en charge_is_charging(void)
{
    if(charge_status.status.charging)
    {
        return (BOOL_TRUE);
    }
    else
    {
        return (BOOL_FALSE);
    }
}


/************************************
功能描述：充满电了吗
输入参数：无
输出返回：满电返回BOOL_TRUE， 否则返回 BOOL_FALSE
*************************************/
boolean_en charge_is_full(void)
{
    if(charge_status.status.full)
    {
        return (BOOL_TRUE);
    }
    else
    {
        return (BOOL_FALSE);
    }
}



/************************************
功能描述：充电IC状态初始化
输入参数：无
输出返回：无
*************************************/
void charge_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
	charge_status.byte = 0;
}


