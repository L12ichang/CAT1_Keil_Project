#include <stdio.h>
#include <string.h>

#include "sys_calibration_snapshot.h"
#include "sys_calibration_service.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_safety.h"
#include "sys_calibration_storage.h"
#include "sys_bl0942_frame.h"

static unsigned int safe_off_calls;
static unsigned int set_level_calls;
static unsigned short last_level;
static unsigned int inhibit_calls;
static boolean_en inhibit_state;
static unsigned char committed_payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];

static u16 test_bound_voltage(void)
{
    return 560U;
}

static void test_safe_off(void)
{
    ++safe_off_calls;
}

static boolean_en test_set_level(u16 level)
{
    ++set_level_calls;
    last_level = level;
    return BOOL_TRUE;
}

static boolean_en test_set_inhibit(boolean_en active)
{
    ++inhibit_calls;
    inhibit_state = active;
    return BOOL_TRUE;
}

static boolean_en test_commit(const sys_calibration_context_st *context,
                              const u8 *payload, u16 length, u32 *generation)
{
    if (context == NULL || payload == NULL || generation == NULL ||
        sys_product_profile_context_validate(context, BOOL_TRUE) != BOOL_TRUE ||
        context->table_crc32 != sys_calibration_storage_crc32(payload, length) ||
        length != sizeof(committed_payload))
    {
        return BOOL_FALSE;
    }
    memcpy(committed_payload, payload, length);
    *generation = 3U;
    return BOOL_TRUE;
}

