#include "sys_persistent_storage.h"
#include "sys_calibration_storage.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_product_profile.h"
#include "zk_runtime_stats.h"
#include "sys_data.h"
#include "flash_address_assignment.h"
#include "hw_flash.h"
#include <stdio.h>
#include <string.h>

#define TEST_FLASH_BASE CAT1_FLASH_PERSISTENT_START
#define TEST_FLASH_SIZE (CAT1_FLASH_PERSISTENT_END - CAT1_FLASH_PERSISTENT_START)
#define TEST_RUNTIME_SAVE_INTERVAL_MS (8UL * 60UL * 60UL * 1000UL)

static u8 test_flash[TEST_FLASH_SIZE];
static u32 erase_calls;
static u32 program_calls;
static u32 fail_erase_call;
static u32 fail_program_call;
static u32 corrupt_after_program_call;
static u32 corrupt_program_offset;
static u32 last_program_address;
static u32 last_program_length;
sys_data_st sys_data __attribute__((aligned(4)));
u32 total_power_this_time;
u32 dim_level;
u8 power_down_flag;
static uint32 fake_tick;

uint32 Timer_GetTickCount(void)
{
    return fake_tick;
}

uint8 Timer_PassedDelay(uint32 start_time, uint32 delay)
{
    return ((fake_tick - start_time) >= delay) ? BOOL_TRUE : BOOL_FALSE;
}

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static u8 *test_address(u32 address, u32 length)
{
    if (address < TEST_FLASH_BASE ||
        address > CAT1_FLASH_PERSISTENT_END ||
        length > CAT1_FLASH_PERSISTENT_END - address)
    {
        return NULL;
    }
    return test_flash + (address - TEST_FLASH_BASE);
}

void hw_flash_read_bytes(u32 address, u8 *data, u32 length)
{
    u8 *source = test_address(address, length);
    if (source == NULL)
    {
        memset(data, 0, length);
        return;
    }
    memcpy(data, source, length);
}

boolean_en user_flash_erase(u32 address)
{
    u8 *page;
    ++erase_calls;
    if (fail_erase_call != 0U && erase_calls == fail_erase_call)
    {
        return BOOL_FALSE;
    }
    if ((address & (CAT1_FLASH_ERASE_PAGE_SIZE - 1U)) != 0U)
    {
        return BOOL_FALSE;
    }
    page = test_address(address, CAT1_FLASH_ERASE_PAGE_SIZE);
    if (page == NULL)
    {
        return BOOL_FALSE;
    }
    memset(page, 0xFF, CAT1_FLASH_ERASE_PAGE_SIZE);
    return BOOL_TRUE;
}

boolean_en hw_flash_erase_page_checked(u32 address)
{
    return user_flash_erase(address);
}

boolean_en hw_flash_program_bytes_no_erase_checked(u32 address,
                                                   const u8 *data,
                                                   u32 length)
{
    u8 *destination;
    u32 index;

    ++program_calls;
    last_program_address = address;
    last_program_length = length;
    if (fail_program_call != 0U && program_calls == fail_program_call)
    {
        return BOOL_FALSE;
    }
    if (data == NULL || length == 0U || (address & 3U) != 0U ||
        (length & 3U) != 0U)
    {
        return BOOL_FALSE;
    }
    destination = test_address(address, length);
    if (destination == NULL)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < length; ++index)
    {
        if ((destination[index] & data[index]) != data[index])
        {
            return BOOL_FALSE;
        }
        destination[index] &= data[index];
    }
    if (corrupt_after_program_call != 0U &&
        program_calls == corrupt_after_program_call &&
        corrupt_program_offset < length)
    {
        destination[corrupt_program_offset] ^= 0x01U;
    }
    return BOOL_TRUE;
}

/* Unused by the persistent production wrapper in this host test. */
boolean_en hw_flash_write_bytes_checked(u32 address,
                                        const u8 *data,
                                        u32 length)
{
    (void)address;
    (void)data;
    (void)length;
    return BOOL_FALSE;
}

static void test_reset(void)
{
    memset(test_flash, 0xA5, sizeof(test_flash));
    erase_calls = 0U;
    program_calls = 0U;
    fail_erase_call = 0U;
    fail_program_call = 0U;
    corrupt_after_program_call = 0U;
    corrupt_program_offset = 0U;
    last_program_address = 0U;
    last_program_length = 0U;
}

