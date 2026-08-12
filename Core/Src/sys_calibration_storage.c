/*************************************************************
程序功能：校准A/B记录校验框架（不绑定Flash地址）
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_storage.h"
#include <string.h>

static u32 sys_calibration_storage_record_crc(
    const sys_calibration_storage_record_st *record)
{
    u8 header[20U];
    u32 index = 0U;
    u32 crc;

    header[index++] = (u8)(record->magic >> 24U);
    header[index++] = (u8)(record->magic >> 16U);
    header[index++] = (u8)(record->magic >> 8U);
    header[index++] = (u8)record->magic;
    header[index++] = (u8)(record->format_version >> 8U);
    header[index++] = (u8)record->format_version;
    header[index++] = (u8)(record->payload_length >> 8U);
    header[index++] = (u8)record->payload_length;
    header[index++] = (u8)(record->generation >> 24U);
    header[index++] = (u8)(record->generation >> 16U);
    header[index++] = (u8)(record->generation >> 8U);
    header[index++] = (u8)record->generation;
    header[index++] = record->mid;
    header[index++] = record->reserved;
    header[index++] = (u8)(record->rs3_mohm >> 8U);
    header[index++] = (u8)record->rs3_mohm;
    header[index++] = (u8)(record->payload_crc32 >> 24U);
    header[index++] = (u8)(record->payload_crc32 >> 16U);
    header[index++] = (u8)(record->payload_crc32 >> 8U);
    header[index++] = (u8)record->payload_crc32;
    crc = sys_calibration_storage_crc32(header, index);
    return sys_calibration_storage_crc32(record->payload,
                                         record->payload_length) ^ crc;
}

u32 sys_calibration_storage_crc32(const u8 *data, u32 length)
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

boolean_en sys_calibration_storage_record_build(
    sys_calibration_storage_record_st *record,
    u32 generation,
    u8 mid,
    u16 rs3_mohm,
    const u8 *payload,
    u16 payload_length)
{
    if (record == NULL || payload == NULL || payload_length == 0U ||
        payload_length > SYS_CALIBRATION_STORAGE_PAYLOAD_MAX)
    {
        return BOOL_FALSE;
    }

    memset(record, 0xFF, sizeof(*record));
    record->magic = SYS_CALIBRATION_STORAGE_MAGIC;
    record->format_version = SYS_CALIBRATION_STORAGE_FORMAT_VERSION;
    record->payload_length = payload_length;
    record->generation = generation;
    record->mid = mid;
    record->reserved = 0U;
    record->rs3_mohm = rs3_mohm;
    memcpy(record->payload, payload, payload_length);
    record->payload_crc32 = sys_calibration_storage_crc32(payload, payload_length);
    record->record_crc32 = sys_calibration_storage_record_crc(record);
    record->commit_word = SYS_CALIBRATION_STORAGE_COMMIT_WORD;
    return BOOL_TRUE;
}

boolean_en sys_calibration_storage_record_is_committed(
    const sys_calibration_storage_record_st *record)
{
    return (record != NULL &&
            record->commit_word == SYS_CALIBRATION_STORAGE_COMMIT_WORD) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_storage_record_validate(
    const sys_calibration_storage_record_st *record)
{
    if (record == NULL ||
        record->magic != SYS_CALIBRATION_STORAGE_MAGIC ||
        record->format_version != SYS_CALIBRATION_STORAGE_FORMAT_VERSION ||
        record->payload_length == 0U ||
        record->payload_length > SYS_CALIBRATION_STORAGE_PAYLOAD_MAX ||
        sys_calibration_storage_record_is_committed(record) != BOOL_TRUE ||
        sys_calibration_storage_crc32(record->payload, record->payload_length) !=
            record->payload_crc32 ||
        sys_calibration_storage_record_crc(record) != record->record_crc32)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_storage_select_newest(
    const sys_calibration_storage_record_st *first,
    const sys_calibration_storage_record_st *second,
    const sys_calibration_storage_record_st **selected)
{
    boolean_en first_valid;
    boolean_en second_valid;

    if (selected == NULL)
    {
        return BOOL_FALSE;
    }
    *selected = NULL;
    first_valid = sys_calibration_storage_record_validate(first);
    second_valid = sys_calibration_storage_record_validate(second);
    if (first_valid != BOOL_TRUE && second_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (first_valid == BOOL_TRUE && second_valid != BOOL_TRUE)
    {
        *selected = first;
    }
    else if (second_valid == BOOL_TRUE && first_valid != BOOL_TRUE)
    {
        *selected = second;
    }
    else if ((s32)(first->generation - second->generation) >= 0)
    {
        *selected = first;
    }
    else
    {
        *selected = second;
    }
    return BOOL_TRUE;
}
