#include "sys_persistent_storage.h"
#include "sys_calibration_storage.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include <string.h>

#define SYS_PERSISTENT_SLOT_A 0U
#define SYS_PERSISTENT_SLOT_B 1U

static const sys_persistent_record_descriptor_st _config_descriptor =
{
    {'C', 'F', 'G', '1'},
    1U,
    SYS_PERSISTENT_CONFIG_RECORD_LENGTH,
    SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH,
    (u16)CAT1_FLASH_CONFIG_RESERVED_PREFIX_SIZE,
    CAT1_FLASH_CONFIG_A_PAGE_START,
    CAT1_FLASH_CONFIG_B_PAGE_START,
    (u16)CAT1_FLASH_ERASE_PAGE_SIZE
};

static const sys_persistent_record_descriptor_st _runtime_descriptor =
{
    {'R', 'U', 'N', '1'},
    1U,
    SYS_PERSISTENT_RUNTIME_RECORD_LENGTH,
    SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH,
    0U,
    CAT1_FLASH_RUNTIME_A_PAGE_START,
    CAT1_FLASH_RUNTIME_B_PAGE_START,
    (u16)CAT1_FLASH_ERASE_PAGE_SIZE
};

static const sys_persistent_record_descriptor_st _calibration_descriptor =
{
    {'C', 'A', 'L', '4'},
    4U,
    SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH,
    SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH,
    0U,
    CAT1_FLASH_CALIBRATION_A_PAGE_START,
    CAT1_FLASH_CALIBRATION_B_PAGE_START,
    (u16)CAT1_FLASH_ERASE_PAGE_SIZE
};

/* One deterministic workspace is shared by serialized boot/main-loop writes. */
static u8 _record_workspace[SYS_PERSISTENT_MAX_RECORD_LENGTH]
    __attribute__((aligned(4)));

typedef char sys_persistent_config_size_contract[
    (SYS_PERSISTENT_CONFIG_RECORD_LENGTH ==
     (SYS_PERSISTENT_HEADER_LENGTH + SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH + 8U)) ? 1 : -1];
typedef char sys_persistent_runtime_size_contract[
    (SYS_PERSISTENT_RUNTIME_RECORD_LENGTH ==
     (SYS_PERSISTENT_HEADER_LENGTH + SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH + 8U)) ? 1 : -1];
typedef char sys_persistent_calibration_size_contract[
    (SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH ==
     (SYS_PERSISTENT_HEADER_LENGTH + SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH + 8U)) ? 1 : -1];

static boolean_en sys_persistent_flash_read(u32 address,
                                            u8 *data,
                                            u32 length,
                                            void *context)
{
    (void)context;
    hw_flash_read_bytes(address, data, length);
    return BOOL_TRUE;
}

static boolean_en sys_persistent_flash_erase(u32 page_address, void *context)
{
    (void)context;
    return hw_flash_erase_page_checked(page_address);
}

static boolean_en sys_persistent_flash_program(u32 address,
                                               const u8 *data,
                                               u32 length,
                                               void *context)
{
    (void)context;
    return hw_flash_program_bytes_no_erase_checked(address, data, length);
}

static const sys_persistent_flash_ops_st _flash_ops =
{
    sys_persistent_flash_read,
    sys_persistent_flash_erase,
    sys_persistent_flash_program,
    NULL
};

static boolean_en sys_persistent_config_payload_validate(
    const u8 *payload,
    u16 length);
static boolean_en sys_persistent_runtime_payload_validate(
    const u8 *payload,
    u16 length);
static boolean_en sys_persistent_calibration_payload_validate(
    const u8 *payload,
    u16 length);

static sys_persistent_payload_validator_fn sys_persistent_payload_validator(
    const sys_persistent_record_descriptor_st *descriptor)
{
    if (descriptor == &_config_descriptor)
    {
        return sys_persistent_config_payload_validate;
    }
    if (descriptor == &_runtime_descriptor)
    {
        return sys_persistent_runtime_payload_validate;
    }
    if (descriptor == &_calibration_descriptor)
    {
        return sys_persistent_calibration_payload_validate;
    }
    return NULL;
}

const sys_persistent_record_descriptor_st *sys_persistent_config_descriptor(void)
{
    return &_config_descriptor;
}

