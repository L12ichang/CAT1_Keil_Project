#ifndef SYS_CALIBRATION_CURVE_H
#define SYS_CALIBRATION_CURVE_H

#include "type.h"
#include "sys_product_profile.h"

#define SYS_CALIBRATION_CURVE_POINT_COUNT 11U
#define SYS_CALIBRATION_IV_POINT_COUNT SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX
#define SYS_CALIBRATION_CURVE_LEVEL_STEP 20U
#define SYS_CALIBRATION_CURVE_PWM_MAX 1000U

typedef sys_product_profile_iv_limit_st sys_calibration_iv_limit_st;

extern boolean_en sys_calibration_curve_validate_pwm(
    const u16 *pwm,
    u32 count);
extern boolean_en sys_calibration_curve_validate_level(u16 level);
extern boolean_en sys_calibration_curve_interpolate(
    const u16 *pwm,
    u32 count,
    u16 level,
    u16 *value);
extern boolean_en sys_calibration_curve_interpolate_u16(
    const u16 *x,
    const u16 *y,
    u32 count,
    u16 input,
    u16 *output);
extern boolean_en sys_calibration_curve_interpolate_u32(
    const u32 *x,
    const u16 *y,
    u32 count,
    u32 input,
    u16 *output);
extern boolean_en sys_calibration_curve_interpolate_s32(
    const s32 *x,
    const u16 *y,
    u32 count,
    s32 input,
    u16 *output);
extern boolean_en sys_calibration_curve_get_iv_limit(
    u32 index,
    sys_calibration_iv_limit_st *limit);

#endif
