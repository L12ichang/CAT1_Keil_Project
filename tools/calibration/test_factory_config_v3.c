#include "factory_user_data.h"
#include "sys_data.h"
#include "sys_calibration_service.h"
#include <stdio.h>
#include <string.h>

sys_data_st sys_data __attribute__((aligned(4)));
static u32 store_calls;
static boolean_en calibration_active;
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

boolean_en sys_calibration_service_is_output_authorized(void)
{
    return calibration_active;
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

    memset(&sys_data, 0, sizeof(sys_data));
    memset(sys_data.fa_Parambuf, 0xFF, sizeof(sys_data.fa_Parambuf));
    calibration_active = BOOL_FALSE;
    factory_user_load_data();
    failures += expect_true(MID == 1U && SET_OUTCUR == 893U &&
                            HWMAX_OUTCUR == 1400U && OUTPUT_CUR_SENSOR == 120U,
                            "50W V3 Config defaults are 893/1400/120/MID1");
    failures += expect_true(
        factory_user_validate_runtime_current(0U, SET_OUTCUR) ==
            SYS_PRODUCT_CURRENT_VALID,
        "runtime SET validity is not bound to voltage/calibration context");

    failures += expect_true(
        factory_user_set_runtime_current(1401U) == SYS_PRODUCT_CURRENT_HW_MAX,
        "SET above current HWMAX is rejected");
    failures += expect_true(
        factory_user_set_hwmax_current(1500U) == SYS_PRODUCT_CURRENT_VALID &&
        HWMAX_OUTCUR == 1500U,
        "Factory HWMAX may change below Hardware Max");
    failures += expect_true(
        factory_user_set_runtime_current(1500U) == SYS_PRODUCT_CURRENT_VALID &&
        SET_OUTCUR == 1500U,
        "User SET persists independently up to HWMAX");
    failures += expect_true(
        factory_user_set_hwmax_current(1499U) == SYS_PRODUCT_CURRENT_HW_MAX,
        "HWMAX below current SET is rejected");
    failures += expect_true(
        factory_user_set_hwmax_current(1681U) == SYS_PRODUCT_CURRENT_HW_MAX,
        "HWMAX above 1680mA Hardware Max is rejected");
    failures += expect_true(
        factory_user_set_hwmax_current(1680U) == SYS_PRODUCT_CURRENT_VALID &&
        factory_user_set_runtime_current(1680U) == SYS_PRODUCT_CURRENT_VALID,
        "frozen SET<=HWMAX<=Hardware Max boundary accepts 1680mA");

    memcpy(candidate, sys_data.fa_Parambuf, sizeof(candidate));
    put_u16be(candidate, 0x10U, 1200U);
    put_u16be(candidate, 0x12U, 1100U);
    failures += expect_true(
        factory_user_validate_candidate(candidate) ==
            SYS_PRODUCT_CURRENT_HW_MAX,
        "combined candidate enforces SET<=HWMAX");

    store_success = BOOL_FALSE;
    failures += expect_true(
        factory_user_set_runtime_current(1600U) ==
            SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE &&
        SET_OUTCUR == 1680U,
        "failed CFG1 commit rolls User SET back in RAM");
    store_success = BOOL_TRUE;

    calibration_active = BOOL_TRUE;
    failures += expect_true(
        factory_user_set_runtime_current(1000U) ==
            SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE &&
        factory_user_set_hwmax_current(1600U) ==
            SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE,
        "active calibration keeps the existing configuration-write gate");
    failures += expect_true(store_calls == 5U,
                            "four successful and one failed Config commits observed");
    failures += expect_true(pwm_reload_calls == 4U,
                            "successful SET/HWMAX changes apply immediately");

    if (failures != 0)
    {
        fprintf(stderr, "factory Config V3 failures: %d\n", failures);
        return 1;
    }
    puts("factory Config V3 tests: PASS");
    return 0;
}
