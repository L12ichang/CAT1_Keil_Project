/*************************************************************
程序功能：看门狗
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2023.7.1
*************************************************************/

#include "watchdog.h"
#include "sys_tick.h"
#include "Portable.h"
#include "NbDriver.h"
#include "sys_bl0942.h"

#define WATCHDOG_MAIN_LOOP_MAX_COST_MS 1500UL
#define WATCHDOG_TICK_LAG_BAD_LIMIT 3UL
#define WATCHDOG_MQTT_PUB_TIMEOUT_BAD_LIMIT 2UL
#define WATCHDOG_UART_DROP_BAD_LIMIT 5UL
#define WATCHDOG_BL0942_TIMEOUT_BAD_LIMIT 5UL
#define WATCHDOG_BL0942_UART_BAD_LIMIT 5UL

IWDG_HandleTypeDef hiwdg;

static mcu_runtime_diag_t mcu_runtime_diag;
static u32 watchdog_loop_start_tick = 0;
static u32 watchdog_last_tick_lag_count = 0;
static u32 watchdog_last_mqtt_pub_timeout_count = 0;
static u32 watchdog_last_uart_drop_count = 0;
static u32 watchdog_last_bl0942_timeout_count = 0;
static u32 watchdog_last_bl0942_uart_error_count = 0;


static u32 watchdog_tick_to_ms(u32 tick)
{
    return tick / (SYS_TOTAL_TICK_PER_US * 1000UL);
}

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

void watchdog_loop_begin(void)
{
    watchdog_loop_start_tick = sys_tick_get_tick();
}

void mcu_runtime_diag_process(void)
{
    u32 tick_lag_count;
    u32 mqtt_pub_timeout_count;
    u32 uart_drop_count;
    u32 bl0942_timeout_snapshot;
    u32 bl0942_uart_error_snapshot;

    tick_lag_count = sys_tick_get_lag_count();
    mqtt_pub_timeout_count = nb_mqtt_get_publish_timeout_count();
    uart_drop_count = usart_queue_drop_count;
    bl0942_timeout_snapshot = bl0942_timeout_count;
    bl0942_uart_error_snapshot = bl0942_uart_error_count;

    mcu_runtime_diag.tick_lag_count = tick_lag_count;
    mcu_runtime_diag.tick_lag_max_ms = watchdog_tick_to_ms(sys_tick_get_max_lag_ticks());
    mcu_runtime_diag.mqtt_pub_timeout_count = mqtt_pub_timeout_count;
    mcu_runtime_diag.uart1_queue_drop_snapshot = uart_drop_count;
    mcu_runtime_diag.bl0942_timeout_count = bl0942_timeout_snapshot;
    mcu_runtime_diag.bl0942_uart_error_count = bl0942_uart_error_snapshot;

    if (tick_lag_count != watchdog_last_tick_lag_count)
    {
        if (mcu_runtime_diag.tick_lag_bad_count < 0xFFFFFFFFUL)
        {
            mcu_runtime_diag.tick_lag_bad_count++;
        }
        watchdog_last_tick_lag_count = tick_lag_count;
    }
    else
    {
        mcu_runtime_diag.tick_lag_bad_count = 0;
    }

    if (mqtt_pub_timeout_count != watchdog_last_mqtt_pub_timeout_count)
    {
        if (mcu_runtime_diag.mqtt_pub_timeout_bad_count < 0xFFFFFFFFUL)
        {
            mcu_runtime_diag.mqtt_pub_timeout_bad_count++;
        }
        watchdog_last_mqtt_pub_timeout_count = mqtt_pub_timeout_count;
    }
    else
    {
        mcu_runtime_diag.mqtt_pub_timeout_bad_count = 0;
    }

    if (uart_drop_count != watchdog_last_uart_drop_count)
    {
        if (mcu_runtime_diag.uart1_queue_drop_bad_count < 0xFFFFFFFFUL)
        {
            mcu_runtime_diag.uart1_queue_drop_bad_count++;
        }
        watchdog_last_uart_drop_count = uart_drop_count;
    }
    else
    {
        mcu_runtime_diag.uart1_queue_drop_bad_count = 0;
    }

    if (bl0942_timeout_snapshot != watchdog_last_bl0942_timeout_count)
    {
        if (mcu_runtime_diag.bl0942_timeout_bad_count < 0xFFFFFFFFUL)
        {
            mcu_runtime_diag.bl0942_timeout_bad_count++;
        }
        watchdog_last_bl0942_timeout_count = bl0942_timeout_snapshot;
    }
    else
    {
        mcu_runtime_diag.bl0942_timeout_bad_count = 0;
    }

    if (bl0942_uart_error_snapshot != watchdog_last_bl0942_uart_error_count)
    {
        if (mcu_runtime_diag.bl0942_uart_error_bad_count < 0xFFFFFFFFUL)
        {
            mcu_runtime_diag.bl0942_uart_error_bad_count++;
        }
        watchdog_last_bl0942_uart_error_count = bl0942_uart_error_snapshot;
    }
    else
    {
        mcu_runtime_diag.bl0942_uart_error_bad_count = 0;
    }
}

boolean_en mcu_health_is_ok(void)
{
    if (mcu_runtime_diag.main_loop_last_cost_ms > WATCHDOG_MAIN_LOOP_MAX_COST_MS)
    {
        return BOOL_FALSE;
    }
    if (mcu_runtime_diag.tick_lag_bad_count >= WATCHDOG_TICK_LAG_BAD_LIMIT)
    {
        return BOOL_FALSE;
    }
    if (mcu_runtime_diag.mqtt_pub_timeout_bad_count >= WATCHDOG_MQTT_PUB_TIMEOUT_BAD_LIMIT)
    {
        return BOOL_FALSE;
    }
    if (mcu_runtime_diag.uart1_queue_drop_bad_count >= WATCHDOG_UART_DROP_BAD_LIMIT)
    {
        return BOOL_FALSE;
    }
    if (mcu_runtime_diag.bl0942_timeout_bad_count >= WATCHDOG_BL0942_TIMEOUT_BAD_LIMIT)
    {
        return BOOL_FALSE;
    }
    if (mcu_runtime_diag.bl0942_uart_error_bad_count >= WATCHDOG_BL0942_UART_BAD_LIMIT)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

void watchdog_loop_end(void)
{
    u32 cost_tick;
    u32 cost_ms;

    cost_tick = sys_tick_get_tick() - watchdog_loop_start_tick;
    cost_ms = watchdog_tick_to_ms(cost_tick);
    mcu_runtime_diag.main_loop_last_cost_ms = cost_ms;
    if (cost_ms > mcu_runtime_diag.main_loop_max_cost_ms)
    {
        mcu_runtime_diag.main_loop_max_cost_ms = cost_ms;
    }
    mcu_runtime_diag.main_loop_count++;

    mcu_runtime_diag_process();
    if (mcu_health_is_ok() == BOOL_TRUE)
    {
        watchdog_feed_dog();
    }
    else if (mcu_runtime_diag.watchdog_health_fail_count < 0xFFFFFFFFUL)
    {
        mcu_runtime_diag.watchdog_health_fail_count++;
    }
}

const mcu_runtime_diag_t *mcu_runtime_diag_get(void)
{
    return &mcu_runtime_diag;
}

