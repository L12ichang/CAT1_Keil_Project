#include <stdio.h>
#include <string.h>

#include "sys_calibration_boot_inhibit.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_dc5200.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_safety.h"
#include "sys_calibration_storage.h"

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static const u8 golden_table_frame[SYS_CALIBRATION_DRIVER_TABLE_FRAME_LENGTH] =
{
    0x3A, 0x24, 0x04, 0xC6, 0x00, 0x02, 0x08, 0xA3, 0x00, 0x6A,
    0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x24, 0x14, 0x26, 0x08, 0xA3, 0x00, 0xA1,
    0x00, 0x86, 0x00, 0x6E, 0x00, 0x73, 0x00, 0x6A, 0x00, 0x6F,
    0x01, 0x05, 0x28, 0x3E, 0x08, 0xA2, 0x00, 0xB9, 0x00, 0xFF,
    0x00, 0xDB, 0x00, 0xE5, 0x00, 0xD5, 0x00, 0xDF, 0x01, 0xC5,
    0x3C, 0x4C, 0x08, 0xA2, 0x00, 0xDD, 0x01, 0x75, 0x01, 0x44,
    0x01, 0x53, 0x01, 0x3F, 0x01, 0x4E, 0x02, 0x83, 0x50, 0x54,
    0x08, 0xA1, 0x01, 0x0A, 0x01, 0xEC, 0x01, 0xAF, 0x01, 0xC2,
    0x01, 0xA9, 0x01, 0xBD, 0x03, 0x49, 0x64, 0x58, 0x08, 0xA1,
    0x01, 0x3B, 0x02, 0x66, 0x02, 0x1C, 0x02, 0x34, 0x02, 0x15,
    0x02, 0x2E, 0x04, 0x14, 0x78, 0x5B, 0x08, 0xA1, 0x01, 0x6F,
    0x02, 0xE1, 0x02, 0x8A, 0x02, 0xA7, 0x02, 0x7F, 0x02, 0x9C,
    0x04, 0xE4, 0x8C, 0x5D, 0x08, 0xA1, 0x01, 0xA3, 0x03, 0x5A,
    0x02, 0xF7, 0x03, 0x18, 0x02, 0xE9, 0x03, 0x07, 0x05, 0xAD,
    0xA0, 0x5E, 0x08, 0xA0, 0x01, 0xD8, 0x03, 0xD0, 0x03, 0x60,
    0x03, 0x86, 0x03, 0x54, 0x03, 0x76, 0x06, 0x72, 0xB4, 0x5E,
    0x08, 0xA0, 0x02, 0x0E, 0x04, 0x45, 0x03, 0xC6, 0x03, 0xF1,
    0x03, 0xBE, 0x03, 0xEA, 0x07, 0x33, 0xC8, 0x5E, 0x08, 0x9F,
    0x02, 0x44, 0x04, 0xB7, 0x04, 0x2A, 0x04, 0x5A, 0x04, 0x28,
    0x04, 0x59, 0x07, 0xF5, 0xCC, 0x0D, 0x0A
};

static const u8 golden_dc5200_reply[SYS_CALIBRATION_DC5200_COMPREHENSIVE_FRAME_LENGTH] =
{
    0xAA, 0xBB, 0xCC, 0x02, 0x01, 0x01, 0x00, 0x3C,
    0x00, 0x03, 0x60, 0xA3, 0x00, 0x00, 0x05, 0xE1,
    0x00, 0x00, 0x24, 0x9E, 0x01, 0x19, 0x13, 0x92,
    0x00, 0x02, 0x60, 0xDF, 0x00, 0x00, 0x01, 0xF0,
    0x00, 0x00, 0x1E, 0x31, 0x00, 0x00, 0x20, 0x35,
    0x07, 0x23, 0x00, 0x0B, 0x00, 0x9A, 0x0C, 0x21,
    0x00, 0x00, 0x02, 0x36, 0x02, 0x44, 0x05, 0x92,
    0x0A, 0xFA, 0x00, 0x02, 0x60, 0xE7, 0x00, 0x00,
    0x01, 0xEF, 0x01, 0x85, 0x4E, 0x2B, 0xAA, 0xBB,
    0xCC, 0x02
};

