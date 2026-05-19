#include "hw_uart1.h"
#include "Portable.h"
UART_HandleTypeDef huart1;
u8 rx1_buffer[1024];
static int rx_index = 0;    //此处对外关闭
static u8 * _pTx;
static u16 _tx_length;
static u16 _tx_index;
static u8 _timer;
void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart)
{  
     u8 dat;
     if (huart->Instance == USART1)
    { 
        if (rx_index < sizeof(rx1_buffer))
        { 
           dat = (u8)huart->Instance->DR;         
            saveUsartByte(dat);
        }
        HAL_UART_Receive_IT(&huart1, (uint8_t*)rx1_buffer + rx_index, 1);
    }
}

void HAL_UART_Tx1CpltCallback(UART_HandleTypeDef *huart)
{
      if (huart->Instance == USART1)
      {
             _tx_index++;
            if (_tx_index < _tx_length)
            {
              HAL_UART_Transmit_IT(&huart1, (uint8_t*)_pTx + _tx_index, 1);
            }
      }
}
void hw_uart1_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
        if(_timer == 0)
        {
            printf_buf(rx1_buffer, rx_index);
            hw_uart1_send(rx1_buffer, rx_index);
            rx_index = 0;
        }
    }
}

void hw_uart1_send(u8* buf, u32 length)
{
      _tx_length = length;
      _pTx = buf;
      _tx_index = 0;
      HAL_UART_Transmit_IT(&huart1, (uint8_t*)_pTx , 1);
}


void hw_uart1_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
  //USART1_TX   PA.9
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  //USART1_RX	  PA.10
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_UART_Receive_IT(&huart1, (uint8_t*)rx1_buffer, 1);

}


