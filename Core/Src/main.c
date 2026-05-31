/*************************************************************
�����ܣ�CAT.1�ǻ۵�Դ
����������keil 5.37    / ������: use default compiler version 5
оƬ�ͺţ�HK32F103CCT6A
������Ա��������
��λ���ƣ��㶫�����Դ�Ƽ����޹�˾
�༭���ڣ�2024.7.1
*************************************************************/
#include "main.h"
#include "hw_tim4_pwm2.h"
#include "sys_tick.h"
#include "u32_q.h"
#include "hw_tim2.h"
#include "buzzer.h"
#include "build_date.h"
#include "hw_tim3_pwm2.h"
#include "sys_dim_voltage_out.h"
#include "hw_tim4_cap1.h"
#include "hw_tim5.h"
#include "crc16_ccitt.h"
#include "crc16_modbus.h"
#include "hw_flash.h"
#include "charge.h"
#include "standby_mode.h"
#include "hw_uart3.h"
#include "hw_uart2.h"
#include "hw_uart1.h"
#include "hw_tim1_pwm2.h"
#include "watchdog.h"
#include "ota.h"
#include "NbDriver.h"
#include "hw_gateway.h"
#include "Portable.h"
#include "net_dim.h"
#include "TcpClient.h"
#include "SystemConfig.h"
#include "hw_tim1_pwm2.h"
#include "adc.h"
#include "dma.h"
#include "hw_uart2.h"
#include "sys_bl0942.h"
#include "oco.h"
#include "hw_4g_io.h"
#include "sys_data.h"
#include "sys_pwm.h"
#include "sys_temp_over_protect.h"
#include "aip1302.h"
#include "sys_aip1302.h"
#include "sys_pow_drop_check.h"
#include "danger_current_check.h"
#include "app.h"
#include "sys_Vo_Io.h"
#include "data_backup.h"
#include "offline_Time_controlled_dimming.h"
#include "json_protocol.h"
#include "zk_work_plan.h"

void SystemClock_Config(void);
void system_colock_monitor(void);
static void MX_GPIO_Init(void);
extern  u8   OTA_ENABLE_state;
extern  void resetNbModule_machine(void);

#if APP_PERF_PROFILE_ENABLE
typedef enum
{
    APP_PERF_TCP_CLIENT,
    APP_PERF_4G_CONFIG,
    APP_PERF_AT_COMMAND,
    APP_PERF_NB_SEND,
    APP_PERF_BL0942,
    APP_PERF_OTA,
    APP_PERF_COPY_FW,
    APP_PERF_ZK_PLAN,
    APP_PERF_JSON,
    APP_PERF_COUNT
} app_perf_slot_en;

volatile u32 app_perf_max_tick[APP_PERF_COUNT];

static void app_perf_profile_update(app_perf_slot_en slot, u32 start_tick)
{
    u32 elapsed = sys_tick_get_tick() - start_tick;

    if(elapsed > app_perf_max_tick[slot])
    {
        app_perf_max_tick[slot] = elapsed;
    }
}

#define APP_PROFILE_CALL(slot, call_expr)            \
    do                                               \
    {                                                \
        u32 app_perf_start_tick = sys_tick_get_tick(); \
        call_expr;                                   \
        app_perf_profile_update((slot), app_perf_start_tick); \
    } while (0)
#else
#define APP_PROFILE_CALL(slot, call_expr) do { call_expr; } while (0)
#endif


#if APP_HEX_LOG_ENABLE
void printf_buf(u8* buf, u16 length)
{
    u16 i;
    printf("\n---------\n");
    for(i=0; i<length; i++)
    {
        printf("%02x,",buf[i]);
    }
    printf("\n---------\n");
}

void printf_buf_char(u8* buf, u16 length)
{
    u16 i;
    printf("\n---------\n");
    for(i=0; i<length; i++)
    {
        printf("%c,",buf[i]);
    }
    printf("\n---------\n");
}
void printf_buf2(const char* str, u8* buf, u16 length)
{
    u16 i;
    printf("%s: ",str);
    for(i=0; i<length; i++)
    {
      printf("%02x,",buf[i]);
    }
    printf("\n");
}
#else
void printf_buf(u8* buf, u16 length)
{
    (void)buf;
    (void)length;
}

void printf_buf_char(u8* buf, u16 length)
{
    (void)buf;
    (void)length;
}

void printf_buf2(const char* str, u8* buf, u16 length)
{
    (void)str;
    (void)buf;
    (void)length;
}
#endif

u8 timer_for_printf=100;
boolean_en init_printf=BOOL_FALSE;

static u8 _timer;
u16 crc16;
u16 upload_timer=0;
u8 softstar=1;

void main_timer(void)
{ 
    ++upload_timer;
    ++_timer;
    if(_timer == 100)
    {
        _timer = 0;


    }
    
        if(timer_for_printf>0)
    {
        --timer_for_printf;
    }
}

// ����RCC_CFGR_PLLSRC_HSI_DIV2��RCC_CFGR_PLLSRC_HSE
#ifndef RCC_CFGR_PLLSRC_HSI_DIV2
#define RCC_CFGR_PLLSRC_HSI_DIV2 ((uint32_t)0x00000000)
#endif
#ifndef RCC_CFGR_PLLSRC_HSE
#define RCC_CFGR_PLLSRC_HSE ((uint32_t)0x00010000)
#endif


