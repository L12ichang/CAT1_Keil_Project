#include <stdio.h>
#include <string.h>

#include "factory_user_data.h"
#include "sys_calibration_service.h"
#include "sys_calibration_snapshot.h"
#include "sys_pwm.h"
#include "sys_temp_over_protect.h"

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp;
sys_data_st sys_data;

u8 Error_1_OL;
u8 Error_Out_LV;
u8 Error_3_OV;
u8 Error_4_LV;
sys_temp_over_protect_state_en sys_temp_over_protect_state;

static boolean_en correction_available;
static u16 correction_pwm;
static boolean_en boot_inhibited;
static boolean_en bl_fresh = BOOL_TRUE;
static boolean_en adc_fresh = BOOL_TRUE;
static u32 test_tick = 1000U;
static u16 hardware_pwm;
static u8 hardware_kind;
static u16 prepared_requested;
static u16 prepared_protected;
static u32 clear_pending_count;

enum
{
    TEST_HW_DEFAULT = 1,
    TEST_HW_CALIBRATED,
    TEST_HW_CAL_POINT,
    TEST_HW_CAL_DEFAULT
};

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

boolean_en low_temp_detect_is_low(u16 *out, u16 in)
{
    (void)out;
    (void)in;
    return BOOL_FALSE;
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

boolean_en temp_detect_is_over(u16 *out, u16 in)
{
    (void)out;
    (void)in;
    return BOOL_FALSE;
}

boolean_en sys_bl0942_is_fresh(u32 now_tick_ms)
{
    (void)now_tick_ms;
    return bl_fresh;
}

boolean_en sys_calibration_service_output_pwm_for_current(
    u16 target_current_ma,
    u16 *logical_pwm)
{
    (void)target_current_ma;
    if (correction_available != BOOL_TRUE || logical_pwm == NULL)
    {
        return BOOL_FALSE;
    }
    *logical_pwm = correction_pwm;
    return BOOL_TRUE;
}

boolean_en sys_calibration_service_is_boot_inhibited(void)
{
    return boot_inhibited;
}

void net_dim_clear_pending(void)
{
    ++clear_pending_count;
}

boolean_en sys_calibration_snapshot_read_adc(
    sys_calibration_adc_snapshot_st *snapshot)
{
    if (snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->tick_ms = adc_fresh == BOOL_TRUE ? test_tick : test_tick - 501U;
    snapshot->vout_raw = 1000U;
    snapshot->iout_raw = 1500U;
    snapshot->valid_flags = SYS_CALIBRATION_ADC_SAMPLE_VALID;
    return BOOL_TRUE;
}

void sys_calibration_snapshot_prepare_pwm(u16 requested_percent,
                                          u16 protected_percent)
{
    prepared_requested = requested_percent;
    prepared_protected = protected_percent;
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
    hardware_kind = TEST_HW_DEFAULT;
    hardware_pwm = pwm;
}

void hw_tim1_pwm2_set_calibrated_PWM_OUT(u16 pwm)
{
    hardware_kind = TEST_HW_CALIBRATED;
    hardware_pwm = pwm;
}

void hw_tim1_pwm2_set_calibration_PWM_OUT(u16 pwm)
{
    hardware_kind = TEST_HW_CAL_POINT;
    hardware_pwm = pwm;
}

void hw_tim1_pwm2_set_calibration_default_PWM_OUT(u16 pwm)
{
    hardware_kind = TEST_HW_CAL_DEFAULT;
    hardware_pwm = pwm;
}

u16 hw_tim1_pwm2_get_logical_pwm(void)
{
    return hardware_pwm;
}

u16 hw_tim1_pwm2_get_ccr(void)
{
    return (u16)(hardware_pwm +
                 ((hardware_kind == TEST_HW_DEFAULT ||
                   hardware_kind == TEST_HW_CAL_DEFAULT) &&
                  hardware_pwm > 0U ? OP_PWM_OFFSET : 0U));
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

static void reset_hardware(void)
{
    hardware_pwm = 0U;
    hardware_kind = 0U;
    prepared_requested = 0U;
    prepared_protected = 0U;
}

int main(void)
{
    u16 actual_pwm = 0U;
    int failures = 0;

    memset(factory_user_buff, 0, sizeof(factory_user_buff));
    MID = SYS_PRODUCT_PROFILE_50W_MID;
    SET_OUTCUR = SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA;
    HWMAX_OUTCUR = SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA;
    OUTPUT_CUR_SENSOR = SYS_PRODUCT_PROFILE_50W_RS3_MOHM;
    OP_PWM_OFFSET = 30U;
    BOUND_OUTPUT_VOLTAGE_01V = 560U;
    sys_data.lamp_power = 100U;

    reset_hardware();
    correction_available = BOOL_FALSE;
    boot_inhibited = BOOL_FALSE;
    pwm_output(100U);
    failures += expect_true(
        hardware_kind == TEST_HW_DEFAULT && hardware_pwm == 619U &&
            prepared_requested == 100U && prepared_protected == 100U,
        "no Calibration uses Default PWM then OP offset boundary");

    reset_hardware();
    correction_available = BOOL_TRUE;
    correction_pwm = 777U;
    pwm_output(80U);
    failures += expect_true(
        hardware_kind == TEST_HW_CALIBRATED && hardware_pwm == 777U,
        "valid Output Calibration writes logical PWM without repeated offset");

    reset_hardware();
    correction_available = BOOL_FALSE;
    pwm_output(80U);
    failures += expect_true(
        hardware_kind == TEST_HW_DEFAULT && hardware_pwm > 0U,
        "out-of-coverage/no curve falls back to nonzero Default path");

    reset_hardware();
    correction_available = BOOL_TRUE;
    boot_inhibited = BOOL_TRUE;
    pwm_output(80U);
    failures += expect_true(hardware_pwm == 0U,
        "boot inhibit blocks normal output");

    reset_hardware();
    pwm_output(50U);
    failures += expect_true(hardware_pwm == 0U,
        "boot inhibit continues to block every ordinary dimming call");

    reset_hardware();
    correction_available = BOOL_FALSE;
    failures += expect_true(
        sys_pwm_calibration_set_level(100U, &actual_pwm) == BOOL_TRUE &&
            hardware_kind == TEST_HW_CAL_DEFAULT && hardware_pwm > 0U &&
            actual_pwm == hardware_pwm,
        "SET_POINT uses the synchronous calibration percent/current mapping");

    correction_pwm = hardware_pwm;
    sys_pwm_normal_output(80U);
    failures += expect_true(
        hardware_pwm == correction_pwm,
        "ordinary dimming cannot replace an active calibration output");

    clear_pending_count = 0U;
    sys_pwm_force_safe_off();
    failures += expect_true(
        clear_pending_count == 1U && hardware_pwm == 0U,
        "safe off clears every pending network dimming request");

    reset_hardware();
    boot_inhibited = BOOL_FALSE;
    sys_pwm_normal_output(80U);
    failures += expect_true(
        hardware_pwm > 0U,
        "ordinary dimming resumes after the calibration inhibit is cleared");

    reset_hardware();
    correction_available = BOOL_TRUE;
    correction_pwm = 456U;
    failures += expect_true(
        sys_pwm_calibration_set_output(45U, &actual_pwm) == BOOL_TRUE &&
            hardware_kind == TEST_HW_CAL_POINT && hardware_pwm == 456U,
        "APPLIED SET_OUTPUT uses staged calibrated logical PWM");

    reset_hardware();
    correction_available = BOOL_FALSE;
    failures += expect_true(
        sys_pwm_calibration_set_output(45U, &actual_pwm) == BOOL_TRUE &&
            hardware_kind == TEST_HW_CAL_DEFAULT && hardware_pwm > 0U,
        "APPLIED out-of-range target uses authorized Default+offset fallback");

    reset_hardware();
    bl_fresh = BOOL_FALSE;
    failures += expect_true(
        sys_pwm_calibration_set_level(20U, &actual_pwm) == BOOL_FALSE &&
            hardware_pwm == 0U,
        "stale BL feedback rejects and safe-offs nonzero SET_POINT");
    bl_fresh = BOOL_TRUE;

    reset_hardware();
    Error_3_OV = 1U;
    failures += expect_true(
        sys_pwm_calibration_set_output(20U, &actual_pwm) == BOOL_FALSE &&
            hardware_pwm == 0U,
        "hardware fault rejects and safe-offs nonzero SET_OUTPUT");
    Error_3_OV = 0U;

    reset_hardware();
    boot_inhibited = BOOL_FALSE;
    SET_OUTCUR = 1500U;
    pwm_output(100U);
    failures += expect_true(hardware_pwm == 0U,
                            "SET greater than HWMAX cannot produce output");

    if (failures != 0)
    {
        return 1;
    }
    return 0;
}
