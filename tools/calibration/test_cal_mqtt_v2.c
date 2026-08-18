#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "sys_calibration_mqtt.h"
#include "sys_calibration_service.h"
#include "sys_calibration_storage.h"
#include "sys_product_profile.h"
#include "factory_user_data.h"
#include "sys_Vo_Io.h"
#include "sys_bl0942.h"

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp = 890U;
u16 HWMAX_OUTCUR_temp = 1680U;
u16 OUTPUT_CUR_SENSOR_temp = 120U;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp = 560U;

u8 Error_0_linght;
u8 Error_1_OL;
u8 Error_Out_LV;
u8 Error_3_OV;
u8 Error_4_LV;
u32 Vo_value;
u32 Io_value;
u32 Po_value;
u16 ac_voltage_8209;
u16 Z_ac_current;
u16 ac_powerpa;
u32 bl0942_checksum_error_count;
u32 bl0942_timeout_count;
u32 bl0942_uart_error_count;
u32 bl0942_compat_frame_count;

static cJSON *captured_response;
static u32 test_tick = 1000U;
static u16 last_level;
static u32 set_level_calls;
static u8 committed_payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];

int dma_printf(const char *format, ...)
{
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

u32 HAL_GetTick(void)
{
    return test_tick++;
}

cJSON *zk_cjson_create_tx_object(const char *context)
{
    (void)context;
    return cJSON_CreateObject();
}

cJSON *zk_create_root_from_header(const zk_message_header_t *header,
                                  int with_er, int er_code)
{
    cJSON *root;
    if (header == NULL)
    {
        return NULL;
    }
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "SN", header->sn);
    cJSON_AddStringToObject(root, "TM", "2026-08-17 10:00:00");
    cJSON_AddStringToObject(root, "SV", header->sv);
    cJSON_AddStringToObject(root, "ID", header->id);
    cJSON_AddStringToObject(root, "CT", header->ct);
    if (with_er != 0)
    {
        cJSON_AddNumberToObject(root, "ER", er_code);
    }
    return root;
}

int zk_send_json_root(cJSON *root, const char *topic)
{
    (void)topic;
    cJSON_Delete(captured_response);
    captured_response = cJSON_Duplicate(root, 1);
    return captured_response == NULL ? -1 : 0;
}

sys_product_current_validation_en factory_user_validate_runtime_current(
    u16 bound_voltage_01v, u32 configured_current_ma)
{
    u16 calibrated_max_current_ma;
    boolean_en calibrated_max_available;
    sys_product_current_validation_en result =
        sys_product_profile_validate_runtime_current(
            sys_product_profile_current(), bound_voltage_01v,
            configured_current_ma);
    if (result != SYS_PRODUCT_CURRENT_VALID)
    {
        return result;
    }
    calibrated_max_available =
        sys_calibration_service_get_calibrated_max_current_ma(
            bound_voltage_01v, &calibrated_max_current_ma);
    return sys_product_profile_validate_calibrated_current(
        configured_current_ma, calibrated_max_available,
        calibrated_max_current_ma);
}

static void safe_off(void)
{
    last_level = 0U;
}

static boolean_en set_level(u16 level)
{
    ++set_level_calls;
    last_level = level;
    return BOOL_TRUE;
}

static boolean_en set_inhibit(boolean_en active)
{
    (void)active;
    return BOOL_TRUE;
}

static boolean_en commit_table(const sys_calibration_context_st *context,
                               const u8 *payload, u16 length,
                               u32 *generation)
{
    if (context == NULL || payload == NULL || generation == NULL ||
        length != sizeof(committed_payload) ||
        context->table_crc32 != sys_calibration_storage_crc32(payload, length))
    {
        return BOOL_FALSE;
    }
    memcpy(committed_payload, payload, length);
    *generation = 3U;
    return BOOL_TRUE;
}

static u16 bound_voltage(void)
{
    return 560U;
}

