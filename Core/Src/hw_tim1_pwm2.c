/*************************************************************
程序功能：CAT.1智慧电源PWM 输出CCO 
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/
#include "hw_tim1_pwm2.h"
#include "oco.h"
#include "factory_user_data.h"
#include "sys_calibration_service.h"
TIM_HandleTypeDef htim1;

u8 pwm_on;
static u16 _pwm_logical_output;

#define TIM1_PWM2_CYCLE     3600

#define PWM_CYCLE       370 //2.7k

void hw_tim1_pwm2_set_on(void)
{
   // TIM1->CCR2 = PWM_CYCLE/2;
}

void hw_tim1_pwm2_set_off(void)
{
  //  TIM1->CCR2 = 0xffffffff;
}

#define PWM_MAX        1000
#define PWM_OFFSET   OP_PWM_OFFSET //由于光耦的延迟问题增加3%输出
#define PWM_USEFUL_RANGE    (u16)(PWM_MAX-PWM_OFFSET)  //

#if defined(HW_TIM1_PWM2_SEQUENCE_TEST)
extern u32 hw_tim1_pwm2_sequence_test_get_compare(void);
#define HW_TIM1_PWM2_GET_COMPARE() hw_tim1_pwm2_sequence_test_get_compare()
#else
#define HW_TIM1_PWM2_GET_COMPARE() \
    ((u32)__HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_1))
#endif

static void hw_tim1_pwm2_set_PWM_OUT_internal(
    u16 pwm,
    boolean_en requires_calibration_authorization,
    boolean_en calibration_authorized,
    boolean_en apply_default_offset)
{
    u32 final_ccr;
    u32 auto_reload;

    /* 先完成权限和逻辑范围裁决；任一失败都进入零输出路径。 */
    if (pwm > 0U &&
        ((requires_calibration_authorization == BOOL_TRUE &&
          calibration_authorized != BOOL_TRUE) ||
         (sys_calibration_service_is_boot_inhibited() == BOOL_TRUE &&
          calibration_authorized != BOOL_TRUE)))
    {
        pwm = 0U;
    }
    if (pwm > PWM_MAX)
    {
        pwm = PWM_MAX;
    }

    /* 零/故障路径必须先断OCO，再清CCR。 */
    if (pwm == 0U)
    {
        oco_off();
        pwm_on = 0U;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
        _pwm_logical_output = 0U;
        return;
    }

    final_ccr = pwm;
    if (apply_default_offset == BOOL_TRUE)
    {
        final_ccr += PWM_OFFSET;
    }
    auto_reload = __HAL_TIM_GET_AUTORELOAD(&htim1);
    if (final_ccr > auto_reload)
    {
        final_ccr = auto_reload;
    }
    if (final_ccr == 0U)
    {
        oco_off();
        pwm_on = 0U;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
        _pwm_logical_output = 0U;
        return;
    }

    /* 非零路径：CCR写入并读回成功后，才允许OCO导通。 */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, final_ccr);
    if (HW_TIM1_PWM2_GET_COMPARE() != final_ccr)
    {
        oco_off();
        pwm_on = 0U;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
        _pwm_logical_output = 0U;
        return;
    }
    _pwm_logical_output = pwm;
    oco_on();
    pwm_on = 1U;

#if APP_PWM_DEBUG_ENABLE
    printf("pwm=%d\r\n",pwm);
#endif
}

void hw_tim1_pwm2_set_PWM_OUT(u16 pwm)//pwm输出
{
    hw_tim1_pwm2_set_PWM_OUT_internal(
        pwm, BOOL_FALSE, BOOL_FALSE, BOOL_TRUE);
}

void hw_tim1_pwm2_set_calibrated_PWM_OUT(u16 pwm)
{
    hw_tim1_pwm2_set_PWM_OUT_internal(
        pwm, BOOL_FALSE, BOOL_FALSE, BOOL_FALSE);
}

void hw_tim1_pwm2_set_calibration_PWM_OUT(u16 pwm)
{
    hw_tim1_pwm2_set_PWM_OUT_internal(
        pwm, BOOL_TRUE, sys_calibration_service_is_output_authorized(),
        BOOL_FALSE);
}

void hw_tim1_pwm2_set_calibration_default_PWM_OUT(u16 pwm)
{
    hw_tim1_pwm2_set_PWM_OUT_internal(
        pwm, BOOL_TRUE, sys_calibration_service_is_output_authorized(),
        BOOL_TRUE);
}

u16 hw_tim1_pwm2_get_logical_pwm(void)
{
    return _pwm_logical_output;
}

u16 hw_tim1_pwm2_get_ccr(void)
{
    return (u16)HW_TIM1_PWM2_GET_COMPARE();
}

u8 hw_tim1_pwm2_get_oco_on(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) ? 1U : 0U;
}

void hw_tim1_pwm2_init(void)
{
    /* USER CODE BEGIN TIM1_Init 0 */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    __HAL_RCC_TIM1_CLK_ENABLE();
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 71;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 999;//1000 1KHZ 2000 500HZ;// htim1.Init.Period = 370;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
    {
      Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
    {
      Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM2;
    sConfigOC.Pulse = 500;//150;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
      Error_Handler();
    }
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
     __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);  //0%输出

}