static boolean_en fill_valid_calibration_payload(
    u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH],
    u32 voltage_gain_q24)
{
    static const u16 output_ref[SYS_CALIBRATION_PAYLOAD_POINT_COUNT] =
        {0U, 89U, 179U, 268U, 357U, 447U,
         536U, 625U, 714U, 804U, 893U};
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_payload_st decoded;
    u32 index;

    memset(&decoded, 0, sizeof(decoded));
    decoded.profile_id = profile->profile_id;
    decoded.profile_version = profile->profile_version;
    decoded.profile_fingerprint = profile->fingerprint_crc32;
    decoded.point_count = SYS_CALIBRATION_PAYLOAD_POINT_COUNT;
    decoded.level_step = SYS_CALIBRATION_PAYLOAD_LEVEL_STEP;
    decoded.valid_flags = SYS_CALIBRATION_PAYLOAD_VALID_FLAGS;
    decoded.voltage_gain_q24 = voltage_gain_q24;
    for (index = 0U; index < SYS_CALIBRATION_PAYLOAD_POINT_COUNT; ++index)
    {
        decoded.output[index].logical_pwm = (u16)(index * 100U);
        decoded.output[index].reference_output_current_ma = output_ref[index];
        decoded.oco[index].oco_adc_raw = (u16)(1000U + index * 100U);
        decoded.oco[index].reference_output_current_ma = output_ref[index];
        decoded.bl_current[index].bl_current_raw =
            100000UL + index * 10000UL;
        decoded.bl_current[index].reference_input_current_ma =
            (u16)(index * 20U);
        decoded.bl_power[index].bl_power_raw =
            (s32)(200000L + (s32)index * 20000L);
        decoded.bl_power[index].reference_input_power_01w =
            (u16)(index * 50U);
    }
    return sys_calibration_payload_encode(
        &decoded, payload, SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH);
}

static void fill_defaults(u8 factory[128], u8 property[304], u8 plans[648])
{
    u32 index;
    for (index = 0U; index < 128U; ++index)
    {
        factory[index] = (u8)(0x40U + index);
    }
    for (index = 0U; index < 304U; ++index)
    {
        property[index] = (u8)(index ^ 0x5AU);
    }
    property[0x034U + 31U] = 0U;
    memset(plans, 0, 648U);
}

static int test_crc_and_record_codecs(void)
{
    int failures = 0;
    u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
    u8 record[SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH];
    sys_persistent_record_meta_st meta;
    sys_calibration_storage_v3_record_st cal4;
    u32 cal4_generation;
    u32 cal4_crc;
    const sys_persistent_record_descriptor_st *descriptor =
        sys_persistent_calibration_descriptor();
    const sys_persistent_record_descriptor_st *config_descriptor =
        sys_persistent_config_descriptor();
    const sys_persistent_record_descriptor_st *runtime_descriptor =
        sys_persistent_runtime_descriptor();

    memset(payload, 0x3C, sizeof(payload));
    failures += expect_true(
        sys_persistent_crc32((const u8 *)"123456789", 9U) == 0xCBF43926UL,
        "CRC-32/ISO-HDLC golden vector");
    failures += expect_true(
        config_descriptor->record_offset == 4U &&
        config_descriptor->payload_length == 1088U &&
        config_descriptor->record_length == 1116U &&
        runtime_descriptor->record_offset == 0U &&
        runtime_descriptor->payload_length == 48U &&
        runtime_descriptor->record_length == 76U &&
        descriptor->payload_length == 244U &&
        descriptor->record_length == 272U,
        "CFG1/RUN1/CAL4 frozen lengths and offsets");
    failures += expect_true(
        sys_persistent_record_build(descriptor,
                                    7U,
                                    payload,
                                    record,
                                    sizeof(record)) == BOOL_TRUE,
        "CAL4 record builds");
    failures += expect_true(memcmp(record, "CAL4", 4U) == 0,
                            "CAL4 magic bytes");
    failures += expect_true(sys_persistent_get_u16_le(record + 0x04U) == 4U &&
                            sys_persistent_get_u16_le(record + 0x06U) == 272U &&
                            sys_persistent_get_u32_le(record + 0x08U) == 7U &&
                            sys_persistent_get_u16_le(record + 0x0CU) == 244U,
                            "CAL4 explicit LE header");
    failures += expect_true(
        sys_persistent_get_u32_le(record + 0x10CU) ==
            SYS_PERSISTENT_COMMIT_WORD,
        "CAL4 commit at 0x10C");
    failures += expect_true(
        sys_persistent_record_validate(descriptor,
                                       record,
                                       sizeof(record),
                                       &meta) == BOOL_TRUE &&
        meta.generation == 7U,
        "CAL4 validates");
    record[0x40U] ^= 1U;
    failures += expect_true(
        sys_persistent_record_validate(descriptor,
                                       record,
                                       sizeof(record),
                                       &meta) == BOOL_FALSE,
        "payload corruption is rejected");

    failures += expect_true(
        fill_valid_calibration_payload(payload, 0x01000000UL) == BOOL_TRUE,
        "valid current-product CALP fixture builds");
    failures += expect_true(
        sys_calibration_storage_v3_record_build(&cal4, 7U, payload) ==
            BOOL_TRUE &&
        sys_calibration_storage_v3_record_validate(&cal4,
                                                   &cal4_generation,
                                                   &cal4_crc) == BOOL_TRUE &&
        cal4_generation == 7U &&
        cal4_crc == sys_persistent_crc32(payload, sizeof(payload)),
        "CALP header and CAL4 wrapper validate");
    payload[0x12U] = 0x3FU;
    failures += expect_true(
        sys_calibration_storage_v3_record_build(&cal4, 8U, payload) ==
            BOOL_FALSE,
        "CALP v1 rejects non-frozen validFlags");
    return failures;
}