static void test_bind_platform(void)
{
    sys_calibration_service_bind_platform(test_set_level,
                                          test_set_inhibit,
                                          test_commit);
    sys_calibration_service_bind_bound_voltage(test_bound_voltage);
    sys_calibration_service_restore_boot(BOOL_FALSE, BOOL_TRUE);
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

static void put_u24_le(unsigned char *frame, unsigned int offset, unsigned int value)
{
    frame[offset] = (unsigned char)(value & 0xFFU);
    frame[offset + 1U] = (unsigned char)((value >> 8) & 0xFFU);
    frame[offset + 2U] = (unsigned char)((value >> 16) & 0xFFU);
}

static void put_u16_be(unsigned char *buffer, unsigned int offset,
                       unsigned int value)
{
    buffer[offset] = (unsigned char)(value >> 8U);
    buffer[offset + 1U] = (unsigned char)value;
}

int main(void)
{
    sys_calibration_meter_snapshot_st meter;
    sys_calibration_adc_snapshot_st adc;
    sys_calibration_pwm_snapshot_st pwm;
    sys_calibration_snapshot_aggregate_st aggregate;
    sys_calibration_service_status_st service_status;
    sys_calibration_context_st calibration_context;
    sys_calibration_context_st staged_context;
    unsigned char frame[SYS_BL0942_READ_FRAME_LENGTH];
    sys_bl0942_frame_st decoded_frame;
    int failures = 0;

    failures += expect_true(sys_product_profile_context_build(
                                560U, 890U, 0U, 0U,
                                &calibration_context) == BOOL_TRUE &&
                                calibration_context.calibrated_max_current_ma == 0U,
                            "BEGIN context has no calibrated maximum before sampling");

    sys_calibration_snapshot_init();
    failures += expect_true(sys_calibration_snapshot_read_meter(&meter) == BOOL_TRUE,
                            "empty meter cache is readable");
    failures += expect_true(meter.valid_flags == 0U, "empty meter cache is invalid");

    sys_calibration_snapshot_publish_meter(100U,
                                           0x010203U,
                                           0x040506U,
                                           0x070809U,
                                           -7,
                                           0x0A0B0CU,
                                           500U,
                                           0x80U,
                                           NULL,
                                           SYS_CALIBRATION_METER_FRAME_VALID |
                                               SYS_CALIBRATION_METER_HEAD_VALID |
                                               SYS_CALIBRATION_METER_CHECKSUM_VALID,
                                           3U);
    sys_calibration_snapshot_publish_adc(110U,
                                         11U,
                                         22U,
                                         33U,
                                         44U,
                                         SYS_CALIBRATION_ADC_SAMPLE_VALID);
    sys_calibration_snapshot_prepare_pwm(60U, 50U);
    sys_calibration_snapshot_publish_pwm(120U,
                                         321U,
                                         322U,
                                         1U,
                                         SYS_CALIBRATION_PWM_SAMPLE_VALID);

    failures += expect_true(sys_calibration_snapshot_read_meter(&meter) == BOOL_TRUE,
                            "meter cache read");
    failures += expect_true(meter.seq == 1U, "meter sequence");
    failures += expect_true(meter.i_rms_raw == 0x010203U, "meter 24-bit raw value");
    failures += expect_true(meter.watt_raw == -7, "signed watt raw value");
    failures += expect_true(meter.status_raw == 0x80U, "meter status");
    failures += expect_true(meter.frame_error_count == 3U, "meter error count");

    failures += expect_true(sys_calibration_snapshot_read_adc(&adc) == BOOL_TRUE,
                            "ADC cache read");
    failures += expect_true(adc.seq == 1U && adc.iout_raw == 44U, "ADC values and sequence");

    failures += expect_true(sys_calibration_snapshot_read_pwm(&pwm) == BOOL_TRUE,
                            "PWM cache read");
    failures += expect_true(pwm.seq == 1U, "PWM sequence");
    failures += expect_true(pwm.requested_percent == 60U && pwm.protected_percent == 50U,
                            "PWM requested and protected percent");
    failures += expect_true(pwm.logical_pwm == 321U && pwm.ccr == 322U && pwm.oco_on == 1U,
                            "PWM hardware snapshot");

    failures += expect_true(sys_calibration_snapshot_read_aggregate(125U, &aggregate) == BOOL_TRUE,
                            "aggregate cache read");
    failures += expect_true(aggregate.meter_age_ms == 25U && aggregate.adc_age_ms == 15U &&
                                aggregate.pwm_age_ms == 5U,
                            "independent ages");
    failures += expect_true(aggregate.meter_adc_skew_ms == 10U &&
                                aggregate.meter_pwm_skew_ms == 20U,
                            "independent time skew");
    failures += expect_true((aggregate.valid_flags & SYS_CALIBRATION_AGGREGATE_METER_PRESENT) != 0U &&
                                (aggregate.valid_flags & SYS_CALIBRATION_AGGREGATE_ADC_PRESENT) != 0U &&
                                (aggregate.valid_flags & SYS_CALIBRATION_AGGREGATE_PWM_PRESENT) != 0U,
                            "aggregate source flags");

    failures += expect_true(sys_calibration_snapshot_read_aggregate(5U, &aggregate) == BOOL_TRUE,
                            "32-bit tick wrap read");
    sys_calibration_snapshot_publish_meter(0xFFFFFFF0UL,
                                           0U,
                                           0U,
                                           0U,
                                           0,
                                           0U,
                                           0U,
                                           0U,
                                           NULL,
                                           SYS_CALIBRATION_METER_FRAME_VALID,
                                           0U);
    failures += expect_true(sys_calibration_snapshot_read_aggregate(5U, &aggregate) == BOOL_TRUE,
                            "wrap source read");
    failures += expect_true(aggregate.meter_age_ms == 21U, "wrap-safe age");

    failures += expect_true(sys_calibration_snapshot_read_meter(NULL) == BOOL_FALSE,
                            "null meter output rejected");
    failures += expect_true(sys_calibration_snapshot_read_aggregate(0U, NULL) == BOOL_FALSE,
                            "null aggregate output rejected");

    sys_calibration_service_init();
    sys_calibration_service_bind_bound_voltage(test_bound_voltage);
    failures += expect_true(sys_calibration_service_get_status(&service_status) == BOOL_TRUE,
                            "service status read");
    failures += expect_true(service_status.state == SYS_CALIBRATION_STATE_DISABLED &&
                                service_status.codec_available == BOOL_TRUE &&
                                service_status.commit_available == BOOL_FALSE &&
                                service_status.nonzero_output_allowed == BOOL_FALSE,
                            "service codec is available but output gates are closed");
    failures += expect_true(
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
            service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN,
            service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT,
            service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U &&
        sys_calibration_safety_arbitrate_pwm(
            0U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
            service_status.boot_inhibit_active, BOOL_FALSE, BOOL_FALSE) == 0U,
        "boot inhibit blocks normal/offline/direct nonzero and preserves zero");
    failures += expect_true(sys_calibration_service_begin_context_seq(
                                1U, 0U, 1000U, 1U, &calibration_context,
                                &service_status) ==
                                SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                            "begin is blocked without safety proof");
    failures += expect_true(sys_calibration_service_set_point(1U, 1U, 100U, &service_status) ==
                                SYS_CALIBRATION_RESULT_INVALID_STATE,
                            "set point requires an active lease");
    failures += expect_true(sys_calibration_service_commit(1U, &service_status) ==
                                SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH,
                            "legacy commit without explicit context is rejected");
    failures += expect_true(sys_calibration_service_abort(1U, &service_status) == BOOL_FALSE &&
                                service_status.state == SYS_CALIBRATION_STATE_FAULT,
                            "abort without persistent inhibit storage fails closed");

    {
        unsigned char staged_payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
        unsigned char readback_payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
        const unsigned char raw_query[7U] =
            {0x3AU, 0x26U, 0x08U, 0x00U, 0x2EU, 0x0DU, 0x0AU};
        const unsigned char legacy_max_frame[15U] =
            {0x3AU, 0x24U, 0x07U, 0x08U, 0x43U, 0x66U, 0x00U, 0x00U,
             0x02U, 0x58U, 0x03U, 0x20U, 0x59U, 0x0DU, 0x0AU};
        unsigned short readback_length;
        unsigned short corrected_current;
        unsigned short gained_pwm;
        unsigned short calibrated_target_current;
        unsigned char corrected_percent;
        unsigned int index;

        memset(staged_payload, 0, sizeof(staged_payload));
        for (index = 0U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
        {
            staged_payload[index * 18U] = (unsigned char)(index * 20U);
            staged_payload[index * 18U + 1U] = 100U;
            put_u16_be(staged_payload, index * 18U + 4U, index * 10U);
            put_u16_be(staged_payload, index * 18U + 6U, index * 50U);
            put_u16_be(staged_payload, index * 18U + 8U, index * 89U);
            put_u16_be(staged_payload, index * 18U + 10U, index * 50U);
            put_u16_be(staged_payload, index * 18U + 12U, index * 88U);
            put_u16_be(staged_payload, index * 18U + 14U, index * 50U);
            put_u16_be(staged_payload, index * 18U + 16U, index * 100U);
        }
        failures += expect_true(sys_product_profile_context_build(
                                    560U, 890U, 800U,
                                    sys_calibration_storage_crc32(
                                        staged_payload, sizeof(staged_payload)),
                                    &staged_context) == BOOL_TRUE,
                                "staged context stores pre-gain legacy 0x07 Imax");
        sys_calibration_service_init();
        safe_off_calls = 0U;
        set_level_calls = 0U;
        inhibit_calls = 0U;
        sys_calibration_service_bind_safe_off(test_safe_off);
        test_bind_platform();
        sys_calibration_service_set_safety_ready(BOOL_TRUE);
        failures += expect_true(sys_calibration_service_begin_context_seq(
                                    42U, 100U, 1000U, 1U,
                                    &calibration_context, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "test safety hook opens a lease");
        failures += expect_true(inhibit_calls == 1U && inhibit_state == BOOL_TRUE &&
                                    service_status.boot_inhibit_active == BOOL_TRUE,
                                "begin persists boot inhibit before output");
        failures += expect_true(sys_calibration_service_begin_context_seq(
                                    42U, 100U, 1000U, 1U,
                                    &calibration_context, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "duplicate begin returns cached result");
        failures += expect_true(sys_calibration_service_set_point_seq(
                                    42U, 101U, 7U, 0U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK &&
                                    service_status.current_level == 0U,
                                "zero safe setpoint is allowed");
        failures += expect_true(set_level_calls == 1U && last_level == 0U,
                                "setpoint reaches bound output callback");
        {
            unsigned int safe_off_before_replay = safe_off_calls;
            failures += expect_true(sys_calibration_service_begin_context_seq(
                                        42U, 101U, 1000U, 1U,
                                        &calibration_context, &service_status) ==
                                        SYS_CALIBRATION_RESULT_DUPLICATE &&
                                        safe_off_calls == safe_off_before_replay,
                                    "old seq is rejected without replaying begin side effect");
            failures += expect_true(
                sys_calibration_safety_arbitrate_pwm(
                    1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
                    service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U &&
                sys_calibration_safety_arbitrate_pwm(
                    1000U, SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN,
                    service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U &&
                sys_calibration_safety_arbitrate_pwm(
                    1000U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT,
                    service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U,
                "active calibration boot inhibit blocks all nonzero sources");
        }
        failures += expect_true(sys_calibration_service_set_point_seq(
                                    42U, 101U, 8U, 20U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK &&
                                    set_level_calls == 2U && last_level == 20U,
                                "authorized nonzero protocol point reaches output callback");
        failures += expect_true(sys_calibration_service_raw_seq(
                                    42U, 102U, 9U, legacy_max_frame,
                                    sizeof(legacy_max_frame), SYS_CALIBRATION_RAW_SET,
                                    &service_status) == SYS_CALIBRATION_RESULT_OK &&
                                    service_status.context.calibrated_max_current_ma == 800U,
                                "legacy 0x07 accepts pre-gain Imax below SET_OUTCUR");
        failures += expect_true(
            sys_calibration_service_apply_fullscale_gain_pwm(
                500U, 1000U, &gained_pwm) == BOOL_TRUE && gained_pwm == 556U,
            "pre-gain Imax derives internal full-scale PWM gain");
        failures += expect_true(
            sys_calibration_service_get_calibrated_target_current_ma(
                560U, &calibrated_target_current) == BOOL_TRUE &&
                calibrated_target_current == 890U,
            "runtime current gate uses calibrated target rather than pre-gain Imax");
        failures += expect_true(sys_calibration_service_raw_seq(
                                    42U, 103U, 10U, raw_query, sizeof(raw_query),
                                    SYS_CALIBRATION_RAW_SET, &service_status) ==
                                    SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
                                "raw direction mismatch is rejected");
        failures += expect_true(sys_calibration_service_snapshot_seq(
                                    42U, 104U, 11U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "RAW snapshot operation succeeds in active lease");
        failures += expect_true(sys_calibration_service_stage_config_context_seq(
                                    42U, 200U, 12U, &staged_context, staged_payload,
                                    sizeof(staged_payload), &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "table payload stages without output");
        failures += expect_true(sys_calibration_service_stage_config_context_seq(
                                    42U, 200U, 12U, &staged_context, staged_payload,
                                    sizeof(staged_payload), &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "duplicate stage returns cached result");
        failures += expect_true(sys_calibration_service_readback_seq(
                                    42U, 201U, 13U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK &&
                                    sys_calibration_service_get_staged_payload(
                                        readback_payload, sizeof(readback_payload),
                                        &readback_length) == BOOL_TRUE &&
                                    readback_length == sizeof(staged_payload) &&
                                    memcmp(readback_payload, staged_payload,
                                           sizeof(staged_payload)) == 0,
                                "staged payload has exact readback");
        failures += expect_true(sys_calibration_service_apply_seq(
                                    42U, 202U, 14U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK &&
                                    service_status.state == SYS_CALIBRATION_STATE_APPLIED,
                                "apply activates staged protocol table");
        failures += expect_true(
            sys_calibration_service_set_validation_percent_seq(
                42U, 202U, 15U, 50U, &service_status) ==
                SYS_CALIBRATION_RESULT_OK &&
            last_level == 100U &&
            sys_calibration_service_get_staged_payload(
                readback_payload, sizeof(readback_payload),
                &readback_length) == BOOL_TRUE &&
            readback_length == sizeof(staged_payload) &&
            memcmp(readback_payload, staged_payload,
                   sizeof(staged_payload)) == 0,
            "validation percent drives an interpolated temporary level without writing the table");
        failures += expect_true(sys_calibration_service_commit_context_seq(
                                    42U, 203U, 16U,
                                    &service_status.context, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK &&
                                    service_status.committed_valid == BOOL_TRUE &&
                                    service_status.committed_generation == 3U &&
                                    memcmp(committed_payload, staged_payload,
                                           sizeof(staged_payload)) == 0,
                                "commit callback persists and reads matching payload");
        failures += expect_true(
            sys_calibration_service_correct_output_percent(
                50U, SYS_PRODUCT_PROFILE_50W_DEFAULT_CURRENT_MA,
                &corrected_percent) == BOOL_TRUE && corrected_percent == 50U &&
            sys_calibration_service_correct_output_percent(
                50U, 800U, &corrected_percent) == BOOL_TRUE &&
                corrected_percent == 45U &&
            sys_calibration_service_runtime_context_matches_voltage(560U) ==
                BOOL_TRUE &&
            sys_calibration_service_runtime_context_matches_voltage(550U) ==
                BOOL_FALSE &&
            service_status.context.calibrated_max_current_ma == 800U &&
            sys_calibration_service_correct_output_current(
                440U, &corrected_current) == BOOL_TRUE &&
                corrected_current == 445U,
            "one full-span table also maps a lower writable SET_OUTCUR");
        failures += expect_true(sys_calibration_service_heartbeat_seq(
                                    42U, 204U, 1000U, 17U, &service_status) ==
                                    SYS_CALIBRATION_RESULT_OK,
                                "heartbeat renews lease");
        failures += expect_true(sys_calibration_service_timer(1204U, &service_status) ==
                                    BOOL_TRUE &&
                                    service_status.state == SYS_CALIBRATION_STATE_FAULT &&
                                    service_status.boot_inhibit_active == BOOL_TRUE &&
                                    safe_off_calls > 0U &&
                                    sys_calibration_safety_arbitrate_pwm(
                                        1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
                                        service_status.boot_inhibit_active, BOOL_FALSE,
                                        BOOL_TRUE) == 0U &&
                                    sys_calibration_safety_arbitrate_pwm(
                                        1000U, SYS_CALIBRATION_OUTPUT_SOURCE_OFFLINE_PLAN,
                                        service_status.boot_inhibit_active, BOOL_FALSE,
                                        BOOL_TRUE) == 0U &&
                                    sys_calibration_safety_arbitrate_pwm(
                                        1000U, SYS_CALIBRATION_OUTPUT_SOURCE_FACTORY_DIRECT,
                                        service_status.boot_inhibit_active, BOOL_FALSE,
                                        BOOL_TRUE) == 0U,
                                "lease expiry fail-offs and inhibits all nonzero sources");
        failures += expect_true(sys_calibration_service_abort(42U, &service_status) ==
                                    BOOL_TRUE &&
                                    service_status.state == SYS_CALIBRATION_STATE_IDLE &&
                                    service_status.boot_inhibit_active == BOOL_FALSE &&
                                    inhibit_state == BOOL_FALSE,
                                "abort after timeout persists inactive and remains off");
    }

    sys_calibration_service_init();
    test_bind_platform();
    sys_calibration_service_set_safety_ready(BOOL_FALSE);
    sys_calibration_service_set_safety_ready(BOOL_TRUE);
    failures += expect_true(sys_calibration_service_begin_context_seq(
                                77U, 0U, 1000U, 1U,
                                &calibration_context, &service_status) ==
                                SYS_CALIBRATION_RESULT_OK,
                            "force fault test begins a lease");
    sys_calibration_service_force_fault();
    failures += expect_true(
        sys_calibration_service_get_status(&service_status) == BOOL_TRUE &&
        service_status.state == SYS_CALIBRATION_STATE_FAULT &&
        service_status.boot_inhibit_active == BOOL_TRUE &&
        sys_calibration_safety_arbitrate_pwm(
            1000U, SYS_CALIBRATION_OUTPUT_SOURCE_NORMAL,
            service_status.boot_inhibit_active, BOOL_FALSE, BOOL_TRUE) == 0U,
        "force fault latches boot inhibit against normal nonzero output");
    sys_calibration_service_set_safety_ready(BOOL_FALSE);
    sys_calibration_service_set_safety_ready(BOOL_TRUE);
    failures += expect_true(
        sys_calibration_service_get_status(&service_status) == BOOL_TRUE &&
        service_status.state == SYS_CALIBRATION_STATE_FAULT &&
        service_status.boot_inhibit_active == BOOL_TRUE,
        "fault cannot be unlocked by repeated ready injection");

    memset(frame, 0, sizeof(frame));
    frame[0] = SYS_BL0942_READ_RESPONSE_HEADER;
    put_u24_le(frame, 1U, 0x010203U);
    put_u24_le(frame, 4U, 0x040506U);
    put_u24_le(frame, 7U, 0x070809U);
    put_u24_le(frame, 10U, 0xFFFFFEU);
    put_u24_le(frame, 13U, 0x0A0B0CU);
    frame[16] = 0x34U;
    frame[17] = 0x12U;
    frame[19] = 0x80U;
    frame[22] = 0x42U;
    failures += expect_true(sys_bl0942_frame_calculate_checksum(frame) == 0x42U,
                            "BL0942 fixed golden checksum");
    failures += expect_true(sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_TRUE,
                            "BL0942 golden frame validates");
    failures += expect_true(sys_bl0942_frame_decode(frame, sizeof(frame), &decoded_frame) == BOOL_TRUE,
                            "BL0942 golden frame decodes");
    failures += expect_true(decoded_frame.i_rms_raw == 0x010203U &&
                                decoded_frame.i_fast_rms_raw == 0x070809U &&
                                decoded_frame.watt_raw == -2 &&
                                decoded_frame.freq_raw == 0x1234U &&
                                decoded_frame.status_raw == 0x80U,
                            "BL0942 decoded fields");
    sys_calibration_snapshot_publish_meter(200U,
                                           decoded_frame.i_rms_raw,
                                           decoded_frame.v_rms_raw,
                                           decoded_frame.i_fast_rms_raw,
                                           decoded_frame.watt_raw,
                                           decoded_frame.cf_cnt_raw,
                                           decoded_frame.freq_raw,
                                           decoded_frame.status_raw,
                                           frame,
                                           SYS_CALIBRATION_METER_FRAME_VALID |
                                               SYS_CALIBRATION_METER_HEAD_VALID |
                                               SYS_CALIBRATION_METER_CHECKSUM_VALID |
                                               SYS_CALIBRATION_METER_RESERVED_VALID,
                                           0U);
    failures += expect_true(sys_calibration_snapshot_read_meter(&meter) == BOOL_TRUE &&
                                meter.raw_frame[0] == SYS_BL0942_READ_RESPONSE_HEADER &&
                                meter.raw_frame[22] == 0x42U,
                            "BL0942 raw frame is retained");
    {
        unsigned char original_checksum = frame[22];
        frame[21] = 1U;
        failures += expect_true(sys_bl0942_frame_calculate_checksum(frame) != original_checksum,
                                "checksum covers byte 21");
        frame[21] = 0U;
        frame[1] ^= 1U;
        failures += expect_true(sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_FALSE,
                                "payload mutation is rejected");
        frame[1] ^= 1U;
        frame[19] = 0x81U;
        frame[22] = sys_bl0942_frame_calculate_checksum(frame);
        failures += expect_true(sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_TRUE,
                                "status mutation with matching checksum validates");
        frame[22] = original_checksum;
    }
    frame[0] = 0x54U;
    failures += expect_true(sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_FALSE,
                            "bad BL0942 frame header is rejected");

    if (failures != 0)
    {
        return 1;
    }
    printf("snapshot host tests: PASS\n");
    return 0;
}
