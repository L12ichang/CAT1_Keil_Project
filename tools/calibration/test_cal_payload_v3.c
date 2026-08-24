#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "sys_calibration_driver_protocol.h"

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static int hex_value(int value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    return -1;
}

static int load_hex_fixture(const char *path, u8 *bytes, u16 capacity)
{
    FILE *file;
    int high = -1;
    int value;
    int nibble;
    u16 length = 0U;

    file = fopen(path, "r");
    if (file == NULL)
    {
        return -1;
    }
    while ((value = fgetc(file)) != EOF)
    {
        if (isspace(value))
        {
            continue;
        }
        nibble = hex_value(value);
        if (nibble < 0)
        {
            fclose(file);
            return -1;
        }
        if (high < 0)
        {
            high = nibble;
        }
        else
        {
            if (length >= capacity)
            {
                fclose(file);
                return -1;
            }
            bytes[length++] = (u8)((high << 4) | nibble);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 ? (int)length : -1;
}

static int load_text_fixture(const char *path, char *text, u16 capacity)
{
    FILE *file;
    size_t length;

    if (capacity == 0U)
    {
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL)
    {
        return -1;
    }
    length = fread(text, 1U, (size_t)capacity - 1U, file);
    if (ferror(file) != 0)
    {
        fclose(file);
        return -1;
    }
    text[length] = '\0';
    fclose(file);
    return (int)length;
}

static u16 get_u16_le(const u8 *bytes)
{
    return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8U));
}

static u32 get_u32_le(const u8 *bytes)
{
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8U) |
           ((u32)bytes[2] << 16U) |
           ((u32)bytes[3] << 24U);
}

static void fill_g4(sys_calibration_payload_st *payload)
{
    static const u16 output_reference[11] =
        {0U, 89U, 179U, 268U, 357U, 447U, 536U, 625U, 714U, 804U, 893U};
    u8 index;

    memset(payload, 0, sizeof(*payload));
    payload->profile_id = 50U;
    payload->profile_version = 1U;
    payload->profile_fingerprint = 0x11223344UL;
    payload->point_count = 11U;
    payload->level_step = 20U;
    payload->valid_flags = 0x001FU;
    for (index = 0U; index < 11U; ++index)
    {
        payload->output[index].logical_pwm = (u16)((u16)index * 100U);
        payload->output[index].reference_output_current_ma =
            output_reference[index];
        payload->oco[index].oco_adc_raw = (u16)(1000U + (u16)index * 100U);
        payload->oco[index].reference_output_current_ma = output_reference[index];
        payload->bl_current[index].bl_current_raw =
            100000UL + (u32)index * 10000UL;
        payload->bl_current[index].reference_input_current_ma =
            (u16)((u16)index * 20U);
        payload->bl_power[index].bl_power_raw =
            (s32)(200000UL + (u32)index * 20000UL);
        payload->bl_power[index].reference_input_power_01w =
            (u16)((u16)index * 50U);
    }
    payload->voltage_gain_q24 = 0x01000000UL;
}