static int test_format_and_config_ab(void)
{
    int failures = 0;
    u8 factory[128];
    u8 property[304];
    u8 plans[648];
    u8 readback[304];
    u32 generation = 0U;
    u32 erase_before;
    sys_persistent_section_update_st updates[2];

    test_reset();
    fill_defaults(factory, property, plans);
    corrupt_after_program_call = 1U;
    corrupt_program_offset = 0x20U;
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory,
                                                   0x12345678UL,
                                                   property,
                                                   plans,
                                                   BOOL_FALSE) == BOOL_FALSE &&
        program_calls == 1U &&
        sys_persistent_get_u32_le(test_address(
            CAT1_FLASH_CONFIG_A_RECORD_START + 0x458U, 4U)) ==
            0xFFFFFFFFUL,
        "first CFG1 body corruption blocks CommitWord");

    test_reset();
    fill_defaults(factory, property, plans);
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory,
                                                   0x12345678UL,
                                                   property,
                                                   plans,
                                                   BOOL_FALSE) == BOOL_TRUE,
        "invalid legacy layout formats to V3");
    failures += expect_true(erase_calls == 6U,
                            "initialization erases exactly six pages");
    failures += expect_true(
        sys_persistent_get_u32_le(test_address(
            CAT1_FLASH_BOOT_OTA_FLAG_ADDRESS, 4U)) == 0xFFFFFFFFUL,
        "format leaves OTA flag erased");
    failures += expect_true(
        memcmp(test_address(CAT1_FLASH_CONFIG_A_RECORD_START, 4U),
               "CFG1", 4U) == 0,
        "CFG1 starts at Config A PageBase+4");
    failures += expect_true(
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
            readback,
            sizeof(readback),
            &generation) == BOOL_TRUE &&
        generation == 1U && memcmp(readback, property, sizeof(property)) == 0,
        "CFG1 property section round-trips");

    erase_before = erase_calls;
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory,
                                                   0x12345678UL,
                                                   property,
                                                   plans,
                                                   BOOL_FALSE) == BOOL_TRUE &&
        erase_calls == erase_before,
        "valid V3 layout is not reformatted");

    property[0] ^= 0xFFU;
    factory[0] ^= 0x5AU;
    updates[0].offset = SYS_PERSISTENT_CONFIG_FACTORY_OFFSET;
    updates[0].data = factory;
    updates[0].length = sizeof(factory);
    updates[1].offset = SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET;
    updates[1].data = property;
    updates[1].length = sizeof(property);
    failures += expect_true(
        sys_persistent_config_update_sections(updates,
                                              2U,
                                              &generation) == BOOL_TRUE &&
        generation == 2U && erase_calls == erase_before + 1U,
        "Factory+Property update is one inactive-page transaction");
    failures += expect_true(
        memcmp(test_address(CAT1_FLASH_CONFIG_B_RECORD_START, 4U),
               "CFG1", 4U) == 0,
        "second Config generation lands in Config B+4");
    failures += expect_true(
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_FACTORY_OFFSET,
            readback,
            sizeof(factory),
            NULL) == BOOL_TRUE &&
        memcmp(readback, factory, sizeof(factory)) == 0 &&
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
            readback,
            sizeof(property),
            NULL) == BOOL_TRUE &&
        memcmp(readback, property, sizeof(property)) == 0,
        "multi-section Config transaction round-trips both sections");
    return failures;
}

