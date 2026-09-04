#include <stdio.h>
#include <string.h>

#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_service.h"
#include "sys_calibration_snapshot.h"
#include "sys_product_profile.h"
#include "sys_temp_over_protect.h"

u8 Error_1_OL;
u8 Error_Out_LV;
u8 Error_3_OV;
u8 Error_4_LV;
sys_temp_over_protect_state_en sys_temp_over_protect_state;

static u32 safe_off_count;
static u32 set_level_count;
static u32 set_output_count;
static u32 probe_pwm_count;
static u32 inhibit_count;
static u32 commit_count;
static boolean_en inhibit_state;
static boolean_en inhibit_success = BOOL_TRUE;
static boolean_en commit_success = BOOL_TRUE;
static boolean_en level_success = BOOL_TRUE;
static boolean_en output_success = BOOL_TRUE;
static boolean_en probe_pwm_success = BOOL_TRUE;
static u16 last_level;
static u8 last_percent;
static u16 last_probe_pwm;
static boolean_en bl_service_fresh = BOOL_TRUE;

boolean_en sys_bl0942_is_fresh(u32 now_tick_ms)
{
    (void)now_tick_ms;
    return bl_service_fresh;
}

boolean_en sys_calibration_flash_set_inhibit(boolean_en active)
{
    inhibit_state = active;
    ++inhibit_count;
    return inhibit_success;
}

boolean_en sys_calibration_flash_commit_v3(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 *generation)
{
    ++commit_count;
    if (commit_success != BOOL_TRUE || payload == NULL || generation == NULL ||
        length != SYS_CALIBRATION_PAYLOAD_LENGTH ||
        sys_calibration_payload_crc32_iso_hdlc(payload, length) !=
            payload_crc32)
    {
        return BOOL_FALSE;
    }
    *generation = 7U;
    return BOOL_TRUE;
}

static void test_safe_off(void)
{
    ++safe_off_count;
}