int main(int argc, char **argv)
{
    sys_calibration_payload_st source;
    sys_calibration_payload_st decoded;
    const sys_product_profile_st *profile;
    u8 fixture[SYS_CALIBRATION_PAYLOAD_LENGTH];
    u8 encoded[SYS_CALIBRATION_PAYLOAD_LENGTH];
    u8 record[272U];
    u8 fingerprint[SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH];
    u8 fingerprint_fixture[SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH];
    char hex[SYS_CALIBRATION_PAYLOAD_HEX_LENGTH + 1U];
    char lower_hex[SYS_CALIBRATION_PAYLOAD_HEX_LENGTH + 1U];
    char text_fixture[128U];
    int fixture_length;
    int record_length;
    u16 index;
    int failures = 0;

    if (argc != 6)
    {
        fprintf(stderr, "usage: %s G1 G2 G3 G4_PAYLOAD G5_RECORD\n", argv[0]);
        return 2;
    }
    failures += expect_true(
        load_text_fixture(argv[1], text_fixture,
                          (u16)sizeof(text_fixture)) > 0 &&
        strcmp(text_fixture,
               "input=123456789\ncrc32=CBF43926\n") == 0,
        "G1 fixture freezes the CRC check input and result");
    failures += expect_true(
        load_text_fixture(argv[2], text_fixture,
                          (u16)sizeof(text_fixture)) > 0 &&
        strcmp(text_fixture,
               "u16_1234=3412\nu32_12345678=78563412\ns32_minus_2=FEFFFFFF\n") == 0,
        "G2 fixture freezes u16/u32/s32 Little Endian bytes");
    fill_g4(&source);
    fixture_length = load_hex_fixture(
        argv[4], fixture, (u16)sizeof(fixture));
    failures += expect_true(
        fixture_length == SYS_CALIBRATION_PAYLOAD_LENGTH,
        "G4 fixture length is 244 bytes");
    failures += expect_true(
        sys_calibration_payload_encode(
            &source, encoded, (u16)sizeof(encoded)) == BOOL_TRUE &&
        memcmp(encoded, fixture, sizeof(encoded)) == 0,
        "G4 explicit LE codec matches all 244 bytes");
    failures += expect_true(
        sys_calibration_payload_crc32_iso_hdlc(
            (const u8 *)"123456789", 9U) == 0xCBF43926UL &&
        sys_calibration_payload_crc32_iso_hdlc(
            encoded, (u16)sizeof(encoded)) == 0x79400F5FUL,
        "G1 and G4 CRC32 vectors match");
    failures += expect_true(
        sys_calibration_payload_decode(
            fixture, (u16)sizeof(fixture), &decoded) == BOOL_TRUE &&
        decoded.bl_power[10].bl_power_raw == 400000L &&
        decoded.voltage_gain_q24 == 0x01000000UL,
        "G4 decoder preserves signed/raw fields and GainQ24");
    decoded = source;
    decoded.bl_power[0].bl_power_raw = -2;
    failures += expect_true(
        sys_calibration_payload_encode(
            &decoded, encoded, (u16)sizeof(encoded)) == BOOL_TRUE &&
        encoded[SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET] == 0xFEU &&
        encoded[SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET + 1U] == 0xFFU &&
        encoded[SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET + 2U] == 0xFFU &&
        encoded[SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET + 3U] == 0xFFU,
        "G2 signed -2 encodes as FE FF FF FF");

    profile = sys_product_profile_current();
    failures += expect_true(
        load_hex_fixture(argv[3], fingerprint_fixture,
                         (u16)sizeof(fingerprint_fixture)) ==
            SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH &&
        sys_product_profile_encode_fingerprint(
            profile, fingerprint, (u16)sizeof(fingerprint)) == BOOL_TRUE &&
        memcmp(fingerprint, fingerprint_fixture, sizeof(fingerprint)) == 0 &&
        fingerprint[0] == 0x01U && fingerprint[2] == 0x32U &&
        fingerprint[0x0BU] == 0x90U && fingerprint[0x0CU] == 0x06U,
        "G2/G3 Product Fingerprint remains explicit Little Endian");
    failures += expect_true(
        sys_calibration_payload_matches_product(&decoded, profile) == BOOL_FALSE,
        "G4 0x11223344 cross-end vector is not accepted as device identity");
    decoded.profile_fingerprint = profile->fingerprint_crc32;
    failures += expect_true(
        sys_calibration_payload_matches_product(&decoded, profile) == BOOL_TRUE,
        "runtime payload accepts only the current Product Fingerprint");

    failures += expect_true(
        sys_calibration_payload_hex_encode(
            fixture, (u16)sizeof(fixture), hex, (u16)sizeof(hex)) == BOOL_TRUE &&
        strlen(hex) == SYS_CALIBRATION_PAYLOAD_HEX_LENGTH,
        "payloadHex encoder emits exactly 488 uppercase characters");
    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_HEX_LENGTH; ++index)
    {
        lower_hex[index] = (char)tolower((unsigned char)hex[index]);
    }
    lower_hex[SYS_CALIBRATION_PAYLOAD_HEX_LENGTH] = '\0';
    memset(encoded, 0, sizeof(encoded));
    failures += expect_true(
        sys_calibration_payload_hex_decode(
            lower_hex, encoded, (u16)sizeof(encoded)) == BOOL_TRUE &&
        memcmp(encoded, fixture, sizeof(encoded)) == 0,
        "payloadHex parser accepts lowercase without introducing an alias");
    lower_hex[17] = 'G';
    failures += expect_true(
        sys_calibration_payload_hex_decode(
            lower_hex, encoded, (u16)sizeof(encoded)) == BOOL_FALSE,
        "payloadHex rejects non-hex characters");

    decoded = source;
    decoded.valid_flags = 0x000FU;
    failures += expect_true(
        sys_calibration_payload_validate(&decoded) == BOOL_FALSE,
        "payload rejects partial validFlags");
    decoded = source;
    decoded.output[5].logical_pwm = 499U;
    failures += expect_true(
        sys_calibration_payload_validate(&decoded) == BOOL_FALSE,
        "payload rejects a non-canonical logical PWM point");
    decoded = source;
    decoded.bl_current[5].bl_current_raw =
        decoded.bl_current[4].bl_current_raw - 1U;
    failures += expect_true(
        sys_calibration_payload_validate(&decoded) == BOOL_FALSE,
        "payload rejects a non-monotonic correction section");
    decoded = source;
    decoded.voltage_gain_q24 = 0U;
    failures += expect_true(
        sys_calibration_payload_validate(&decoded) == BOOL_FALSE,
        "payload rejects a zero Voltage GainQ24");

    record_length = load_hex_fixture(argv[5], record, (u16)sizeof(record));
    failures += expect_true(
        record_length == 272 && memcmp(record, "CAL4", 4U) == 0 &&
        get_u16_le(&record[0x04]) == 4U &&
        get_u16_le(&record[0x06]) == 272U &&
        get_u32_le(&record[0x08]) == 7U &&
        get_u16_le(&record[0x0C]) == 244U &&
        get_u16_le(&record[0x0E]) == 0U &&
        get_u32_le(&record[0x10]) == 0x79400F5FUL &&
        memcmp(&record[0x14], fixture, sizeof(fixture)) == 0 &&
        get_u32_le(&record[0x108]) ==
            sys_calibration_payload_crc32_iso_hdlc(record, 0x108U) &&
        get_u32_le(&record[0x10C]) == 0xC0A17EEDUL,
        "G5 CAL4 generation 7 fixture has exact offsets and CRC coverage");

    if (failures == 0)
    {
        printf("Calibration V3 G1-G5 codec tests: PASS\n");
    }
    return failures == 0 ? 0 : 1;
}