static int test_commit_fault_and_generation_wrap(void)
{
    int failures = 0;
    u8 payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
    u8 loaded[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
    u8 record[SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH];
    u32 generation;
    u32 crc;
    u32 calls_before;
    const sys_persistent_record_descriptor_st *descriptor =
        sys_persistent_calibration_descriptor();

    memset(test_flash, 0xFF, sizeof(test_flash));
    erase_calls = 0U;
    program_calls = 0U;
    fail_erase_call = 0U;
    fail_program_call = 0U;
    failures += expect_true(
        fill_valid_calibration_payload(payload, 0x01000000UL) == BOOL_TRUE,
        "valid CALP builds for A/B commit tests");
    failures += expect_true(
        sys_persistent_calibration_commit(payload, &generation, &crc) == BOOL_TRUE &&
        generation == 1U,
        "first CAL4 commit uses generation 1");
    failures += expect_true(last_program_address ==
                                CAT1_FLASH_CALIBRATION_A_PAGE_START + 0x10CU &&
                            last_program_length == 4U,
                            "CommitWord is the final program operation");

    calls_before = program_calls;
    fail_program_call = calls_before + 2U;
    (void)fill_valid_calibration_payload(payload, 0x01000001UL);
    failures += expect_true(
        sys_persistent_calibration_commit(payload, &generation, &crc) == BOOL_FALSE,
        "power loss/failure at CommitWord rejects new page");
    failures += expect_true(
        sys_persistent_calibration_load(loaded, &generation, &crc) == BOOL_TRUE &&
        generation == 1U &&
        sys_persistent_get_u32_le(
            loaded + SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET) ==
            0x01000000UL,
        "old committed Calibration survives failed inactive-page commit");

    memset(test_flash, 0xFF, sizeof(test_flash));
    (void)fill_valid_calibration_payload(payload, 0x01000000UL);
    failures += expect_true(
        sys_persistent_record_build(descriptor,
                                    0xFFFFFFFFUL,
                                    payload,
                                    record,
                                    sizeof(record)) == BOOL_TRUE,
        "max generation record builds");
    memcpy(test_address(CAT1_FLASH_CALIBRATION_A_PAGE_START,
                        sizeof(record)),
           record,
           sizeof(record));
    fail_program_call = 0U;
    program_calls = 0U;
    (void)fill_valid_calibration_payload(payload, 0x01000002UL);
    failures += expect_true(
        sys_persistent_calibration_commit(payload, &generation, &crc) == BOOL_TRUE &&
        generation == 1U,
        "generation wrap skips zero");
    return failures;
}

static int test_ota_flag_and_clear(void)
{
    int failures = 0;
    u8 factory[128];
    u8 property[304];
    u8 plans[648];
    u32 generation;
    u32 erase_before;

    test_reset();
    fill_defaults(factory, property, plans);
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory, 0x80U,
                                                   property, plans,
                                                   BOOL_TRUE) == BOOL_FALSE &&
        erase_calls == 0U,
        "existing OTA pending blocks legacy format");

    memset(test_flash, 0xFF, sizeof(test_flash));
    erase_calls = 0U;
    program_calls = 0U;
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory, 0x80U,
                                                   property, plans,
                                                   BOOL_FALSE) == BOOL_TRUE,
        "fresh CFG1 initialized before OTA flag test");
    failures += expect_true(sys_persistent_ota_flag_mark() == BOOL_TRUE &&
                            sys_persistent_ota_flag_is_set() == BOOL_TRUE,
                            "OTA flag programs reserved Config A prefix");
    erase_before = erase_calls;
    property[1] ^= 1U;
    failures += expect_true(
        sys_persistent_config_update_section(
            SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
            property,
            sizeof(property),
            &generation) == BOOL_FALSE &&
        erase_calls == erase_before,
        "flag-written window cannot erase/commit Config");

    corrupt_after_program_call = program_calls + 1U;
    corrupt_program_offset = 0x24U;
    failures += expect_true(
        sys_persistent_ota_flag_clear_preserving_config() == BOOL_FALSE &&
        sys_persistent_ota_flag_is_set() == BOOL_TRUE &&
        sys_persistent_get_u32_le(test_address(
            CAT1_FLASH_CONFIG_B_RECORD_START + 0x458U, 4U)) ==
            0xFFFFFFFFUL &&
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            property,
            SYS_PERSISTENT_CONFIG_DEVICE_LENGTH,
            &generation) == BOOL_TRUE && generation == 1U,
        "corrupt OTA A-to-B body never commits and preserves old Config A");
    corrupt_after_program_call = 0U;
    failures += expect_true(
        sys_persistent_ota_flag_clear_preserving_config() == BOOL_TRUE &&
        sys_persistent_ota_flag_is_set() == BOOL_FALSE &&
        memcmp(test_address(CAT1_FLASH_CONFIG_B_RECORD_START, 4U),
               "CFG1", 4U) == 0,
        "clear flag copies sole Config A to B before erasing A");
    return failures;
}

