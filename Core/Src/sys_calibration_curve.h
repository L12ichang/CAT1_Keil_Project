#ifndef SYS_CALIBRATION_CURVE_H
#define SYS_CALIBRATION_CURVE_H

#include "type.h"

#define SYS_CALIBRATION_CURVE_POINT_COUNT 11U
#define SYS_CALIBRATION_IV_POINT_COUNT 9U
#define SYS_CALIBRATION_CURVE_LEVEL_STEP 20U
#define SYS_CALIBRATION_CURVE_PWM_MAX 1000U

#define SYS_CALIBRATION_50W_MID 1U
#define SYS_CALIBRATION_50W_RS3_MOHM 120U
#define SYS_CALIBRATION_50W_RATED_CURRENT_MA 890U

typedef struct
{
    u16 voltage_01v;
    u16 current_ma;
    u16 power_01w;
} sys_calibration_iv_limit_st;

extern boolean_en sys_calibration_curve_validate_pwm(
    const u16 *pwm,
    u32 count);
extern boolean_en sys_calibration_curve_validate_level(u16 level);
extern boolean_en sys_calibration_curve_interpolate(
    const u16 *pwm,
    u32 count,
    u16 level,
    u16 *value);
extern boolean_en sys_calibration_curve_validate_context(
    u8 mid,
    u16 rs3_mohm,
    u16 rated_current_ma);
extern boolean_en sys_calibration_curve_get_iv_limit(
    u32 index,
    sys_calibration_iv_limit_st *limit);

#endif
