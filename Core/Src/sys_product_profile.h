#ifndef SYS_PRODUCT_PROFILE_H
#define SYS_PRODUCT_PROFILE_H

#include "type.h"

/* Exactly one Keil Product Target must be selected by the build. */
#if ((defined(PRODUCT_TARGET_50W) ? 1 : 0) + \
     (defined(PRODUCT_TARGET_75W) ? 1 : 0) + \
     (defined(PRODUCT_TARGET_100W) ? 1 : 0) + \
     (defined(PRODUCT_TARGET_150W) ? 1 : 0) + \
     (defined(PRODUCT_TARGET_200W) ? 1 : 0) + \
     (defined(PRODUCT_TARGET_240W) ? 1 : 0)) == 0
#error "No PRODUCT_TARGET_xxx selected"
#elif ((defined(PRODUCT_TARGET_50W) ? 1 : 0) + \
       (defined(PRODUCT_TARGET_75W) ? 1 : 0) + \
       (defined(PRODUCT_TARGET_100W) ? 1 : 0) + \
       (defined(PRODUCT_TARGET_150W) ? 1 : 0) + \
       (defined(PRODUCT_TARGET_200W) ? 1 : 0) + \
       (defined(PRODUCT_TARGET_240W) ? 1 : 0)) > 1
#error "Multiple PRODUCT_TARGET_xxx selected"
#endif

/* V3 phase one freezes only the 50W E1.1 Product Profile. */
#if !defined(PRODUCT_TARGET_50W)
#error "Selected Product Target Profile is not frozen"
#endif

#define SYS_PRODUCT_PROFILE_VERSION                    1U
#define SYS_PRODUCT_PROFILE_FINGERPRINT_INPUT_LENGTH   18U

#define SYS_PRODUCT_PROFILE_ID_50W                     50U
#define SYS_PRODUCT_PROFILE_50W_MID                     1U
#define SYS_PRODUCT_PROFILE_50W_HARDWARE_REVISION  0x0101U
#define SYS_PRODUCT_PROFILE_50W_RATED_POWER_W          50U
#define SYS_PRODUCT_PROFILE_50W_RS3_MOHM              120U
#define SYS_PRODUCT_PROFILE_50W_HARDWARE_MAX_MA      1680U
#define SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA     1400U
#define SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA 893U
#define SYS_PRODUCT_PROFILE_50W_FORMAL_POINT_COUNT     11U
#define SYS_PRODUCT_PROFILE_50W_LEVEL_MIN                0U
#define SYS_PRODUCT_PROFILE_50W_LEVEL_MAX              200U
#define SYS_PRODUCT_PROFILE_50W_LEVEL_STEP              20U
#define SYS_PRODUCT_PROFILE_50W_PWM_FULL_SCALE       1000U
#define SYS_PRODUCT_PROFILE_50W_PWM_POLARITY            1U
#define SYS_PRODUCT_PROFILE_50W_OCO_HARDWARE_REVISION 0x0101U
#define SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32 0x42EE2391UL

/* Mature fixed hardware-protection data retained for the 50W Target only. */
#define SYS_PRODUCT_PROFILE_50W_POWER_TOLERANCE_PM       0U
#define SYS_PRODUCT_PROFILE_50W_ABSOLUTE_FAIL_MA      1680U
#define SYS_PRODUCT_PROFILE_50W_MIN_VOLTAGE_01V        250U
#define SYS_PRODUCT_PROFILE_50W_CP_MIN_VOLTAGE_01V     360U
#define SYS_PRODUCT_PROFILE_50W_MAX_VOLTAGE_01V        560U
#define SYS_PRODUCT_PROFILE_SPECIAL_TEST_VOLTAGE_01V   580U
#define SYS_PRODUCT_PROFILE_IV_POINT_COUNT_MAX           9U

#if (SYS_PRODUCT_PROFILE_VERSION == 0U) || \
    (SYS_PRODUCT_PROFILE_ID_50W == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_MID == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_HARDWARE_REVISION == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_RATED_POWER_W == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_RS3_MOHM == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_HARDWARE_MAX_MA == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_FORMAL_POINT_COUNT == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_LEVEL_STEP == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_PWM_FULL_SCALE == 0U) || \
    (SYS_PRODUCT_PROFILE_50W_OCO_HARDWARE_REVISION == 0U)
#error "50W Product Profile required field is not frozen"
#endif

#if (SYS_PRODUCT_PROFILE_50W_PWM_POLARITY > 1U)
#error "50W pwmPolarity must be 0 or 1"
#endif

#if (((SYS_PRODUCT_PROFILE_50W_LEVEL_MAX - \
       SYS_PRODUCT_PROFILE_50W_LEVEL_MIN) / \
      SYS_PRODUCT_PROFILE_50W_LEVEL_STEP) + 1U) != \
    SYS_PRODUCT_PROFILE_50W_FORMAL_POINT_COUNT
#error "50W formal calibration levels must define exactly 11 points"
#endif

#if (SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA > \
     SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA) || \
    (SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA > \
     SYS_PRODUCT_PROFILE_50W_HARDWARE_MAX_MA)
#error "50W requires SET_OUTCUR <= HWMAX <= Hardware Max"
#endif

/* Selected-build defaults consumed by current Factory/User and safety code. */
#define SYS_PRODUCT_PROFILE_CURRENT_MID                SYS_PRODUCT_PROFILE_50W_MID
#define SYS_PRODUCT_PROFILE_CURRENT_DEFAULT_CURRENT_MA SYS_PRODUCT_PROFILE_50W_DEFAULT_SET_OUTCUR_MA
#define SYS_PRODUCT_PROFILE_CURRENT_RS3_MOHM           SYS_PRODUCT_PROFILE_50W_RS3_MOHM
#define SYS_PRODUCT_PROFILE_CURRENT_HW_MAX_CURRENT_MA  SYS_PRODUCT_PROFILE_50W_DEFAULT_HWMAX_MA
#define SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA    SYS_PRODUCT_PROFILE_50W_HARDWARE_MAX_MA
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
    u16 hardware_revision;
    u16 rated_power_w;
    u16 rs3_mohm;
    /* Product-owned absolute hardware capability, not Factory HWMAX. */
    u16 hw_max_current_ma;
    u16 default_hwmax_current_ma;
    /* User Config default SET_OUTCUR. */
    u16 default_runtime_current_ma;
    u16 pwm_full_scale;
    u8 pwm_polarity;
    u16 oco_hardware_revision;
    /* Fixed hardware power-protection tolerance, unrelated to calibration. */
    u16 power_limit_tolerance_permille;
    u16 absolute_fail_current_ma;
    u16 minimum_voltage_01v;
    u16 constant_power_min_voltage_01v;
    u16 maximum_voltage_01v;
    u16 special_test_voltage_01v;
    const sys_product_profile_iv_limit_st *iv_limits;
    u8 iv_limit_count;
} sys_product_profile_st;

typedef enum
{
    SYS_PRODUCT_CURRENT_VALID = 0,
    SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE,
    SYS_PRODUCT_CURRENT_ZERO,
    SYS_PRODUCT_CURRENT_HW_MAX,
    SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE
} sys_product_current_validation_en;

extern const sys_product_profile_st *sys_product_profile_current(void);
extern boolean_en sys_product_profile_encode_fingerprint(
    const sys_product_profile_st *profile,
    u8 *encoded,
    u16 encoded_size);
extern u32 sys_product_profile_crc32_iso_hdlc(
    const u8 *data,
    u16 length);
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

#endif
