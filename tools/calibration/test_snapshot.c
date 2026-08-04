#include <stdio.h>
#include <string.h>

#include "sys_calibration_snapshot.h"
#include "sys_calibration_service.h"
#include "sys_bl0942_frame.h"

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

int main(void)
{
    sys_calibration_meter_snapshot_st meter;
    sys_calibration_adc_snapshot_st adc;
    sys_calibration_pwm_snapshot_st pwm;
    sys_calibration_snapshot_aggregate_st aggregate;
    sys_calibration_service_status_st service_status;
    unsigned char frame[SYS_BL0942_READ_FRAME_LENGTH];
    sys_bl0942_frame_st decoded_frame;
    int failures = 0;

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
    failures += expect_true(sys_calibration_service_get_status(&service_status) == BOOL_TRUE,
                            "service status read");
    failures += expect_true(service_status.state == SYS_CALIBRATION_STATE_DISABLED &&
                                service_status.codec_available == BOOL_FALSE &&
                                service_status.commit_available == BOOL_FALSE &&
                                service_status.nonzero_output_allowed == BOOL_FALSE,
                            "service safety gates are disabled");
    failures += expect_true(sys_calibration_service_begin(1U, 0U, 1000U, &service_status) ==
                                SYS_CALIBRATION_RESULT_NOT_AVAILABLE,
                            "begin is blocked without frozen protocol");
    failures += expect_true(sys_calibration_service_set_point(1U, 1U, 100U, &service_status) ==
                                SYS_CALIBRATION_RESULT_NOT_AVAILABLE,
                            "set point is blocked without safety proof");
    failures += expect_true(sys_calibration_service_commit(1U, &service_status) ==
                                SYS_CALIBRATION_RESULT_NOT_AVAILABLE,
                            "commit is blocked without Flash contract");
    failures += expect_true(sys_calibration_service_abort(1U, &service_status) == BOOL_TRUE &&
                                service_status.state == SYS_CALIBRATION_STATE_ABORTED,
                            "abort enters safe terminal state");

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
    frame[22] = sys_bl0942_frame_calculate_checksum(frame);
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