typedef struct
{
    u16 id;
    const char *model;
    u32 fingerprint;
    u8 mid;
    u16 default_current_ma;
    u16 rs3_mohm;
    u16 hw_max_ma;
    u16 iv56_ma;
} expected_profile_st;

static const expected_profile_st expected_profiles[] =
{
    {50U,  "DL-50Z-56T-MXG",  0xA7777C1EUL, 1U,  890U, 120U, 1680U,  900U},
    {75U,  "DL-75Z-56T-MXG",  0xCE42B60EUL, 2U, 1360U,  50U, 2150U, 1340U},
    {100U, "DL-100Z-56T-MXG", 0xACEC0DDCUL, 3U, 1780U,  50U, 2800U, 1780U},
    {150U, "DL-150Z-56T-MXG", 0x64357DFDUL, 4U, 2700U,  30U, 4500U, 2680U},
    {200U, "DL-200Z-56T-MXG", 0x2364B8B9UL, 5U, 3600U,  15U, 6000U, 3570U},
    {240U, "DL-240Z-56T-MXG", 0x9B5756DAUL, 6U, 4300U,  15U, 7000U, 4300U}
};

int main(void)
{
    sys_calibration_driver_message_st message;
    sys_calibration_driver_message_st max_message;
    sys_calibration_driver_table_st table;
    sys_calibration_driver_max_context_st max_context;
    sys_calibration_driver_measurement_st measurement;
    sys_calibration_storage_record_st first_record;
    sys_calibration_storage_record_st second_record;
    const sys_calibration_storage_record_st *selected;
    sys_calibration_boot_inhibit_record_st inhibit_first;
    sys_calibration_boot_inhibit_record_st inhibit_second;
    sys_calibration_boot_inhibit_state_en inhibit_state;
    sys_calibration_context_st calibration_context;
    const sys_product_profile_st *profile;
    u8 encoded[SYS_CALIBRATION_DRIVER_TABLE_FRAME_LENGTH];
    u8 simple_frame[SYS_CALIBRATION_DRIVER_FRAME_OVERHEAD + 8U];
    u16 simple_length;
    u8 dc_query[SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH];
    u8 dc_mutated[SYS_CALIBRATION_DC5200_COMPREHENSIVE_FRAME_LENGTH];
    u16 dc_length;
    sys_calibration_dc5200_comprehensive_st dc_measurement;
    u8 payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
    u8 max_payload[8U] = {0x43, 0x5D, 0x20, 0x42, 0x06, 0x16, 0x04, 0x2B};
    u8 measure_payload[6U] = {0x01, 0x02, 0x00, 0x64, 0x00, 0x23};
    u16 pwm[SYS_CALIBRATION_CURVE_POINT_COUNT] =
        {0U, 100U, 200U, 300U, 400U, 500U, 600U, 700U, 800U, 900U, 1000U};
    u16 interpolated;
    u16 scaled_pwm;
    u16 encoded_length;
    u8 safe_percent;
    u32 profile_index;
    int failures = 0;

    profile = sys_product_profile_current();
    failures += expect_true(
        profile != NULL && profile->profile_id == SYS_PRODUCT_PROFILE_ID_50W &&
        profile->fingerprint_crc32 == SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32 &&
        sys_product_profile_calculate_fingerprint(profile) == profile->fingerprint_crc32 &&
        sys_product_profile_is_complete(profile) == BOOL_TRUE,
        "selected default 50W profile is complete");
    failures += expect_true(
        sys_product_profile_context_build(360U, 1388U, 1400U, 0U,
                                          &calibration_context) == BOOL_TRUE &&
        calibration_context.calibrated_max_current_ma == 1400U,
        "legacy measured Imax may exceed theoretical rated-power I100 within physical I-V");
    failures += expect_true(
        sys_product_profile_context_build(360U, 1388U, 1401U, 0U,
                                          &calibration_context) == BOOL_FALSE,
        "legacy measured Imax remains bounded by physical I-V");

    for (profile_index = 0U;
         profile_index < (u32)(sizeof(expected_profiles) / sizeof(expected_profiles[0]));
         ++profile_index)
    {
        const expected_profile_st *expected = &expected_profiles[profile_index];
        const sys_product_profile_st *candidate =
            sys_product_profile_find(expected->id);
        u16 i100_36 = 0U;
        u16 i100_56 = 0U;

        failures += expect_true(
            candidate != NULL &&
            strcmp(candidate->model_code, expected->model) == 0 &&
            candidate->fingerprint_crc32 == expected->fingerprint &&
            candidate->mid == expected->mid &&
            candidate->default_runtime_current_ma == expected->default_current_ma &&
            candidate->rs3_mohm == expected->rs3_mohm &&
            candidate->hw_max_current_ma == expected->hw_max_ma &&
            candidate->absolute_fail_current_ma == expected->hw_max_ma &&
            candidate->iv_limits != NULL && candidate->iv_limit_count == 9U &&
            candidate->iv_limits[0].voltage_01v == 250U &&
            candidate->iv_limits[8].voltage_01v == 560U &&
            candidate->iv_limits[8].current_ma == expected->iv56_ma &&
            candidate->build_enabled == BOOL_TRUE &&
            candidate->nonzero_calibration_enabled == BOOL_TRUE &&
            strcmp(candidate->block_code, "OK") == 0 &&
            sys_product_profile_calculate_fingerprint(candidate) == expected->fingerprint &&
            sys_product_profile_is_complete(candidate) == BOOL_TRUE,
            "six-power legacy profile identity and safety material are complete");
        failures += expect_true(
            sys_product_profile_compute_i100_ma(candidate, 360U, &i100_36) == BOOL_TRUE &&
            sys_product_profile_compute_i100_ma(candidate, 560U, &i100_56) == BOOL_TRUE &&
            i100_36 > 0U && i100_56 > 0U,
            "six-power profile computes 36V/56V calibration full scale");
        failures += expect_true(
            sys_product_profile_validate_runtime_current(
                candidate, 360U, candidate->default_runtime_current_ma) ==
                SYS_PRODUCT_CURRENT_VALID,
            "six-power default SET_OUTCUR is valid at common 36V calibration point");
    }

    {
        static const u8 dc_query_candidate_3c[SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH] =
            {0xAA, 0xBB, 0xCC, 0x01, 0x01, 0x01, 0x00, 0x02,
             0x00, 0x3C, 0xD2, 0x3A, 0xAA, 0xBB, 0xCC, 0x01};
        static const u8 dc_query_candidate_3a[SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH] =
            {0xAA, 0xBB, 0xCC, 0x01, 0x01, 0x01, 0x00, 0x02,
             0x00, 0x3A, 0xB2, 0xFC, 0xAA, 0xBB, 0xCC, 0x01};

        failures += expect_true(sys_calibration_storage_crc32(
                                    (const u8 *)"123456789", 9U) == 0xCBF43926UL,
                                "CRC32 version vector");
        dc_length = 0U;
        failures += expect_true(sys_calibration_dc5200_build_comprehensive_query(
                                    dc_query, sizeof(dc_query), &dc_length) == BOOL_FALSE &&
                                    dc_length == 0U,
                                "DC5200 query remains gated pending HIL capture");
        failures += expect_true(
            sys_calibration_dc5200_crc16_ccitt(dc_query_candidate_3c, 10U) == 0xD23AU &&
            dc_query_candidate_3c[10] == 0xD2U && dc_query_candidate_3c[11] == 0x3AU &&
            sys_calibration_dc5200_crc16_ccitt(dc_query_candidate_3a, 10U) == 0xB2FCU &&
            dc_query_candidate_3a[10] == 0xB2U && dc_query_candidate_3a[11] == 0xFCU,
            "DC5200 conflicting candidates are retained without selection");
    }

    failures += expect_true(sizeof(golden_dc5200_reply) == 74U &&
                                sys_calibration_dc5200_validate_comprehensive_reply(
                                    golden_dc5200_reply,
                                    sizeof(golden_dc5200_reply)) == BOOL_TRUE &&
                                sys_calibration_dc5200_decode_comprehensive_reply(
                                    golden_dc5200_reply,
                                    sizeof(golden_dc5200_reply),
                                    &dc_measurement) == BOOL_TRUE &&
                                dc_measurement.input_voltage_001v == 0x000360A3UL &&
                                dc_measurement.output_rms_voltage_001v == 0x000260DFUL &&
                                dc_measurement.output_average_current_0001a == 0x000001EFUL,
                            "DC5200 comprehensive golden reply and endian");
    memcpy(dc_mutated, golden_dc5200_reply, sizeof(dc_mutated));
    dc_mutated[68U] ^= 1U;
    failures += expect_true(sys_calibration_dc5200_validate_comprehensive_reply(
                                dc_mutated, sizeof(dc_mutated)) == BOOL_FALSE,
                            "DC5200 bad CRC is rejected");

    failures += expect_true(sizeof(golden_table_frame) == 205U,
                            "old 11-point table frame is 205 bytes");
    failures += expect_true(sys_calibration_driver_decode(
                                golden_table_frame, sizeof(golden_table_frame),
                                &message) == BOOL_TRUE &&
                                message.command == SYS_CALIBRATION_DRIVER_CMD_SET &&
                                message.offset == SYS_CALIBRATION_DRIVER_OFFSET_TABLE &&
                                message.length == 198U,
                            "old 198-byte table envelope decodes");
    failures += expect_true(sys_calibration_driver_table_decode(
                                message.data, message.length, &table) == BOOL_TRUE &&
                                table.point[0].level == 0U &&
                                table.point[0].input_voltage_01v == 0x08A3U &&
                                table.point[10].level == 200U &&
                                table.point[10].device_output_power_01w == 0x0459U,
                            "old table big-endian fields decode");
    failures += expect_true(sys_calibration_driver_table_encode(
                                &table, payload, sizeof(payload)) == BOOL_TRUE &&
                                memcmp(payload, message.data, sizeof(payload)) == 0,
                            "old table encode roundtrip is byte exact");
    failures += expect_true(sys_calibration_driver_encode(
                                message.command, message.offset, message.data,
                                message.length, encoded, sizeof(encoded),
                                &encoded_length) == BOOL_TRUE &&
                                encoded_length == sizeof(golden_table_frame) &&
                                memcmp(encoded, golden_table_frame,
                                       sizeof(golden_table_frame)) == 0,
                            "old table frame roundtrip is byte exact");

    failures += expect_true(sys_calibration_driver_encode(
                                SYS_CALIBRATION_DRIVER_CMD_SET,
                                SYS_CALIBRATION_DRIVER_OFFSET_LEVEL,
                                (const u8[]){0xC8U}, 1U,
                                simple_frame, sizeof(simple_frame),
                                &simple_length) == BOOL_TRUE &&
                                simple_length == 8U && simple_frame[0] == 0x3AU &&
                                simple_frame[1] == 0x24U && simple_frame[2] == 0x05U &&
                                simple_frame[3] == 0x01U && simple_frame[4] == 0xC8U &&
                                simple_frame[5] == 0xF2U && simple_frame[6] == 0x0DU &&
                                simple_frame[7] == 0x0AU,
                            "old SET LEVEL 200 golden envelope");

    {
        const u8 query_frame[7U] = {0x3A, 0x26, 0x08, 0x00, 0x2E, 0x0D, 0x0A};
        const u8 reply_frame[13U] =
            {0x3A, 0x27, 0x08, 0x06, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x23, 0x58, 0x0D, 0x0A};
        failures += expect_true(sys_calibration_driver_decode(
                                    query_frame, 7U, &message) == BOOL_TRUE &&
                                    message.command == SYS_CALIBRATION_DRIVER_CMD_QUERY,
                                "old QUERY measurement envelope");
        failures += expect_true(sys_calibration_driver_decode(
                                    reply_frame, 13U, &message) == BOOL_TRUE &&
                                    message.command == SYS_CALIBRATION_DRIVER_CMD_QUERY_REPLY,
                                "old QUERY measurement reply envelope");
    }

    memcpy(encoded, golden_table_frame, sizeof(encoded));
    encoded[sizeof(encoded) - 3U] ^= 1U;
    failures += expect_true(sys_calibration_driver_decode(
                                encoded, sizeof(encoded), &message) == BOOL_FALSE,
                            "bad table checksum is rejected");

    memset(&max_message, 0, sizeof(max_message));
    max_message.command = SYS_CALIBRATION_DRIVER_CMD_SET;
    max_message.offset = SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT;
    max_message.length = sizeof(max_payload);
    memcpy(max_message.data, max_payload, sizeof(max_payload));
    failures += expect_true(sys_calibration_driver_validate_message(
                                &max_message) == BOOL_TRUE &&
                                sys_calibration_driver_max_context_decode(
                                    max_payload, sizeof(max_payload), &max_context) == BOOL_TRUE &&
                                max_context.input_ac_voltage_float_bits == 0x435D2042UL &&
                                max_context.maximum_output_voltage_01v == 0x0616U &&
                                max_context.maximum_output_current_ma == 0x042BU,
                            "old UAC/Umax/Imax context is big endian");
    failures += expect_true(sys_calibration_driver_measurement_decode(
                                measure_payload, sizeof(measure_payload), &measurement) == BOOL_TRUE &&
                                measurement.device_output_current_ma == 0x0102U &&
                                measurement.device_output_power_01w == 0x0064U &&
                                measurement.input_current_ad == 0x0023U,
                            "old device measurement reply is big endian");

    failures += expect_true(sys_calibration_curve_validate_pwm(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT) == BOOL_TRUE &&
                                sys_calibration_curve_interpolate(
                                    pwm, SYS_CALIBRATION_CURVE_POINT_COUNT, 10U,
                                    &interpolated) == BOOL_TRUE && interpolated == 50U,
                            "11-point curve remains monotonic and interpolates");
    pwm[5] = 1001U;
    failures += expect_true(sys_calibration_curve_validate_pwm(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT) == BOOL_FALSE,
                            "PWM overflow is rejected");
    pwm[5] = 500U;

    failures += expect_true(sys_calibration_safety_limit_current_ma(250U) == 1400U &&
                                sys_calibration_safety_limit_current_ma(360U) == 1388U &&
                                sys_calibration_safety_limit_current_ma(560U) == 892U &&
                                sys_calibration_safety_limit_current_ma(249U) == 0U,
                            "default 50W voltage/current safety cap remains intact");
    failures += expect_true(sys_calibration_safety_limit_percent(
                                100U, 400U, 1400U, &safe_percent) == BOOL_TRUE &&
                                safe_percent == 89U,
                            "default 50W percent cap remains intact");
    failures += expect_true(sys_calibration_safety_is_absolute_overcurrent(1680U) == BOOL_TRUE &&
                                sys_calibration_safety_is_absolute_overcurrent(1679U) == BOOL_FALSE,
                            "default 50W absolute fail threshold remains intact");
    failures += expect_true(
        sys_product_profile_validate_runtime_current(profile, 560U, 890U) ==
            SYS_PRODUCT_CURRENT_VALID &&
        sys_product_profile_validate_runtime_current(profile, 560U, 893U) ==
            SYS_PRODUCT_CURRENT_POWER_LIMIT &&
        sys_product_profile_validate_runtime_current(profile, 360U, 1388U) ==
            SYS_PRODUCT_CURRENT_VALID &&
        sys_product_profile_validate_runtime_current(profile, 360U, 1389U) ==
            SYS_PRODUCT_CURRENT_POWER_LIMIT &&
        sys_product_profile_validate_runtime_current(profile, 250U, 1400U) ==
            SYS_PRODUCT_CURRENT_VALID &&
        sys_product_profile_validate_runtime_current(profile, 250U, 1401U) ==
            SYS_PRODUCT_CURRENT_IV_LIMIT &&
        sys_product_profile_validate_runtime_current(profile, 560U, 1680U) ==
            SYS_PRODUCT_CURRENT_ABSOLUTE_FAIL &&
        sys_product_profile_validate_runtime_current(profile, 580U, 890U) ==
            SYS_PRODUCT_CURRENT_VOLTAGE_UNBOUND,
        "actual operating point still enforces I-V/power/Hardware Max safety");
    failures += expect_true(
        sys_product_profile_scale_percent_to_pwm(
            profile, 560U, 45U, 1000U, &scaled_pwm) == BOOL_TRUE &&
        scaled_pwm == 238U,
        "legacy Level path keeps profile I100 PWM baseline");

    failures += expect_true(sys_product_profile_context_build(
                                560U, 892U, 890U,
                                sys_calibration_storage_crc32(payload, sizeof(payload)),
                                &calibration_context) == BOOL_TRUE &&
                                calibration_context.calibrated_max_current_ma == 890U,
                            "calibration context binds theoretical I100, old measured Imax and CRC");
    failures += expect_true(sys_calibration_storage_record_build(
                                &first_record, 1U, &calibration_context, payload,
                                sizeof(payload)) == BOOL_TRUE &&
                                sys_calibration_storage_record_validate(&first_record) == BOOL_TRUE,
                            "A calibration record builds and validates");
    failures += expect_true(sys_calibration_storage_record_build(
                                &second_record, 2U, &calibration_context, payload,
                                sizeof(payload)) == BOOL_TRUE &&
                                sys_calibration_storage_select_newest(
                                    &first_record, &second_record, &selected) == BOOL_TRUE &&
                                selected == &second_record,
                            "newest A/B calibration record is selected");
    second_record.payload[0] ^= 1U;
    failures += expect_true(sys_calibration_storage_record_validate(&second_record) == BOOL_FALSE &&
                                sys_calibration_storage_select_newest(
                                    &first_record, &second_record, &selected) == BOOL_TRUE &&
                                selected == &first_record,
                            "torn calibration payload is rejected");

    failures += expect_true(sys_calibration_boot_inhibit_record_build(
                                &inhibit_first, 1U,
                                SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE) == BOOL_TRUE &&
                                sys_calibration_boot_inhibit_record_validate(
                                    &inhibit_first) == BOOL_TRUE,
                            "active boot-inhibit record validates");
    failures += expect_true(sys_calibration_boot_inhibit_record_build(
                                &inhibit_second, 2U,
                                SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE) == BOOL_TRUE &&
                                sys_calibration_boot_inhibit_select_newest(
                                    &inhibit_first, &inhibit_second, &inhibit_state) == BOOL_TRUE &&
                                inhibit_state == SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE,
                            "newest boot-inhibit state is selected");
    inhibit_second.record_crc32 ^= 1U;
    failures += expect_true(sys_calibration_boot_inhibit_select_newest(
                                &inhibit_first, &inhibit_second, &inhibit_state) == BOOL_TRUE &&
                                inhibit_state == SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE,
                            "invalid boot-inhibit slot is ignored");

    if (failures != 0)
    {
        return 1;
    }

    printf("legacy 11-point protocol, six profiles and safety host tests: PASS\n");
    return 0;
}
