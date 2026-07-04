/*************************************************************
程序功能：串口打印
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_uart3.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdarg.h>

#define RCC_APB1Periph_USART3            ((uint32_t)0x00040000)
#define RCC_APB2Periph_GPIOB             ((uint32_t)0x00000008)
#define RCC_APB2Periph_AFIO              ((uint32_t)0x00000001)


UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;
DMA_HandleTypeDef sg_USART3_TxDMAHandleStruct;
DMA_HandleTypeDef hdma_usart1_tx;
DMA_HandleTypeDef hdma_usart3_tx;

static boolean_en volatile _flag_txing = BOOL_FALSE;

char buffer[50] __attribute__ ((aligned(4)));;

#define DMA_BUFFER_SIZE 768
char dma_buffer[DMA_BUFFER_SIZE];
static volatile u8 _timer; 
void hw_uart3_dma_tx(u8* buf, u16 length) 
{    
    //_flag_txing = BOOL_TRUE;
    //DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CCR &= (uint16_t)(~1);
    DMA1_Channel2->CNDTR = length;
    DMA1_Channel2->CMAR = (u32)buf;
    //DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA1_Channel2->CCR |= 1;
}


void hw_uart3_dma_tx_complete(void)
{
    _flag_txing = BOOL_FALSE;
}

void hw_uart3_timer(void)
{
    if(_timer > 0)
    {
        --_timer;
    }
}

int dma_printf(const char* format, ...)
{
#if (!APP_LOG_ENABLE) && (!APP_OTA_LOG_ENABLE)
    (void)format;
    return 0;
#else
    int n;
    va_list args;

    if(_flag_txing == BOOL_TRUE)
    {
        return 0;
    }
    va_start(args, format);
    n = vsnprintf(dma_buffer, DMA_BUFFER_SIZE - 1, format, args);
    va_end(args);
    if(n > 0)
    {
        if(n >= DMA_BUFFER_SIZE)
        {
            n = DMA_BUFFER_SIZE - 1;
        }

        if(HAL_UART_Transmit_DMA(&huart3, (uint8_t *)dma_buffer, (uint16_t)n) == HAL_OK)
        {
            _flag_txing = BOOL_TRUE;
        }
        else
        {
            _flag_txing = BOOL_FALSE;
        }
    }
    return n;
#endif
}
void USART3_UART_Transmit(uint8_t *pData, uint16_t len)
{
    if (__HAL_DMA_GET_FLAG(&huart3, DMA_FLAG_TC2))
    {
        __HAL_DMA_CLEAR_FLAG(&huart3, DMA_FLAG_TC2);    /* 清除DMA1_Steam7传输完成标志 */
        HAL_UART_DMAStop(&huart3);                      /* 传输完成以后关闭串口DMA */             
    }
	    
    HAL_UART_Transmit_DMA(&huart3, pData, len);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART3)
  {
    /* Peripheral clock disable */
    __HAL_RCC_USART3_CLK_DISABLE();

    /**USART3 GPIO Configuration
    PB10     ------> USART3_TX
    PB11     ------> USART3_RX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10);

    HAL_DMA_DeInit(uartHandle->hdmatx);

  }
}


u8 debug8=0;
u8 debug8b=0;
u32 debug32b;
u32 debug32;
void HAL_DMA_TxCpltCallback(DMA_HandleTypeDef *dma)
{
    if(dma == &hdma_usart3_tx)
    {
        _flag_txing = BOOL_FALSE;
    }
}

void hw_uart3_init(void)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();   
    __HAL_RCC_AFIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);  
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
    /* USART3 DMA Init */
    /* USART3_TX Init */
    hdma_usart3_tx.Instance = DMA1_Channel2;
    //hdma_usart3_tx.ChannelIndex = 2;
    hdma_usart3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_tx.Init.Mode = DMA_NORMAL;
    hdma_usart3_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart3_tx) != HAL_OK)
    {
      Error_Handler();
    }

    
    __HAL_RCC_USART3_CLK_ENABLE();      
    __HAL_LINKDMA(&huart3,hdmatx,hdma_usart3_tx);
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 1000000;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
      Error_Handler();
    }



     sprintf(buffer, "Hello, World!\r"); // 生成字符串
     //HAL_UART_Transmit(&huart3, (uint8_t*) buffer, strlen(buffer), HAL_MAX_DELAY); // 发送字符串


    
}


void hw_uart3_process(void)
{
    //HAL_Delay(1000);
    //++debug8;
    //dma_printf("0x%lx,%d\n", debug8, debug8b);
    //u32_to_bcd_str(str, debug32);
    //str[8] = ',';
   // u32_to_bcd_str(str+9, debug32b);
    //HAL_UART_Transmit_DMA(&huart3, (uint8_t *)str, strlen(str));  //串口发送Senbuff数组
    //hw_uart3_dma_tx((uint8_t *)buffer, strlen(buffer));
}

