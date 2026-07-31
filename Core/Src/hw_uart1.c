/*************************************************************
程序功能：UART1 唯一收发入口、SPSC 环形缓冲和错误恢复
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.31
*************************************************************/
#include "hw_uart1.h"

#define HW_UART1_RX_CAPACITY       ((u16)2048U)
#define HW_UART1_TX_CAPACITY       ((u16)2048U)
#define HW_UART1_RX_MASK           ((u16)(HW_UART1_RX_CAPACITY - 1U))
#define HW_UART1_TX_MASK           ((u16)(HW_UART1_TX_CAPACITY - 1U))
#define HW_UART1_U32_MAX           ((u32)0xFFFFFFFFUL)

UART_HandleTypeDef huart1;

static u8 _rx_ring[HW_UART1_RX_CAPACITY];
static u8 _tx_ring[HW_UART1_TX_CAPACITY];
static volatile u16 _rx_head;
static volatile u16 _rx_tail;
static volatile u16 _tx_head;
static volatile u16 _tx_tail;
static volatile boolean_en _tx_active;
static volatile boolean_en _rx_armed;
static u8 _rx_isr_byte;
static u8 _tx_isr_byte;
static hw_uart1_stats_st _stats;

static void hw_uart1_increment_saturated(volatile u32 *value)
{
    if (*value < HW_UART1_U32_MAX)
    {
        (*value)++;
    }
}

