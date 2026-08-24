#ifndef SYS_CALIBRATION_DRIVER_PROTOCOL_H
#define SYS_CALIBRATION_DRIVER_PROTOCOL_H

#include "type.h"
#include "sys_product_profile.h"

#define SYS_CALIBRATION_PAYLOAD_MAGIC_0              0x43U
#define SYS_CALIBRATION_PAYLOAD_MAGIC_1              0x41U
#define SYS_CALIBRATION_PAYLOAD_MAGIC_2              0x4CU
#define SYS_CALIBRATION_PAYLOAD_MAGIC_3              0x50U
#define SYS_CALIBRATION_PAYLOAD_VERSION                  1U
#define SYS_CALIBRATION_PAYLOAD_LENGTH                 244U
#define SYS_CALIBRATION_PAYLOAD_HEX_LENGTH             488U
#define SYS_CALIBRATION_PAYLOAD_POINT_COUNT             11U
#define SYS_CALIBRATION_PAYLOAD_LEVEL_STEP              20U
#define SYS_CALIBRATION_PAYLOAD_VALID_FLAGS          0x001FU

#define SYS_CALIBRATION_PAYLOAD_HEADER_OFFSET         0x000U
#define SYS_CALIBRATION_PAYLOAD_OUTPUT_OFFSET         0x014U
#define SYS_CALIBRATION_PAYLOAD_OCO_OFFSET            0x040U
#define SYS_CALIBRATION_PAYLOAD_BL_CURRENT_OFFSET     0x06CU
#define SYS_CALIBRATION_PAYLOAD_BL_POWER_OFFSET       0x0AEU
#define SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET     0x0F0U

typedef struct
{
    u16 logical_pwm;
    u16 reference_output_current_ma;
} sys_calibration_output_point_st;

typedef struct
{
    u16 oco_adc_raw;
    u16 reference_output_current_ma;
} sys_calibration_oco_point_st;

typedef struct
{
    u32 bl_current_raw;
    u16 reference_input_current_ma;
} sys_calibration_bl_current_point_st;

typedef struct
{
    s32 bl_power_raw;
    u16 reference_input_power_01w;
} sys_calibration_bl_power_point_st;

typedef struct
{
    u16 profile_id;
    u16 profile_version;
    u32 profile_fingerprint;
    u8 point_count;
    u8 level_step;
    u16 valid_flags;
    sys_calibration_output_point_st output[SYS_CALIBRATION_PAYLOAD_POINT_COUNT];
    sys_calibration_oco_point_st oco[SYS_CALIBRATION_PAYLOAD_POINT_COUNT];
    sys_calibration_bl_current_point_st bl_current[SYS_CALIBRATION_PAYLOAD_POINT_COUNT];
    sys_calibration_bl_power_point_st bl_power[SYS_CALIBRATION_PAYLOAD_POINT_COUNT];
    u32 voltage_gain_q24;
} sys_calibration_payload_st;

extern u32 sys_calibration_payload_crc32_iso_hdlc(
    const u8 *data,
    u16 length);
extern boolean_en sys_calibration_payload_validate(
    const sys_calibration_payload_st *payload);
extern boolean_en sys_calibration_payload_matches_product(
    const sys_calibration_payload_st *payload,
    const sys_product_profile_st *profile);
extern boolean_en sys_calibration_payload_within_product_limits(
    const sys_calibration_payload_st *payload,
    const sys_product_profile_st *profile);
extern boolean_en sys_calibration_payload_encode(
    const sys_calibration_payload_st *payload,
    u8 *encoded,
    u16 encoded_capacity);
extern boolean_en sys_calibration_payload_decode(
    const u8 *encoded,
    u16 encoded_length,
    sys_calibration_payload_st *payload);
extern boolean_en sys_calibration_payload_hex_encode(
    const u8 *payload,
    u16 payload_length,
    char *hex,
    u16 hex_capacity);
extern boolean_en sys_calibration_payload_hex_decode(
    const char *hex,
    u8 *payload,
    u16 payload_capacity);

#endif
