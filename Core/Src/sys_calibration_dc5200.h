#ifndef SYS_CALIBRATION_DC5200_H
#define SYS_CALIBRATION_DC5200_H

#include "type.h"

#define SYS_CALIBRATION_DC5200_REPLY_START_0 0xAAU
#define SYS_CALIBRATION_DC5200_REPLY_START_1 0xBBU
#define SYS_CALIBRATION_DC5200_REPLY_START_2 0xCCU
#define SYS_CALIBRATION_DC5200_REPLY_START_3 0x02U
#define SYS_CALIBRATION_DC5200_REQUEST_START_3 0x01U
#define SYS_CALIBRATION_DC5200_ADDRESS 0x01U
#define SYS_CALIBRATION_DC5200_COMPREHENSIVE_PAGE 0x01U
#define SYS_CALIBRATION_DC5200_QUERY_DATA_LENGTH 2U
#define SYS_CALIBRATION_DC5200_COMPREHENSIVE_DATA_LENGTH 60U
#define SYS_CALIBRATION_DC5200_QUERY_FRAME_LENGTH 16U
#define SYS_CALIBRATION_DC5200_COMPREHENSIVE_FRAME_LENGTH 74U

/* 两份资料对综合页查询字段/CRC给出不同黄金向量，实机抓包前禁止发送。 */
#define SYS_CALIBRATION_DC5200_QUERY_ENABLED 0U

typedef struct
{
    u32 input_voltage_001v;
    u32 input_current_0001a;
    u32 input_power_001w;
    u16 power_factor_001;
    u16 frequency;
    u32 output_rms_voltage_001v;
    u32 output_rms_current_0001a;
    u32 output_power_001w;
    u32 efficiency;
    u16 ripple_voltage;
    u16 ripple_voltage_percent;
    u16 ripple_current;
    u16 ripple_current_percent;
    u16 start_phase;
    u16 peak_phase;
    u16 end_phase;
    u16 input_voltage_crest;
    u16 input_current_crest;
    u32 output_average_voltage_001v;
    u32 output_average_current_0001a;
    u16 df_power_factor_001;
} sys_calibration_dc5200_comprehensive_st;

extern u16 sys_calibration_dc5200_crc16_ccitt(
    const u8 *data,
    u16 length);
extern boolean_en sys_calibration_dc5200_build_comprehensive_query(
    u8 *frame,
    u16 capacity,
    u16 *length);
extern boolean_en sys_calibration_dc5200_validate_comprehensive_reply(
    const u8 *frame,
    u16 length);
extern boolean_en sys_calibration_dc5200_decode_comprehensive_reply(
    const u8 *frame,
    u16 length,
    sys_calibration_dc5200_comprehensive_st *measurement);

#endif
