#include <stdio.h>
#include <string.h>

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
        profile->profile_id == 50U &&
        profile->mid == 1U &&
        profile->hardware_revision == 0x0101U &&
        profile->rated_power_w == 50U &&
        profile->rs3_mohm == 120U &&
        profile->hw_max_current_ma == 1680U &&
        profile->default_hwmax_current_ma == 1400U &&
        profile->default_runtime_current_ma == 893U &&
        SYS_PRODUCT_PROFILE_50W_FORMAL_POINT_COUNT == 11U &&
        SYS_PRODUCT_PROFILE_50W_LEVEL_MIN == 0U &&
        SYS_PRODUCT_PROFILE_50W_LEVEL_MAX == 200U &&
        SYS_PRODUCT_PROFILE_50W_LEVEL_STEP == 20U &&
        profile->pwm_full_scale == 1000U &&
        profile->pwm_polarity == 1U &&
        profile->oco_hardware_revision == 0x0101U,
        "50W V3 Product Profile fields are frozen");
    failures += expect_true(
        profile == sys_product_profile_current(),
        "current() is the only runtime Product Profile accessor");
    failures += expect_true(
        sys_product_profile_is_complete(profile) == BOOL_TRUE,
        "50W Product Profile is complete");

    fixture_length = load_hex_fixture(
        argv[1], fixture, (u16)sizeof(fixture));
    failures += expect_true(
        fixture_length == SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH,
        "G3 fixture is exactly 18 bytes");
    failures += expect_true(
        sys_product_profile_encode_fingerprint(
            profile, encoded, (u16)sizeof(encoded)) == BOOL_TRUE &&
        memcmp(encoded, fixture, sizeof(encoded)) == 0,
        "50W G3 fingerprint bytes match explicit LE fixture");
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
        sys_product_profile_calculate_fingerprint(profile) == 0x42EE2391UL &&
        profile->fingerprint_crc32 == 0x42EE2391UL,
        "G3 fingerprint CRC matches frozen 50W vector");

    if (failures == 0)
    {
        printf("product profile V3 tests: PASS\n");
    }
    return failures == 0 ? 0 : 1;
}
