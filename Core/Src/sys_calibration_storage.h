#ifndef SYS_CALIBRATION_STORAGE_H
#define SYS_CALIBRATION_STORAGE_H

#include "type.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_product_profile.h"
#include <stddef.h>

#define SYS_CALIBRATION_STORAGE_MAGIC 0x43414C31UL
#define SYS_CALIBRATION_STORAGE_FORMAT_VERSION 4U
#define SYS_CALIBRATION_STORAGE_COMMIT_WORD 0xC0A17EEDUL
#define SYS_CALIBRATION_STORAGE_PAYLOAD_MAX \
    SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH

typedef struct
{
    u32 magic;
    u16 format_version;
    u16 payload_length;
    u32 generation;
    sys_calibration_context_st context;
    u32 payload_crc32;
    u32 record_crc32;
    u8 payload[SYS_CALIBRATION_STORAGE_PAYLOAD_MAX];
    /* 必须最后写入；掉电发生在此之前时记录不可提交。 */
    u32 commit_word;
} sys_calibration_storage_record_st;

typedef char sys_calibration_payload_length_must_remain_198[
    (SYS_CALIBRATION_STORAGE_PAYLOAD_MAX == 198U) ? 1 : -1];
typedef char sys_calibration_record_payload_offset_must_remain_40[
    (offsetof(sys_calibration_storage_record_st, payload) == 40U) ? 1 : -1];
typedef char sys_calibration_record_size_must_remain_244[
    (sizeof(sys_calibration_storage_record_st) == 244U) ? 1 : -1];

extern u32 sys_calibration_storage_crc32(const u8 *data, u32 length);
extern boolean_en sys_calibration_storage_record_build(
    sys_calibration_storage_record_st *record,
    u32 generation,
    const sys_calibration_context_st *context,
    const u8 *payload,
    u16 payload_length);
extern boolean_en sys_calibration_storage_record_validate(
    const sys_calibration_storage_record_st *record);
extern boolean_en sys_calibration_storage_record_is_committed(
    const sys_calibration_storage_record_st *record);
extern boolean_en sys_calibration_storage_select_newest(
    const sys_calibration_storage_record_st *first,
    const sys_calibration_storage_record_st *second,
    const sys_calibration_storage_record_st **selected);

#endif
