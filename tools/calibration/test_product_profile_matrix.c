#include <stdio.h>

#include "sys_product_profile.h"

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 i100_36 = 0U;
    u16 i100_56 = 0U;
    int failures = 0;

    failures += expect_true(profile != NULL, "selected profile exists");
    if (profile == NULL)
    {
        return 1;
    }

    failures += expect_true(profile->profile_id == (u16)SYS_PRODUCT_PROFILE_SELECT,
                            "selected profile id matches build define");
    failures += expect_true(profile->build_enabled == BOOL_TRUE &&
                                profile->nonzero_calibration_enabled == BOOL_TRUE,
                            "selected profile has no artificial calibration gate");
    failures += expect_true(profile->absolute_fail_current_ma == profile->hw_max_current_ma,
                            "physical fail threshold matches frozen Hardware Max");
    failures += expect_true(sys_product_profile_calculate_fingerprint(profile) ==
                                profile->fingerprint_crc32,
                            "profile fingerprint matches material");
    failures += expect_true(sys_product_profile_is_complete(profile) == BOOL_TRUE,
                            "selected profile is complete");
    failures += expect_true(profile->iv_limits != NULL && profile->iv_limit_count == 9U &&
                                profile->iv_limits[0].voltage_01v == 250U &&
                                profile->iv_limits[8].voltage_01v == 560U,
                            "selected profile has full 25..56V I-V table");
    failures += expect_true(sys_product_profile_compute_i100_ma(profile, 360U, &i100_36) == BOOL_TRUE &&
                                i100_36 > 0U,
                            "selected profile computes 36V calibration full scale");
    failures += expect_true(sys_product_profile_compute_i100_ma(profile, 560U, &i100_56) == BOOL_TRUE &&
                                i100_56 > 0U,
                            "selected profile computes 56V calibration full scale");
    failures += expect_true(sys_product_profile_validate_runtime_current(
                                profile, 360U, profile->default_runtime_current_ma) ==
                                SYS_PRODUCT_CURRENT_VALID,
                            "default SET_OUTCUR is valid at common 36V calibration point");

    if (failures != 0)
    {
        return 1;
    }

    printf("%uW legacy profile matrix: PASS (I100@36=%umA I100@56=%umA)\n",
           (unsigned)profile->rated_power_w,
           (unsigned)i100_36,
           (unsigned)i100_56);
    return 0;
}
