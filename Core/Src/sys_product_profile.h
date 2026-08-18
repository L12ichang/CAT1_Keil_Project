#ifndef SYS_PRODUCT_PROFILE_H
#define SYS_PRODUCT_PROFILE_H

#include "type.h"

#define SYS_PRODUCT_PROFILE_ID_50W  50U
#define SYS_PRODUCT_PROFILE_ID_75W  75U
#define SYS_PRODUCT_PROFILE_ID_100W 100U
#define SYS_PRODUCT_PROFILE_ID_150W 150U
#define SYS_PRODUCT_PROFILE_ID_200W 200U
#define SYS_PRODUCT_PROFILE_ID_240W 240U

#ifndef SYS_PRODUCT_PROFILE_SELECT
#define SYS_PRODUCT_PROFILE_SELECT SYS_PRODUCT_PROFILE_ID_50W
#endif

/*
 * A non-50W image must not be produced until all hardware safety fields are
 * frozen.  Keeping this as a compile-time error prevents an incomplete
 * profile from becoming a field binary merely because runtime output is off.
 */
#if SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_50W
#define SYS_PRODUCT_PROFILE_BUILD_ENABLED 1U
#elif SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_75W
#error "75W build disabled: default 1380mA is within +5% power tolerance but exceeds the frozen 56V Io_MAX 1340mA"
#elif SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_100W
#error "100W build disabled: absoluteFailCurrent and complete protection review are not frozen"
#elif SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_150W
#error "150W build disabled: I-V table and absoluteFailCurrent are not frozen"
#elif SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_200W
#error "200W build disabled: I-V table and absoluteFailCurrent are not frozen"
#elif SYS_PRODUCT_PROFILE_SELECT == SYS_PRODUCT_PROFILE_ID_240W
#error "240W build disabled: I-V table and absoluteFailCurrent are not frozen"
#else
#error "Unsupported SYS_PRODUCT_PROFILE_SELECT"
#endif

#define SYS_PRODUCT_PROFILE_VERSION                  1U
#define SYS_CALIBRATION_CONTEXT_VERSION               1U
#define SYS_PRODUCT_PROFILE_50W_MID                  1U
#define SYS_PRODUCT_PROFILE_50W_RATED_POWER_W        50U
#define SYS_PRODUCT_PROFILE_50W_POWER_TOLERANCE_PM   0U
#define SYS_PRODUCT_PROFILE_50W_CAL_SPAN_TOLERANCE_PM 50U
#define SYS_PRODUCT_PROFILE_50W_DEFAULT_CURRENT_MA   890U
#define SYS_PRODUCT_PROFILE_50W_RS3_MOHM             120U
#define SYS_PRODUCT_PROFILE_50W_HW_MAX_CURRENT_MA    1680U
#define SYS_PRODUCT_PROFILE_50W_ABSOLUTE_FAIL_MA     1680U
#define SYS_PRODUCT_PROFILE_50W_MIN_VOLTAGE_01V      250U
#define SYS_PRODUCT_PROFILE_50W_CP_MIN_VOLTAGE_01V   360U
#define SYS_PRODUCT_PROFILE_50W_MAX_VOLTAGE_01V      560U
#define SYS_PRODUCT_PROFILE_SPECIAL_TEST_VOLTAGE_01V 580U
#define SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32    0xA7777C1EUL
#define SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX       9U

#define SYS_PRODUCT_PROFILE_75W_MID                   2U
#define SYS_PRODUCT_PROFILE_75W_RATED_POWER_W         75U
#define SYS_PRODUCT_PROFILE_75W_POWER_TOLERANCE_PM    50U
#define SYS_PRODUCT_PROFILE_75W_CAL_SPAN_TOLERANCE_PM 50U
#define SYS_PRODUCT_PROFILE_75W_DEFAULT_CURRENT_MA    1380U
#define SYS_PRODUCT_PROFILE_75W_RS3_MOHM              50U
#define SYS_PRODUCT_PROFILE_75W_HW_MAX_CURRENT_MA     2150U

#define SYS_PRODUCT_PROFILE_100W_MID                  3U
#define SYS_PRODUCT_PROFILE_100W_RATED_POWER_W        100U
#define SYS_PRODUCT_PROFILE_100W_DEFAULT_CURRENT_MA   1780U
#define SYS_PRODUCT_PROFILE_100W_RS3_MOHM             50U
#define SYS_PRODUCT_PROFILE_100W_HW_MAX_CURRENT_MA    2800U

#define SYS_PRODUCT_PROFILE_150W_MID                  4U
#define SYS_PRODUCT_PROFILE_150W_RATED_POWER_W        150U
#define SYS_PRODUCT_PROFILE_150W_DEFAULT_CURRENT_MA   2700U
#define SYS_PRODUCT_PROFILE_150W_RS3_MOHM             30U
#define SYS_PRODUCT_PROFILE_150W_HW_MAX_CURRENT_MA    4500U

#define SYS_PRODUCT_PROFILE_200W_MID                  5U
#define SYS_PRODUCT_PROFILE_200W_RATED_POWER_W        200U
#define SYS_PRODUCT_PROFILE_200W_DEFAULT_CURRENT_MA   3600U
#define SYS_PRODUCT_PROFILE_200W_RS3_MOHM             15U
#define SYS_PRODUCT_PROFILE_200W_HW_MAX_CURRENT_MA    6000U

#define SYS_PRODUCT_PROFILE_240W_MID                  6U
#define SYS_PRODUCT_PROFILE_240W_RATED_POWER_W        240U
#define SYS_PRODUCT_PROFILE_240W_DEFAULT_CURRENT_MA   4300U
#define SYS_PRODUCT_PROFILE_240W_RS3_MOHM             15U
#define SYS_PRODUCT_PROFILE_240W_HW_MAX_CURRENT_MA    7000U

