#include <stdio.h>

#include "sys_bl0942_frame.h"
#include "sys_calibration_snapshot.h"

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    sys_calibration_snapshot_aggregate_st snapshot;
    int failures = 0;

    sys_calibration_snapshot_init();
    (void)sys_calibration_snapshot_read_aggregate(0U, &snapshot);
    failures += expect_true(
        snapshot.meter_age_ms == SYS_CALIBRATION_SNAPSHOT_AGE_INVALID &&
            snapshot.bl_fresh == 0U,
        "empty BL snapshot is stale");

    sys_calibration_snapshot_publish_meter(
        100U, 0x010203U, 0x040506U, 0x070809U, -7, 0x0A0B0CU,
        500U, 0x80U, NULL,
        SYS_CALIBRATION_METER_FRAME_VALID |
            SYS_CALIBRATION_METER_HEAD_VALID |
            SYS_CALIBRATION_METER_CHECKSUM_VALID,
        0U);
    (void)sys_calibration_snapshot_read_aggregate(600U, &snapshot);
    failures += expect_true(
        snapshot.meter.i_rms_raw == 0x010203U &&
            snapshot.meter.v_rms_raw == 0x040506U &&
            snapshot.meter.watt_raw == -7,
        "RAW snapshot remains uncorrected");
    failures += expect_true(snapshot.meter_age_ms == 500U &&
                                snapshot.bl_fresh == 1U,
                            "BL snapshot is fresh at exactly 500ms");
    (void)sys_calibration_snapshot_read_aggregate(601U, &snapshot);
    failures += expect_true(snapshot.meter_age_ms == 501U &&
                                snapshot.bl_fresh == 0U,
                            "BL snapshot is stale after 500ms");

    sys_calibration_snapshot_publish_meter(
        0xFFFFFFF0UL, 0U, 0U, 0U, 0, 0U, 0U, 0U, NULL,
        SYS_CALIBRATION_METER_FRAME_VALID, 0U);
    (void)sys_calibration_snapshot_read_aggregate(5U, &snapshot);
    failures += expect_true(snapshot.meter_age_ms == 21U &&
                                snapshot.bl_fresh == 1U,
                            "BL snapshot age is wrap-safe");

    if (failures != 0)
    {
        return 1;
    }
    printf("BL0942 RAW snapshot freshness tests: PASS\n");
    return 0;
}
