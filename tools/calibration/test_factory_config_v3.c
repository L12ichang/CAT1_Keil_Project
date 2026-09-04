#include "factory_user_data.h"
#include "sys_data.h"
#include "sys_calibration_service.h"
#include <stdio.h>
#include <string.h>

sys_data_st sys_data __attribute__((aligned(4)));
static u32 store_calls;
static sys_calibration_state_en calibration_state;
static boolean_en store_success = BOOL_TRUE;
static u32 pwm_reload_calls;

boolean_en sys_data_store_checked(void)
{
    ++store_calls;
    return store_success;
}

void sys_data_store(void)
{
    (void)sys_data_store_checked();
}

boolean_en sys_calibration_service_is_session_active(void)
{
    return calibration_state != SYS_CALIBRATION_STATE_IDLE ?
           BOOL_TRUE : BOOL_FALSE;
}

void sys_pwm_reload(void)
{
    ++pwm_reload_calls;
}

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static void put_u16be(u8 *buffer, u16 offset, u16 value)
{
    buffer[offset] = (u8)(value >> 8U);
    buffer[offset + 1U] = (u8)value;
}

int main(void)
{
    int failures = 0;
    u8 candidate[128];
    u16 raised_hwmax;
    u8 state;

    memset(&sys_data, 0, sizeof(sys_data));
    memset(sys_data.fa_Parambuf, 0xFF, sizeof(sys_data.fa_Parambuf));
    calibration_state = SYS_CALIBRATION_STATE_IDLE;
    factory_user_load_data();
    failures += expect_true(
        MID == SYS_PRODUCT_PROFILE_CURRENT_MID &&
        SET_OUTCUR == SYS_PRODUCT_PROFILE_CURRENT_DEFAULT_CURRENT_MA &&
        HWMAX_OUTCUR == SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA &&
        OUTPUT_CUR_SENSOR == SYS_PRODUCT_PROFILE_CURRENT_RS3_MOHM,
        "selected Product Target supplies its frozen Factory defaults");
    failures += expect_true(
        factory_user_validate_runtime_current(0U, SET_OUTCUR) ==
            SYS_PRODUCT_CURRENT_VALID,
        "runtime SET validity is not bound to voltage/calibration context");

    failures += expect_true(
        factory_user_set_runtime_current(
            SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA + 1U) ==
            SYS_PRODUCT_CURRENT_HW_MAX,
        "SET above current HWMAX is rejected");
    raised_hwmax = (u16)(SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA +
        (SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA -
         SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA) / 2U);
    if (raised_hwmax == SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA)
    {
        raised_hwmax = SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA;
    }
    failures += expect_true(
        factory_user_set_hwmax_current(raised_hwmax) ==
            SYS_PRODUCT_CURRENT_VALID &&
        HWMAX_OUTCUR == raised_hwmax,
        "Factory HWMAX may change below Hardware Max");
    failures += expect_true(
        factory_user_set_runtime_current(raised_hwmax) ==
            SYS_PRODUCT_CURRENT_VALID &&
        SET_OUTCUR == raised_hwmax,
        "User SET persists independently up to HWMAX");
    failures += expect_true(
        factory_user_set_hwmax_current(raised_hwmax - 1U) ==
            SYS_PRODUCT_CURRENT_HW_MAX,
        "HWMAX below current SET is rejected");
    failures += expect_true(
        factory_user_set_hwmax_current(
            SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA + 1U) ==
            SYS_PRODUCT_CURRENT_HW_MAX,
        "HWMAX above selected Hardware Max is rejected");
    failures += expect_true(
        factory_user_set_hwmax_current(
            SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA) ==
            SYS_PRODUCT_CURRENT_VALID &&
        factory_user_set_runtime_current(
            SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA) ==
            SYS_PRODUCT_CURRENT_VALID,
        "selected SET<=HWMAX<=Hardware Max boundary is accepted");

    memcpy(candidate, sys_data.fa_Parambuf, sizeof(candidate));
    put_u16be(candidate, 0x10U, 1200U);
    put_u16be(candidate, 0x12U, 1100U);
    failures += expect_true(
        factory_user_validate_candidate(candidate) ==
            SYS_PRODUCT_CURRENT_HW_MAX,
        "combined candidate enforces SET<=HWMAX");

    store_success = BOOL_FALSE;
    failures += expect_true(
        factory_user_set_runtime_current(
            SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA - 1U) ==
            SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE &&
        SET_OUTCUR == SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA,
        "failed CFG1 commit rolls User SET back in RAM");
    store_success = BOOL_TRUE;

    for (state = (u8)SYS_CALIBRATION_STATE_ACTIVE;
         state <= (u8)SYS_CALIBRATION_STATE_COMMITTED; ++state)
    {
        calibration_state = (sys_calibration_state_en)state;
        failures += expect_true(
            factory_user_validate_candidate(candidate) ==
                SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE &&
            factory_user_set_runtime_current(
                SYS_PRODUCT_PROFILE_CURRENT_DEFAULT_CURRENT_MA) ==
                SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE &&
            factory_user_set_hwmax_current(
                SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA) ==
                SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE,
            "ACTIVE/STAGED/APPLIED/COMMITTED all reject Factory current writes");
    }
    calibration_state = SYS_CALIBRATION_STATE_IDLE;
    failures += expect_true(
        factory_user_set_runtime_current(
            SYS_PRODUCT_PROFILE_CURRENT_DEFAULT_CURRENT_MA) ==
            SYS_PRODUCT_CURRENT_VALID,
        "Factory current writes resume after returning to IDLE");
    failures += expect_true(store_calls == 6U,
                            "five successful and one failed Config commits observed");
    failures += expect_true(pwm_reload_calls == 5U,
                            "successful SET/HWMAX changes apply immediately");

    if (failures != 0)
    {
        fprintf(stderr, "factory Config V3 failures: %d\n", failures);
        return 1;
    }
    puts("factory Config selected-target V3 tests: PASS");
    return 0;
}
