#include <stdio.h>
#include <string.h>

#include "sys_product_profile.h"

#if defined(PRODUCT_TARGET_50W)
#define TEST_PROFILE_ID 50U
#define TEST_MODEL_CODE "DL-50Z-56T-MXG"
#define TEST_MID 1U
#define TEST_RATED_POWER_W 50U
#define TEST_RS3_MOHM 120U
#define TEST_HARDWARE_MAX_MA 1680U
#define TEST_DEFAULT_HWMAX_MA 1400U
#define TEST_DEFAULT_SET_OUTCUR_MA 893U
#define TEST_FINGERPRINT_CRC32 0x42EE2391UL
static const u16 test_iv_ma[9] =
    {1400U, 1400U, 1400U, 1400U, 1250U, 1140U, 1040U, 960U, 900U};
#elif defined(PRODUCT_TARGET_75W)
#define TEST_PROFILE_ID 75U
#define TEST_MODEL_CODE "DL-75Z-56T-MXG"
#define TEST_MID 2U
#define TEST_RATED_POWER_W 75U
#define TEST_RS3_MOHM 50U
#define TEST_HARDWARE_MAX_MA 2150U
#define TEST_DEFAULT_HWMAX_MA 2100U
#define TEST_DEFAULT_SET_OUTCUR_MA 1360U
#define TEST_FINGERPRINT_CRC32 0xCFE530F1UL
static const u16 test_iv_ma[9] =
    {2100U, 2100U, 2100U, 2100U, 1870U, 1700U, 1560U, 1440U, 1340U};
#elif defined(PRODUCT_TARGET_100W)
#define TEST_PROFILE_ID 100U
#define TEST_MODEL_CODE "DL-100Z-56T-MXG"
#define TEST_MID 3U
#define TEST_RATED_POWER_W 100U
#define TEST_RS3_MOHM 50U
#define TEST_HARDWARE_MAX_MA 2800U
#define TEST_DEFAULT_HWMAX_MA 2800U
#define TEST_DEFAULT_SET_OUTCUR_MA 1780U
#define TEST_FINGERPRINT_CRC32 0x1F8B54DCUL
static const u16 test_iv_ma[9] =
    {2800U, 2800U, 2800U, 2800U, 2500U, 2270U, 2080U, 1920U, 1780U};
#elif defined(PRODUCT_TARGET_150W)
#define TEST_PROFILE_ID 150U
#define TEST_MODEL_CODE "DL-150Z-56T-MXG"
#define TEST_MID 4U
#define TEST_RATED_POWER_W 150U
#define TEST_RS3_MOHM 30U
#define TEST_HARDWARE_MAX_MA 4500U
#define TEST_DEFAULT_HWMAX_MA 4200U
#define TEST_DEFAULT_SET_OUTCUR_MA 2700U
#define TEST_FINGERPRINT_CRC32 0xDDFFB026UL
static const u16 test_iv_ma[9] =
    {4200U, 4200U, 4200U, 4200U, 3750U, 3400U, 3130U, 2880U, 2680U};
#elif defined(PRODUCT_TARGET_200W)
#define TEST_PROFILE_ID 200U
#define TEST_MODEL_CODE "DL-200Z-56T-MXG"
#define TEST_MID 5U
#define TEST_RATED_POWER_W 200U
#define TEST_RS3_MOHM 15U
#define TEST_HARDWARE_MAX_MA 6000U
#define TEST_DEFAULT_HWMAX_MA 5600U
#define TEST_DEFAULT_SET_OUTCUR_MA 3600U
#define TEST_FINGERPRINT_CRC32 0x18EEAE96UL
static const u16 test_iv_ma[9] =
    {5600U, 5600U, 5600U, 5600U, 5000U, 4550U, 4170U, 3850U, 3570U};
#elif defined(PRODUCT_TARGET_240W)
#define TEST_PROFILE_ID 240U
#define TEST_MODEL_CODE "DL-240Z-56T-MXG"
#define TEST_MID 6U
#define TEST_RATED_POWER_W 240U
#define TEST_RS3_MOHM 15U
#define TEST_HARDWARE_MAX_MA 7000U
#define TEST_DEFAULT_HWMAX_MA 6700U
#define TEST_DEFAULT_SET_OUTCUR_MA 4300U
#define TEST_FINGERPRINT_CRC32 0xFFBD4C38UL
static const u16 test_iv_ma[9] =
    {6700U, 6700U, 6700U, 6700U, 6000U, 5450U, 5000U, 4620U, 4300U};
#endif

