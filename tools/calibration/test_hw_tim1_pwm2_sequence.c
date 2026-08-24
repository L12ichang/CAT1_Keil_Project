#include <stdio.h>
#include <string.h>

#include "factory_user_data.h"
#include "hw_tim1_pwm2.h"

extern TIM_HandleTypeDef htim1;
extern u8 pwm_on;

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp;

static TIM_TypeDef fake_tim;
static boolean_en boot_inhibited;
static boolean_en output_authorized;
static u32 expected_ccr;
static u32 ccr_at_oco_off;
static u8 event_log[8];
static u8 event_count;
static u32 oco_on_count;
static boolean_en inject_compare_mismatch;

enum
{
    EVENT_SET_COMPARE_OBSERVED = 1,
    EVENT_OCO_ON,
    EVENT_OCO_OFF,
    EVENT_COMPARE_NOT_READY
};

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return boot_inhibited;
}

boolean_en sys_calibration_service_is_output_authorized(void)
{
    return output_authorized;
}

u32 hw_tim1_pwm2_sequence_test_get_compare(void)
{
    if (inject_compare_mismatch == BOOL_TRUE && fake_tim.CCR1 != 0U)
    {
        return fake_tim.CCR1 + 1U;
    }
    return fake_tim.CCR1;
}

void oco_on(void)
{
    event_log[event_count++] =
        fake_tim.CCR1 == expected_ccr ? EVENT_SET_COMPARE_OBSERVED :
                                        EVENT_COMPARE_NOT_READY;
    event_log[event_count++] = EVENT_OCO_ON;
    ++oco_on_count;
}

void oco_off(void)
{
    ccr_at_oco_off = fake_tim.CCR1;
    event_log[event_count++] = EVENT_OCO_OFF;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    (void)GPIOx;
    (void)GPIO_Pin;
    return oco_on_count != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
    (void)GPIOx;
    (void)GPIO_Init;
}

HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim)
{
    (void)htim;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(
    TIM_HandleTypeDef *htim,
    TIM_MasterConfigTypeDef *config)
{
    (void)htim;
    (void)config;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(
    TIM_HandleTypeDef *htim,
    TIM_OC_InitTypeDef *config,
    uint32_t channel)
{
    (void)htim;
    (void)config;
    (void)channel;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim,
                                    uint32_t channel)
{
    (void)htim;
    (void)channel;
    return HAL_OK;
}

void Error_Handler(void)
{
}

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static void reset_case(u32 initial_ccr, u32 expected)
{
    memset(event_log, 0, sizeof(event_log));
    event_count = 0U;
    oco_on_count = 0U;
    ccr_at_oco_off = 0U;
    inject_compare_mismatch = BOOL_FALSE;
    expected_ccr = expected;
    fake_tim.CCR1 = initial_ccr;
}

static int expect_nonzero_sequence(const char *name)
{
    return expect_true(
        event_count >= 2U &&
            event_log[0] == EVENT_SET_COMPARE_OBSERVED &&
            event_log[1] == EVENT_OCO_ON &&
            fake_tim.CCR1 == expected_ccr &&
            fake_tim.CCR1 <= fake_tim.ARR,
        name);
}

int main(void)
{
    int failures = 0;

    memset(&fake_tim, 0, sizeof(fake_tim));
    htim1.Instance = &fake_tim;
    htim1.Init.Period = 999U;
    fake_tim.ARR = 999U;
    OP_PWM_OFFSET = 30U;
    boot_inhibited = BOOL_FALSE;
    output_authorized = BOOL_FALSE;

    reset_case(0U, 130U);
    hw_tim1_pwm2_set_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "Default 0->nonzero writes offset CCR before OCO_ON");

    reset_case(0U, 100U);
    hw_tim1_pwm2_set_calibrated_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "Calibrated 0->nonzero writes raw logical CCR before OCO_ON");

    boot_inhibited = BOOL_TRUE;
    output_authorized = BOOL_TRUE;
    reset_case(0U, 100U);
    hw_tim1_pwm2_set_calibration_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "SET_POINT writes verified CCR before authorized OCO_ON");

    reset_case(0U, 100U);
    hw_tim1_pwm2_set_calibration_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "SET_OUTPUT calibrated path writes CCR before authorized OCO_ON");

    reset_case(0U, 130U);
    hw_tim1_pwm2_set_calibration_default_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "SET_OUTPUT fallback writes offset CCR before authorized OCO_ON");

    boot_inhibited = BOOL_FALSE;
    output_authorized = BOOL_FALSE;
    reset_case(0U, 0U);
    hw_tim1_pwm2_set_calibration_PWM_OUT(100U);
    failures += expect_true(
        oco_on_count == 0U && event_log[0] == EVENT_OCO_OFF &&
            fake_tim.CCR1 == 0U,
        "unauthorized calibration request never enables OCO");

    boot_inhibited = BOOL_TRUE;
    reset_case(0U, 0U);
    hw_tim1_pwm2_set_PWM_OUT(100U);
    failures += expect_true(
        oco_on_count == 0U && event_log[0] == EVENT_OCO_OFF &&
            fake_tim.CCR1 == 0U,
        "boot inhibit request never enables OCO");

    boot_inhibited = BOOL_FALSE;
    reset_case(130U, 0U);
    hw_tim1_pwm2_set_PWM_OUT(0U);
    failures += expect_true(
        oco_on_count == 0U && event_log[0] == EVENT_OCO_OFF &&
            ccr_at_oco_off == 130U && fake_tim.CCR1 == 0U,
        "nonzero->zero turns OCO_OFF before clearing CCR");

    reset_case(0U, 0U);
    hw_tim1_pwm2_set_calibration_PWM_OUT(0U);
    failures += expect_true(
        oco_on_count == 0U && event_log[0] == EVENT_OCO_OFF &&
            fake_tim.CCR1 == 0U,
        "fault/stale safe-off input never enables OCO");

    OP_PWM_OFFSET = 950U;
    output_authorized = BOOL_FALSE;
    reset_case(0U, 999U);
    hw_tim1_pwm2_set_PWM_OUT(100U);
    failures += expect_nonzero_sequence(
        "Default offset final CCR is clamped to ARR before OCO_ON");

    OP_PWM_OFFSET = 0U;
    reset_case(0U, 999U);
    hw_tim1_pwm2_set_calibrated_PWM_OUT(1200U);
    failures += expect_true(
        hw_tim1_pwm2_get_logical_pwm() == 1000U,
        "logical PWM input is range-limited to 1000");
    failures += expect_nonzero_sequence(
        "calibrated final CCR never exceeds ARR");

    reset_case(0U, 130U);
    OP_PWM_OFFSET = 30U;
    inject_compare_mismatch = BOOL_TRUE;
    hw_tim1_pwm2_set_PWM_OUT(100U);
    failures += expect_true(
        oco_on_count == 0U && event_log[0] == EVENT_OCO_OFF &&
            ccr_at_oco_off == 130U && fake_tim.CCR1 == 0U && pwm_on == 0U,
        "CCR readback mismatch keeps OCO off and clears CCR");

    if (failures != 0)
    {
        return 1;
    }
    return 0;
}
