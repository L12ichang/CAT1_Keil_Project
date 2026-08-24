#ifndef SYS_CALIBRATION_FLASH_H
#define SYS_CALIBRATION_FLASH_H

#include "type.h"
#include "sys_persistent_storage.h"

typedef struct
{
    boolean_en persistence_ready;
    boolean_en boot_inhibited;
    boolean_en committed_valid;
    u32 committed_generation;
    u32 committed_crc32;
    u16 committed_length;
    u8 committed_payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
} sys_calibration_flash_v3_boot_st;

extern boolean_en sys_calibration_flash_boot_load_v3(
    sys_calibration_flash_v3_boot_st *boot);
extern boolean_en sys_calibration_flash_commit_v3(
    const u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 *generation);

extern boolean_en sys_calibration_flash_set_inhibit(boolean_en active);

#endif
