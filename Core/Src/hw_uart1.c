#include "hw_uart1.h"
#include "Portable.h"
UART_HandleTypeDef huart1;
/* USART1单字节中断接收缓冲。HAL_UART_IRQHandler的UART_Receive_IT已把DR数据读入此变量，
   回调直接使用，不再二次读DR（二次读在RXNE清后可能读到新字节或残留值，导致丢/重字节）。 */
static u8 uart1_rx_byte;
static u8 * _pTx;
static u16 _tx_length;
static u16 _tx_index;
void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        saveUsartByte(uart1_rx_byte);
        HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
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
u8 hw_uart1_send_with_result(u8* buf, u32 length)
{
      HAL_StatusTypeDef status;

      if (buf == 0 || length == 0U || length > 0xFFFFU)
      {
          return (u8)HAL_ERROR;
      }
      if (huart1.gState != HAL_UART_STATE_READY)
      {
          return (u8)HAL_BUSY;
      }
      _tx_length = (u16)length;
      _pTx = buf;
      _tx_index = 0;
      status = HAL_UART_Transmit_IT(&huart1, (uint8_t*)_pTx , 1);
      if (status != HAL_OK)
      {
          _tx_length = 0;
          _pTx = 0;
          _tx_index = 0;
      }
      return (u8)status;
}

void hw_uart1_send(u8* buf, u32 length)
{
      _tx_length = length;
      _pTx = buf;
      _tx_index = 0;
      HAL_UART_Transmit_IT(&huart1, (uint8_t*)_pTx , 1);
}


/* USART1(4G模组口)接收恢复：ORE/FE/NE 错误后HAL会关闭RXNEIE/EIE中断（阻塞性ORE走UART_EndRxTransfer），
   必须清除错误标志并重新挂起接收，否则USART1将永久收不到模组数据（假在线）。 */
void hw_uart1_resume_rx(void)
{
    /* F1上ORE/FE/NE共用"读SR再读DR"序列清除，一次调用即清全部；
       连续多次清会在清错期间把新到达的字节（DR）读走丢弃。 */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    (void)HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
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
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);

}


