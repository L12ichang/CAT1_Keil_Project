#include "sys_persistent_record.h"
#include <string.h>

#define SYS_PERSISTENT_GENERATION_OFFSET       0x008U
#define SYS_PERSISTENT_PAYLOAD_LENGTH_OFFSET   0x00CU
#define SYS_PERSISTENT_RESERVED_OFFSET         0x00EU
#define SYS_PERSISTENT_PAYLOAD_CRC_OFFSET      0x010U

static boolean_en sys_persistent_descriptor_valid(
    const sys_persistent_record_descriptor_st *descriptor)
{
    u32 end_a;
    u32 end_b;

    if (descriptor == NULL || descriptor->page_size == 0U ||
        descriptor->payload_length == 0U ||
        descriptor->record_length !=
            (u16)(SYS_PERSISTENT_HEADER_LENGTH +
                  descriptor->payload_length + 8U) ||
        descriptor->record_length > SYS_PERSISTENT_MAX_RECORD_LENGTH ||
        descriptor->record_offset > descriptor->page_size ||
        descriptor->record_length >
            (u16)(descriptor->page_size - descriptor->record_offset))
    {
        return BOOL_FALSE;
    }
    end_a = descriptor->page_a + descriptor->page_size;
    end_b = descriptor->page_b + descriptor->page_size;
    if (end_a < descriptor->page_a || end_b < descriptor->page_b ||
        descriptor->page_a == descriptor->page_b)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

static boolean_en sys_persistent_ops_valid(
    const sys_persistent_flash_ops_st *ops)
{
    return (ops != NULL && ops->read != NULL && ops->erase_page != NULL &&
            ops->program != NULL) ? BOOL_TRUE : BOOL_FALSE;
}

u16 sys_persistent_get_u16_le(const u8 *data)
{
    return (data == NULL) ? 0U :
           (u16)((u16)data[0] | ((u16)data[1] << 8U));
}

u32 sys_persistent_get_u32_le(const u8 *data)
{
    return (data == NULL) ? 0U :
           ((u32)data[0] | ((u32)data[1] << 8U) |
            ((u32)data[2] << 16U) | ((u32)data[3] << 24U));
}

void sys_persistent_put_u16_le(u8 *data, u16 value)
{
    if (data != NULL)
    {
        data[0] = (u8)value;
        data[1] = (u8)(value >> 8U);
    }
}

void sys_persistent_put_u32_le(u8 *data, u32 value)
{
    if (data != NULL)
    {
        data[0] = (u8)value;
        data[1] = (u8)(value >> 8U);
        data[2] = (u8)(value >> 16U);
        data[3] = (u8)(value >> 24U);
    }
}

u32 sys_persistent_crc32(const u8 *data, u32 length)
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
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

boolean_en sys_persistent_generation_is_newer(u32 first, u32 second)
{
    if (first == second)
    {
        return BOOL_FALSE;
    }
    return ((s32)(first - second) > 0) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_persistent_record_build(
    const sys_persistent_record_descriptor_st *descriptor,
    u32 generation,
    const u8 *payload,
    u8 *record,
    u16 record_capacity)
{
    u16 record_crc_offset;
    u32 payload_crc;
    u32 record_crc;

    if (sys_persistent_descriptor_valid(descriptor) != BOOL_TRUE ||
        generation == 0U || payload == NULL || record == NULL ||
        record_capacity < descriptor->record_length)
    {
        return BOOL_FALSE;
    }
    record_crc_offset =
        (u16)(SYS_PERSISTENT_HEADER_LENGTH + descriptor->payload_length);

    /* memmove deliberately supports payload already residing at record+0x14. */
    memmove(record + SYS_PERSISTENT_HEADER_LENGTH,
            payload,
            descriptor->payload_length);
    memcpy(record, descriptor->magic, sizeof(descriptor->magic));
    sys_persistent_put_u16_le(record + 0x004U, descriptor->format_version);
    sys_persistent_put_u16_le(record + 0x006U, descriptor->record_length);
    sys_persistent_put_u32_le(record + SYS_PERSISTENT_GENERATION_OFFSET,
                              generation);
    sys_persistent_put_u16_le(record + SYS_PERSISTENT_PAYLOAD_LENGTH_OFFSET,
                              descriptor->payload_length);
    sys_persistent_put_u16_le(record + SYS_PERSISTENT_RESERVED_OFFSET, 0U);
    payload_crc = sys_persistent_crc32(
        record + SYS_PERSISTENT_HEADER_LENGTH,
        descriptor->payload_length);
    sys_persistent_put_u32_le(record + SYS_PERSISTENT_PAYLOAD_CRC_OFFSET,
                              payload_crc);
    record_crc = sys_persistent_crc32(record, record_crc_offset);
    sys_persistent_put_u32_le(record + record_crc_offset, record_crc);
    sys_persistent_put_u32_le(record + record_crc_offset + 4U,
                              SYS_PERSISTENT_COMMIT_WORD);
    return BOOL_TRUE;
}

static boolean_en sys_persistent_record_validate_body(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *record,
    u16 record_length,
    boolean_en require_commit,
    sys_persistent_record_meta_st *meta)
{
    u16 record_crc_offset;
    u32 generation;
    u32 payload_crc;

    if (meta != NULL)
    {
        memset(meta, 0, sizeof(*meta));
    }
    if (sys_persistent_descriptor_valid(descriptor) != BOOL_TRUE ||
        record == NULL ||
        record_length < (u16)(descriptor->record_length -
                              ((require_commit == BOOL_TRUE) ? 0U : 4U)) ||
        memcmp(record, descriptor->magic, sizeof(descriptor->magic)) != 0 ||
        sys_persistent_get_u16_le(record + 0x004U) !=
            descriptor->format_version ||
        sys_persistent_get_u16_le(record + 0x006U) !=
            descriptor->record_length ||
        sys_persistent_get_u16_le(record + SYS_PERSISTENT_PAYLOAD_LENGTH_OFFSET) !=
            descriptor->payload_length ||
        sys_persistent_get_u16_le(record + SYS_PERSISTENT_RESERVED_OFFSET) != 0U)
    {
        return BOOL_FALSE;
    }
    generation =
        sys_persistent_get_u32_le(record + SYS_PERSISTENT_GENERATION_OFFSET);
    if (generation == 0U)
    {
        return BOOL_FALSE;
    }
    payload_crc =
        sys_persistent_get_u32_le(record + SYS_PERSISTENT_PAYLOAD_CRC_OFFSET);
    if (payload_crc !=
        sys_persistent_crc32(record + SYS_PERSISTENT_HEADER_LENGTH,
                             descriptor->payload_length))
    {
        return BOOL_FALSE;
    }
    record_crc_offset =
        (u16)(SYS_PERSISTENT_HEADER_LENGTH + descriptor->payload_length);
    if (sys_persistent_get_u32_le(record + record_crc_offset) !=
        sys_persistent_crc32(record, record_crc_offset))
    {
        return BOOL_FALSE;
    }
    if (require_commit == BOOL_TRUE &&
        sys_persistent_get_u32_le(record + record_crc_offset + 4U) !=
            SYS_PERSISTENT_COMMIT_WORD)
    {
        return BOOL_FALSE;
    }
    if (meta != NULL)
    {
        meta->valid = BOOL_TRUE;
        meta->generation = generation;
        meta->payload_crc32 = payload_crc;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_record_validate(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *record,
    u16 record_length,
    sys_persistent_record_meta_st *meta)
{
    return sys_persistent_record_validate_body(descriptor,
                                               record,
                                               record_length,
                                               BOOL_TRUE,
                                               meta);
}

boolean_en sys_persistent_record_body_validate(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *record_body,
    u16 body_length,
    sys_persistent_record_meta_st *meta)
{
    return sys_persistent_record_validate_body(descriptor,
                                               record_body,
                                               body_length,
                                               BOOL_FALSE,
                                               meta);
}

static boolean_en sys_persistent_read_slot(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    u8 slot,
    u8 *workspace,
    sys_persistent_record_meta_st *meta)
{
    u32 page = (slot == 0U) ? descriptor->page_a : descriptor->page_b;

    memset(meta, 0, sizeof(*meta));
    meta->slot = slot;
    if (ops->read(page + descriptor->record_offset,
                  workspace,
                  descriptor->record_length,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate(descriptor,
                                       workspace,
                                       descriptor->record_length,
                                       meta) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        meta->valid = BOOL_FALSE;
        meta->slot = slot;
        return BOOL_FALSE;
    }
    meta->slot = slot;
    return BOOL_TRUE;
}

static boolean_en sys_persistent_select_active(
    const sys_persistent_record_meta_st *first,
    const sys_persistent_record_meta_st *second,
    sys_persistent_record_meta_st *selected)
{
    if (first->valid != BOOL_TRUE && second->valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (first->valid == BOOL_TRUE && second->valid != BOOL_TRUE)
    {
        *selected = *first;
    }
    else if (second->valid == BOOL_TRUE && first->valid != BOOL_TRUE)
    {
        *selected = *second;
    }
    else if (sys_persistent_generation_is_newer(second->generation,
                                                first->generation) == BOOL_TRUE)
    {
        *selected = *second;
    }
    else
    {
        *selected = *first;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_ab_load_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    u8 *payload,
    u16 payload_capacity,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length)
{
    sys_persistent_record_meta_st first;
    sys_persistent_record_meta_st second;
    sys_persistent_record_meta_st selected;
    u32 page;

    if (sys_persistent_descriptor_valid(descriptor) != BOOL_TRUE ||
        sys_persistent_ops_valid(ops) != BOOL_TRUE || payload == NULL ||
        payload_capacity < descriptor->payload_length || meta == NULL ||
        workspace == NULL || workspace_length < descriptor->record_length)
    {
        return BOOL_FALSE;
    }
    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   0U, workspace, &first);
    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   1U, workspace, &second);
    if (sys_persistent_select_active(&first, &second, &selected) != BOOL_TRUE)
    {
        memset(meta, 0, sizeof(*meta));
        return BOOL_FALSE;
    }
    page = (selected.slot == 0U) ? descriptor->page_a : descriptor->page_b;
    if (ops->read(page + descriptor->record_offset,
                  workspace,
                  descriptor->record_length,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate(descriptor,
                                       workspace,
                                       descriptor->record_length,
                                       meta) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        memset(meta, 0, sizeof(*meta));
        return BOOL_FALSE;
    }
    meta->slot = selected.slot;
    memmove(payload,
            workspace + SYS_PERSISTENT_HEADER_LENGTH,
            descriptor->payload_length);
    return BOOL_TRUE;
}

boolean_en sys_persistent_ab_commit_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    const u8 *payload,
    u16 payload_length,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length)
{
    sys_persistent_record_meta_st first;
    sys_persistent_record_meta_st second;
    sys_persistent_record_meta_st active;
    u32 next_generation = 1U;
    u8 inactive_slot = 0U;
    u32 inactive_page;
    u32 record_address;
    u16 commit_offset;

    if (sys_persistent_descriptor_valid(descriptor) != BOOL_TRUE ||
        sys_persistent_ops_valid(ops) != BOOL_TRUE || payload == NULL ||
        payload_length != descriptor->payload_length || meta == NULL ||
        workspace == NULL || workspace_length < descriptor->record_length)
    {
        return BOOL_FALSE;
    }
    if (payload_validator != NULL &&
        payload_validator(payload, payload_length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   0U, workspace, &first);
    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   1U, workspace, &second);
    if (sys_persistent_select_active(&first, &second, &active) == BOOL_TRUE)
    {
        next_generation = active.generation + 1U;
        if (next_generation == 0U)
        {
            next_generation = 1U;
        }
        inactive_slot = (active.slot == 0U) ? 1U : 0U;
    }
    inactive_page = (inactive_slot == 0U) ?
                    descriptor->page_a : descriptor->page_b;
    record_address = inactive_page + descriptor->record_offset;
    if (sys_persistent_record_build(descriptor,
                                    next_generation,
                                    payload,
                                    workspace,
                                    workspace_length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    commit_offset = (u16)(descriptor->record_length - 4U);

    if (ops->erase_page(inactive_page, ops->context) != BOOL_TRUE ||
        ops->program(record_address,
                     workspace,
                     commit_offset,
                     ops->context) != BOOL_TRUE ||
        ops->read(record_address,
                  workspace,
                  commit_offset,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate_body(descriptor,
                                            workspace,
                                            commit_offset,
                                            BOOL_FALSE,
                                            NULL) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    sys_persistent_put_u32_le(workspace, SYS_PERSISTENT_COMMIT_WORD);
    if (ops->program(record_address + commit_offset,
                     workspace,
                     4U,
                     ops->context) != BOOL_TRUE ||
        ops->read(record_address,
                  workspace,
                  descriptor->record_length,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate(descriptor,
                                       workspace,
                                       descriptor->record_length,
                                       meta) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    meta->slot = inactive_slot;
    return BOOL_TRUE;
}

boolean_en sys_persistent_ab_update_sections_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    const sys_persistent_section_update_st *updates,
    u8 update_count,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length)
{
    sys_persistent_record_meta_st first;
    sys_persistent_record_meta_st second;
    sys_persistent_record_meta_st active;
    u32 active_page;
    u32 inactive_page;
    u32 record_address;
    u32 next_generation;
    u8 inactive_slot;
    u16 commit_offset;
    u8 *payload;
    u8 update_index;
    boolean_en changed = BOOL_FALSE;

    if (sys_persistent_descriptor_valid(descriptor) != BOOL_TRUE ||
        sys_persistent_ops_valid(ops) != BOOL_TRUE || updates == NULL ||
        update_count == 0U ||
        meta == NULL || workspace == NULL ||
        workspace_length < descriptor->record_length)
    {
        return BOOL_FALSE;
    }
    for (update_index = 0U; update_index < update_count; ++update_index)
    {
        if (updates[update_index].data == NULL ||
            updates[update_index].length == 0U ||
            updates[update_index].offset > descriptor->payload_length ||
            updates[update_index].length >
                (u16)(descriptor->payload_length - updates[update_index].offset))
        {
            return BOOL_FALSE;
        }
    }

    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   0U, workspace, &first);
    (void)sys_persistent_read_slot(descriptor, ops, payload_validator,
                                   1U, workspace, &second);
    if (sys_persistent_select_active(&first, &second, &active) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    active_page = (active.slot == 0U) ? descriptor->page_a : descriptor->page_b;
    if (ops->read(active_page + descriptor->record_offset,
                  workspace,
                  descriptor->record_length,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate(descriptor,
                                       workspace,
                                       descriptor->record_length,
                                       meta) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    payload = workspace + SYS_PERSISTENT_HEADER_LENGTH;
    for (update_index = 0U; update_index < update_count; ++update_index)
    {
        if (memcmp(payload + updates[update_index].offset,
                   updates[update_index].data,
                   updates[update_index].length) != 0)
        {
            memcpy(payload + updates[update_index].offset,
                   updates[update_index].data,
                   updates[update_index].length);
            changed = BOOL_TRUE;
        }
    }
    if (changed != BOOL_TRUE)
    {
        meta->slot = active.slot;
        return BOOL_TRUE;
    }
    if (payload_validator != NULL &&
        payload_validator(payload, descriptor->payload_length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    next_generation = active.generation + 1U;
    if (next_generation == 0U)
    {
        next_generation = 1U;
    }
    inactive_slot = (active.slot == 0U) ? 1U : 0U;
    inactive_page = (inactive_slot == 0U) ?
                    descriptor->page_a : descriptor->page_b;
    record_address = inactive_page + descriptor->record_offset;
    if (sys_persistent_record_build(descriptor,
                                    next_generation,
                                    payload,
                                    workspace,
                                    workspace_length) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    commit_offset = (u16)(descriptor->record_length - 4U);
    if (ops->erase_page(inactive_page, ops->context) != BOOL_TRUE ||
        ops->program(record_address,
                     workspace,
                     commit_offset,
                     ops->context) != BOOL_TRUE ||
        ops->read(record_address,
                  workspace,
                  commit_offset,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate_body(descriptor,
                                            workspace,
                                            commit_offset,
                                            BOOL_FALSE,
                                            NULL) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    sys_persistent_put_u32_le(workspace, SYS_PERSISTENT_COMMIT_WORD);
    if (ops->program(record_address + commit_offset,
                     workspace,
                     4U,
                     ops->context) != BOOL_TRUE ||
        ops->read(record_address,
                  workspace,
                  descriptor->record_length,
                  ops->context) != BOOL_TRUE ||
        sys_persistent_record_validate(descriptor,
                                       workspace,
                                       descriptor->record_length,
                                       meta) != BOOL_TRUE ||
        (payload_validator != NULL &&
         payload_validator(workspace + SYS_PERSISTENT_HEADER_LENGTH,
                           descriptor->payload_length) != BOOL_TRUE))
    {
        return BOOL_FALSE;
    }
    meta->slot = inactive_slot;
    return BOOL_TRUE;
}

boolean_en sys_persistent_ab_update_section_with_ops(
    const sys_persistent_record_descriptor_st *descriptor,
    const sys_persistent_flash_ops_st *ops,
    sys_persistent_payload_validator_fn payload_validator,
    u16 section_offset,
    const u8 *section,
    u16 section_length,
    sys_persistent_record_meta_st *meta,
    u8 *workspace,
    u16 workspace_length)
{
    sys_persistent_section_update_st update;

    update.offset = section_offset;
    update.data = section;
    update.length = section_length;
    return sys_persistent_ab_update_sections_with_ops(descriptor,
                                                      ops,
                                                      payload_validator,
                                                      &update,
                                                      1U,
                                                      meta,
                                                      workspace,
                                                      workspace_length);
}
