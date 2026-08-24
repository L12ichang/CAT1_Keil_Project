#ifndef SYS_CALIBRATION_STORAGE_H
#define SYS_CALIBRATION_STORAGE_H

#include "type.h"

#define SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH 244U
#define SYS_CALIBRATION_STORAGE_V3_RECORD_LENGTH 272U

typedef struct
{
    u8 bytes[SYS_CALIBRATION_STORAGE_V3_RECORD_LENGTH];
} sys_calibration_storage_v3_record_st;

extern boolean_en sys_calibration_storage_v3_payload_header_validate(
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH]);
extern boolean_en sys_calibration_storage_v3_payload_validate(
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH]);
extern boolean_en sys_calibration_storage_v3_record_build(
    sys_calibration_storage_v3_record_st *record,
    u32 generation,
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH]);
extern boolean_en sys_calibration_storage_v3_record_validate(
    const sys_calibration_storage_v3_record_st *record,
    u32 *generation,
    u32 *payload_crc32);

#endif
