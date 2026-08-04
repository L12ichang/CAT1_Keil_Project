#ifndef SYS_CALIBRATION_DRIVER_PROTOCOL_H
#define SYS_CALIBRATION_DRIVER_PROTOCOL_H

#include "type.h"

#define SYS_CALIBRATION_DRIVER_PROTOCOL_VERSION       1U
#define SYS_CALIBRATION_DRIVER_FRAME_HEADER            0x3AU
#define SYS_CALIBRATION_DRIVER_FRAME_TAIL_0           0x0DU
#define SYS_CALIBRATION_DRIVER_FRAME_TAIL_1           0x0AU
#define SYS_CALIBRATION_DRIVER_FRAME_OVERHEAD          7U
#define SYS_CALIBRATION_DRIVER_MAX_PAYLOAD_LENGTH     198U
#define SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH   198U
#define SYS_CALIBRATION_DRIVER_TABLE_FRAME_LENGTH     205U
#define SYS_CALIBRATION_DRIVER_POINT_COUNT             11U
#define SYS_CALIBRATION_DRIVER_LEVEL_STEP              20U
#define SYS_CALIBRATION_DRIVER_ACK_VALUE             0x55U

#define SYS_CALIBRATION_DRIVER_CMD_SET                0x24U
#define SYS_CALIBRATION_DRIVER_CMD_SET_ACK            0x25U
#define SYS_CALIBRATION_DRIVER_CMD_QUERY              0x26U
#define SYS_CALIBRATION_DRIVER_CMD_QUERY_REPLY        0x27U

#define SYS_CALIBRATION_DRIVER_OFFSET_MODE             0x03U
#define SYS_CALIBRATION_DRIVER_OFFSET_TABLE            0x04U
#define SYS_CALIBRATION_DRIVER_OFFSET_LEVEL            0x05U
#define SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT      0x07U
#define SYS_CALIBRATION_DRIVER_OFFSET_MEASURE           0x08U

typedef struct
{
    u8 command;
    u8 offset;
    u8 length;
    u8 data[SYS_CALIBRATION_DRIVER_MAX_PAYLOAD_LENGTH];
} sys_calibration_driver_message_st;

typedef struct
{
    u8 level;
    u8 power_factor_percent;
    u16 input_voltage_01v;
    u16 input_current_ma;
    u16 input_power_01w;
    u16 instrument_output_current_ma;
    u16 instrument_output_power_01w;
    u16 device_output_current_ma;
    u16 device_output_power_01w;
    u16 input_current_ad;
} sys_calibration_driver_point_st;

typedef struct
{
    sys_calibration_driver_point_st point[SYS_CALIBRATION_DRIVER_POINT_COUNT];
} sys_calibration_driver_table_st;

typedef struct
{
    u32 input_ac_voltage_float_bits;
    u16 maximum_output_voltage_01v;
    u16 maximum_output_current_ma;
} sys_calibration_driver_max_context_st;

typedef struct
{
    u16 device_output_current_ma;
    u16 device_output_power_01w;
    u16 input_current_ad;
} sys_calibration_driver_measurement_st;

extern u8 sys_calibration_driver_checksum(u8 command,
                                           u8 offset,
                                           u8 length,
                                           const u8 *data);

extern boolean_en sys_calibration_driver_encode(
    u8 command,
    u8 offset,
    const u8 *data,
    u8 length,
    u8 *frame,
    u16 frame_capacity,
    u16 *frame_length);

extern boolean_en sys_calibration_driver_decode(
    const u8 *frame,
    u16 frame_length,
    sys_calibration_driver_message_st *message);

extern boolean_en sys_calibration_driver_validate_message(
    const sys_calibration_driver_message_st *message);

extern boolean_en sys_calibration_driver_table_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_table_st *table);

extern boolean_en sys_calibration_driver_table_encode(
    const sys_calibration_driver_table_st *table,
    u8 *payload,
    u16 payload_capacity);

extern boolean_en sys_calibration_driver_max_context_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_max_context_st *context);

extern boolean_en sys_calibration_driver_measurement_decode(
    const u8 *payload,
    u16 payload_length,
    sys_calibration_driver_measurement_st *measurement);

#endif
