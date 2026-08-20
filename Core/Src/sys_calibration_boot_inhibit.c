/*************************************************************
程序功能：校准持久化boot-inhibit记录校验框架（不绑定Flash地址）
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_boot_inhibit.h"

static u32 sys_calibration_boot_inhibit_record_crc(
    const sys_calibration_boot_inhibit_record_st *record)
{
    u8 bytes[12U];

    bytes[0] = (u8)(record->magic >> 24U);
    bytes[1] = (u8)(record->magic >> 16U);
    bytes[2] = (u8)(record->magic >> 8U);
    bytes[3] = (u8)record->magic;
    bytes[4] = (u8)(record->format_version >> 8U);
    bytes[5] = (u8)record->format_version;
    bytes[6] = (u8)(record->state >> 8U);
    bytes[7] = (u8)record->state;
    bytes[8] = (u8)(record->generation >> 24U);
    bytes[9] = (u8)(record->generation >> 16U);
    bytes[10] = (u8)(record->generation >> 8U);
    bytes[11] = (u8)record->generation;
    return sys_calibration_boot_inhibit_crc32(bytes, sizeof(bytes));
}

u32 sys_calibration_boot_inhibit_crc32(const u8 *data, u32 length)
{
    u32 crc = 0xFFFFFFFFUL;
    u32 index;
    u8 bit;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) :
                  (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

boolean_en sys_calibration_boot_inhibit_record_build(
    sys_calibration_boot_inhibit_record_st *record,
    u32 generation,
    sys_calibration_boot_inhibit_state_en state)
{
    if (record == NULL ||
        (state != SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE &&
         state != SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE))
    {
        return BOOL_FALSE;
    }
    record->magic = SYS_CALIBRATION_BOOT_INHIBIT_MAGIC;
    record->format_version = SYS_CALIBRATION_BOOT_INHIBIT_FORMAT_VERSION;
    record->state = (u16)state;
    record->generation = generation;
    record->record_crc32 = sys_calibration_boot_inhibit_record_crc(record);
    record->commit_word = SYS_CALIBRATION_BOOT_INHIBIT_COMMIT_WORD;
    return BOOL_TRUE;
}

boolean_en sys_calibration_boot_inhibit_record_validate(
    const sys_calibration_boot_inhibit_record_st *record)
{
    if (record == NULL ||
        record->magic != SYS_CALIBRATION_BOOT_INHIBIT_MAGIC ||
        record->format_version != SYS_CALIBRATION_BOOT_INHIBIT_FORMAT_VERSION ||
        (record->state != SYS_CALIBRATION_BOOT_INHIBIT_INACTIVE &&
         record->state != SYS_CALIBRATION_BOOT_INHIBIT_ACTIVE) ||
        record->commit_word != SYS_CALIBRATION_BOOT_INHIBIT_COMMIT_WORD ||
        sys_calibration_boot_inhibit_record_crc(record) != record->record_crc32)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_boot_inhibit_select_newest(
    const sys_calibration_boot_inhibit_record_st *first,
    const sys_calibration_boot_inhibit_record_st *second,
    sys_calibration_boot_inhibit_state_en *state)
{
    boolean_en first_valid;
    boolean_en second_valid;
    const sys_calibration_boot_inhibit_record_st *selected;

    if (state == NULL)
    {
        return BOOL_FALSE;
    }
    *state = SYS_CALIBRATION_BOOT_INHIBIT_UNKNOWN;
    first_valid = sys_calibration_boot_inhibit_record_validate(first);
    second_valid = sys_calibration_boot_inhibit_record_validate(second);
    if (first_valid != BOOL_TRUE && second_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (first_valid == BOOL_TRUE && second_valid != BOOL_TRUE)
    {
        selected = first;
    }
    else if (second_valid == BOOL_TRUE && first_valid != BOOL_TRUE)
    {
        selected = second;
    }
    else if ((s32)(first->generation - second->generation) >= 0)
    {
        selected = first;
    }
    else
    {
        selected = second;
    }
    *state = (sys_calibration_boot_inhibit_state_en)selected->state;
    return BOOL_TRUE;
}
