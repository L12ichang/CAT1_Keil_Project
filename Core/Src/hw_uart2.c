/*************************************************************
程序功能：CAT.1智慧电源 电量计量
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/

/* CAT.1项目保留USART2是为了读取BL0942电参芯片，不启用485业务协议。 */

#include "hw_uart2.h"
#include "hw_uart1.h"



UART_HandleTypeDef huart2;



#ifdef  _4G_CAT_1


static u8 * volatile _buffer;
u8 rx3_buffer[1];// HAL_UART_Receive_IT(&huart2, (uint8_t*)rx3_buffer, 1);中断需要，可能是_buffer指针在初始下是空，无法正常产生中断
//uint8_t u8_buffer[1024];
static volatile u8 _index;
static volatile u8 _tx_length;
static volatile u8 _rx_length;
static volatile hw_bl0942_state_en _bl0942_state =  BL0942_STATE_IDLE;
static volatile u32 hw_uart2_error_count = 0;

#if BL0942_REPRO_TEST_ENABLE
volatile u8  g_bl0942_repro_inject_once = 0;
volatile u8  g_bl0942_repro_force_recover = 0;

volatile u32 g_bl0942_repro_inject_count = 0;
volatile u32 g_bl0942_repro_rx_cplt_count = 0;
volatile u32 g_bl0942_repro_dummy_rx_count = 0;
volatile u32 g_bl0942_repro_error_count = 0;
volatile u32 g_bl0942_repro_ore_count = 0;
volatile u32 g_bl0942_repro_fe_count = 0;
volatile u32 g_bl0942_repro_ne_count = 0;
volatile u32 g_bl0942_repro_last_error = 0;
volatile u32 g_bl0942_repro_tx_fail_count = 0;
volatile u32 g_bl0942_repro_rx_fail_count = 0;
volatile u32 g_bl0942_repro_force_recover_count = 0;
#endif

static void hw_bl0942_uart_abort_rx(void)
{
    (void)HAL_UART_AbortReceive(&huart2);
    (void)__HAL_UART_FLUSH_DRREGISTER(&huart2);
}

static void hw_bl0942_uart_start_rx(void)
{
    if(_rx_length == 0 || _buffer == 0)
    {
        _bl0942_state = BL0942_STATE_READ_READY;
        return;
    }

    (void)HAL_UART_Receive_IT(&huart2, (uint8_t*)_buffer, 1);
}



hw_bl0942_state_en hw_bl0942_get_state(void)
{
    return (_bl0942_state);
}


void hw_bl0942_uart_write(u8 * buf, u8 length)
{
    _bl0942_state = BL0942_STATE_WRITE;
    _buffer = buf;
    _index = 1;
    _tx_length = length;    
    HAL_UART_Transmit_IT(&huart2, (uint8_t*)_buffer, 1);
}


//buf放发送的两个字节，接收到的数据也放里面。length 接收的数据长度，包括校验值
void hw_bl0942_uart_read(u8 * buf, u8 length)
{
#if BL0942_REPRO_TEST_ENABLE
    HAL_StatusTypeDef uart_ret;
#endif

    _bl0942_state = BL0942_STATE_READ_TX;
    _buffer = buf;
    _index = 1;
    _tx_length = 2;
    _rx_length = length;
    hw_bl0942_uart_abort_rx();
#if BL0942_REPRO_TEST_ENABLE
    uart_ret = HAL_UART_Transmit_IT(&huart2, (uint8_t*)_buffer, 1);
    if(uart_ret != HAL_OK)
    {
        g_bl0942_repro_tx_fail_count++;
    }
#else
    HAL_UART_Transmit_IT(&huart2, (uint8_t*)_buffer, 1);
#endif

}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
#if BL0942_REPRO_TEST_ENABLE
          g_bl0942_repro_rx_cplt_count++;
