/*************************************************************
程序功能：校准表和boot-inhibit的A/B掉电安全Flash事务
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_calibration_flash.h"
#include "sys_calibration_storage.h"
#include "sys_calibration_boot_inhibit.h"
#include "sys_calibration_curve.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include <stddef.h>
#include <string.h>

#define SYS_CALIBRATION_INHIBIT_OFFSET 0x300UL

typedef char sys_calibration_record_fits_slot[
    (sizeof(sys_calibration_storage_record_st) < SYS_CALIBRATION_INHIBIT_OFFSET) ? 1 : -1];
typedef char sys_calibration_inhibit_fits_slot[
    ((SYS_CALIBRATION_INHIBIT_OFFSET + sizeof(sys_calibration_boot_inhibit_record_st)) <=
     CAT1_FLASH_CALIBRATION_SLOT_SIZE) ? 1 : -1];

static sys_calibration_storage_record_st _record_a;
static sys_calibration_storage_record_st _record_b;
static sys_calibration_boot_inhibit_record_st _inhibit_a;
static sys_calibration_boot_inhibit_record_st _inhibit_b;

static boolean_en sys_calibration_flash_is_blank(const u8 *data, u32 length)
{
    u32 index;
    for (index = 0U; index < length; ++index)
    {
        if (data[index] != 0xFFU)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static void sys_calibration_flash_read_records(void)
{
    hw_flash_read_bytes(CAT1_FLASH_CALIBRATION_A_CANDIDATE_START,
                        (u8 *)&_record_a, sizeof(_record_a));
    hw_flash_read_bytes(CAT1_FLASH_CALIBRATION_B_CANDIDATE_START,
                        (u8 *)&_record_b, sizeof(_record_b));
    hw_flash_read_bytes(CAT1_FLASH_CALIBRATION_A_CANDIDATE_START +
                            SYS_CALIBRATION_INHIBIT_OFFSET,
                        (u8 *)&_inhibit_a, sizeof(_inhibit_a));
    hw_flash_read_bytes(CAT1_FLASH_CALIBRATION_B_CANDIDATE_START +
                            SYS_CALIBRATION_INHIBIT_OFFSET,
                        (u8 *)&_inhibit_b, sizeof(_inhibit_b));
}

static boolean_en sys_calibration_flash_write_committed(u32 address,
                                                        u8 *record,
                                                        u32 length,
                                                        u32 commit_offset,
                                                        u32 commit_word)
{
    u32 pending = 0xFFFFFFFFUL;
    if (commit_offset > length - sizeof(u32))
    {
        return BOOL_FALSE;
    }
    memcpy(record + commit_offset, &pending, sizeof(pending));
    if (hw_flash_write_bytes_checked(address, record, length) != BOOL_TRUE ||
        hw_flash_write_bytes_checked(address + commit_offset,
                                     (const u8 *)&commit_word,
                                     sizeof(commit_word)) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_flash_boot_load(sys_calibration_flash_boot_st *boot)
{
    const sys_calibration_storage_record_st *selected = NULL;
    sys_calibration_boot_inhibit_state_en state;

    if (boot == NULL)
    {
        return BOOL_FALSE;
    }
    memset(boot, 0, sizeof(*boot));
    sys_calibration_flash_read_records();
    boot->persistence_ready = BOOL_TRUE;

    if (sys_calibration_boot_inhibit_select_newest(
            &_inhibit_a, &_inhibit_b, &state) == BOOL_TRUE)
    {
        boot->boot_inhibited = (state == SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE) ?
                               BOOL_TRUE : BOOL_FALSE;
    }
    else if (sys_calibration_flash_is_blank((const u8 *)&_inhibit_a,
                                            sizeof(_inhibit_a)) == BOOL_TRUE &&
             sys_calibration_flash_is_blank((const u8 *)&_inhibit_b,
                                            sizeof(_inhibit_b)) == BOOL_TRUE)
    {
        /* 升级前设备没有记录，按正常启动处理；非空损坏记录则失败关闭。 */
        boot->boot_inhibited = BOOL_FALSE;
    }
    else
    {
        boot->boot_inhibited = BOOL_TRUE;
        boot->persistence_ready = BOOL_FALSE;
    }

    if (sys_calibration_storage_select_newest(
            &_record_a, &_record_b, &selected) == BOOL_TRUE &&
        selected->mid == SYS_CALIBRATION_50W_MID &&
        selected->rs3_mohm == SYS_CALIBRATION_50W_RS3_MOHM)
    {
        boot->committed_valid = BOOL_TRUE;
        boot->committed_generation = selected->generation;
        boot->committed_length = selected->payload_length;
        memcpy(boot->committed_payload, selected->payload,
               selected->payload_length);
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_flash_set_inhibit(boolean_en active)
{
    boolean_en a_valid;
    boolean_en b_valid;
    u32 generation = 1U;
    u32 address;
    sys_calibration_boot_inhibit_record_st record;

    sys_calibration_flash_read_records();
    a_valid = sys_calibration_boot_inhibit_record_validate(&_inhibit_a);
    b_valid = sys_calibration_boot_inhibit_record_validate(&_inhibit_b);
    if (a_valid == BOOL_TRUE || b_valid == BOOL_TRUE)
    {
        if (a_valid == BOOL_TRUE &&
            (b_valid != BOOL_TRUE || (s32)(_inhibit_a.generation - _inhibit_b.generation) >= 0))
        {
            generation = _inhibit_a.generation + 1U;
            address = CAT1_FLASH_CALIBRATION_B_CANDIDATE_START +
                      SYS_CALIBRATION_INHIBIT_OFFSET;
        }
        else
        {
            generation = _inhibit_b.generation + 1U;
            address = CAT1_FLASH_CALIBRATION_A_CANDIDATE_START +
                      SYS_CALIBRATION_INHIBIT_OFFSET;
        }
    }
    else
    {
        address = CAT1_FLASH_CALIBRATION_A_CANDIDATE_START +
                  SYS_CALIBRATION_INHIBIT_OFFSET;
    }
    if (sys_calibration_boot_inhibit_record_build(
            &record, generation,
            (active == BOOL_TRUE) ? SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE :
                                    SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE) != BOOL_TRUE ||
        sys_calibration_flash_write_committed(
            address, (u8 *)&record, sizeof(record),
            offsetof(sys_calibration_boot_inhibit_record_st, commit_word),
            SYS_CALIBRATION_BOOT_INHIBIT_COMMIT_WORD) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, (u8 *)&record, sizeof(record));
    return sys_calibration_boot_inhibit_record_validate(&record);
}

boolean_en sys_calibration_flash_commit(const u8 *payload,
                                        u16 length,
                                        u32 *generation)
{
    const sys_calibration_storage_record_st *selected = NULL;
    sys_calibration_storage_record_st record;
    u32 next_generation = 1U;
    u32 address = CAT1_FLASH_CALIBRATION_A_CANDIDATE_START;

    if (payload == NULL || generation == NULL)
    {
        return BOOL_FALSE;
    }
    sys_calibration_flash_read_records();
    if (sys_calibration_storage_select_newest(
            &_record_a, &_record_b, &selected) == BOOL_TRUE)
    {
        next_generation = selected->generation + 1U;
        address = (selected == &_record_a) ?
                  CAT1_FLASH_CALIBRATION_B_CANDIDATE_START :
                  CAT1_FLASH_CALIBRATION_A_CANDIDATE_START;
    }
    if (sys_calibration_storage_record_build(
            &record, next_generation, SYS_CALIBRATION_50W_MID,
            SYS_CALIBRATION_50W_RS3_MOHM, payload, length) != BOOL_TRUE ||
        sys_calibration_flash_write_committed(
            address, (u8 *)&record, sizeof(record),
            offsetof(sys_calibration_storage_record_st, commit_word),
            SYS_CALIBRATION_STORAGE_COMMIT_WORD) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(address, (u8 *)&record, sizeof(record));
    if (sys_calibration_storage_record_validate(&record) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    *generation = next_generation;
    return BOOL_TRUE;
}
