#ifndef CURRENT_CAL_CURVE_H
#define CURRENT_CAL_CURVE_H

#include "common.h"

#define CURRENT_CAL_POINT_COUNT       21U
#define CURRENT_CAL_CURVE_VERSION     1U
#define CURRENT_CAL_PWM_PERIOD_COUNTS 1000U

typedef enum
{
    CURRENT_CAL_CURVE_OK = 0,
    CURRENT_CAL_CURVE_NULL,
    CURRENT_CAL_CURVE_BAD_VERSION,
    CURRENT_CAL_CURVE_BAD_COUNT,
    CURRENT_CAL_CURVE_BAD_ZERO,
    CURRENT_CAL_CURVE_NOT_MONOTONIC,
    CURRENT_CAL_CURVE_OUT_OF_RANGE,
    CURRENT_CAL_CURVE_PROFILE_MISMATCH,
    CURRENT_CAL_CURVE_CRC_MISMATCH
} current_cal_curve_result_en;

typedef struct
{
    u16 curve_version;
    u16 point_count;
    u16 logical_pwm[CURRENT_CAL_POINT_COUNT];
    u32 profile_crc;
    u32 curve_crc;
} current_cal_curve_t;

u32 current_cal_crc32(const u8 *data, u32 length);
u16 current_cal_pwm_logical_max(void);
u32 current_cal_profile_crc(void);
u32 current_cal_curve_crc(const current_cal_curve_t *curve);
current_cal_curve_result_en current_cal_curve_validate(const current_cal_curve_t *curve,
                                                       u32 expected_profile_crc);
u16 current_cal_curve_interpolate(const current_cal_curve_t *curve, u8 percent);

#endif