int main(void)
{
    log_type_st log;
    *(uint32_t *)0x400220D0=0x0;//��˳�ر�flash��������ST���Բ�����һ��
    SCB->VTOR = APROM_OFFSET_ADDR;

    /* MCU Configuration----------------Reset of all peripherals, Initializes the Flash interface and the Systick.------------------------------------------*/

    HAL_Init();
    SystemClock_Config();
    watchdog_init();
    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();	
    HAL_ADCEx_Calibration_Start(&hadc1);// 2.ADCУ׼
    hw_uart3_init();
    hw_uart2_init();
    hw_uart1_init();
    hw_tim2_init();
    sys_tick_init();
    hw_tim4_pwm2_init();
    hw_tim4_cap1_init();
    hw_tim3_pwm2_init();
    hw_tim1_pwm2_init();
    printf("%s,%s\n",DataStr,TimeStr);
    oco_init();
    hw_tim1_pwm2_set_PWM_OUT(0);//�ȵ����ٽ��п���������,CCO�����Ǹߵ�ƽ
    portableInit();
    sys_data_load();
    zk_work_plan_init();
    zk_runtime_stats_init();
    if(sys_data.sn==0xaa5555aa)
    {
        sys_data.sn =0;    //����AP���ˣ�BAKROM��ʶ����Ҫ����
        sys_data_store();      
    }  
    if( sys_data.ac_EnergyP==0xffffffff)
    {
        sys_data.ac_EnergyP=0;
    }
    system_colock_monitor();
    hw_4g_io_init();
    pwr_off(); //λ�ò�Ҫת��  �ϵ����׹ػ�
    delayMs(750);//��ʱ750mS   ģ��������λ�ò�Ҫת��
    sys_bl0942_init();
    sys_pow_drop_check_inint();  
    mac_reset();
    __enable_irq();


  while (1)
  {
    watchdog_feed_dog();
    if(softstar) //����������
    {
        softstar=0;
        dim_level = 100;                    /* 同步调光状态，确保RunSts上报亮灯+100%亮度 */
        sys_pwm_fade_output(0, 100);        /* 上电默认满功率输出（不走zk_apply_brightness避免误触发变化上报） *///������������
    }
    hw_uart3_process();
    sys_tick_process();
    zk_runtime_counter_process();
  
    hw_gateway_process();           
    uart_diam_process();
    adc_process();
    sys_temp_over_protect_process();
    if(OTA_ENABLE_IS_SET()==BOOL_FALSE)
    {
        APP_PROFILE_CALL(APP_PERF_TCP_CLIENT, tcpClientProcess());
    }
    resetNbModule_machine();
    APP_PROFILE_CALL(APP_PERF_4G_CONFIG, _4G_configModule_machine());
    APP_PROFILE_CALL(APP_PERF_AT_COMMAND, send_AT_Command_machine());
    APP_PROFILE_CALL(APP_PERF_NB_SEND, nbSendTcpData_sm());
    APP_PROFILE_CALL(APP_PERF_BL0942, sys_bl0942_process());
    APP_PROFILE_CALL(APP_PERF_OTA, _4G_OTA_machine());
    APP_PROFILE_CALL(APP_PERF_COPY_FW, mcu_copy_firmware_machine());
    if( OTA_ENABLE_state==0)//OTAʱ���رմ˳����Ӱ��OTA����
    {
         if(init_printf==BOOL_FALSE && timer_for_printf==0)
        {
            init_printf = BOOL_TRUE;
            Ds1302_init();
            sys_aip1302_init();
            Ds1302_set_charge();
        }
        if(init_printf == BOOL_TRUE)
        {  
            sys_aip1302_process();
        }
     }
  
     if(u32_queue_out(&log))
     {
        printf("%d:%d > %c-0x%x\r\n",log.sn, log.index, log.dat, log.dat);
     }
    sys_pow_drop_check_process();
    danger_current_check_process();
    error_report_process();
    sys_pwm_process();
    sys_temp_low_protect_process();
#if APP_PERF_PROFILE_ENABLE
    APP_PROFILE_CALL(APP_PERF_ZK_PLAN, zk_work_plan_process());
#else
    zk_work_plan_process();
#endif

    APP_PROFILE_CALL(APP_PERF_JSON, json_process());
  }
}


/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct =   {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct =   {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
 
      RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
      RCC_OscInitStruct.HSEState = RCC_HSE_ON;
      RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
      RCC_OscInitStruct.HSIState = RCC_HSI_ON;
      RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
      RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
      RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
      if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
      {
        Error_Handler();
      }

      
  
  /** Initializes the CPU, AHB and APB buses clocks
  */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
        PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

}

/**
  * @brief  GPIO Initialization Function
  * @param  None
  * @retval None
*/
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();


    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);


}


void system_colock_monitor(void)
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
        // PLL
        printf("PLL\n");
        if ((RCC->CFGR & RCC_CFGR_PLLSRC) == RCC_CFGR_PLLSRC_HSI_DIV2) 
         {
          printf("PLL from HSI\n");// �ڲ�����RC������HSI������2��ΪPLL����ʱ��Դ
        } 
            else if ((RCC->CFGR & RCC_CFGR_PLLSRC) == RCC_CFGR_PLLSRC_HSE) 
        {
            printf("PLL from HSE\n");// �ⲿ���پ���������HSE����ΪPLL����ʱ��Դ
        } 
        else 
        {
          // �������
        }
    } 
    else 
    {    
        printf("OTHER\n"); // ������������磬ʹ�õ����ڲ�RC������
    }

}


void Error_Handler(void)
{
    printf("wothdongerro\n");
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