static const u16 test_iv_voltage_01v[9] =
    {250U, 290U, 320U, 360U, 400U, 440U, 480U, 520U, 560U};

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static int load_hex_fixture(const char *path, u8 *output, u16 capacity)
{
    FILE *fixture;
    unsigned int value;
    u16 length = 0U;

    fixture = fopen(path, "r");
    if (fixture == NULL)
    {
        return -1;
    }
    while (fscanf(fixture, "%2x", &value) == 1)
    {
        if (length >= capacity)
        {
            fclose(fixture);
            return -1;
        }
        output[length++] = (u8)value;
    }
    fclose(fixture);
    return (int)length;
}

int main(int argc, char **argv)
{
    const sys_product_profile_st *profile;
    u8 encoded[SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH];
    u8 fixture[SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH];
    sys_product_profile_iv_limit_st limit;
    u32 default_power_01w;
    u32 protected_power_limit_01w;
    u8 index;
    int fixture_length;
    int failures = 0;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s FINGERPRINT_FIXTURE\n", argv[0]);
        return 2;
    }

    profile = sys_product_profile_current();
    failures += expect_true(
        profile != NULL &&
        profile->profile_version == 1U &&
        profile->profile_id == TEST_PROFILE_ID &&
        strcmp(profile->model_code, TEST_MODEL_CODE) == 0 &&
        profile->mid == TEST_MID &&
        profile->hardware_revision == 0x0101U &&
        profile->rated_power_w == TEST_RATED_POWER_W &&
        profile->rs3_mohm == TEST_RS3_MOHM &&
        profile->hw_max_current_ma == TEST_HARDWARE_MAX_MA &&
        profile->default_hwmax_current_ma == TEST_DEFAULT_HWMAX_MA &&
        profile->default_runtime_current_ma == TEST_DEFAULT_SET_OUTCUR_MA &&
        profile->absolute_fail_current_ma == TEST_HARDWARE_MAX_MA &&
        profile->power_limit_tolerance_permille == 50U &&
        SYS_PRODUCT_PROFILE_FORMAL_POINT_COUNT == 11U &&
        SYS_PRODUCT_PROFILE_LEVEL_MIN == 0U &&
        SYS_PRODUCT_PROFILE_LEVEL_MAX == 200U &&
        SYS_PRODUCT_PROFILE_LEVEL_STEP == 20U &&
        profile->pwm_full_scale == 1000U &&
        profile->pwm_polarity == 1U &&
        profile->oco_hardware_revision == 0x0101U,
        "selected V3 Product Profile fields are frozen");

    default_power_01w =
        ((u32)profile->maximum_voltage_01v *
         profile->default_runtime_current_ma) / 1000U;
    protected_power_limit_01w =
        ((u32)profile->rated_power_w * 10U *
         (1000U + profile->power_limit_tolerance_permille)) / 1000U;
    failures += expect_true(
        protected_power_limit_01w ==
            ((u32)TEST_RATED_POWER_W * 105U) / 10U &&
        default_power_01w <= protected_power_limit_01w,
        "selected default output stays within the frozen 5% power limit");
    failures += expect_true(
        profile == sys_product_profile_current(),
        "current() is the only runtime Product Profile accessor");
    failures += expect_true(
        sys_product_profile_is_complete(profile) == BOOL_TRUE,
        "selected Product Profile is complete");

    for (index = 0U; index < 9U; ++index)
    {
        failures += expect_true(
            sys_product_profile_get_iv_limit(profile, index, &limit) ==
                BOOL_TRUE &&
            limit.voltage_01v == test_iv_voltage_01v[index] &&
            limit.current_ma == test_iv_ma[index],
            "selected Product Profile I-V point matches the specification");
    }

    fixture_length = load_hex_fixture(
        argv[1], fixture, (u16)sizeof(fixture));
    failures += expect_true(
        fixture_length == SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH,
        "G3 fixture is exactly 18 bytes");
    failures += expect_true(
        sys_product_profile_encode_fingerprint(
            profile, encoded, (u16)sizeof(encoded)) == BOOL_TRUE &&
        memcmp(encoded, fixture, sizeof(encoded)) == 0,
        "selected fingerprint bytes match explicit LE fixture");
    failures += expect_true(
        sys_product_profile_encode_fingerprint(
            profile, encoded,
            SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH - 1U) == BOOL_FALSE,
        "fingerprint codec rejects undersized output");
    failures += expect_true(
        sys_product_profile_crc32_iso_hdlc(
            (const u8 *)"123456789", 9U) == 0xCBF43926UL,
        "G1 CRC-32/ISO-HDLC check vector");
    failures += expect_true(
        sys_product_profile_calculate_fingerprint(profile) ==
            TEST_FINGERPRINT_CRC32 &&
        profile->fingerprint_crc32 == TEST_FINGERPRINT_CRC32,
        "fingerprint CRC matches the selected frozen vector");

    if (failures == 0)
    {
        printf("product profile %uW V3 tests: PASS\n", TEST_RATED_POWER_W);
    }
    return failures == 0 ? 0 : 1;
}
