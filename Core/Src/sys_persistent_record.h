#ifndef SYS_PERSISTENT_RECORD_H
#define SYS_PERSISTENT_RECORD_H

#include "type.h"

#define SYS_PERSISTENT_HEADER_LENGTH          20U
#define SYS_PERSISTENT_COMMIT_WORD            0xC0A17EEDUL
#define SYS_PERSISTENT_MAX_RECORD_LENGTH      1116U

typedef struct
{
    u8 magic[4];
    u16 format_version;
    u16 record_length;
    u16 payload_length;
    u16 record_offset;
    u32 page_a;
    u32 page_b;
    u16 page_size;
} sys_persistent_record_descriptor_st;

typedef struct
{
    boolean_en valid;
    u8 slot;
    u32 generation;
    u32 payload_crc32;
} sys_persistent_record_meta_st;

typedef struct
{
    boolean_en (*read)(u32 address, u8 *data, u32 length, void *context);
    boolean_en (*erase_page)(u32 page_address, void *context);
    boolean_en (*program)(u32 address,
                          const u8 *data,
                          u32 length,
                          void *context);
    void *context;
} sys_persistent_flash_ops_st;

typedef struct
{
    u16 offset;
    const u8 *data;
    u16 length;
} sys_persistent_section_update_st;

typedef boolean_en (*sys_persistent_payload_validator_fn)(
    const u8 *payload,
    u16 length);

extern u16 sys_persistent_get_u16_le(const u8 *data);
extern u32 sys_persistent_get_u32_le(const u8 *data);
extern void sys_persistent_put_u16_le(u8 *data, u16 value);
extern void sys_persistent_put_u32_le(u8 *data, u32 value);
extern u32 sys_persistent_crc32(const u8 *data, u32 length);
extern boolean_en sys_persistent_generation_is_newer(u32 first, u32 second);

extern boolean_en sys_persistent_record_build(
    const sys_persistent_record_descriptor_st *descriptor,
    u32 generation,
    const u8 *payload,
    u8 *record,
    u16 record_capacity);
extern boolean_en sys_persistent_record_validate(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *record,
    u16 record_length,
    sys_persistent_record_meta_st *meta);
extern boolean_en sys_persistent_record_body_validate(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *record_body,
    u16 body_length,
    sys_persistent_record_meta_st *meta);

extern boolean_en sys_persistent_ab_load_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    u8 *payload,
    u16 payload_capacity,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length);
extern boolean_en sys_persistent_ab_commit_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    const u8 *payload,
    u16 payload_length,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length);
extern boolean_en sys_persistent_ab_update_section_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    u16 section_offset,
    const u8 *section,
    u16 section_length,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length);
extern boolean_en sys_persistent_ab_update_sections_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    const sys_persistent_section_update_st *updates,
    u8 update_count,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length);

#endif
