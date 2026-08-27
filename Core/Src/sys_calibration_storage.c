/*************************************************************
程序功能：校准A/B记录校验框架（不绑定Flash地址）
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_storage.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_persistent_record.h"
#include "sys_product_profile.h"
#include <string.h>

static const sys_persistent_record_descriptor_st
    _sys_calibration_v3_codec_descriptor =
{
    {'C', 'A', 'L', '4'},
    4U,
    SYS_CALIBRATION_STORAGE_V3_RECORD_LENGTH,
    SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH,
    0U,
    0U,
    0x800U,
    0x800U
};

boolean_en sys_calibration_storage_v3_payload_header_validate(
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH])
{
    if (payload == NULL || memcmp(payload, "CALP", 4U) != 0 ||
        sys_persistent_get_u16_le(payload + 0x04U) !=
            SYS_CALIBRATION_PAYLOAD_VERSION ||
        sys_persistent_get_u16_le(payload + 0x06U) !=
            SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH ||
        payload[0x10U] != 11U || payload[0x11U] != 20U ||
        sys_persistent_get_u16_le(payload + 0x12U) != 0x001FU)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_storage_v3_payload_validate(
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH])
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_payload_st decoded;

    if (sys_calibration_storage_v3_payload_header_validate(payload) != BOOL_TRUE ||
        sys_calibration_payload_decode(
            payload,
            SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH,
            &decoded) != BOOL_TRUE ||
        sys_calibration_payload_validate(&decoded) != BOOL_TRUE ||
        sys_calibration_payload_matches_product(&decoded, profile) != BOOL_TRUE ||
        sys_calibration_payload_within_product_limits(&decoded, profile) !=
            BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_calibration_storage_v3_record_build(
    sys_calibration_storage_v3_record_st *record,
    u32 generation,
    const u8 payload[SYS_CALIBRATION_STORAGE_V3_PAYLOAD_LENGTH])
{
    if (record == NULL ||
        sys_calibration_storage_v3_payload_validate(payload) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return sys_persistent_record_build(
        &_sys_calibration_v3_codec_descriptor,
        generation,
        payload,
        record->bytes,
        sizeof(record->bytes));
}

boolean_en sys_calibration_storage_v3_record_validate(
    const sys_calibration_storage_v3_record_st *record,
    u32 *generation,
    u32 *payload_crc32)
{
    sys_persistent_record_meta_st meta;

    if (record == NULL ||
        sys_persistent_record_validate(&_sys_calibration_v3_codec_descriptor,
                                       record->bytes,
                                       sizeof(record->bytes),
                                       &meta) != BOOL_TRUE ||
        sys_calibration_storage_v3_payload_validate(
            record->bytes + SYS_PERSISTENT_HEADER_LENGTH) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    if (payload_crc32 != NULL)
    {
        *payload_crc32 = meta.payload_crc32;
    }
    return BOOL_TRUE;
}
