#include <stdio.h>
#include <string.h>

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

static void make_frame(unsigned char *frame)
{
    memset(frame, 0, SYS_BL0942_READ_FRAME_LENGTH);
    frame[0] = SYS_BL0942_READ_RESPONSE_HEADER;
    frame[1] = 0x11U;
    frame[4] = 0x22U;
    frame[10] = 0x33U;
    frame[13] = 0x44U;
    frame[16] = 0x55U;
    frame[19] = 0x80U;
    frame[22] = sys_bl0942_frame_calculate_checksum(frame);
}

int main(void)
{
    unsigned char frame[SYS_BL0942_READ_FRAME_LENGTH];
    sys_bl0942_frame_st decoded;
    sys_bl0942_health_st health;
    unsigned short corrected_voltage;
    int failures = 0;

    make_frame(frame);
    failures += expect_true(
        sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_TRUE &&
            sys_bl0942_frame_reserved_valid(frame) == BOOL_TRUE &&
            sys_bl0942_frame_uses_legacy_checksum(frame) == BOOL_FALSE,
        "official canonical frame validates");
    failures += expect_true(
        sys_bl0942_frame_decode(frame, sizeof(frame), &decoded) == BOOL_TRUE &&
            decoded.i_rms_raw == 0x11U && decoded.v_rms_raw == 0x22U &&
            decoded.watt_raw == 0x33,
        "official frame decodes measurement fields");

    frame[21] = 0x5AU;
    frame[22] = sys_bl0942_frame_calculate_checksum(frame);
    failures += expect_true(
        sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_TRUE &&
            sys_bl0942_frame_reserved_valid(frame) == BOOL_FALSE &&
            sys_bl0942_frame_uses_legacy_checksum(frame) == BOOL_FALSE,
        "official checksum accepts noncanonical reserved byte for compatibility");

    frame[22] = sys_bl0942_frame_calculate_legacy_checksum(frame);
    failures += expect_true(
        sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_TRUE &&
            sys_bl0942_frame_uses_legacy_checksum(frame) == BOOL_TRUE,
        "baseline checksum accepts devices with variable byte 21");

    frame[1] ^= 0x01U;
    failures += expect_true(
        sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_FALSE,
        "measurement corruption remains rejected by compatibility checksum");
    frame[1] ^= 0x01U;
    frame[0] = 0x54U;
    failures += expect_true(
        sys_bl0942_frame_validate(frame, sizeof(frame)) == BOOL_FALSE,
        "bad response header remains rejected");
    failures += expect_true(
        sys_bl0942_frame_validate(frame,
                                  SYS_BL0942_READ_FRAME_LENGTH - 1U) == BOOL_FALSE,
        "short frame remains rejected");

    corrected_voltage = 0U;
    failures += expect_true(
        sys_bl0942_voltage_apply_gain_q24(
            2300U, 0x01000000UL, &corrected_voltage) == BOOL_TRUE &&
            corrected_voltage == 2300U,
        "Gain-only Q24 applies unity gain with 64-bit intermediate");
    failures += expect_true(
        sys_bl0942_voltage_apply_gain_q24(
            3U, 0x00800000UL, &corrected_voltage) == BOOL_TRUE &&
            corrected_voltage == 2U,
        "Gain-only Q24 rounds half upward");
    failures += expect_true(
        sys_bl0942_voltage_apply_gain_q24(
            1U, 0U, &corrected_voltage) == BOOL_FALSE &&
            sys_bl0942_voltage_apply_gain_q24(
                0xFFFFFFFFUL, 0xFFFFFFFFUL,
                &corrected_voltage) == BOOL_FALSE,
        "Gain-only Q24 rejects zero gain and u16 overflow");

    sys_bl0942_health_init(&health);
    failures += expect_true(
        sys_bl0942_health_age_ms(&health, 0U) ==
                SYS_BL0942_DATA_AGE_INVALID &&
            sys_bl0942_health_is_fresh(&health, 0U) == BOOL_FALSE,
        "no valid BL frame is stale with invalid age");
    sys_bl0942_health_record_valid(&health, 1000U);
    failures += expect_true(
        sys_bl0942_health_is_fresh(&health, 1500U) == BOOL_TRUE &&
            sys_bl0942_health_is_fresh(&health, 1501U) == BOOL_FALSE,
        "BL freshness boundary is inclusive at 500ms");
    sys_bl0942_health_record_valid(&health, 0xFFFFFFF0UL);
    failures += expect_true(
        sys_bl0942_health_age_ms(&health, 5U) == 21U,
        "BL age handles 32-bit tick wrap");

    failures += expect_true(
        sys_bl0942_health_begin_recovery(&health) == BOOL_TRUE &&
            health.recovery_count == 1U &&
            health.last_recovery_state ==
                SYS_BL0942_RECOVERY_WAIT_VALID_FRAME,
        "first classified fault starts one recovery");
    sys_bl0942_health_record_recovery_start(&health, BOOL_TRUE);
    failures += expect_true(
        sys_bl0942_health_begin_recovery(&health) == BOOL_FALSE &&
            health.recovery_count == 1U &&
            health.recovery_fail_count == 1U &&
            health.last_recovery_state == SYS_BL0942_RECOVERY_FAILED,
        "fault before a new frame fails recovery without another reset");
    failures += expect_true(
        sys_bl0942_health_begin_recovery(&health) == BOOL_FALSE &&
            health.recovery_fail_count == 1U,
        "same stale fault window cannot grow recovery attempts");
    sys_bl0942_health_record_valid(&health, 2000U);
    failures += expect_true(
        sys_bl0942_health_begin_recovery(&health) == BOOL_TRUE &&
            health.recovery_count == 2U,
        "a new valid frame opens a later independent recovery window");
    sys_bl0942_health_record_recovery_start(&health, BOOL_TRUE);
    sys_bl0942_health_record_valid(&health, 2100U);
    failures += expect_true(
        health.last_recovery_state == SYS_BL0942_RECOVERY_SUCCEEDED &&
            health.recovery_waiting_frame == 0U,
        "recovery succeeds only after a new valid frame");
    if (failures != 0)
    {
        return 1;
    }
    printf("BL0942 frame, freshness, recovery and Gain-only tests: PASS\n");
    return 0;
}
