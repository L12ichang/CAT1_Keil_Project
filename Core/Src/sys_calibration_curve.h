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
extern boolean_en sys_calibration_curve_get_iv_limit(
    u32 index,
    sys_calibration_iv_limit_st *limit);

#endif