/* Selected-build aliases used by generic factory fallback code. */
#define SYS_PRODUCT_PROFILE_CURRENT_MID                SYS_PRODUCT_PROFILE_50W_MID
#define SYS_PRODUCT_PROFILE_CURRENT_DEFAULT_CURRENT_MA SYS_PRODUCT_PROFILE_50W_DEFAULT_CURRENT_MA
#define SYS_PRODUCT_PROFILE_CURRENT_RS3_MOHM           SYS_PRODUCT_PROFILE_50W_RS3_MOHM
#define SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA  SYS_PRODUCT_PROFILE_50W_HW_MAX_CURRENT_MA
#define SYS_PRODUCT_PROFILE_CURRENT_MIN_VOLTAGE_01V    SYS_PRODUCT_PROFILE_50W_MIN_VOLTAGE_01V
#define SYS_PRODUCT_PROFILE_CURRENT_MAX_VOLTAGE_01V    SYS_PRODUCT_PROFILE_50W_MAX_VOLTAGE_01V

typedef struct
{
    u16 voltage_01v;
    u16 current_ma;
} sys_product_profile_iv_limit_st;

typedef struct
{
    u16 profile_id;
    const char *model_code;
    u16 profile_version;
    u32 fingerprint_crc32;
    u8 mid;
    u16 rated_power_w;
    /* Conservative zero until a product-approved tolerance is frozen. */
    u16 power_limit_tolerance_permille;
    /* Full-load output-current accuracy from the selected product spec. */
    u16 calibration_span_tolerance_permille;
    /* Factory fallback only; the persisted SET_OUTCUR remains writable. */
    u16 default_runtime_current_ma;
    u16 rs3_mohm;
    u16 hw_max_current_ma;
    u16 absolute_fail_current_ma;
    u16 minimum_voltage_01v;
    u16 constant_power_min_voltage_01v;
    u16 maximum_voltage_01v;
    u16 special_test_voltage_01v;
    const sys_product_profile_iv_limit_st *iv_limits;
    u8 iv_limit_count;
    boolean_en build_enabled;
    boolean_en nonzero_calibration_enabled;
    const char *block_reason;
    const char *block_code;
} sys_product_profile_st;

typedef struct
{
    u16 profile_id;
    u16 profile_version;
    u32 profile_fingerprint_crc32;
    u16 calibration_voltage_01v;
    u16 configured_rated_current_ma;
    u16 calibrated_max_current_ma;
    u32 table_crc32;
} sys_calibration_context_st;

typedef char sys_calibration_context_size_must_remain_20[
    (sizeof(sys_calibration_context_st) == 20U) ? 1 : -1];

typedef enum
{
    SYS_PRODUCT_CURRENT_VALID = 0,
    SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE,
    SYS_PRODUCT_CURRENT_VOLTAGE_UNBOUND,
    SYS_PRODUCT_CURRENT_ZERO,
    SYS_PRODUCT_CURRENT_IV_LIMIT,
    SYS_PRODUCT_CURRENT_POWER_LIMIT,
    SYS_PRODUCT_CURRENT_HW_MAX,
    SYS_PRODUCT_CURRENT_ABSOLUTE_FAIL,
    SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE,
    SYS_PRODUCT_CURRENT_CALIBRATION_MAX_UNAVAILABLE,
    SYS_PRODUCT_CURRENT_EXCEEDS_CALIBRATED_MAX
} sys_product_current_validation_en;

extern const sys_product_profile_st *sys_product_profile_current(void);
extern const sys_product_profile_st *sys_product_profile_find(u16 profile_id);
extern u32 sys_product_profile_calculate_fingerprint(
    const sys_product_profile_st *profile);
extern boolean_en sys_product_profile_is_complete(
    const sys_product_profile_st *profile);
extern boolean_en sys_product_profile_runtime_matches(
    u8 mid,
    u16 rs3_mohm,
    u16 hw_max_current_ma);
extern boolean_en sys_product_profile_get_iv_limit(
    const sys_product_profile_st *profile,
    u32 index,
    sys_product_profile_iv_limit_st *limit);
extern boolean_en sys_product_profile_compute_i100_ma(
    const sys_product_profile_st *profile,
    u16 calibration_voltage_01v,
    u16 *current_ma);
extern boolean_en sys_product_profile_scale_percent_to_pwm(
    const sys_product_profile_st *profile,
    u16 voltage_01v,
    u8 percent,
    u16 pwm_range,
    u16 *pwm_value);
extern sys_product_current_validation_en sys_product_profile_validate_runtime_current(
    const sys_product_profile_st *profile,
    u16 bound_voltage_01v,
    u32 configured_current_ma);
extern sys_product_current_validation_en sys_product_profile_validate_calibrated_current(
    u32 configured_current_ma,
    boolean_en calibrated_max_available,
    u16 calibrated_max_current_ma);
extern const char *sys_product_profile_current_validation_reason(
    sys_product_current_validation_en result);
extern boolean_en sys_product_profile_context_build(
    u16 calibration_voltage_01v,
    u16 configured_rated_current_ma,
    u16 calibrated_max_current_ma,
    u32 table_crc32,
    sys_calibration_context_st *context);
extern u32 sys_product_profile_context_binding_crc32(
    const sys_calibration_context_st *context);
extern boolean_en sys_product_profile_context_validate(
    const sys_calibration_context_st *context,
    boolean_en require_table_crc);
extern boolean_en sys_product_profile_context_equal(
    const sys_calibration_context_st *first,
    const sys_calibration_context_st *second,
    boolean_en compare_table_crc);

#endif
