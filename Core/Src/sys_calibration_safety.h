#ifndef SYS_CALIBRATION_SAFETY_H
#define SYS_CALIBRATION_SAFETY_H

#include "type.h"

#define SYS_CALIBRATION_50W_MIN_VOLTAGE_01V 250U
#define SYS_CALIBRATION_50W_MAX_VOLTAGE_01V 560U
#define SYS_CALIBRATION_50W_POWER_LIMIT_01W 500U
#define SYS_CALIBRATION_50W_CURRENT_LIMIT_MA 1400U
#define SYS_CALIBRATION_ABSOLUTE_FAIL_CURRENT_MA 1680U

extern u16 sys_calibration_safety_limit_current_ma(u16 voltage_01v);
extern boolean_en sys_calibration_safety_limit_percent(
    u8 requested_percent,
    u16 voltage_01v,
    u16 requested_current_ma,
    u8 *safe_percent);
extern boolean_en sys_calibration_safety_is_absolute_overcurrent(u32 current_ma);

#endif