const sys_persistent_record_descriptor_st *sys_persistent_runtime_descriptor(void)
{
    return &_runtime_descriptor;
}

const sys_persistent_record_descriptor_st *sys_persistent_calibration_descriptor(void)
{
    return &_calibration_descriptor;
}

static boolean_en sys_persistent_load_payload(
    const sys_persistent_record_descriptor_st *descriptor,
    u8 *payload,
    u16 payload_capacity,
    sys_persistent_record_meta_st *meta)
{
    return sys_persistent_ab_load_with_ops(descriptor,
                                           &_flash_ops,
                                           sys_persistent_payload_validator(descriptor),
                                           payload,
                                           payload_capacity,
                                           meta,
                                           _record_workspace,
                                           sizeof(_record_workspace));
}

static boolean_en sys_persistent_config_reserved_valid(const u8 *payload)
{
    return (payload != NULL &&
            payload[SYS_PERSISTENT_CONFIG_RESERVED_OFFSET + 0U] == 0U &&
            payload[SYS_PERSISTENT_CONFIG_RESERVED_OFFSET + 1U] == 0U &&
            payload[SYS_PERSISTENT_CONFIG_RESERVED_OFFSET + 2U] == 0U &&
            payload[SYS_PERSISTENT_CONFIG_RESERVED_OFFSET + 3U] == 0U) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_persistent_config_payload_validate(
    const u8 *payload,
    u16 length)
{
    u8 plan_index;
    u8 job_index;
    u8 action_index;
    const u8 *plan;
    u16 job_offset;
    u16 action_offset;

    if (payload == NULL || length != SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH ||
        sys_persistent_config_reserved_valid(payload) != BOOL_TRUE ||
        payload[SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET + 0x034U + 31U] != 0U)
    {
        return BOOL_FALSE;
    }
    for (plan_index = 0U; plan_index < 8U; ++plan_index)
    {
        plan = payload + SYS_PERSISTENT_CONFIG_PLAN_OFFSET +
               (u16)plan_index * 81U;
        if (plan[0x00U] > 1U || plan[0x01U] > 1U ||
            plan[0x10U] != 0U || plan[0x18U] != 0U ||
            (plan[0x00U] == 1U &&
             (plan[0x02U] == 0U || plan[0x02U] > 8U ||
              plan[0x06U] > 2U)))
        {
            return BOOL_FALSE;
        }
        for (job_index = 0U; job_index < 2U; ++job_index)
        {
            job_offset = (u16)(0x19U + (u16)job_index * 28U);
            if (plan[job_offset + 2U] > 6U ||
                plan[job_offset + 3U] != 0U)
            {
                return BOOL_FALSE;
            }
            for (action_index = 0U; action_index < 6U; ++action_index)
            {
                action_offset = (u16)(job_offset + 4U +
                                      (u16)action_index * 4U);
                if (plan[action_offset + 3U] != 0U)
                {
                    return BOOL_FALSE;
                }
            }
        }
    }
    return BOOL_TRUE;
}

static boolean_en sys_persistent_config_update_range_valid(u16 offset,
                                                           u16 length)
{
    u16 end;

    if (length == 0U || offset > SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH ||
        length > (u16)(SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH - offset))
    {
        return BOOL_FALSE;
    }
    end = (u16)(offset + length);
    if ((offset >= SYS_PERSISTENT_CONFIG_FACTORY_OFFSET &&
         end <= (SYS_PERSISTENT_CONFIG_DEVICE_OFFSET +
                 SYS_PERSISTENT_CONFIG_DEVICE_LENGTH)) ||
        (offset >= SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET &&
         end <= (SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET +
                 SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH)) ||
        (offset >= SYS_PERSISTENT_CONFIG_PLAN_OFFSET &&
         end <= (SYS_PERSISTENT_CONFIG_PLAN_OFFSET +
                 SYS_PERSISTENT_CONFIG_PLAN_LENGTH)))
    {
        return BOOL_TRUE;
    }
    return BOOL_FALSE;
}

static boolean_en sys_persistent_commit_payload(
    const sys_persistent_record_descriptor_st *descriptor,
    const u8 *payload,
    u16 payload_length,
    sys_persistent_record_meta_st *meta)
{
    return sys_persistent_ab_commit_with_ops(descriptor,
                                             &_flash_ops,
                                             sys_persistent_payload_validator(descriptor),
                                             payload,
                                             payload_length,
                                             meta,
                                             _record_workspace,
                                             sizeof(_record_workspace));
}

boolean_en sys_persistent_config_read_section(u16 offset,
                                              u8 *section,
                                              u16 length,
                                              u32 *generation)
{
    sys_persistent_record_meta_st meta;

    if (section == NULL || length == 0U ||
        offset > SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH ||
        length > (u16)(SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH - offset) ||
        sys_persistent_load_payload(&_config_descriptor,
                                    _record_workspace,
                                    SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH,
                                    &meta) != BOOL_TRUE ||
        sys_persistent_config_reserved_valid(_record_workspace) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memcpy(section, _record_workspace + offset, length);
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_ota_flag_is_set(void)
{
    u8 flag[4];
    hw_flash_read_bytes(CAT1_FLASH_BOOT_OTA_FLAG_ADDRESS, flag, sizeof(flag));
    return (sys_persistent_get_u32_le(flag) == CAT1_FLASH_BOOT_OTA_FLAG_VALUE) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_persistent_config_update_section(u16 offset,
                                                const u8 *section,
                                                u16 length,
                                                u32 *generation)
{
    sys_persistent_record_meta_st meta;

    /* Physical flag protection complements, but does not replace, OTA's gate. */
    if (sys_persistent_config_update_range_valid(offset, length) != BOOL_TRUE ||
        sys_persistent_ota_flag_is_set() == BOOL_TRUE ||
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_RESERVED_OFFSET,
            _record_workspace,
            SYS_PERSISTENT_CONFIG_RESERVED_LENGTH,
            NULL) != BOOL_TRUE ||
        sys_persistent_ab_update_section_with_ops(
            &_config_descriptor,
            &_flash_ops,
            sys_persistent_config_payload_validate,
            offset,
            section,
            length,
            &meta,
            _record_workspace,
            sizeof(_record_workspace)) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_config_update_sections(
    const sys_persistent_section_update_st *updates,
    u8 update_count,
    u32 *generation)
{
    sys_persistent_record_meta_st meta;
    u8 index;

    if (updates == NULL || update_count == 0U ||
        sys_persistent_ota_flag_is_set() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < update_count; ++index)
    {
        if (sys_persistent_config_update_range_valid(
                updates[index].offset,
                updates[index].length) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
    }
    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_RESERVED_OFFSET,
            _record_workspace,
            SYS_PERSISTENT_CONFIG_RESERVED_LENGTH,
            NULL) != BOOL_TRUE ||
        sys_persistent_ab_update_sections_with_ops(
            &_config_descriptor,
            &_flash_ops,
            sys_persistent_config_payload_validate,
            updates,
            update_count,
            &meta,
            _record_workspace,
            sizeof(_record_workspace)) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    return BOOL_TRUE;
}

static boolean_en sys_persistent_page_is_blank(u32 page_address)
{
    u8 sample[32];
    u32 offset;
    u32 index;

    for (offset = 0U; offset < CAT1_FLASH_ERASE_PAGE_SIZE;
         offset += sizeof(sample))
    {
        hw_flash_read_bytes(page_address + offset, sample, sizeof(sample));
        for (index = 0U; index < sizeof(sample); ++index)
        {
            if (sample[index] != 0xFFU)
            {
                return BOOL_FALSE;
            }
        }
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_runtime_pages_are_empty(void)
{
    return (sys_persistent_page_is_blank(CAT1_FLASH_RUNTIME_A_PAGE_START) ==
                BOOL_TRUE &&
            sys_persistent_page_is_blank(CAT1_FLASH_RUNTIME_B_PAGE_START) ==
                BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_persistent_format_six_pages(void)
{
    static const u32 pages[6] =
    {
        CAT1_FLASH_CONFIG_A_PAGE_START,
        CAT1_FLASH_CONFIG_B_PAGE_START,
        CAT1_FLASH_CALIBRATION_A_PAGE_START,
        CAT1_FLASH_CALIBRATION_B_PAGE_START,
        CAT1_FLASH_RUNTIME_A_PAGE_START,
        CAT1_FLASH_RUNTIME_B_PAGE_START
    };
    u8 index;

    for (index = 0U; index < 6U; ++index)
    {
        if (hw_flash_erase_page_checked(pages[index]) != BOOL_TRUE ||
            sys_persistent_page_is_blank(pages[index]) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

static boolean_en sys_persistent_finalize_first_config(void)
{
    u8 *payload = _record_workspace + SYS_PERSISTENT_HEADER_LENGTH;
    u16 commit_offset = (u16)(SYS_PERSISTENT_CONFIG_RECORD_LENGTH - 4U);
    sys_persistent_record_meta_st meta;

    memset(payload + SYS_PERSISTENT_CONFIG_RESERVED_OFFSET,
           0,
           SYS_PERSISTENT_CONFIG_RESERVED_LENGTH);
    if (sys_persistent_record_build(&_config_descriptor,
                                    1U,
                                    payload,
                                    _record_workspace,
                                    sizeof(_record_workspace)) != BOOL_TRUE ||
        _flash_ops.program(CAT1_FLASH_CONFIG_A_RECORD_START,
                           _record_workspace,
                           commit_offset,
                           _flash_ops.context) != BOOL_TRUE ||
        _flash_ops.read(CAT1_FLASH_CONFIG_A_RECORD_START,
                        _record_workspace,
                        commit_offset,
                        _flash_ops.context) != BOOL_TRUE ||
        sys_persistent_record_body_validate(&_config_descriptor,
                                            _record_workspace,
                                            commit_offset,
                                            &meta) != BOOL_TRUE ||
        sys_persistent_config_payload_validate(
            _record_workspace + SYS_PERSISTENT_HEADER_LENGTH,
            SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    sys_persistent_put_u32_le(_record_workspace, SYS_PERSISTENT_COMMIT_WORD);
    if (_flash_ops.program(CAT1_FLASH_CONFIG_A_RECORD_START + commit_offset,
                           _record_workspace,
                           4U,
                           _flash_ops.context) != BOOL_TRUE ||
        _flash_ops.read(CAT1_FLASH_CONFIG_A_RECORD_START,
                        _record_workspace,
                        SYS_PERSISTENT_CONFIG_RECORD_LENGTH,
                        _flash_ops.context) != BOOL_TRUE ||
        sys_persistent_record_validate(&_config_descriptor,
                                       _record_workspace,
                                       SYS_PERSISTENT_CONFIG_RECORD_LENGTH,
                                       &meta) != BOOL_TRUE ||
        sys_persistent_config_payload_validate(
            _record_workspace + SYS_PERSISTENT_HEADER_LENGTH,
            SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return (sys_persistent_ota_flag_is_set() == BOOL_FALSE) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_persistent_write_first_config(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    const u8 property_config[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH],
    const u8 plan_records[SYS_PERSISTENT_CONFIG_PLAN_LENGTH])
{
    u8 *payload = _record_workspace + SYS_PERSISTENT_HEADER_LENGTH;

    memcpy(payload + SYS_PERSISTENT_CONFIG_FACTORY_OFFSET,
           factory_user_compat,
           SYS_PERSISTENT_CONFIG_FACTORY_LENGTH);
    sys_persistent_put_u32_le(payload + SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
                              device_address);
    memcpy(payload + SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
           property_config,
           SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH);
    memcpy(payload + SYS_PERSISTENT_CONFIG_PLAN_OFFSET,
           plan_records,
           SYS_PERSISTENT_CONFIG_PLAN_LENGTH);
    return sys_persistent_finalize_first_config();
}

boolean_en sys_persistent_layout_initialize_if_needed(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    const u8 property_config[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH],
    const u8 plan_records[SYS_PERSISTENT_CONFIG_PLAN_LENGTH],
    boolean_en existing_ota_pending)
{
    sys_persistent_record_meta_st meta;

    if (factory_user_compat == NULL || property_config == NULL ||
        plan_records == NULL)
    {
        return BOOL_FALSE;
    }
    if (sys_persistent_load_payload(&_config_descriptor,
                                    _record_workspace,
                                    SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH,
                                    &meta) == BOOL_TRUE &&
        sys_persistent_config_reserved_valid(_record_workspace) == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (existing_ota_pending == BOOL_TRUE ||
        sys_persistent_ota_flag_is_set() == BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (sys_persistent_format_six_pages() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return sys_persistent_write_first_config(factory_user_compat,
                                             device_address,
                                             property_config,
                                             plan_records);
}

boolean_en sys_persistent_layout_initialize_with_defaults(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    sys_persistent_default_section_writer_fn property_writer,
    sys_persistent_default_section_writer_fn plan_writer,
    boolean_en existing_ota_pending)
{
    sys_persistent_record_meta_st meta;
    u8 *payload = _record_workspace + SYS_PERSISTENT_HEADER_LENGTH;

    if (factory_user_compat == NULL || property_writer == NULL ||
        plan_writer == NULL)
    {
        return BOOL_FALSE;
    }
    if (sys_persistent_load_payload(&_config_descriptor,
                                    _record_workspace,
                                    SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH,
                                    &meta) == BOOL_TRUE &&
        sys_persistent_config_reserved_valid(_record_workspace) == BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    if (existing_ota_pending == BOOL_TRUE ||
        sys_persistent_ota_flag_is_set() == BOOL_TRUE ||
        sys_persistent_format_six_pages() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memcpy(payload + SYS_PERSISTENT_CONFIG_FACTORY_OFFSET,
           factory_user_compat,
           SYS_PERSISTENT_CONFIG_FACTORY_LENGTH);
    sys_persistent_put_u32_le(payload + SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
                              device_address);
    if (property_writer(payload + SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
                        SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH) != BOOL_TRUE ||
        plan_writer(payload + SYS_PERSISTENT_CONFIG_PLAN_OFFSET,
                    SYS_PERSISTENT_CONFIG_PLAN_LENGTH) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return sys_persistent_finalize_first_config();
}

static void sys_persistent_runtime_encode(
    const sys_persistent_runtime_data_st *runtime,
    u8 payload[SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH])
{
    memset(payload, 0, SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH);
    sys_persistent_put_u32_le(payload + 0x00U, runtime->total_run_time_sec);
    sys_persistent_put_u32_le(payload + 0x04U, runtime->total_light_time_sec);
    sys_persistent_put_u32_le(payload + 0x08U, runtime->total_energy_001wh);
    payload[0x0CU] = (runtime->calibration_inhibit == BOOL_TRUE) ? 1U : 0U;
    sys_persistent_put_u32_le(payload + 0x10U, runtime->ota_report_state);
    memcpy(payload + 0x14U, runtime->ota_id, 8U);
    sys_persistent_put_u32_le(payload + 0x1CU, runtime->ota_url_hash);
    sys_persistent_put_u32_le(payload + 0x20U, runtime->ota_image_checksum);
    sys_persistent_put_u32_le(payload + 0x24U, runtime->ota_image_size);
    sys_persistent_put_u16_le(payload + 0x28U, runtime->ota_device_type);
    sys_persistent_put_u16_le(payload + 0x2AU, 0U);
    sys_persistent_put_u32_le(payload + 0x2CU, runtime->ota_retry_count);
}

static boolean_en sys_persistent_runtime_decode(
    const u8 payload[SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH],
    sys_persistent_runtime_data_st *runtime)
{
    if (payload == NULL || runtime == NULL || payload[0x0CU] > 1U ||
        payload[0x0DU] != 0U || payload[0x0EU] != 0U ||
        payload[0x0FU] != 0U ||
        sys_persistent_get_u16_le(payload + 0x2AU) != 0U)
    {
        return BOOL_FALSE;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->total_run_time_sec = sys_persistent_get_u32_le(payload + 0x00U);
    runtime->total_light_time_sec = sys_persistent_get_u32_le(payload + 0x04U);
    runtime->total_energy_001wh = sys_persistent_get_u32_le(payload + 0x08U);
    runtime->calibration_inhibit =
        (payload[0x0CU] != 0U) ? BOOL_TRUE : BOOL_FALSE;
    runtime->ota_report_state = sys_persistent_get_u32_le(payload + 0x10U);
    memcpy(runtime->ota_id, payload + 0x14U, 8U);
    runtime->ota_url_hash = sys_persistent_get_u32_le(payload + 0x1CU);
    runtime->ota_image_checksum = sys_persistent_get_u32_le(payload + 0x20U);
    runtime->ota_image_size = sys_persistent_get_u32_le(payload + 0x24U);
    runtime->ota_device_type = sys_persistent_get_u16_le(payload + 0x28U);
    runtime->ota_retry_count = sys_persistent_get_u32_le(payload + 0x2CU);
    return BOOL_TRUE;
}

static boolean_en sys_persistent_runtime_payload_validate(
    const u8 *payload,
    u16 length)
{
    sys_persistent_runtime_data_st runtime;

    return (length == SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH &&
            sys_persistent_runtime_decode(payload, &runtime) == BOOL_TRUE) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_persistent_calibration_payload_validate(
    const u8 *payload,
    u16 length)
{
    return (length == SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH &&
            sys_calibration_storage_v3_payload_validate(payload) == BOOL_TRUE) ?
           BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_persistent_runtime_load(sys_persistent_runtime_data_st *runtime,
                                       u32 *generation)
{
    u8 payload[SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH];
    sys_persistent_record_meta_st meta;

    if (runtime == NULL ||
        sys_persistent_load_payload(&_runtime_descriptor,
                                    payload,
                                    sizeof(payload),
                                    &meta) != BOOL_TRUE ||
        sys_persistent_runtime_decode(payload, runtime) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_runtime_commit(
    const sys_persistent_runtime_data_st *runtime,
    u32 *generation)
{
    u8 payload[SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH];
    sys_persistent_record_meta_st meta;

    if (runtime == NULL)
    {
        return BOOL_FALSE;
    }
    sys_persistent_runtime_encode(runtime, payload);
    if (sys_persistent_commit_payload(&_runtime_descriptor,
                                      payload,
                                      sizeof(payload),
                                      &meta) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (generation != NULL)
    {
        *generation = meta.generation;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_runtime_get_ota_report(
    sys_persistent_ota_report_st *report)
{
    sys_persistent_runtime_data_st runtime;

    if (report == NULL ||
        sys_persistent_runtime_load(&runtime, NULL) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    memset(report, 0, sizeof(*report));
    report->state = runtime.ota_report_state;
    memcpy(report->ota_id, runtime.ota_id, sizeof(report->ota_id));
    report->url_hash = runtime.ota_url_hash;
    report->image_checksum = runtime.ota_image_checksum;
    report->image_size = runtime.ota_image_size;
    report->device_type = runtime.ota_device_type;
    report->retry_count = runtime.ota_retry_count;
    return BOOL_TRUE;
}

boolean_en sys_persistent_calibration_load(
    u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u32 *generation,
    u32 *payload_crc32)
{
    sys_persistent_record_meta_st meta;

    if (payload == NULL ||
        sys_persistent_load_payload(&_calibration_descriptor,
                                    payload,
                                    SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH,
                                    &meta) != BOOL_TRUE)
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

boolean_en sys_persistent_calibration_commit(
    const u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u32 *generation,
    u32 *payload_crc32)
{
    sys_persistent_record_meta_st meta;

    if (payload == NULL ||
        sys_persistent_commit_payload(&_calibration_descriptor,
                                      payload,
                                      SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH,
                                      &meta) != BOOL_TRUE)
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

boolean_en sys_persistent_ota_flag_mark(void)
{
    u8 flag[4];
    u8 config_probe[SYS_PERSISTENT_CONFIG_DEVICE_LENGTH];
    u32 current;

    if (sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            config_probe,
            sizeof(config_probe),
            NULL) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(CAT1_FLASH_BOOT_OTA_FLAG_ADDRESS, flag, sizeof(flag));
    current = sys_persistent_get_u32_le(flag);
    if (current == CAT1_FLASH_BOOT_OTA_FLAG_VALUE)
    {
        return BOOL_TRUE;
    }
    if (current != 0xFFFFFFFFUL)
    {
        return BOOL_FALSE;
    }
    sys_persistent_put_u32_le(flag, CAT1_FLASH_BOOT_OTA_FLAG_VALUE);
    return hw_flash_program_bytes_no_erase_checked(
        CAT1_FLASH_BOOT_OTA_FLAG_ADDRESS,
        flag,
        sizeof(flag));
}

static boolean_en sys_persistent_read_config_slot(
    u8 slot,
    sys_persistent_record_meta_st *meta)
{
    u32 address = (slot == SYS_PERSISTENT_SLOT_A) ?
                  CAT1_FLASH_CONFIG_A_RECORD_START :
                  CAT1_FLASH_CONFIG_B_RECORD_START;

    hw_flash_read_bytes(address,
                        _record_workspace,
                        SYS_PERSISTENT_CONFIG_RECORD_LENGTH);
    if (sys_persistent_record_validate(&_config_descriptor,
                                       _record_workspace,
                                       SYS_PERSISTENT_CONFIG_RECORD_LENGTH,
                                       meta) != BOOL_TRUE ||
        sys_persistent_config_payload_validate(
            _record_workspace + SYS_PERSISTENT_HEADER_LENGTH,
            SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH) != BOOL_TRUE)
    {
        memset(meta, 0, sizeof(*meta));
        meta->slot = slot;
        return BOOL_FALSE;
    }
    meta->slot = slot;
    return BOOL_TRUE;
}

static boolean_en sys_persistent_copy_config_a_to_b(void)
{
    sys_persistent_record_meta_st check;
    u16 commit_offset = (u16)(SYS_PERSISTENT_CONFIG_RECORD_LENGTH - 4U);

    if (sys_persistent_read_config_slot(SYS_PERSISTENT_SLOT_A, &check) != BOOL_TRUE ||
        hw_flash_erase_page_checked(CAT1_FLASH_CONFIG_B_PAGE_START) != BOOL_TRUE ||
        hw_flash_program_bytes_no_erase_checked(
            CAT1_FLASH_CONFIG_B_RECORD_START,
            _record_workspace,
            commit_offset) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    hw_flash_read_bytes(CAT1_FLASH_CONFIG_B_RECORD_START,
                        _record_workspace,
                        commit_offset);
    if (sys_persistent_record_body_validate(&_config_descriptor,
                                            _record_workspace,
                                            commit_offset,
                                            &check) != BOOL_TRUE ||
        sys_persistent_config_payload_validate(
            _record_workspace + SYS_PERSISTENT_HEADER_LENGTH,
            SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    sys_persistent_put_u32_le(_record_workspace, SYS_PERSISTENT_COMMIT_WORD);
    if (hw_flash_program_bytes_no_erase_checked(
            CAT1_FLASH_CONFIG_B_RECORD_START + commit_offset,
            _record_workspace,
            4U) != BOOL_TRUE ||
        sys_persistent_read_config_slot(SYS_PERSISTENT_SLOT_B, &check) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

boolean_en sys_persistent_ota_flag_clear_preserving_config(void)
{
    sys_persistent_record_meta_st a;
    sys_persistent_record_meta_st b;
    boolean_en a_valid;
    boolean_en b_valid;
    boolean_en b_has_latest;

    if (sys_persistent_ota_flag_is_set() != BOOL_TRUE)
    {
        return BOOL_TRUE;
    }
    a_valid = sys_persistent_read_config_slot(SYS_PERSISTENT_SLOT_A, &a);
    b_valid = sys_persistent_read_config_slot(SYS_PERSISTENT_SLOT_B, &b);
    if (a_valid != BOOL_TRUE && b_valid != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    b_has_latest =
        (b_valid == BOOL_TRUE &&
         (a_valid != BOOL_TRUE ||
          sys_persistent_generation_is_newer(a.generation,
                                              b.generation) != BOOL_TRUE)) ?
        BOOL_TRUE : BOOL_FALSE;
    if (b_has_latest != BOOL_TRUE &&
        sys_persistent_copy_config_a_to_b() != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (hw_flash_erase_page_checked(CAT1_FLASH_CONFIG_A_PAGE_START) != BOOL_TRUE ||
        sys_persistent_ota_flag_is_set() == BOOL_TRUE ||
        sys_persistent_read_config_slot(SYS_PERSISTENT_SLOT_B, &b) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}