#endif

          if(_bl0942_state == BL0942_STATE_READ_RX)
            {
                if(_index < _rx_length)
                {
                    ++_index;
                }

#if BL0942_REPRO_TEST_ENABLE
                /*
                 * One-shot real ORE injection: after the fourth received
                 * byte, deliberately leave RX unarmed.  BL0942 continues
                 * transmitting, so USART2 hardware can assert ORE.
                 */
                if((g_bl0942_repro_inject_once != 0U) && (_index == 4U))
                {
                    g_bl0942_repro_inject_once = 0U;
                    g_bl0942_repro_inject_count++;
                    return;
                }
#endif

                if(_index >= _rx_length)
                {
                    _bl0942_state = BL0942_STATE_READ_READY;
                }
                else
                {
#if BL0942_REPRO_TEST_ENABLE
                    if(HAL_UART_Receive_IT(&huart2,
                                           (uint8_t*)_buffer + _index,
                                           1) != HAL_OK)
                    {
                        g_bl0942_repro_rx_fail_count++;
                    }
#else
                    HAL_UART_Receive_IT(&huart2, (uint8_t*)_buffer + _index, 1);
#endif
                }
            }
          else
            {
#if BL0942_REPRO_TEST_ENABLE
                g_bl0942_repro_dummy_rx_count++;
#endif
            }
  }
  HAL_UART_Rx1CpltCallback(huart);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
       if(_index < _tx_length)
    {
     
        HAL_UART_Transmit_IT(&huart2, (uint8_t*)_buffer + _index, 1);
        ++_index;
    }
    else
    {
        if(_bl0942_state == BL0942_STATE_READ_TX)
        {
            _bl0942_state = BL0942_STATE_READ_RX;
            _index = 0;
            hw_bl0942_uart_start_rx();
        }
        else
        {
            _bl0942_state = BL0942_STATE_IDLE;
        }
        
    }

}
  
  HAL_UART_Tx1CpltCallback(huart);
 
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
#if BL0942_REPRO_TEST_ENABLE
      g_bl0942_repro_error_count++;
      g_bl0942_repro_last_error = huart->ErrorCode;

      if((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U)
      {
          g_bl0942_repro_ore_count++;
      }
      if((huart->ErrorCode & HAL_UART_ERROR_FE) != 0U)
      {
          g_bl0942_repro_fe_count++;
      }
      if((huart->ErrorCode & HAL_UART_ERROR_NE) != 0U)
      {
          g_bl0942_repro_ne_count++;
      }
#endif

      hw_uart2_error_count++;
      _bl0942_state = BL0942_STATE_IDLE;
      _index = 0;
      hw_bl0942_uart_abort_rx();
      /* F1上ORE/FE/NE共用"读SR再读DR"序列清除，一次调用即清全部 */
      __HAL_UART_CLEAR_OREFLAG(&huart2);
      (void)HAL_UART_Receive_IT(&huart2, (uint8_t*)rx3_buffer, 1);
  }
  else if (huart->Instance == USART1)
  {
      /* USART1(4G模组口)发生ORE/FE/NE错误后，HAL会关闭RXNEIE/EIE中断（阻塞性ORE走UART_EndRxTransfer）。
         必须清除错误标志并重新挂起接收，否则USART1将永久收不到模组数据（假在线）。 */
      hw_uart1_resume_rx();
  }
}

u32 hw_uart2_get_error_count(void)
{
    return hw_uart2_error_count;
}

#if BL0942_REPRO_TEST_ENABLE
void hw_bl0942_repro_force_recover(void)
{
    (void)HAL_UART_Abort(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    huart2.ErrorCode = HAL_UART_ERROR_NONE;

    _bl0942_state = BL0942_STATE_IDLE;
    _index = 0;
    _tx_length = 0;
    _rx_length = 0;
    _buffer = 0;

    g_bl0942_repro_force_recover_count++;
}
#endif




#else
u8 rx2_buffer[1024];
#define RX_FROM_485PORT(buf, length)  rx_packet_from_485port(buf, length)




//char tx_buffer[100];
int rx_index = 0;
//int tx_index = 0;

static u8 * _pTx;
static u16 _tx_length;
static u16 _tx_index;
static u8 _timer;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    if (rx_index < sizeof(rx2_buffer))
    {
      rx2_buffer[rx_index++] = (char)huart->Instance->DR;
    }
     HAL_UART_Receive_IT(&huart2, (uint8_t*)rx2_buffer + rx_index, 1);
    _timer = 10;
  }
  HAL_UART_Rx1CpltCallback(huart);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    _tx_index++;
    if (_tx_index < _tx_length)
    {
      HAL_UART_Transmit_IT(&huart2, (uint8_t*)_pTx + _tx_index, 1);
    }
    else
    {        
        RS485_TX_DISABLE();
    }
  }
  HAL_UART_Tx1CpltCallback(huart);
  
}


void hw_uart2_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
        if(_timer == 0)
        {
           // hw_uart2_send(rx2_buffer, rx_index);
            
            RX_FROM_485PORT(rx2_buffer,rx_index);
            rx_index = 0;
        }
    }
}

void hw_uart2_send(u8* buf, u16 length)
{
    RS485_TX_ENABLE();
    _tx_length = length;
    _pTx = buf;
    _tx_index = 0;
    HAL_UART_Transmit_IT(&huart2, (uint8_t*)_pTx , 1);
}

#endif



void hw_uart2_init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 2 */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  #ifndef  _4G_CAT_1   
    RS485_TX_DISABLE();
  #endif
    
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
   #ifdef  _4G_CAT_1
  huart2.Init.BaudRate = 4800;
  #else
   huart2.Init.BaudRate = 2400;
  #endif
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */
   
  #ifdef  _4G_CAT_1  
  HAL_UART_Receive_IT(&huart2, (uint8_t*)rx3_buffer, 1);// HAL_UART_Receive_IT(&huart2, (uint8_t*)rx3_buffer, 1);中断需要，可能是_buffer指针在初始下是空，无法正常产生中断
  #else
   HAL_UART_Receive_IT(&huart2, (uint8_t*)rx2_buffer, 1);
  #endif
  
  
  
  /* USER CODE END USART2_Init 2 */

}



#if 0
/**
* @brief UART MSP Initialization
* This function configures the hardware resources used in this example
* @param huart: UART handle pointer
* @retval None
*/
void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(huart->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */
    HAL_UART_Receive_IT(&huart2, (uint8_t*)rx2_buffer, 1);

  /* USER CODE END USART2_MspInit 1 */
  }

}

/**
* @brief UART MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param huart: UART handle pointer
* @retval None
*/
void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
  if(huart->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);

    /* USART2 interrupt DeInit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }

}

#endif





