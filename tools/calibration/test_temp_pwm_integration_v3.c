#include <stdio.h>
#include <string.h>

#include "factory_user_data.h"
#include "ntc.h"
#include "sys_calibration_service.h"
#include "sys_calibration_snapshot.h"
#include "sys_data.h"
#include "sys_pwm.h"
#include "sys_temp_over_protect.h"

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp;
sys_data_st sys_data;
ntcTemp_t Ntctemp;
ntcTemp_t Ntctemp2;
boolean_en power_status = BOOL_TRUE;

u8 Error_1_OL;
u8 Error_Out_LV;
u8 Error_3_OV;
u8 Error_4_LV;

static u32 test_tick = 1000U;
static u16 hardware_pwm;
static u16 hardware_ccr;
static u16 protected_percent;

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

boolean_en DC_low_voltage_detect_is_low(u16 *out, u16 in)
{
    (void)out;
    (void)in;
    return BOOL_FALSE;
}

boolean_en High_voltage_detect_is_high(u16 *out, u16 in)
{
    (void)out;
    (void)in;
    return BOOL_FALSE;
}

boolean_en sys_bl0942_is_fresh(u32 now_tick_ms)
{
    (void)now_tick_ms;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_output_pwm_for_current(
    u16 target_current_ma,
    u16 *logical_pwm)
{
    (void)target_current_ma;
    (void)logical_pwm;
    return BOOL_FALSE;
}

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return BOOL_FALSE;
}

boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status)
{
    if (status == NULL)
    {
        return BOOL_FALSE;
    }
    memset(status, 0, sizeof(*status));
    status->state = SYS_CALIBRATION_STATE_IDLE;
    return BOOL_TRUE;
}

sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id, u32 now_ms, u32 lease_ms, u32 seq,
    u16 profile_id, u32 profile_fingerprint,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    (void)lease_ms;
    (void)seq;
    (void)profile_id;
    (void)profile_fingerprint;
    (void)status;
    return SYS_CALIBRATION_RESULT_BAD_STATE;
}

void net_dim_clear_pending(void)
{
}

boolean_en sys_calibration_snapshot_read_adc(
    sys_calibration_adc_snapshot_st *snapshot)
{
    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->tick_ms = test_tick;
    snapshot->valid_flags = SYS_CALIBRATION_ADC_SAMPLE_VALID;
    return BOOL_TRUE;
}

void sys_calibration_snapshot_prepare_pwm(u16 requested_percent,
                                          u16 limited_percent)
{
    (void)requested_percent;
    protected_percent = limited_percent;
}

void sys_calibration_snapshot_publish_pwm(u32 tick_ms,
                                          u16 logical_pwm,
                                          u16 ccr,
                                          u8 oco_on,
                                          u16 valid_flags)
{
    (void)tick_ms;
    (void)logical_pwm;
    (void)ccr;
    (void)oco_on;
    (void)valid_flags;
}

void hw_tim1_pwm2_set_PWM_OUT(u16 pwm)
{
    hardware_pwm = pwm;
    hardware_ccr = pwm > 0U ? (u16)(pwm + OP_PWM_OFFSET) : 0U;
}

void hw_tim1_pwm2_set_calibrated_PWM_OUT(u16 pwm)
{
    hardware_pwm = pwm;
    hardware_ccr = pwm;
}

void hw_tim1_pwm2_set_calibration_PWM_OUT(u16 pwm)
{
    hardware_pwm = pwm;
    hardware_ccr = pwm;
}

void hw_tim1_pwm2_set_calibration_default_PWM_OUT(u16 pwm)
{
    hardware_pwm = pwm;
    hardware_ccr = pwm > 0U ? (u16)(pwm + OP_PWM_OFFSET) : 0U;
}

u16 hw_tim1_pwm2_get_logical_pwm(void)
{
    return hardware_pwm;
}

u16 hw_tim1_pwm2_get_ccr(void)
{
    return hardware_ccr;
}

u8 hw_tim1_pwm2_get_oco_on(void)
{
    return hardware_pwm > 0U ? 1U : 0U;
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

static void advance_temperature_step(void)
{
    u16 tick;
    for (tick = 0U; tick < 1000U; ++tick)
    {
        sys_temp_over_protect_timer();
    }
    test_tick += 10000U;
    sys_temp_over_protect_process();
    sys_pwm_process();
}

int main(void)
{
    u16 initial_pwm;
    u16 actual_pwm = 0U;
    int failures = 0;

    memset(&sys_data, 0, sizeof(sys_data));
    memset(factory_user_buff, 0, sizeof(factory_user_buff));
    MID = SYS_PRODUCT_PROFILE_50W_MID;
    SET_OUTCUR = SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA;
    HWMAX_OUTCUR = SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA;
    OUTPUT_CUR_SENSOR = SYS_PRODUCT_PROFILE_50W_RS3_MOHM;
    OP_PWM_OFFSET = 30U;
    INNRE_TEMP_PRO_EN = 1U;
    INNRE_TEMP_PRO = 85;
    sys_data.lamp_power = 100U;

    pwm_output(100U);
    initial_pwm = hardware_pwm;
    failures += expect_true(
        initial_pwm > 0U && protected_percent == 100U &&
            hardware_ccr == (u16)(hardware_pwm + OP_PWM_OFFSET),
        "normal 100 percent uses mature Default PWM plus offset");

    Ntctemp.Ntctemp = 850;
    advance_temperature_step();
    failures += expect_true(
        sys_temp_over_protect_state == SYS_TEMP_OVER_PROTECT_STATE_OVER &&
            driver_temperarure_warn == 1U && sys_data.lamp_power == 100U &&
            protected_percent == 100U && hardware_pwm == initial_pwm &&
            hardware_pwm > 0U &&
            hardware_ccr == (u16)(hardware_pwm + OP_PWM_OFFSET),
        "first OVER keeps ordinary output nonzero at 100 percent");

    advance_temperature_step();
    failures += expect_true(
        sys_data.lamp_power == 90U && protected_percent == 90U &&
            hardware_pwm > 0U && hardware_pwm < initial_pwm &&
            hardware_ccr == (u16)(hardware_pwm + OP_PWM_OFFSET),
        "next existing temperature step derates ordinary PWM to 90 percent");

    hardware_pwm = 123U;
    failures += expect_true(
        sys_pwm_calibration_set_level(20U, &actual_pwm) == BOOL_FALSE &&
            hardware_pwm == 0U,
        "Calibration remains fail-closed whenever temperature state is OVER");

    Ntctemp.Ntctemp = 960;
    advance_temperature_step();
    failures += expect_true(
        sys_data.lamp_power == 0U && protected_percent == 0U &&
            hardware_pwm == 0U && hardware_ccr == 0U,
        "ordinary PWM reaches zero only at the existing explicit shutdown threshold");

    if (failures != 0)
    {
        return 1;
    }
    return 0;
}
