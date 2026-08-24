/*************************************************************
程序功能：V3 Calibration/Runtime A/B Flash入口
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_calibration_flash.h"
#include "sys_calibration_storage.h"
#include "sys_persistent_storage.h"
#include "zk_runtime_stats.h"
#include <string.h>

boolean_en sys_calibration_flash_boot_load_v3(
    sys_calibration_flash_v3_boot_st *boot)
{
    sys_persistent_runtime_data_st runtime;
    u8 config_probe[SYS_PERSISTENT_CONFIG_DEVICE_LENGTH];

    if (boot == NULL)
    {
        return BOOL_FALSE;
    }
    memset(boot, 0, sizeof(*boot));

    /* A valid CFG1 is the proof that the V3 layout was initialized. */
    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            config_probe,
            sizeof(config_probe),
            NULL) != BOOL_TRUE)
    {
        boot->boot_inhibited = BOOL_TRUE;
        return BOOL_TRUE;
    }
    boot->persistence_ready = BOOL_TRUE;

    if (sys_persistent_runtime_load(&runtime, NULL) == BOOL_TRUE)
    {
        boot->boot_inhibited = runtime.calibration_inhibit;
    }
    else if (sys_persistent_runtime_pages_are_empty() == BOOL_TRUE)
    {
        /* A freshly formatted V3 layout intentionally starts with RUN1 empty. */
        boot->boot_inhibited = BOOL_FALSE;
    }
    else
    {
        boot->persistence_ready = BOOL_FALSE;
        boot->boot_inhibited = BOOL_TRUE;
    }

    if (sys_persistent_calibration_load(boot->committed_payload,
                                        &boot->committed_generation,
                                        &boot->committed_crc32) == BOOL_TRUE &&
        sys_calibration_storage_v3_payload_validate(
            boot->committed_payload) == BOOL_TRUE)
    {
        boot->committed_valid = BOOL_TRUE;
        boot->committed_length = SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_flash_set_inhibit(boolean_en active)
{
    return zk_runtime_stats_set_calibration_inhibit(active);
}

boolean_en sys_calibration_flash_commit_v3(
    const u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u16 length,
    u32 payload_crc32,
    u32 *generation)
{
    u32 committed_crc32;

    if (payload == NULL || generation == NULL ||
        length != SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH ||
        sys_calibration_storage_v3_payload_validate(payload) != BOOL_TRUE ||
        sys_persistent_crc32(payload, length) != payload_crc32 ||
        sys_persistent_calibration_commit(payload,
                                          generation,
                                          &committed_crc32) != BOOL_TRUE ||
        committed_crc32 != payload_crc32)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