static boolean_en test_set_level(u16 level, u16 *actual_pwm)
{
    ++set_level_count;
    last_level = level;
    if (actual_pwm == NULL)
    {
        return BOOL_FALSE;
    }
    if (level_success != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *actual_pwm = (u16)(level * 3U);
    return BOOL_TRUE;
}

static boolean_en test_set_output(u8 percent, u16 *actual_pwm)
{
    ++set_output_count;
    last_percent = percent;
    if (actual_pwm == NULL)
    {
        return BOOL_FALSE;
    }
    if (output_success != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *actual_pwm = (u16)percent * 10U;
    return BOOL_TRUE;
}

static boolean_en test_probe_pwm(u16 logical_pwm, u16 *actual_pwm)
{
    ++probe_pwm_count;
    last_probe_pwm = logical_pwm;
    if (actual_pwm == NULL || probe_pwm_success != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *actual_pwm = logical_pwm;
    return BOOL_TRUE;
}

u16 sys_calibration_service_calibration_span_ma(void)
{
    return 1400U;
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

static void build_payload(sys_calibration_payload_st *payload,
                          u8 encoded[SYS_CALIBRATION_PAYLOAD_LENGTH],
                          u32 *crc)
{
    static const u16 output_ref[SYS_CALIBRATION_POINT_COUNT] =
        {0U, 140U, 280U, 420U, 560U, 700U,
         840U, 980U, 1120U, 1260U, 1400U};
    u32 index;

    memset(payload, 0, sizeof(*payload));
    payload->profile_id = SYS_PRODUCT_PROFILE_ID_50W;
    payload->profile_version = SYS_PRODUCT_PROFILE_VERSION;
    payload->profile_fingerprint =
        SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32;
    payload->point_count = SYS_CALIBRATION_POINT_COUNT;
    payload->level_step = SYS_CALIBRATION_LEVEL_STEP;
    payload->valid_flags = SYS_CALIBRATION_REQUIRED_SECTIONS;
    payload->voltage_gain_q24 = 0x01000000UL;
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        payload->output[index].logical_pwm = (u16)(index * 60U);
        payload->output[index].reference_output_current_ma = output_ref[index];
        payload->oco[index].oco_adc_raw = (u16)(1000U + index * 100U);
        payload->oco[index].reference_output_current_ma = output_ref[index];
        payload->bl_current[index].bl_current_raw = 100000UL + index * 10000UL;
        payload->bl_current[index].reference_input_current_ma =
            (u16)(index * 20U);
        payload->bl_power[index].bl_power_raw =
            (s32)(200000L + (s32)index * 20000L);
        payload->bl_power[index].reference_input_power_01w =
            (u16)(index * 50U);
    }
    (void)sys_calibration_payload_encode(
        payload, encoded, SYS_CALIBRATION_PAYLOAD_LENGTH);
    *crc = sys_calibration_payload_crc32_iso_hdlc(
        encoded, SYS_CALIBRATION_PAYLOAD_LENGTH);
}

static void publish_fresh_raw(u32 tick_ms,
                              u16 logical_pwm,
                              u16 requested_percent)
{
    u8 frame[SYS_CALIBRATION_METER_RAW_FRAME_LENGTH] = {0};

    sys_calibration_snapshot_publish_meter(
        tick_ms, 150000UL, 2300UL, 0U, 300000L, 0U, 500U, 0U,
        frame,
        SYS_CALIBRATION_METER_FRAME_VALID |
            SYS_CALIBRATION_METER_HEAD_VALID |
            SYS_CALIBRATION_METER_CHECKSUM_VALID,
        0U);
    sys_calibration_snapshot_publish_adc(
        tick_ms, 0U, 1000U, 0U, 1500U,
        SYS_CALIBRATION_ADC_SAMPLE_VALID);
    sys_calibration_snapshot_prepare_pwm(requested_percent,
                                         requested_percent);
    sys_calibration_snapshot_publish_pwm(
        tick_ms, logical_pwm, logical_pwm, logical_pwm == 0U ? 0U : 1U,
        SYS_CALIBRATION_PWM_SAMPLE_VALID);
}

int main(void)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_payload_st decoded;
    sys_calibration_service_status_st status;
    sys_calibration_raw_st raw;
    u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH];
    u8 alternate_payload[SYS_CALIBRATION_PAYLOAD_LENGTH];
    u8 readback[SYS_CALIBRATION_PAYLOAD_LENGTH];
    u16 readback_length = 0U;
    u32 payload_crc = 0U;
    u32 readback_crc = 0U;
    u32 generation = 0U;
    u32 alternate_crc;
    u32 index;
    u16 corrected = 0U;
    u32 side_effect_count;
    u32 safe_off_before;
    int failures = 0;

    build_payload(&decoded, payload, &payload_crc);
    sys_calibration_snapshot_init();
    sys_calibration_service_init();
    sys_calibration_service_bind_output(
        test_safe_off, test_set_level, test_set_output);
    sys_calibration_service_bind_probe(test_probe_pwm);
    sys_calibration_service_bind_storage(
        sys_calibration_flash_set_inhibit,
        sys_calibration_flash_commit_v3);
    sys_calibration_service_restore_boot(BOOL_FALSE, BOOL_TRUE);
    sys_calibration_service_set_safety_ready(BOOL_TRUE);

    failures += expect_true(
        sys_calibration_service_output_pwm_for_current(700U, &corrected) ==
            BOOL_FALSE,
        "no committed calibration selects Default PWM path");
    failures += expect_true(
        sys_calibration_service_begin_seq(
            123456UL, 100U, 30000U, 1U, profile->profile_id,
            profile->fingerprint_crc32, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_ACTIVE &&
            status.boot_inhibit_active == BOOL_TRUE && inhibit_state == BOOL_TRUE,
        "BEGIN opens one inhibited ACTIVE session");
    side_effect_count = safe_off_count + inhibit_count;
    failures += expect_true(
        sys_calibration_service_begin_seq(
            123456UL, 100U, 30000U, 1U, profile->profile_id,
            profile->fingerprint_crc32, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            safe_off_count + inhibit_count == side_effect_count,
        "exact duplicate BEGIN has no repeated side effect");
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            123456UL, 110U, 2U, 21U, &status) ==
            SYS_CALIBRATION_RESULT_RANGE_ERROR,
        "SET_POINT rejects non-grid level");
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            123456UL, 120U, 3U, 100U, &status) ==
            SYS_CALIBRATION_RESULT_OK &&
            status.current_level == 100U && status.current_percent == 50U &&
            status.actual_pwm == 300U && set_level_count == 1U &&
            last_level == 100U,
        "SET_POINT synchronously applies 50 percent through its bound callback");
    side_effect_count = set_level_count;
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            123456UL, 121U, 3U, 120U, &status) ==
                SYS_CALIBRATION_RESULT_BAD_REQUEST &&
            set_level_count == side_effect_count,
        "same sid/seq/op with changed parameters is not an exact duplicate");
    publish_fresh_raw(140U, 500U, 50U);
    failures += expect_true(
        sys_calibration_service_raw_seq(
            123456UL, 150U, 5U, &raw, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            (raw.valid_flags & 0x003FU) == 0x003FU &&
            raw.oco_raw == 1500U && raw.bl_current_raw == 150000UL,
        "ACTIVE RAW exposes ordinary-dimming PWM plus uncorrected fitting fields");
    failures += expect_true(
        sys_calibration_service_stage_seq(
            123456UL, 160U, 6U, payload,
            SYS_CALIBRATION_PAYLOAD_LENGTH, payload_crc ^ 1U, &status) ==
            SYS_CALIBRATION_RESULT_CRC_ERROR,
        "STAGE rejects payload CRC mismatch");
    failures += expect_true(
        sys_calibration_service_stage_seq(
            123456UL, 170U, 7U, payload,
            SYS_CALIBRATION_PAYLOAD_LENGTH, payload_crc, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_STAGED &&
            status.staged_length == SYS_CALIBRATION_PAYLOAD_LENGTH,
        "STAGE accepts one complete Output+OCO+BL V/I/P payload");
    failures += expect_true(
        sys_calibration_service_apply_seq(
            123456UL, 180U, 8U, &status) == SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_APPLIED,
        "APPLY selects staged RAM Correction without Flash commit");
    side_effect_count = probe_pwm_count;
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            123456UL, 181U, 8U, 650U, &status) ==
                SYS_CALIBRATION_RESULT_BAD_STATE &&
            probe_pwm_count == side_effect_count,
        "PROBE_PWM is rejected outside ACTIVE without a physical write");

    failures += expect_true(
        sys_calibration_service_output_pwm_for_current(700U, &corrected) ==
                BOOL_TRUE && corrected == 300U,
        "Output target current inverse-interpolates directly to logical PWM");
    failures += expect_true(
        sys_calibration_service_correct_output_current_raw(
            1500U, &corrected) == BOOL_TRUE && corrected == 700U,
        "OCO corrected business branch interpolates Raw to reference");
    failures += expect_true(
        sys_calibration_service_correct_output_current_raw(
            999U, &corrected) == BOOL_TRUE && corrected == 0U &&
        sys_calibration_service_correct_output_current_raw(
            1000U, &corrected) == BOOL_TRUE && corrected == 0U,
        "OCO Raw at or below OffsetRaw clamps to calibrated zero");
    failures += expect_true(
        sys_calibration_service_correct_bl_voltage(
            2300U, &corrected) == BOOL_TRUE && corrected == 2300U,
        "BL voltage applies Gain-only Q24");
    failures += expect_true(
        sys_calibration_service_correct_bl_current(
            150000UL, &corrected) == BOOL_TRUE && corrected == 100U,
        "BL current applies 11-point Raw to reference");
    failures += expect_true(
        sys_calibration_service_correct_bl_power(
            300000L, &corrected) == BOOL_TRUE && corrected == 250U,
        "BL power applies 11-point Raw to reference");
    failures += expect_true(
        sys_calibration_service_output_pwm_for_current(1500U, &corrected) ==
                BOOL_FALSE &&
            sys_calibration_service_get_status(&status) == BOOL_TRUE &&
            status.output_fallback_count == 1U,
        "legal target beyond Calibration coverage requests Default fallback");

    failures += expect_true(
        sys_calibration_service_set_output_seq(
            123456UL, 190U, 9U, 45U, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            last_percent == 45U && set_output_count == 1U,
        "SET_OUTPUT delegates staged normal output without MCU PASS/FAIL");
    publish_fresh_raw(195U, 450U, 45U);
    failures += expect_true(
        sys_calibration_service_raw_seq(
            123456UL, 200U, 10U, &raw, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            (raw.valid_flags & 0x03C0U) == 0x03C0U &&
            raw.corrected_output_current_ma == 700U &&
            raw.corrected_input_voltage_01v == 2300U,
        "APPLIED RAW carries corrected fields beside untouched Raw");

    Error_3_OV = 1U;
    failures += expect_true(
        sys_calibration_service_raw_seq(
            123456UL, 205U, 11U, &raw, &status) ==
                SYS_CALIBRATION_RESULT_HARDWARE_FAULT &&
            (raw.fault_flags & SYS_CALIBRATION_FAULT_INPUT_OVERVOLTAGE) != 0U,
        "nonzero RAW reports unified hardware fault");
    Error_3_OV = 0U;

    failures += expect_true(
        sys_calibration_service_commit_seq(
            123456UL, 210U, 12U, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_COMMITTED &&
            status.committed_generation == 7U && commit_count == 1U,
        "COMMIT writes the complete 244B through V3 storage once");
    side_effect_count = commit_count;
    failures += expect_true(
        sys_calibration_service_commit_seq(
            123456UL, 210U, 12U, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            commit_count == side_effect_count,
        "exact duplicate COMMIT does not write Flash twice");
    failures += expect_true(
        sys_calibration_service_read_seq(
            123456UL, 220U, 13U, readback, sizeof(readback),
            &readback_length, &readback_crc, &generation, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            readback_length == SYS_CALIBRATION_PAYLOAD_LENGTH &&
            readback_crc == payload_crc && generation == 7U &&
            memcmp(readback, payload, sizeof(payload)) == 0,
        "READ returns committed 244B Payload byte-for-byte");
    failures += expect_true(
        sys_calibration_service_abort_seq(
            123456UL, 230U, 14U, &status) ==
            SYS_CALIBRATION_RESULT_BAD_STATE,
        "ABORT cannot undo COMMITTED Calibration");
    failures += expect_true(
        sys_calibration_service_release_seq(
            123456UL, 240U, 15U, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_IDLE &&
            status.committed_valid == BOOL_TRUE && inhibit_state == BOOL_FALSE,
        "RELEASE safe-offs and retains committed Correction");
    failures += expect_true(
        sys_calibration_service_output_pwm_for_current(700U, &corrected) ==
                BOOL_TRUE && corrected == 300U,
        "committed Calibration remains independent of session context");

    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        decoded.output[index].logical_pwm = (u16)(index * 50U);
    }
    failures += expect_true(
        sys_calibration_payload_encode(
            &decoded, alternate_payload, sizeof(alternate_payload)) == BOOL_TRUE,
        "alternate full payload encodes for ABORT restoration test");
    alternate_crc = sys_calibration_payload_crc32_iso_hdlc(
        alternate_payload, sizeof(alternate_payload));
    failures += expect_true(
        sys_calibration_service_begin_seq(
            77U, 500U, 30000U, 1U, profile->profile_id,
            profile->fingerprint_crc32, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            sys_calibration_service_stage_seq(
                77U, 510U, 2U, alternate_payload,
                sizeof(alternate_payload), alternate_crc, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            sys_calibration_service_apply_seq(
                77U, 520U, 3U, &status) == SYS_CALIBRATION_RESULT_OK &&
            sys_calibration_service_output_pwm_for_current(
                700U, &corrected) == BOOL_TRUE && corrected != 300U,
        "APPLY temporarily selects the new staged complete Correction");
    side_effect_count = set_output_count;
    safe_off_before = safe_off_count;
    bl_service_fresh = BOOL_FALSE;
    failures += expect_true(
        sys_calibration_service_set_output_seq(
            77U, 525U, 4U, 45U, &status) ==
                SYS_CALIBRATION_RESULT_DATA_STALE &&
            (u32)SYS_CALIBRATION_RESULT_DATA_STALE == 6U &&
            set_output_count == side_effect_count &&
            safe_off_count > safe_off_before && status.actual_pwm == 0U,
        "SET_OUTPUT stale safe-offs and exposes MQTT rc DATA_STALE=6");
    bl_service_fresh = BOOL_TRUE;
    output_success = BOOL_FALSE;
    failures += expect_true(
        sys_calibration_service_set_output_seq(
            77U, 526U, 5U, 45U, &status) ==
                SYS_CALIBRATION_RESULT_HARDWARE_FAULT &&
            (u32)SYS_CALIBRATION_RESULT_HARDWARE_FAULT == 9U,
        "SET_OUTPUT real output callback failure remains HARDWARE_FAULT=9");
    output_success = BOOL_TRUE;
    failures += expect_true(
        sys_calibration_service_abort_seq(
            77U, 530U, 6U, &status) == SYS_CALIBRATION_RESULT_OK &&
            status.state == SYS_CALIBRATION_STATE_IDLE &&
            sys_calibration_service_output_pwm_for_current(
                700U, &corrected) == BOOL_TRUE && corrected == 300U,
        "ABORT safe-offs, discards staged and restores old committed Correction");

    failures += expect_true(
        sys_calibration_service_begin_seq(
            99U, 1000U, 1000U, 1U, profile->profile_id,
            profile->fingerprint_crc32, &status) ==
            SYS_CALIBRATION_RESULT_OK,
        "second independent session begins");
    side_effect_count = set_level_count;
    safe_off_before = safe_off_count;
    bl_service_fresh = BOOL_FALSE;
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            99U, 1001U, 2U, 20U, &status) ==
                SYS_CALIBRATION_RESULT_DATA_STALE &&
            (u32)SYS_CALIBRATION_RESULT_DATA_STALE == 6U &&
            set_level_count == side_effect_count &&
            safe_off_count > safe_off_before && status.actual_pwm == 0U,
        "SET_POINT stale safe-offs and exposes MQTT rc DATA_STALE=6");
    bl_service_fresh = BOOL_TRUE;
    level_success = BOOL_FALSE;
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            99U, 1002U, 3U, 20U, &status) ==
                SYS_CALIBRATION_RESULT_HARDWARE_FAULT &&
            set_level_count == side_effect_count + 1U &&
            status.actual_pwm == 0U,
        "SET_POINT fails closed when the bound normal dimming path rejects the point");
    level_success = BOOL_TRUE;
    side_effect_count = set_level_count;
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            99U, 1002U, 4U, 650U, &status) ==
                SYS_CALIBRATION_RESULT_BAD_STATE && probe_pwm_count == 0U,
        "PROBE_PWM is rejected until normal SET_POINT reaches 100 percent");
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            99U, 1002U, 5U, SYS_CALIBRATION_LEVEL_MAX, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            set_level_count == side_effect_count + 1U,
        "100 percent SET_POINT performs one normal SET/HWMAX write before probing");
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            99U, 1002U, 6U, 650U, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            probe_pwm_count == 1U && last_probe_pwm == 650U &&
            status.current_level == SYS_CALIBRATION_LEVEL_MAX &&
            status.current_percent == 100U && status.actual_pwm == 650U &&
            status.last_request_seq == 6U,
        "PROBE_PWM performs exactly one independent endpoint PWM request");
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            99U, 1002U, 6U, 650U, &status) ==
                SYS_CALIBRATION_RESULT_OK && probe_pwm_count == 1U,
        "exact PROBE_PWM replay does not write PWM again");
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            99U, 1002U, 6U, 651U, &status) ==
                SYS_CALIBRATION_RESULT_BAD_REQUEST && probe_pwm_count == 1U,
        "conflicting PROBE_PWM replay is rejected without output");
    safe_off_before = safe_off_count;
    probe_pwm_success = BOOL_FALSE;
    failures += expect_true(
        sys_calibration_service_probe_pwm_seq(
            99U, 1002U, 7U, 700U, &status) ==
                SYS_CALIBRATION_RESULT_HARDWARE_FAULT &&
            probe_pwm_count == 2U && safe_off_count > safe_off_before &&
            status.actual_pwm == 0U,
        "failed PROBE_PWM safe-offs without recording a successful PWM");
    probe_pwm_success = BOOL_TRUE;
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            99U, 1002U, 8U, 0U, &status) ==
            SYS_CALIBRATION_RESULT_OK,
        "SET_POINT zero labels the ordinary off sample");
    publish_fresh_raw(1000U, 0U, 0U);
    Error_Out_LV = 1U;
    failures += expect_true(
        sys_calibration_service_raw_seq(
            99U, 1003U, 9U, &raw, &status) ==
                SYS_CALIBRATION_RESULT_OK &&
            (raw.fault_flags &
             SYS_CALIBRATION_FAULT_OUTPUT_LOW_VOLTAGE) != 0U,
        "zero output permits natural OUTPUT_LOW_VOLTAGE but reports the flag");
    Error_Out_LV = 0U;
    side_effect_count = safe_off_count;
    failures += expect_true(
        sys_calibration_service_timer(2000U, &status) == BOOL_TRUE &&
            status.state == SYS_CALIBRATION_STATE_IDLE &&
            safe_off_count > side_effect_count,
        "lease expiry safe-offs, restores committed and clears session");
    failures += expect_true(
        sys_calibration_service_set_point_seq(
            99U, 2001U, 10U, 20U, &status) ==
            SYS_CALIBRATION_RESULT_SESSION_EXPIRED,
        "expired session cannot replay old side effects");
    failures += expect_true(
        sys_calibration_service_begin_seq(
            99U, 2002U, 1000U, 1U, profile->profile_id,
            profile->fingerprint_crc32, &status) ==
            SYS_CALIBRATION_RESULT_SESSION_EXPIRED,
        "expired sid cannot masquerade as a fresh BEGIN");

    if (failures != 0)
    {
        return 1;
    }
    printf("Calibration V3 service state-machine tests: PASS\n");
    return 0;
}
