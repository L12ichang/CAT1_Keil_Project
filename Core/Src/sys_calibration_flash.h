#ifndef SYS_CALIBRATION_FLASH_H
#define SYS_CALIBRATION_FLASH_H

#include "type.h"
#include "sys_calibration_driver_protocol.h"

typedef struct
{
    boolean_en persistence_ready;
    boolean_en boot_inhibited;
    boolean_en committed_valid;
    u32 committed_generation;
    u16 committed_length;
    u8 committed_payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
} sys_calibration_flash_boot_st;

extern boolean_en sys_calibration_flash_boot_load(
    sys_calibration_flash_boot_st *boot);
extern boolean_en sys_calibration_flash_set_inhibit(boolean_en active);
extern boolean_en sys_calibration_flash_commit(const u8 *payload,
                                               u16 length,
                                               u32 *generation);

#endif
