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
    u16 encoded_length;
    u8 safe_percent;
    int failures = 0;

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
    memcpy(dc_mutated, golden_dc5200_reply, sizeof(dc_mutated));
    dc_mutated[7U] = 0x3BU;
    failures += expect_true(sys_calibration_dc5200_validate_comprehensive_reply(
                                dc_mutated, sizeof(dc_mutated)) == BOOL_FALSE,
                            "DC5200 bad length is rejected");
    failures += expect_true(sizeof(golden_table_frame) == 205U,
                            "golden table frame is 205 bytes");
    failures += expect_true(sys_calibration_driver_decode(
                                golden_table_frame, sizeof(golden_table_frame),
                                &message) == BOOL_TRUE,
                            "golden driver table frame decodes");
    failures += expect_true(message.command == SYS_CALIBRATION_DRIVER_CMD_SET &&
                                message.offset == SYS_CALIBRATION_DRIVER_OFFSET_TABLE &&
                                message.length == 198U,
                            "golden table envelope fields");
    failures += expect_true(sys_calibration_driver_table_decode(
                                message.data, message.length, &table) == BOOL_TRUE,
                            "golden table payload decodes");
    failures += expect_true(table.point[0].level == 0U &&
                                table.point[0].input_voltage_01v == 0x08A3U &&
                                table.point[10].level == 200U &&
                                table.point[10].device_output_power_01w == 0x0459U,
                            "golden table big-endian fields");
    failures += expect_true(sys_calibration_driver_table_encode(
                                &table, payload, sizeof(payload)) == BOOL_TRUE,
                            "table encodes");
    failures += expect_true(memcmp(payload, message.data, sizeof(payload)) == 0,
                            "table encode roundtrip is byte exact");
    failures += expect_true(sys_calibration_driver_encode(
                                message.command, message.offset, message.data,
                                message.length, encoded, sizeof(encoded),
                                &encoded_length) == BOOL_TRUE,
                            "golden table envelope encodes");
    failures += expect_true(memcmp(encoded, golden_table_frame,
                                   sizeof(golden_table_frame)) == 0,
                            "golden table frame roundtrip is byte exact");

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
                            "set point golden envelope");
    {
        const u8 query_frame[7U] = {0x3A, 0x26, 0x08, 0x00, 0x2E, 0x0D, 0x0A};
        const u8 reply_frame[13U] =
            {0x3A, 0x27, 0x08, 0x06, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x23, 0x58, 0x0D, 0x0A};
        failures += expect_true(sys_calibration_driver_decode(
                                    query_frame, 7U, &message) == BOOL_TRUE &&
                                    message.command == SYS_CALIBRATION_DRIVER_CMD_QUERY,
                                "query golden envelope");
        failures += expect_true(sys_calibration_driver_decode(
                                    reply_frame, 13U, &message) == BOOL_TRUE &&
                                    message.command == SYS_CALIBRATION_DRIVER_CMD_QUERY_REPLY,
                                "query reply golden envelope");
    }

    memcpy(encoded, golden_table_frame, sizeof(encoded));
    encoded[sizeof(encoded) - 3U] ^= 1U;
    failures += expect_true(sys_calibration_driver_decode(
                                encoded, sizeof(encoded), &message) == BOOL_FALSE,
                            "bad checksum is rejected");
    memcpy(encoded, golden_table_frame, sizeof(encoded));
    encoded[3] = 0xC5U;
    failures += expect_true(sys_calibration_driver_decode(
                                encoded, sizeof(encoded), &message) == BOOL_FALSE,
                            "bad length is rejected");
    memcpy(encoded, golden_table_frame, sizeof(encoded));
    encoded[0] = 0x3BU;
    failures += expect_true(sys_calibration_driver_decode(
                                encoded, sizeof(encoded), &message) == BOOL_FALSE,
                            "bad header is rejected");

    memset(&max_message, 0, sizeof(max_message));
    max_message.command = SYS_CALIBRATION_DRIVER_CMD_SET;
    max_message.offset = SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT;
    max_message.length = sizeof(max_payload);
    memcpy(max_message.data, max_payload, sizeof(max_payload));
    failures += expect_true(sys_calibration_driver_validate_message(
                                &max_message) == BOOL_TRUE,
                            "maximum context validates");
    failures += expect_true(sys_calibration_driver_max_context_decode(
                                max_payload, sizeof(max_payload), &max_context) ==
                                BOOL_TRUE &&
                                max_context.input_ac_voltage_float_bits == 0x435D2042UL &&
                                max_context.maximum_output_voltage_01v == 0x0616U &&
                                max_context.maximum_output_current_ma == 0x042BU,
                            "maximum context is big endian");
    failures += expect_true(sys_calibration_driver_measurement_decode(
                                measure_payload, sizeof(measure_payload), &measurement) ==
                                BOOL_TRUE && measurement.device_output_current_ma == 0x0102U &&
                                measurement.device_output_power_01w == 0x0064U &&
                                measurement.input_current_ad == 0x0023U,
                            "measurement reply is big endian");

    failures += expect_true(sys_calibration_curve_validate_pwm(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT) == BOOL_TRUE,
                            "11 point curve is monotonic");
    failures += expect_true(sys_calibration_curve_interpolate(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT, 10U,
                                &interpolated) == BOOL_TRUE && interpolated == 50U,
                            "curve interpolation boundary");
    pwm[5] = 1001U;
    failures += expect_true(sys_calibration_curve_validate_pwm(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT) == BOOL_FALSE,
                            "PWM overflow is rejected");
    pwm[5] = 500U;
    pwm[6] = 400U;
    failures += expect_true(sys_calibration_curve_validate_pwm(
                                pwm, SYS_CALIBRATION_CURVE_POINT_COUNT) == BOOL_FALSE,
                            "non-monotonic curve is rejected");
    failures += expect_true(sys_calibration_curve_validate_context(
                                SYS_CALIBRATION_50W_MID,
                                SYS_CALIBRATION_50W_RS3_MOHM,
                                SYS_CALIBRATION_50W_RATED_CURRENT_MA) == BOOL_TRUE,
                            "50W context validates");
    failures += expect_true(sys_calibration_curve_validate_context(
                                3U, 50U, 1780U) == BOOL_FALSE,
                            "100W context remains disabled");

    failures += expect_true(sys_calibration_safety_limit_current_ma(250U) == 1400U &&
                                sys_calibration_safety_limit_current_ma(360U) == 1388U &&
                                sys_calibration_safety_limit_current_ma(560U) == 892U &&
                                sys_calibration_safety_limit_current_ma(249U) == 0U,
                            "50W 25/36/56V current and power cap");
    failures += expect_true(sys_calibration_safety_limit_percent(
                                100U, 400U, 1400U, &safe_percent) == BOOL_TRUE &&
                                safe_percent == 89U,
                            "50W percent cap");
    failures += expect_true(sys_calibration_safety_is_absolute_overcurrent(1680U) ==
                                BOOL_TRUE &&
                                sys_calibration_safety_is_absolute_overcurrent(1679U) ==
                                BOOL_FALSE,
                            "1.68A absolute current fail-off threshold");
    failures += expect_true(
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL, BOOL_FALSE,
            BOOL_FALSE, BOOL_TRUE) == 1000U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN, BOOL_TRUE,
            BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT, BOOL_FALSE,
            BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT, BOOL_FALSE,
            BOOL_FALSE, BOOL_FALSE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL, BOOL_FALSE,
            BOOL_TRUE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, (sys_calibration_output_source_en)99U, BOOL_FALSE,
            BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            0U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT, BOOL_TRUE,
            BOOL_TRUE, BOOL_FALSE) == 0U,
        "normal/offline/direct inhibit, emergency, invalid and stale feedback gates");

    failures += expect_true(sys_calibration_storage_record_build(
                                &first_record, 1U, SYS_CALIBRATION_50W_MID,
                                SYS_CALIBRATION_50W_RS3_MOHM, payload,
                                sizeof(payload)) == BOOL_TRUE &&
                                sys_calibration_storage_record_validate(&first_record) ==
                                BOOL_TRUE,
                            "A record builds and validates");
    failures += expect_true(sys_calibration_storage_record_build(
                                &second_record, 2U, SYS_CALIBRATION_50W_MID,
                                SYS_CALIBRATION_50W_RS3_MOHM, payload,
                                sizeof(payload)) == BOOL_TRUE,
                            "B record builds");
    failures += expect_true(sys_calibration_storage_select_newest(
                                &first_record, &second_record, &selected) == BOOL_TRUE &&
                                selected == &second_record,
                            "newest A/B generation selected");
    second_record.payload[0] ^= 1U;
    failures += expect_true(sys_calibration_storage_record_validate(&second_record) ==
                                BOOL_FALSE &&
                                sys_calibration_storage_select_newest(
                                    &first_record, &second_record, &selected) == BOOL_TRUE &&
                                selected == &first_record,
                            "torn payload is not selected");
    first_record.commit_word = 0xFFFFFFFFUL;
    failures += expect_true(sys_calibration_storage_record_validate(&first_record) ==
                                BOOL_FALSE,
                            "torn commit is not valid");

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
                                    &inhibit_first, &inhibit_second, &inhibit_state) ==
                                BOOL_TRUE && inhibit_state ==
                                SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE,
                            "newest boot-inhibit state selected");
    inhibit_second.record_crc32 ^= 1U;
    failures += expect_true(sys_calibration_boot_inhibit_select_newest(
                                &inhibit_first, &inhibit_second, &inhibit_state) == BOOL_TRUE &&
                                inhibit_state == SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE,
                            "invalid boot-inhibit slot is ignored");
    memset(&inhibit_first, 0xFF, sizeof(inhibit_first));
    memset(&inhibit_second, 0xFF, sizeof(inhibit_second));
    inhibit_state = SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE;
    failures += expect_true(
        sys_calibration_boot_inhibit_select_newest(
            &inhibit_first, &inhibit_second, &inhibit_state) == BOOL_FALSE &&
        inhibit_state == SYS_CALIBRATION_BOOT_INHIBIT_UNKNOWN,
        "pristine boot-inhibit slots fail closed without unlock");

    if (failures != 0)
    {
        return 1;
    }
    printf("protocol and safety host tests: PASS\n");
    return 0;
}