static void setup_service(void)
{
    factory_user_buff[5] = 1U;
    sys_calibration_service_init();
    sys_calibration_service_bind_safe_off(safe_off);
    sys_calibration_service_bind_platform(set_level, set_inhibit, commit_table);
    sys_calibration_service_bind_bound_voltage(bound_voltage);
    sys_calibration_service_restore_boot(BOOL_FALSE, BOOL_TRUE);
    sys_calibration_service_set_safety_ready(BOOL_TRUE);
}

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *text;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
    {
        if (file != NULL) fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1U);
    if (text == NULL || fread(text, 1U, (size_t)length, file) != (size_t)length)
    {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static int json_subset_equal(const cJSON *expected, const cJSON *actual)
{
    const cJSON *item;
    const cJSON *candidate;
    int index;
    if (expected == NULL || actual == NULL)
    {
        return 0;
    }
    if (cJSON_IsObject(expected))
    {
        if (!cJSON_IsObject(actual)) return 0;
        cJSON_ArrayForEach(item, expected)
        {
            candidate = cJSON_GetObjectItemCaseSensitive(actual, item->string);
            if (!json_subset_equal(item, candidate)) return 0;
        }
        return 1;
    }
    if (cJSON_IsArray(expected))
    {
        if (!cJSON_IsArray(actual) ||
            cJSON_GetArraySize(expected) != cJSON_GetArraySize(actual)) return 0;
        for (index = 0; index < cJSON_GetArraySize(expected); ++index)
        {
            if (!json_subset_equal(cJSON_GetArrayItem(expected, index),
                                   cJSON_GetArrayItem(actual, index))) return 0;
        }
        return 1;
    }
    if (cJSON_IsString(expected))
        return cJSON_IsString(actual) && expected->valuestring != NULL &&
               actual->valuestring != NULL &&
               strcmp(expected->valuestring, actual->valuestring) == 0;
    if (cJSON_IsNumber(expected))
        return cJSON_IsNumber(actual) && expected->valuedouble == actual->valuedouble;
    if (cJSON_IsBool(expected))
        return cJSON_IsBool(actual) && cJSON_IsTrue(expected) == cJSON_IsTrue(actual);
    if (cJSON_IsNull(expected)) return cJSON_IsNull(actual);
    return 0;
}

static void copy_header_string(cJSON *root, const char *key,
                               char *destination, size_t capacity)
{
    cJSON *node = cJSON_GetObjectItemCaseSensitive(root, key);
    if (node == NULL || !cJSON_IsString(node) || node->valuestring == NULL ||
        capacity == 0U)
    {
        return;
    }
    strncpy(destination, node->valuestring, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static int run_exchange(cJSON *request, cJSON *expected_response,
                        const char *name)
{
    zk_message_header_t header;
    char *actual_text;
    memset(&header, 0, sizeof(header));
    copy_header_string(request, "SN", header.sn, sizeof(header.sn));
    copy_header_string(request, "TM", header.tm, sizeof(header.tm));
    copy_header_string(request, "SV", header.sv, sizeof(header.sv));
    copy_header_string(request, "ID", header.id, sizeof(header.id));
    copy_header_string(request, "CT", header.ct, sizeof(header.ct));
    cJSON_Delete(captured_response);
    captured_response = NULL;
    if (sys_calibration_mqtt_handle(request, &header) != BOOL_TRUE ||
        captured_response == NULL)
    {
        fprintf(stderr, "FAIL: %s produced no response\n", name);
        return 1;
    }
    actual_text = cJSON_PrintUnformatted(captured_response);
    if (actual_text == NULL || strlen(actual_text) + 1U > ZK_JSON_BUF_SIZE ||
        !json_subset_equal(expected_response, captured_response))
    {
        fprintf(stderr, "FAIL: %s response mismatch/overflow: %s\n", name,
                actual_text == NULL ? "<null>" : actual_text);
        free(actual_text);
        return 1;
    }
    free(actual_text);
    return 0;
}

static int run_fixture(const char *directory, const char *filename)
{
    char path[512];
    char *text;
    cJSON *fixture;
    cJSON *request;
    cJSON *expected;
    cJSON *steps;
    cJSON *step;
    int failures = 0;
    int index = 0;

    snprintf(path, sizeof(path), "%s/%s", directory, filename);
    text = read_file(path);
    fixture = text == NULL ? NULL : cJSON_Parse(text);
    free(text);
    if (fixture == NULL)
    {
        fprintf(stderr, "FAIL: parse fixture %s\n", filename);
        return 1;
    }
    request = cJSON_GetObjectItemCaseSensitive(fixture, "request");
    expected = cJSON_GetObjectItemCaseSensitive(fixture, "expectedResponse");
    if (request != NULL && expected != NULL)
    {
        failures += run_exchange(request, expected, filename);
    }
    else
    {
        steps = cJSON_GetObjectItemCaseSensitive(fixture, "steps");
        if (steps == NULL || !cJSON_IsArray(steps))
        {
            failures += expect_true(0, "fixture has request or steps");
        }
        cJSON_ArrayForEach(step, steps)
        {
            char step_name[128];
            snprintf(step_name, sizeof(step_name), "%s[%d]", filename, index++);
            failures += run_exchange(
                cJSON_GetObjectItemCaseSensitive(step, "request"),
                cJSON_GetObjectItemCaseSensitive(step, "expectedResponse"),
                step_name);
        }
    }
    cJSON_Delete(fixture);
    return failures;
}

static int validate_fixture_crc(const char *directory)
{
    char path[512];
    char *text;
    cJSON *fixture;
    cJSON *request;
    cJSON *dt;
    cJSON *context;
    cJSON *payload;
    sys_calibration_context_st parsed;
    u8 bytes[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
    size_t index;
    int failures = 0;
    unsigned value;

    snprintf(path, sizeof(path), "%s/%s", directory,
             "STAGE_50W_56V_198B.json");
    text = read_file(path);
    fixture = text == NULL ? NULL : cJSON_Parse(text);
    free(text);
    request = fixture == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(fixture, "request");
    dt = request == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(request, "DT");
    context = dt == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(dt, "profileContext");
    payload = dt == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(dt, "payloadHex");
    failures += expect_true(context != NULL && cJSON_IsObject(context) &&
                                payload != NULL && cJSON_IsString(payload) &&
                                strlen(payload->valuestring) == sizeof(bytes) * 2U &&
                                cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(
                                    context, "profileFingerprint")) &&
                                cJSON_GetObjectItemCaseSensitive(
                                    context, "calibrationVoltageV") == NULL,
                            "fixture types and nested V2 context are frozen");
    if (payload != NULL && cJSON_IsString(payload))
    {
        for (index = 0U; index < sizeof(bytes); ++index)
        {
            if (sscanf(&payload->valuestring[index * 2U], "%2x", &value) != 1)
            {
                failures += expect_true(0, "payloadHex decodes");
                break;
            }
            bytes[index] = (u8)value;
        }
    }
    failures += expect_true(
        sys_product_profile_context_build(
            560U, 890U, 890U,
            sys_calibration_storage_crc32(bytes, sizeof(bytes)), &parsed) == BOOL_TRUE &&
        parsed.table_crc32 == 3796061022UL &&
        sys_product_profile_context_binding_crc32(&parsed) == 538152895UL,
        "198-byte table CRC and big-endian profile binding CRC match fixture");
    failures += expect_true(
        sys_product_profile_context_build(560U, 890U, 0U, 0U, &parsed) == BOOL_TRUE &&
        sys_product_profile_context_binding_crc32(&parsed) == 2215728488UL,
        "first-calibration zero context binding CRC matches fixture");
    cJSON_Delete(fixture);
    return failures;
}

static int validate_percent_boundaries(void)
{
    static const char request_zero[] =
        "{\"SN\":\"TEST50W0000001\",\"TM\":\"2026-08-17 10:00:08\","
        "\"SV\":\"cal\",\"ID\":\"V000\",\"CT\":\"W\",\"DT\":{"
        "\"op\":\"SET_VALIDATION_PERCENT\",\"protocolVersion\":2,"
        "\"sessionId\":1234,\"seq\":8,\"targetPercent\":0}}";
    static const char request_hundred[] =
        "{\"SN\":\"TEST50W0000001\",\"TM\":\"2026-08-17 10:00:09\","
        "\"SV\":\"cal\",\"ID\":\"V100\",\"CT\":\"W\",\"DT\":{"
        "\"op\":\"SET_VALIDATION_PERCENT\",\"protocolVersion\":2,"
        "\"sessionId\":1234,\"seq\":9,\"targetPercent\":100}}";
    static const char expected_text[] =
        "{\"DT\":{\"op\":\"SET_VALIDATION_PERCENT\","
        "\"protocolVersion\":2,\"result\":6,\"ack\":false}}";
    cJSON *zero = cJSON_Parse(request_zero);
    cJSON *hundred = cJSON_Parse(request_hundred);
    cJSON *expected = cJSON_Parse(expected_text);
    int failures = 0;
    u32 calls_before = set_level_calls;

    failures += run_exchange(zero, expected,
                             "SET_VALIDATION_PERCENT rejects zero");
    failures += run_exchange(hundred, expected,
                             "SET_VALIDATION_PERCENT rejects one hundred");
    failures += expect_true(set_level_calls == calls_before,
                            "invalid validation percentages have no output side effect");
    cJSON_Delete(zero);
    cJSON_Delete(hundred);
    cJSON_Delete(expected);
    return failures;
}

static int validate_rejected_wire_shapes(void)
{
    static const char flat_begin_text[] =
        "{\"SN\":\"TEST50W0000001\",\"TM\":\"2026-08-17 09:59:58\","
        "\"SV\":\"cal\",\"ID\":\"X001\",\"CT\":\"W\",\"DT\":{"
        "\"op\":\"BEGIN\",\"protocolVersion\":2,\"sessionId\":999,"
        "\"seq\":1,\"leaseMs\":60000,\"profileId\":50,"
        "\"profileVersion\":1,\"profileFingerprint\":2809625630,"
        "\"calibrationVoltage01V\":560,\"configuredRatedCurrentMa\":890,"
        "\"calibratedMaxCurrentMa\":0,\"tableCrc32\":0,"
        "\"profileBindingCrc32\":2215728488}}";
    static const char array_stage_text[] =
        "{\"SN\":\"TEST50W0000001\",\"TM\":\"2026-08-17 09:59:59\","
        "\"SV\":\"cal\",\"ID\":\"X002\",\"CT\":\"W\",\"DT\":{"
        "\"op\":\"STAGE_CONFIG\",\"protocolVersion\":2,"
        "\"sessionId\":999,\"seq\":1,\"payloadHex\":[0]}}";
    static const char expected_text[] =
        "{\"DT\":{\"protocolVersion\":2,\"result\":11,\"ack\":false}}";
    cJSON *flat_begin = cJSON_Parse(flat_begin_text);
    cJSON *array_stage = cJSON_Parse(array_stage_text);
    cJSON *expected = cJSON_Parse(expected_text);
    int failures = 0;

    setup_service();
    failures += run_exchange(flat_begin, expected,
                             "flat profile context is rejected fail-closed");
    setup_service();
    failures += run_exchange(array_stage, expected,
                             "payloadHex array is rejected fail-closed");
    cJSON_Delete(flat_begin);
    cJSON_Delete(array_stage);
    cJSON_Delete(expected);
    return failures;
}

int main(int argc, char **argv)
{
    int failures = 0;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s FIXTURE_DIRECTORY\n", argv[0]);
        return 2;
    }
    failures += validate_rejected_wire_shapes();
    setup_service();
    set_level_calls = 0U;
    last_level = 0U;
    failures += validate_fixture_crc(argv[1]);
    failures += run_fixture(argv[1], "CAPABILITIES_50W_FIRST_CAL.json");
    failures += run_fixture(argv[1], "BEGIN_50W_56V.json");
    failures += run_fixture(argv[1], "STAGE_50W_56V_198B.json");
    failures += run_fixture(argv[1], "COMMIT_READBACK_50W_56V.json");
    failures += expect_true(last_level == 100U && set_level_calls == 1U,
                            "SET_VALIDATION_PERCENT 50 is temporary and bounded");
    failures += validate_percent_boundaries();
    cJSON_Delete(captured_response);
    if (failures != 0) return 1;
    printf("CAL_MQTT_V2 shared fixture host tests: PASS\n");
    return 0;
}