static int test_runtime_fields_are_preserved(void)
{
    int failures = 0;
    sys_persistent_runtime_data_st runtime;
    sys_persistent_runtime_data_st loaded;
    sys_persistent_ota_report_st report;
    sys_persistent_ota_report_st report_loaded;
    u32 generation;

    memset(test_flash, 0xFF, sizeof(test_flash));
    erase_calls = 0U;
    program_calls = 0U;
    fail_erase_call = 0U;
    fail_program_call = 0U;
    memset(&runtime, 0, sizeof(runtime));
    runtime.total_run_time_sec = 100U;
    runtime.total_light_time_sec = 50U;
    runtime.total_energy_001wh = 25U;
    failures += expect_true(
        sys_persistent_runtime_commit(&runtime, &generation) == BOOL_TRUE &&
        generation == 1U,
        "RUN1 first commit");
    failures += expect_true(
        memcmp(test_address(CAT1_FLASH_RUNTIME_A_PAGE_START, 4U),
               "RUN1", 4U) == 0 &&
        sys_persistent_get_u32_le(test_address(
            CAT1_FLASH_RUNTIME_A_PAGE_START + 0x48U, 4U)) ==
            SYS_PERSISTENT_COMMIT_WORD,
        "RUN1 record and commit offsets match G7");
    failures += expect_true(
        sys_persistent_runtime_load(&loaded, NULL) == BOOL_TRUE,
        "RUN1 loads before inhibit update");
    loaded.calibration_inhibit = BOOL_TRUE;
    failures += expect_true(
        sys_persistent_runtime_commit(&loaded, NULL) == BOOL_TRUE &&
        sys_persistent_runtime_load(&loaded, &generation) == BOOL_TRUE &&
        loaded.calibration_inhibit == BOOL_TRUE &&
        loaded.total_run_time_sec == 100U,
        "inhibit update preserves runtime counters");

    memset(&report, 0, sizeof(report));
    report.state = 2U;
    memcpy(report.ota_id, "OTA0001", 7U);
    report.url_hash = 0x11223344UL;
    report.image_checksum = 0x55667788UL;
    report.image_size = 97536U;
    report.device_type = 3U;
    report.retry_count = 4U;
    loaded.ota_report_state = report.state;
    memcpy(loaded.ota_id, report.ota_id, sizeof(loaded.ota_id));
    loaded.ota_url_hash = report.url_hash;
    loaded.ota_image_checksum = report.image_checksum;
    loaded.ota_image_size = report.image_size;
    loaded.ota_device_type = report.device_type;
    loaded.ota_retry_count = report.retry_count;
    failures += expect_true(
        sys_persistent_runtime_commit(&loaded, NULL) == BOOL_TRUE &&
        sys_persistent_runtime_get_ota_report(&report_loaded) == BOOL_TRUE &&
        report_loaded.state == report.state &&
        report_loaded.url_hash == report.url_hash &&
        report_loaded.image_checksum == report.image_checksum &&
        report_loaded.image_size == report.image_size &&
        report_loaded.device_type == report.device_type &&
        report_loaded.retry_count == report.retry_count,
        "RUN1 OTA durable API round-trips");
    failures += expect_true(
        sys_persistent_runtime_load(&loaded, NULL) == BOOL_TRUE &&
        loaded.calibration_inhibit == BOOL_TRUE &&
        loaded.total_run_time_sec == 100U,
        "OTA update preserves inhibit and counters");
    return failures;
}

