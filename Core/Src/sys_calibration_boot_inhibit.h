#ifndef SYS_CALIBRATION_BOOT_INHIBIT_H
#define SYS_CALIBRATION_BOOT_INHIBIT_H

#include "type.h"

#define SYS_CALIBRATION_BOOT_INHIBIT_MAGIC 0x43494254UL
#define SYS_CALIBRATION_BOOT_INHIBIT_FORMAT_VERSION 1U
#define SYS_CALIBRATION_BOOT_INHIBIT_COMMIT_WORD 0xB0071EEDUL

typedef enum
{
    SYS_CALIBRATION_BOOT_INHIBIT_UNKNOWN = 0,
    SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE,
    SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE
} sys_calibration_boot_inhibit_state_en;

typedef struct
{
    u32 magic;
    u16 format_version;
    u16 state;
    u32 generation;
    u32 record_crc32;
    u32 commit_word;
} sys_calibration_boot_inhibit_record_st;

extern u32 sys_calibration_boot_inhibit_crc32(const u8 *data, u32 length);
extern boolean_en sys_calibration_boot_inhibit_record_build(
    sys_calibration_boot_inhibit_record_st *record,
    u32 generation,
    sys_calibration_boot_inhibit_state_en state);
extern boolean_en sys_calibration_boot_inhibit_record_validate(
    const sys_calibration_boot_inhibit_record_st *record);
extern boolean_en sys_calibration_boot_inhibit_select_newest(
    const sys_calibration_boot_inhibit_record_st *first,
    const sys_calibration_boot_inhibit_record_st *second,
    sys_calibration_boot_inhibit_state_en *state);

#endif