static u32 hw_uart1_enter_critical(void)
{
    u32 primask;

    primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void hw_uart1_exit_critical(u32 primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static u16 hw_uart1_ring_count(u16 head, u16 tail, u16 mask)
{
    return (u16)((head - tail) & mask);
}

static void hw_uart1_update_rx_high_watermark(void)
{
    u16 count;

    count = hw_uart1_ring_count(_rx_head, _rx_tail, HW_UART1_RX_MASK);
    if (count > _stats.rx_high_watermark)
    {
        _stats.rx_high_watermark = count;
    }
}

static void hw_uart1_update_tx_high_watermark(void)
{
    u16 count;

    count = hw_uart1_ring_count(_tx_head, _tx_tail, HW_UART1_TX_MASK);
    if (count > _stats.tx_high_watermark)
    {
        _stats.tx_high_watermark = count;
    }
}

static void hw_uart1_arm_rx(void)
{
    if (HAL_UART_Receive_IT(&huart1, &_rx_isr_byte, 1U) == HAL_OK)
    {
        _rx_armed = BOOL_TRUE;
    }
    else
    {
        _rx_armed = BOOL_FALSE;
        hw_uart1_increment_saturated(&_stats.rx_rearm_error_count);
    }
}

static void hw_uart1_kick_tx(void)
{
    HAL_StatusTypeDef status;

    if ((_tx_active == BOOL_TRUE) || (_tx_tail == _tx_head))
    {
        return;
    }

    _tx_isr_byte = _tx_ring[_tx_tail];
    _tx_active = BOOL_TRUE;
    status = HAL_UART_Transmit_IT(&huart1, &_tx_isr_byte, 1U);
    if (status != HAL_OK)
    {
        _tx_active = BOOL_FALSE;
        if (status == HAL_BUSY)
        {
            hw_uart1_increment_saturated(&_stats.tx_busy_count);
        }
        else
        {
            hw_uart1_increment_saturated(&_stats.tx_error_count);
        }
    }
}

void HAL_UART_Rx1CpltCallback(UART_HandleTypeDef *huart)
{
    u16 next_head;

    if (huart->Instance != USART1)
    {
        return;
    }

    _rx_armed = BOOL_FALSE;
    next_head = (u16)((_rx_head + 1U) & HW_UART1_RX_MASK);
    if (next_head == _rx_tail)
    {
        hw_uart1_increment_saturated(&_stats.rx_overflow_count);
    }
    else
    {
        _rx_ring[_rx_head] = _rx_isr_byte;
        _rx_head = next_head;
        hw_uart1_increment_saturated(&_stats.rx_byte_count);
        hw_uart1_update_rx_high_watermark();
    }
    hw_uart1_arm_rx();
}

void HAL_UART_Tx1CpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

    _tx_tail = (u16)((_tx_tail + 1U) & HW_UART1_TX_MASK);
    hw_uart1_increment_saturated(&_stats.tx_byte_count);
    _tx_active = BOOL_FALSE;
    hw_uart1_kick_tx();
}

void HAL_UART_Error1Callback(UART_HandleTypeDef *huart)
{
    u32 error_code;

    if (huart->Instance != USART1)
    {
        return;
    }

    error_code = huart->ErrorCode;
    if ((error_code & HAL_UART_ERROR_ORE) != 0U)
    {
        hw_uart1_increment_saturated(&_stats.ore_count);
    }
    if ((error_code & HAL_UART_ERROR_FE) != 0U)
    {
        hw_uart1_increment_saturated(&_stats.fe_count);
    }
    if ((error_code & HAL_UART_ERROR_NE) != 0U)
    {
        hw_uart1_increment_saturated(&_stats.ne_count);
    }
    if ((error_code & HAL_UART_ERROR_PE) != 0U)
    {
        hw_uart1_increment_saturated(&_stats.pe_count);
    }

    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_PEFLAG(&huart1);
    huart1.ErrorCode = HAL_UART_ERROR_NONE;
    /*
     * ORE 路径中 HAL 已结束接收事务，仅标记给主循环恢复；FE/NE/PE
     * 属于非阻塞错误，原接收事务仍在进行，不能在中断内 Abort/重挂。
     */
    if ((error_code & HAL_UART_ERROR_ORE) != 0U)
    {
        _rx_armed = BOOL_FALSE;
    }
}

void hw_uart1_init(void)
{
    GPIO_InitTypeDef gpio_init;

    memset(&gpio_init, 0, sizeof(gpio_init));
    memset(&_stats, 0, sizeof(_stats));
    _rx_head = 0U;
    _rx_tail = 0U;
    _tx_head = 0U;
    _tx_tail = 0U;
    _tx_active = BOOL_FALSE;
    _rx_armed = BOOL_FALSE;

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio_init.Pin = GPIO_PIN_9;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_10;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio_init);

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

    HAL_NVIC_SetPriority(USART1_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    hw_uart1_arm_rx();
}

void hw_uart1_process(void)
{
    u32 primask;

    if (_rx_armed == BOOL_FALSE)
    {
        hw_uart1_arm_rx();
    }
    primask = hw_uart1_enter_critical();
    hw_uart1_kick_tx();
    hw_uart1_exit_critical(primask);
}

boolean_en hw_uart1_read_byte(u8 *byte)
{
    if ((byte == NULL) || (_rx_tail == _rx_head))
    {
        return BOOL_FALSE;
    }

    *byte = _rx_ring[_rx_tail];
    _rx_tail = (u16)((_rx_tail + 1U) & HW_UART1_RX_MASK);
    return BOOL_TRUE;
}

u16 hw_uart1_available(void)
{
    return hw_uart1_ring_count(_rx_head, _rx_tail, HW_UART1_RX_MASK);
}

u16 hw_uart1_write(const u8 *buf, u16 length)
{
    u16 free_count;
    u16 index;
    u32 primask;

    if ((buf == NULL) || (length == 0U))
    {
        return 0U;
    }

    primask = hw_uart1_enter_critical();
    free_count = (u16)(HW_UART1_TX_MASK -
        hw_uart1_ring_count(_tx_head, _tx_tail, HW_UART1_TX_MASK));
    if (length > free_count)
    {
        hw_uart1_increment_saturated(&_stats.tx_busy_count);
        hw_uart1_exit_critical(primask);
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        _tx_ring[_tx_head] = buf[index];
        _tx_head = (u16)((_tx_head + 1U) & HW_UART1_TX_MASK);
    }
    hw_uart1_update_tx_high_watermark();
    hw_uart1_kick_tx();
    hw_uart1_exit_critical(primask);
    return length;
}

boolean_en hw_uart1_tx_idle(void)
{
    return ((_tx_active == BOOL_FALSE) && (_tx_head == _tx_tail)) ?
        BOOL_TRUE : BOOL_FALSE;
}

void hw_uart1_get_stats(hw_uart1_stats_st *stats)
{
    u32 primask;

    if (stats == NULL)
    {
        return;
    }

    primask = hw_uart1_enter_critical();
    *stats = _stats;
    hw_uart1_exit_critical(primask);
}

u8 hw_uart1_send_with_result(u8 *buf, u32 length)
{
    if ((buf == NULL) || (length == 0U) || (length > 0xFFFFU))
    {
        return (u8)HAL_ERROR;
    }
    return (hw_uart1_write(buf, (u16)length) == (u16)length) ?
        (u8)HAL_OK : (u8)HAL_BUSY;
}

void hw_uart1_send(u8 *buf, u32 length)
{
    (void)hw_uart1_send_with_result(buf, length);
}

void hw_uart1_timer(void)
{
    /* 阶段 8 删除：保留空入口兼容旧 sys_tick 调用。 */
}
