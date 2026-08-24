#ifndef SYS_PERSISTENT_STORAGE_H
#define SYS_PERSISTENT_STORAGE_H

#include "type.h"
#include "sys_persistent_record.h"

#define SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH       1088U
#define SYS_PERSISTENT_CONFIG_RECORD_LENGTH        1116U
#define SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH      48U
#define SYS_PERSISTENT_RUNTIME_RECORD_LENGTH       76U
#define SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH  244U
#define SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH   272U

#define SYS_PERSISTENT_CONFIG_FACTORY_OFFSET       0x000U
#define SYS_PERSISTENT_CONFIG_FACTORY_LENGTH       128U
#define SYS_PERSISTENT_CONFIG_DEVICE_OFFSET        0x080U
#define SYS_PERSISTENT_CONFIG_DEVICE_LENGTH        4U
#define SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET      0x084U
#define SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH      304U
#define SYS_PERSISTENT_CONFIG_PLAN_OFFSET          0x1B4U
#define SYS_PERSISTENT_CONFIG_PLAN_LENGTH          648U
#define SYS_PERSISTENT_CONFIG_RESERVED_OFFSET      0x43CU
#define SYS_PERSISTENT_CONFIG_RESERVED_LENGTH      4U

typedef struct
{
    u32 total_run_time_sec;
    u32 total_light_time_sec;
    u32 total_energy_001wh;
    boolean_en calibration_inhibit;
    u32 ota_report_state;
    char ota_id[8];
    u32 ota_url_hash;
    u32 ota_image_checksum;
    u32 ota_image_size;
    u16 ota_device_type;
    u32 ota_retry_count;
} sys_persistent_runtime_data_st;

typedef struct
{
    u32 state;
    char ota_id[8];
    u32 url_hash;
    u32 image_checksum;
    u32 image_size;
    u16 device_type;
    u32 retry_count;
} sys_persistent_ota_report_st;

typedef boolean_en (*sys_persistent_default_section_writer_fn)(
    u8 *section,
    u16 length);

extern const sys_persistent_record_descriptor_st *
    sys_persistent_config_descriptor(void);
extern const sys_persistent_record_descriptor_st *
    sys_persistent_runtime_descriptor(void);
extern const sys_persistent_record_descriptor_st *
    sys_persistent_calibration_descriptor(void);

extern boolean_en sys_persistent_config_read_section(
    u16 offset,
    u8 *section,
    u16 length,
    u32 *generation);
extern boolean_en sys_persistent_config_update_section(
    u16 offset,
    const u8 *section,
    u16 length,
    u32 *generation);
extern boolean_en sys_persistent_config_update_sections(
    const sys_persistent_section_update_st *updates,
    u8 update_count,
    u32 *generation);
extern boolean_en sys_persistent_layout_initialize_if_needed(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    const u8 property_config[SYS_PERSISTENT_CONFIG_PROPERTY_LENGTH],
    const u8 plan_records[SYS_PERSISTENT_CONFIG_PLAN_LENGTH],
    boolean_en existing_ota_pending);
extern boolean_en sys_persistent_layout_initialize_with_defaults(
    const u8 factory_user_compat[SYS_PERSISTENT_CONFIG_FACTORY_LENGTH],
    u32 device_address,
    sys_persistent_default_section_writer_fn property_writer,
    sys_persistent_default_section_writer_fn plan_writer,
    boolean_en existing_ota_pending);

extern boolean_en sys_persistent_runtime_load(
    sys_persistent_runtime_data_st *runtime,
    u32 *generation);
extern boolean_en sys_persistent_runtime_pages_are_empty(void);
extern boolean_en sys_persistent_runtime_commit(
    const sys_persistent_runtime_data_st *runtime,
    u32 *generation);
extern boolean_en sys_persistent_runtime_get_ota_report(
    sys_persistent_ota_report_st *report);

extern boolean_en sys_persistent_calibration_load(
    u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u32 *generation,
    u32 *payload_crc32);
extern boolean_en sys_persistent_calibration_commit(
    const u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u32 *generation,
    u32 *payload_crc32);

extern boolean_en sys_persistent_ota_flag_is_set(void);
extern boolean_en sys_persistent_ota_flag_mark(void);
extern boolean_en sys_persistent_ota_flag_clear_preserving_config(void);

#endif
