/*************************************************************
程序功能：阶段 1 系统启动入口与旧初始化顺序适配
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.7.30
*************************************************************/
#include "app_boot.h"
#include "app_scheduler.h"
#include "adc.h"
#include "aip1302.h"
#include "build_date.h"
#include "current_calibration.h"
#include "dma.h"
#include "hw_4g_io.h"
#include "hw_tim1_pwm2.h"
#include "hw_tim2.h"
#include "hw_tim3_pwm2.h"
#include "hw_tim4_cap1.h"
#include "hw_tim4_pwm2.h"
#include "hw_uart1.h"
#include "hw_uart2.h"
#include "hw_uart3.h"
#include "main.h"
#include "meter_runtime.h"
#include "NbDriver.h"
#include "oco.h"
#include "Portable.h"
#include "sys_bl0942.h"
#include "sys_data.h"
#include "sys_event.h"
#include "sys_at_engine.h"
#include "sys_resource.h"
#include "sys_pow_drop_check.h"
#include "sys_tick.h"
#include "sys_time.h"
#include "TcpClient.h"
#include "watchdog.h"
#include "zk_runtime_stats.h"
#include "zk_work_plan.h"

#define APP_BOOT_FLASH_COMPAT_REGISTER_ADDR    ((u32)0x400220D0UL)
#define APP_BOOT_FLASH_COMPAT_REGISTER_VALUE   ((u32)0x00000000UL)
#define APP_BOOT_BAKROM_MARKER                 ((u32)0xAA5555AAUL)
#define APP_BOOT_ERASED_U32                    ((u32)0xFFFFFFFFUL)

#ifndef RCC_CFGR_PLLSRC_HSI_DIV2
#define RCC_CFGR_PLLSRC_HSI_DIV2               ((u32)0x00000000UL)
#endif

#ifndef RCC_CFGR_PLLSRC_HSE
#define RCC_CFGR_PLLSRC_HSE                    ((u32)0x00010000UL)
#endif

/************************************
功能描述：配置阶段 1 沿用的 72MHz 系统时钟
输入参数：无
输出返回：无
*************************************/
static void app_boot_system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator_config = {0};
    RCC_ClkInitTypeDef clock_config = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock_config = {0};

    oscillator_config.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator_config.HSEState = RCC_HSE_ON;
    oscillator_config.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    oscillator_config.HSIState = RCC_HSI_ON;
    oscillator_config.PLL.PLLState = RCC_PLL_ON;
    oscillator_config.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator_config.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
    {
        Error_Handler();
    }

    clock_config.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock_config.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock_config.APB1CLKDivider = RCC_HCLK_DIV2;
    clock_config.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }

    peripheral_clock_config.PeriphClockSelection = RCC_PERIPHCLK_USB;
    peripheral_clock_config.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock_config) != HAL_OK)
    {
        Error_Handler();
    }

    peripheral_clock_config.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    peripheral_clock_config.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock_config) != HAL_OK)
    {
        Error_Handler();
    }
}


/************************************
功能描述：初始化阶段 1 沿用的 GPIO 基础配置
输入参数：无
输出返回：无
*************************************/
static void app_boot_gpio_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    gpio_config.Pin = GPIO_PIN_15;
    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio_config);
}


/************************************
功能描述：输出当前系统时钟源诊断信息
输入参数：无
输出返回：无
*************************************/
static void app_boot_clock_monitor(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI)
    {
        printf("HSI\n");
    }
    else if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSE)
    {
        printf("HSE\n");
    }
    else if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)
    {
        printf("PLL\n");
        if ((RCC->CFGR & RCC_CFGR_PLLSRC) == RCC_CFGR_PLLSRC_HSI_DIV2)
        {
            printf("PLL from HSI\n");
        }
        else if ((RCC->CFGR & RCC_CFGR_PLLSRC) == RCC_CFGR_PLLSRC_HSE)
        {
            printf("PLL from HSE\n");
        }
        else
        {
            /* 保留未知 PLL 时钟源的无动作行为。 */
        }
    }
    else
    {
        printf("OTHER\n");
    }
}


/************************************
功能描述：按原有先后关系初始化外设、参数、校准、计量和网络适配
输入参数：无
输出返回：无
注意：本函数中的旧模块初始化调用为阶段 5/8 删除的临时适配，顺序不得调整。
*************************************/
void app_boot_init(void)
{
    *(volatile u32 *)APP_BOOT_FLASH_COMPAT_REGISTER_ADDR =
        APP_BOOT_FLASH_COMPAT_REGISTER_VALUE;
    SCB->VTOR = APROM_OFFSET_ADDR;

    HAL_Init();
    app_boot_system_clock_config();
    watchdog_init();
    app_boot_gpio_init();
    MX_DMA_Init();
    MX_ADC1_Init();
    HAL_ADCEx_Calibration_Start(&hadc1);
#if APP_LOG_ENABLE || APP_OTA_LOG_ENABLE
    hw_uart3_init();
#endif
    hw_uart2_init();
    hw_uart1_init();
    hw_tim2_init();
    sys_time_init();
    sys_event_init();
    sys_resource_init();
    sys_at_engine_init();
    sys_tick_init();
    nb_mark_boot_start();
    hw_tim4_pwm2_init();
    hw_tim4_cap1_init();
    hw_tim3_pwm2_init();
    hw_tim1_pwm2_init();
    printf("%s,%s\n", DataStr, TimeStr);
    oco_init();
    hw_tim1_pwm2_set_PWM_OUT(0);
    portableInit();
    sys_data_load();
    current_calibration_init();
    zk_work_plan_init();
    zk_runtime_stats_init();

    if (sys_data.sn == APP_BOOT_BAKROM_MARKER)
    {
        sys_data.sn = 0;
        sys_data_store();
    }
    if (sys_data.ac_EnergyP == APP_BOOT_ERASED_U32)
    {
        sys_data.ac_EnergyP = 0;
    }

    meter_runtime_init();
    app_boot_clock_monitor();
    hw_4g_io_init();
    sys_bl0942_init();
    sys_pow_drop_check_inint();
    mac_reset();
    app_scheduler_init();
    __enable_irq();
}