static int test_semantic_fallback_selection(void)
{
    int failures = 0;
    u8 factory[128];
    u8 property[304];
    u8 plans[648];
    u8 config_payload[SYS_PERSISTENT_CONFIG_PAYLOAD_LENGTH];
    u8 config_record[SYS_PERSISTENT_CONFIG_RECORD_LENGTH];
    u8 runtime_payload[SYS_PERSISTENT_RUNTIME_PAYLOAD_LENGTH];
    u8 runtime_record[SYS_PERSISTENT_RUNTIME_RECORD_LENGTH];
    u8 calibration_payload[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
    u8 calibration_loaded[SYS_PERSISTENT_CALIBRATION_PAYLOAD_LENGTH];
    u8 calibration_record[SYS_PERSISTENT_CALIBRATION_RECORD_LENGTH];
    sys_persistent_runtime_data_st runtime;
    u32 generation = 0U;

    memset(test_flash, 0xFF, sizeof(test_flash));
    erase_calls = 0U;
    program_calls = 0U;
    fill_defaults(factory, property, plans);
    failures += expect_true(
        sys_persistent_layout_initialize_if_needed(factory, 0x80U,
                                                   property, plans,
                                                   BOOL_FALSE) == BOOL_TRUE,
        "semantic fallback test initializes CFG1 A");
    memcpy(config_payload,
           test_address(CAT1_FLASH_CONFIG_A_RECORD_START +
                            SYS_PERSISTENT_HEADER_LENGTH,
                        sizeof(config_payload)),
           sizeof(config_payload));
    config_payload[SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET + 0x034U + 31U] =
        0x41U;
    failures += expect_true(
        sys_persistent_record_build(sys_persistent_config_descriptor(),
                                    2U,
                                    config_payload,
                                    config_record,
                                    sizeof(config_record)) == BOOL_TRUE,
        "structurally valid but semantically invalid CFG1 builds");
    memcpy(test_address(CAT1_FLASH_CONFIG_B_RECORD_START,
                        sizeof(config_record)),
           config_record,
           sizeof(config_record));
    failures += expect_true(
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            config_payload,
            SYS_PERSISTENT_CONFIG_DEVICE_LENGTH,
            &generation) == BOOL_TRUE && generation == 1U,
        "CFG1 Boot selection falls back from invalid generation 2 to valid 1");
    memcpy(config_payload,
           test_address(CAT1_FLASH_CONFIG_A_RECORD_START +
                            SYS_PERSISTENT_HEADER_LENGTH,
                        sizeof(config_payload)),
           sizeof(config_payload));
    config_payload[SYS_PERSISTENT_CONFIG_RESERVED_OFFSET] = 1U;
    (void)sys_persistent_record_build(sys_persistent_config_descriptor(),
                                      2U,
                                      config_payload,
                                      config_record,
                                      sizeof(config_record));
    memcpy(test_address(CAT1_FLASH_CONFIG_B_RECORD_START,
                        sizeof(config_record)),
           config_record,
           sizeof(config_record));
    failures += expect_true(
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            config_payload,
            SYS_PERSISTENT_CONFIG_DEVICE_LENGTH,
            &generation) == BOOL_TRUE && generation == 1U,
        "CFG1 Boot selection rejects nonzero payload reserved bytes");
    memcpy(config_payload,
           test_address(CAT1_FLASH_CONFIG_A_RECORD_START +
                            SYS_PERSISTENT_HEADER_LENGTH,
                        sizeof(config_payload)),
           sizeof(config_payload));
    config_payload[SYS_PERSISTENT_CONFIG_PLAN_OFFSET + 0x20U] = 1U;
    (void)sys_persistent_record_build(sys_persistent_config_descriptor(),
                                      2U,
                                      config_payload,
                                      config_record,
                                      sizeof(config_record));
    memcpy(test_address(CAT1_FLASH_CONFIG_B_RECORD_START,
                        sizeof(config_record)),
           config_record,
           sizeof(config_record));
    failures += expect_true(
        sys_persistent_config_read_section(
            SYS_PERSISTENT_CONFIG_DEVICE_OFFSET,
            config_payload,
            SYS_PERSISTENT_CONFIG_DEVICE_LENGTH,
            &generation) == BOOL_TRUE && generation == 1U,
        "CFG1 Boot selection rejects invalid Plan action reserved codec");
    property[0] ^= 1U;
    failures += expect_true(
        sys_persistent_config_update_section(
            SYS_PERSISTENT_CONFIG_PROPERTY_OFFSET,
            property,
            sizeof(property),
            &generation) == BOOL_TRUE && generation == 2U,
        "CFG1 commit erases semantic-invalid inactive page, not old valid page");

    memset(&runtime, 0, sizeof(runtime));
    runtime.total_run_time_sec = 10U;
    failures += expect_true(
        sys_persistent_runtime_commit(&runtime, &generation) == BOOL_TRUE &&
        generation == 1U,
        "semantic fallback test initializes RUN1 A");
    memcpy(runtime_payload,
           test_address(CAT1_FLASH_RUNTIME_A_PAGE_START +
                            SYS_PERSISTENT_HEADER_LENGTH,
                        sizeof(runtime_payload)),
           sizeof(runtime_payload));
    runtime_payload[0x0CU] = 2U;
    failures += expect_true(
        sys_persistent_record_build(sys_persistent_runtime_descriptor(),
                                    2U,
                                    runtime_payload,
                                    runtime_record,
                                    sizeof(runtime_record)) == BOOL_TRUE,
        "structurally valid but semantically invalid RUN1 builds");
    memcpy(test_address(CAT1_FLASH_RUNTIME_B_PAGE_START,
                        sizeof(runtime_record)),
           runtime_record,
           sizeof(runtime_record));
    failures += expect_true(
        sys_persistent_runtime_load(&runtime, &generation) == BOOL_TRUE &&
        generation == 1U && runtime.total_run_time_sec == 10U,
        "RUN1 Boot selection rejects invalid inhibit and falls back");
    memcpy(runtime_payload,
           test_address(CAT1_FLASH_RUNTIME_A_PAGE_START +
                            SYS_PERSISTENT_HEADER_LENGTH,
                        sizeof(runtime_payload)),
           sizeof(runtime_payload));
    runtime_payload[0x0DU] = 1U;
    (void)sys_persistent_record_build(sys_persistent_runtime_descriptor(),
                                      2U,
                                      runtime_payload,
                                      runtime_record,
                                      sizeof(runtime_record));
    memcpy(test_address(CAT1_FLASH_RUNTIME_B_PAGE_START,
                        sizeof(runtime_record)),
           runtime_record,
           sizeof(runtime_record));
    failures += expect_true(
        sys_persistent_runtime_load(&runtime, &generation) == BOOL_TRUE &&
        generation == 1U && runtime.total_run_time_sec == 10U,
        "RUN1 Boot selection rejects nonzero reserved bytes and falls back");

    failures += expect_true(
        fill_valid_calibration_payload(
            calibration_payload, 0x01000000UL) == BOOL_TRUE &&
        sys_persistent_calibration_commit(
            calibration_payload, &generation, NULL) == BOOL_TRUE &&
            generation == 1U,
        "semantic fallback test initializes CAL4 A");
    sys_persistent_put_u16_le(calibration_payload + 0x06U, 243U);
    (void)sys_persistent_record_build(sys_persistent_calibration_descriptor(),
                                      2U,
                                      calibration_payload,
                                      calibration_record,
                                      sizeof(calibration_record));
    memcpy(test_address(CAT1_FLASH_CALIBRATION_B_PAGE_START,
                        sizeof(calibration_record)),
           calibration_record,
           sizeof(calibration_record));
    failures += expect_true(
        sys_persistent_calibration_load(calibration_loaded,
                                        &generation, NULL) == BOOL_TRUE &&
        generation == 1U,
        "CAL4 Boot selection rejects invalid CALP header length and falls back");
    (void)fill_valid_calibration_payload(calibration_payload, 0x01000000UL);
    sys_persistent_put_u16_le(
        calibration_payload + 0x08U,
        (u16)(sys_product_profile_current()->profile_id + 1U));
    (void)sys_persistent_record_build(sys_persistent_calibration_descriptor(),
                                      2U,
                                      calibration_payload,
                                      calibration_record,
                                      sizeof(calibration_record));
    memcpy(test_address(CAT1_FLASH_CALIBRATION_B_PAGE_START,
                        sizeof(calibration_record)),
           calibration_record,
           sizeof(calibration_record));
    failures += expect_true(
        sys_persistent_calibration_load(calibration_loaded,
                                        &generation, NULL) == BOOL_TRUE &&
        generation == 1U,
        "CAL4 Boot selection rejects wrong Product profile and falls back");
    (void)fill_valid_calibration_payload(calibration_payload, 0x01000000UL);
    sys_persistent_put_u32_le(
        calibration_payload + 0x0CU,
        0x11223344UL);
    failures += expect_true(
        sys_persistent_record_build(sys_persistent_calibration_descriptor(),
                                    2U,
                                    calibration_payload,
                                    calibration_record,
                                    sizeof(calibration_record)) == BOOL_TRUE,
        "structurally valid but wrong-product CAL4 builds");
    memcpy(test_address(CAT1_FLASH_CALIBRATION_B_PAGE_START,
                        sizeof(calibration_record)),
           calibration_record,
           sizeof(calibration_record));
    failures += expect_true(
        sys_persistent_calibration_load(calibration_loaded,
                                        &generation, NULL) == BOOL_TRUE &&
        generation == 1U &&
        sys_persistent_get_u32_le(
            calibration_loaded +
                0x0CU) ==
            sys_product_profile_current()->fingerprint_crc32,
        "CAL4 Boot selection rejects wrong Product/Fingerprint and falls back");
    (void)fill_valid_calibration_payload(calibration_payload, 0x01000000UL);
    sys_persistent_put_u32_le(
        calibration_payload + SYS_CALIBRATION_PAYLOAD_BL_VOLTAGE_OFFSET,
        0U);
    (void)sys_persistent_record_build(sys_persistent_calibration_descriptor(),
                                      2U,
                                      calibration_payload,
                                      calibration_record,
                                      sizeof(calibration_record));
    memcpy(test_address(CAT1_FLASH_CALIBRATION_B_PAGE_START,
                        sizeof(calibration_record)),
           calibration_record,
           sizeof(calibration_record));
    failures += expect_true(
        sys_persistent_calibration_load(calibration_loaded,
                                        &generation, NULL) == BOOL_TRUE &&
        generation == 1U,
        "CAL4 Boot selection rejects semantically invalid full CALP payload");
    return failures;
}

