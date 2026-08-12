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

    if (failures != 0)
    {
        return 1;
    }
    printf("BL0942 official and baseline compatibility tests: PASS\n");
    return 0;
}