static int test_runtime_owner_overlays_current_counters(void)
{
    int failures = 0;
    sys_persistent_runtime_data_st loaded;
    sys_persistent_ota_report_st report;
    u32 calls_before;
    u32 checkpoint_tick;

    memset(test_flash, 0xFF, sizeof(test_flash));
    memset(&sys_data, 0, sizeof(sys_data));
    erase_calls = 0U;
    program_calls = 0U;
    fail_erase_call = 0U;
    fail_program_call = 0U;
    total_power_this_time = 0U;
    dim_level = 100U;
    power_down_flag = 0U;
    fake_tick = 1000U;
    zk_runtime_stats_init();
    fake_tick = 3601000U;
    zk_runtime_counter_process();
    sys_data.ac_EnergyP = 100U;
    total_power_this_time = 20U;

    memset(&report, 0, sizeof(report));
    report.state = 1U;
    memcpy(report.ota_id, "OTA0002", 7U);
    report.url_hash = 0x01020304UL;
    failures += expect_true(
        zk_runtime_stats_set_ota_report(&report) == BOOL_TRUE &&
        sys_persistent_runtime_load(&loaded, NULL) == BOOL_TRUE &&
        loaded.total_run_time_sec == 3600U &&
        loaded.total_light_time_sec == 3600U &&
        loaded.total_energy_001wh == 120U &&
        loaded.ota_report_state == 1U,
        "OTA durable update overlays current RAM counters in one RUN1 commit");

    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        zk_runtime_stats_set_calibration_inhibit(BOOL_TRUE) == BOOL_TRUE &&
        sys_persistent_runtime_load(&loaded, NULL) == BOOL_TRUE &&
        loaded.total_run_time_sec == 3601U &&
        loaded.total_light_time_sec == 3601U &&
        loaded.calibration_inhibit == BOOL_TRUE &&
        loaded.ota_report_state == 1U &&
        loaded.ota_url_hash == report.url_hash,
        "inhibit update preserves OTA fields and current counters");
    failures += expect_true(
        zk_runtime_get_boot_run_seconds() == 3601U &&
        zk_runtime_get_boot_light_seconds() == 3601U &&
        zk_runtime_get_boot_energy_001wh() == 120U,
        "current run/light/energy remain RAM-only boot counters");

    calls_before = program_calls;
    fake_tick = 1000U + TEST_RUNTIME_SAVE_INTERVAL_MS - 1U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before,
        "normal Runtime does not write before the exact 8-hour boundary");

    checkpoint_tick = 1000U + TEST_RUNTIME_SAVE_INTERVAL_MS;
    fake_tick = checkpoint_tick;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before + 2U,
        "exact 8-hour boundary commits one RUN1 body and CommitWord");
    calls_before = program_calls;
    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before,
        "successful 8-hour checkpoint is not repeated on the next process call");

    fail_program_call = program_calls + 1U;
    fake_tick = checkpoint_tick + TEST_RUNTIME_SAVE_INTERVAL_MS;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == fail_program_call,
        "failed 8-hour body program is observed without being marked saved");
    fail_program_call = 0U;
    calls_before = program_calls;
    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before + 2U,
        "failed 8-hour checkpoint retries on the next existing process call");
    calls_before = program_calls;
    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before,
        "successful retry restores the normal no-repeat interval");

    fail_program_call = program_calls + 1U;
    fake_tick += 1000U;
    failures += expect_true(
        zk_runtime_stats_powerdown_checkpoint() == BOOL_FALSE &&
        program_calls == fail_program_call,
        "failed explicit power-down checkpoint remains retryable");
    fail_program_call = 0U;
    power_down_flag = 1U;
    calls_before = program_calls;
    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before + 2U,
        "power-down process retries and commits after the prior failure");
    calls_before = program_calls;
    fake_tick += 1000U;
    zk_runtime_counter_process();
    failures += expect_true(
        program_calls == calls_before,
        "successful power-down checkpoint is marked once and not repeated");
    power_down_flag = 0U;
    zk_runtime_counter_process();
    return failures;
}

int main(void)
{
    int failures = 0;
    failures += test_crc_and_record_codecs();
    failures += test_format_and_config_ab();
    failures += test_commit_fault_and_generation_wrap();
    failures += test_ota_flag_and_clear();
    failures += test_runtime_fields_are_preserved();
    failures += test_semantic_fallback_selection();
    failures += test_runtime_owner_overlays_current_counters();
    if (failures != 0)
    {
        fprintf(stderr, "persistent V3 failures: %d\n", failures);
        return 1;
    }
    puts("persistent V3 tests: PASS");
    return 0;
}
