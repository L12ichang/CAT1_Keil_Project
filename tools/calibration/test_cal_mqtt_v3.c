#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys_calibration_mqtt.h"
#include "sys_calibration_service.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_bl0942.h"
#include "factory_user_data.h"
#include "zk_protocol_internal.h"

#ifdef printf
#undef printf
#endif

#define TEST_TX_POOL_SIZE 4096U
#define TEST_JSON_SIZE    2048U

u8 factory_user_buff[128];
u16 SET_OUTCUR_temp = 893U;
u16 HWMAX_OUTCUR_temp = 1400U;
u16 OUTPUT_CUR_SENSOR_temp = 120U;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp = 560U;

static unsigned char tx_pool[TEST_TX_POOL_SIZE];
static size_t tx_pool_offset;
static size_t tx_pool_peak;
static int tx_pool_exhausted;
static char last_json[TEST_JSON_SIZE];
static size_t max_json_length[14];
static size_t max_pool_peak[14];
static sys_calibration_service_status_st fake_status;
static u8 fake_staged[SYS_CALIBRATION_PAYLOAD_LENGTH];
static u8 fake_committed[SYS_CALIBRATION_PAYLOAD_LENGTH];
static u32 fake_tick = 1000U;
static u32 begin_calls;
static u32 set_point_calls;
static u32 stage_calls;
static u32 commit_calls;
static int fake_raw_stale;
static int fake_raw_hardware_fault;
static int fake_set_point_hardware_fault;
static const char *const metric_operation_name[14] =
{
    "", "CAP", "BEGIN", "HEARTBEAT", "SET_POINT", "RAW", "STAGE",
    "APPLY", "SET_OUTPUT", "COMMIT", "READ", "ABORT", "RELEASE",
    "DIAG"
};

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

static int response_lacks_raw_fields(cJSON *dt)
{
    static const char *const raw_fields[] =
    {
        "level", "actualPwm", "ocoRaw", "blVoltageRaw", "blCurrentRaw",
        "blPowerRaw", "correctedOutputCurrentMa",
        "correctedInputVoltage01V", "correctedInputCurrentMa",
        "correctedInputPower01W", "outputVoltage01V", "blAgeMs",
        "validFlags", "faultFlags"
    };
    u8 index;

    if (dt == NULL)
    {
        return 0;
    }
    for (index = 0U;
         index < (u8)(sizeof(raw_fields) / sizeof(raw_fields[0]));
         ++index)
    {
        if (cJSON_GetObjectItem(dt, raw_fields[index]) != NULL)
        {
            return 0;
        }
    }
    return 1;
}

static void *test_pool_malloc(size_t size)
{
    size_t aligned = (size + 7U) & ~(size_t)7U;
    void *result;

    if (tx_pool_offset + aligned > sizeof(tx_pool))
    {
        tx_pool_exhausted = 1;
        return NULL;
    }
    result = &tx_pool[tx_pool_offset];
    tx_pool_offset += aligned;
    if (tx_pool_offset > tx_pool_peak)
    {
        tx_pool_peak = tx_pool_offset;
    }
    return result;
}

static void test_pool_free(void *pointer)
{
    (void)pointer;
}

static int operation_index(const char *operation)
{
    int index;

    for (index = 1; index < 14; ++index)
    {
        if (operation != NULL &&
            strcmp(operation, metric_operation_name[index]) == 0)
        {
            return index;
        }
    }
    return 0;
}

cJSON *zk_cjson_create_tx_object(const char *context)
{
    (void)context;
    return cJSON_CreateObject();
}

cJSON *zk_cjson_create_tx_array(const char *context)
{
    (void)context;
    return cJSON_CreateArray();
}

boolean_en zk_cjson_tx_allocation_ok(void)
{
    return tx_pool_exhausted == 0 ? BOOL_TRUE : BOOL_FALSE;
}

cJSON *zk_create_root_from_header(
    const zk_message_header_t *header,
    int with_er,
    int er_code)
{
    cJSON_Hooks hooks;
    cJSON *root;

    tx_pool_offset = 0U;
    tx_pool_peak = 0U;
    tx_pool_exhausted = 0;
    hooks.malloc_fn = test_pool_malloc;
    hooks.free_fn = test_pool_free;
    cJSON_InitHooks(&hooks);
    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return NULL;
    }
    cJSON_AddStringToObject(root, "SN", header->sn);
    cJSON_AddStringToObject(root, "TM", "2026-08-22 12:00:00");
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
    cJSON *dt;
    cJSON *op;
    int index;
    size_t length;

    (void)topic;
    memset(last_json, 0, sizeof(last_json));
    if (cJSON_PrintPreallocated(root, last_json, (int)sizeof(last_json), 0) == 0)
    {
        return -1;
    }
    length = strlen(last_json);
    dt = cJSON_GetObjectItem(root, "DT");
    op = dt != NULL ? cJSON_GetObjectItem(dt, "op") : NULL;
    index = operation_index(
        op != NULL && cJSON_IsString(op) ? op->valuestring : NULL);
    if (index > 0)
    {
        if (length > max_json_length[index])
        {
            max_json_length[index] = length;
        }
        if (tx_pool_peak > max_pool_peak[index])
        {
            max_pool_peak[index] = tx_pool_peak;
        }
    }
    return 0;
}

u32 HAL_GetTick(void)
{
    return fake_tick;
}

boolean_en sys_bl0942_get_diag(u32 now_tick_ms, sys_bl0942_diag_st *diag)
{
    if (diag == NULL)
    {
        return BOOL_FALSE;
    }
    memset(diag, 0, sizeof(*diag));
    diag->valid_frame_count = 101U;
    diag->ore_count = 2U;
    diag->fe_count = 3U;
    diag->ne_count = 4U;
    diag->timeout_count = 5U;
    diag->uart_error_count = 6U;
    diag->recovery_count = 7U;
    diag->recovery_fail_count = 1U;
    diag->last_valid_frame_tick = now_tick_ms - 25U;
    diag->bl_age_ms = 25U;
    diag->bl_fresh = 1U;
    diag->uart_g_state = 0x20U;
    diag->uart_rx_state = 0x20U;
    diag->uart_error_code = 0U;
    return BOOL_TRUE;
}

static sys_calibration_result_en fake_finish(
    sys_calibration_result_en result,
    u32 seq,
    sys_calibration_service_status_st *status)
{
    fake_status.last_result = result;
    fake_status.result_seq = seq;
    if (seq > fake_status.last_request_seq)
    {
        fake_status.last_request_seq = seq;
    }
    if (status != NULL)
    {
        *status = fake_status;
    }
    return result;
}

boolean_en sys_calibration_service_get_status(
    sys_calibration_service_status_st *status)
{
    if (status == NULL)
    {
        return BOOL_FALSE;
    }
    *status = fake_status;
    return BOOL_TRUE;
}

sys_calibration_result_en sys_calibration_service_begin_seq(
    u32 session_id, u32 now_ms, u32 lease_ms, u32 seq,
    u16 profile_id, u32 profile_fingerprint,
    sys_calibration_service_status_st *status)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    ++begin_calls;
    if (fake_status.state != SYS_CALIBRATION_STATE_IDLE)
    {
        return fake_finish(SYS_CALIBRATION_RESULT_BUSY, seq, status);
    }
    if (profile_id != profile->profile_id ||
        profile_fingerprint != profile->fingerprint_crc32)
    {
        return fake_finish(SYS_CALIBRATION_RESULT_PROFILE_MISMATCH, seq, status);
    }
    fake_status.state = SYS_CALIBRATION_STATE_ACTIVE;
    fake_status.session_id = session_id;
    fake_status.lease_ms = lease_ms;
    fake_status.lease_deadline_ms = now_ms + lease_ms;
    fake_status.boot_inhibit_active = BOOL_TRUE;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_heartbeat_seq(
    u32 session_id, u32 now_ms, u32 lease_ms, u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    fake_status.lease_ms = lease_ms;
    fake_status.lease_deadline_ms = now_ms + lease_ms;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_set_point_seq(
    u32 session_id, u32 now_ms, u32 seq, u16 level,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    ++set_point_calls;
    if (fake_set_point_hardware_fault != 0)
    {
        return fake_finish(SYS_CALIBRATION_RESULT_HARDWARE_FAULT,
                           seq, status);
    }
    fake_status.current_level = level;
    fake_status.actual_pwm = (u16)(level * 5U);
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id, u32 now_ms, u32 seq, sys_calibration_raw_st *raw,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    memset(raw, 0, sizeof(*raw));
    raw->level = fake_status.current_level;
    raw->actual_pwm = fake_status.actual_pwm;
    raw->oco_raw = 1234U;
    raw->bl_voltage_raw = 230000U;
    raw->bl_current_raw = 150000U;
    raw->bl_power_raw = 300000L;
    raw->corrected_output_current_ma = 447U;
    raw->corrected_input_voltage_01v = 2200U;
    raw->corrected_input_current_ma = 100U;
    raw->corrected_input_power_01w = 250U;
    raw->output_voltage_01v = 480U;
    raw->bl_age_ms = fake_raw_stale != 0 ? 700U : 25U;
    raw->valid_flags = fake_raw_stale != 0 ? 0x001FU : 0x07FFU;
    raw->fault_flags = fake_raw_hardware_fault != 0 ?
                       SYS_CALIBRATION_FAULT_OUTPUT_OVERLOAD : 0U;
    return fake_finish(
        fake_raw_stale != 0 ? SYS_CALIBRATION_RESULT_DATA_STALE :
        (fake_raw_hardware_fault != 0 ?
         SYS_CALIBRATION_RESULT_HARDWARE_FAULT : SYS_CALIBRATION_RESULT_OK),
        seq, status);
}

sys_calibration_result_en sys_calibration_service_stage_seq(
    u32 session_id, u32 now_ms, u32 seq,
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH], u16 length,
    u32 payload_crc32, sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    ++stage_calls;
    memcpy(fake_staged, payload, length);
    fake_status.state = SYS_CALIBRATION_STATE_STAGED;
    fake_status.staged_valid = BOOL_TRUE;
    fake_status.staged_length = length;
    fake_status.staged_crc32 = payload_crc32;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_apply_seq(
    u32 session_id, u32 now_ms, u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    fake_status.state = SYS_CALIBRATION_STATE_APPLIED;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_set_output_seq(
    u32 session_id, u32 now_ms, u32 seq, u8 percent,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    fake_status.current_percent = percent;
    fake_status.actual_pwm = (u16)((u16)percent * 10U);
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_commit_seq(
    u32 session_id, u32 now_ms, u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    ++commit_calls;
    memcpy(fake_committed, fake_staged, sizeof(fake_committed));
    fake_status.state = SYS_CALIBRATION_STATE_COMMITTED;
    fake_status.committed_valid = BOOL_TRUE;
    fake_status.committed_length = SYS_CALIBRATION_PAYLOAD_LENGTH;
    fake_status.committed_crc32 = fake_status.staged_crc32;
    fake_status.committed_generation = 7U;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_read_seq(
    u32 session_id, u32 now_ms, u32 seq, u8 *payload, u16 capacity,
    u16 *length, u32 *payload_crc32, u32 *generation,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    if (capacity < sizeof(fake_committed))
    {
        return fake_finish(SYS_CALIBRATION_RESULT_BAD_REQUEST, seq, status);
    }
    memcpy(payload, fake_committed, sizeof(fake_committed));
    *length = sizeof(fake_committed);
    *payload_crc32 = fake_status.committed_crc32;
    *generation = fake_status.committed_generation;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_abort_seq(
    u32 session_id, u32 now_ms, u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    fake_status.state = SYS_CALIBRATION_STATE_IDLE;
    fake_status.session_id = 0U;
    fake_status.boot_inhibit_active = BOOL_FALSE;
    fake_status.staged_valid = BOOL_FALSE;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

sys_calibration_result_en sys_calibration_service_release_seq(
    u32 session_id, u32 now_ms, u32 seq,
    sys_calibration_service_status_st *status)
{
    (void)session_id;
    (void)now_ms;
    fake_status.state = SYS_CALIBRATION_STATE_IDLE;
    fake_status.session_id = 0U;
    fake_status.boot_inhibit_active = BOOL_FALSE;
    return fake_finish(SYS_CALIBRATION_RESULT_OK, seq, status);
}

static char *load_text(const char *path)
{
    FILE *file;
    long length;
    char *text;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }
    text = (char *)malloc((size_t)length + 1U);
    if (text == NULL)
    {
        fclose(file);
        return NULL;
    }
    if (fread(text, 1U, (size_t)length, file) != (size_t)length)
    {
        free(text);
        fclose(file);
        return NULL;
    }
    text[length] = '\0';
    fclose(file);
    return text;
}

static int send_request(const char *dt_json, const char *ct)
{
    zk_message_header_t header;
    cJSON *root;
    cJSON *dt;
    cJSON_Hooks hooks;
    boolean_en handled;

    cJSON_InitHooks(NULL);
    dt = cJSON_Parse(dt_json);
    root = cJSON_CreateObject();
    if (dt == NULL || root == NULL)
    {
        cJSON_Delete(dt);
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "DT", dt);
    memset(&header, 0, sizeof(header));
    strcpy(header.sn, "TEST50W0000001");
    strcpy(header.tm, "2026-08-22 12:00:00");
    strcpy(header.sv, "cal");
    strcpy(header.id, "V30001");
    strcpy(header.ct, ct);
    handled = sys_calibration_mqtt_handle(root, &header);
    hooks.malloc_fn = malloc;
    hooks.free_fn = free;
    cJSON_InitHooks(&hooks);
    cJSON_Delete(root);
    return handled == BOOL_TRUE ? 0 : -1;
}

static int send_fixture(const char *directory, const char *name, const char *ct)
{
    char path[512];
    char *text;
    int result;

    snprintf(path, sizeof(path), "%s/%s", directory, name);
    text = load_text(path);
    if (text == NULL)
    {
        return -1;
    }
    result = send_request(text, ct);
    free(text);
    return result;
}

static cJSON *last_response(void)
{
    cJSON_InitHooks(NULL);
    return cJSON_Parse(last_json);
}

static int response_has_number(
    cJSON *root,
    const char *key,
    double expected)
{
    cJSON *dt = cJSON_GetObjectItem(root, "DT");
    cJSON *node = dt != NULL ? cJSON_GetObjectItem(dt, key) : NULL;
    return node != NULL && cJSON_IsNumber(node) &&
           cJSON_GetNumberValue(node) == expected;
}

static int response_has_string(
    cJSON *root,
    const char *key,
    const char *expected)
{
    cJSON *dt = cJSON_GetObjectItem(root, "DT");
    cJSON *node = dt != NULL ? cJSON_GetObjectItem(dt, key) : NULL;
    return node != NULL && cJSON_IsString(node) &&
           strcmp(node->valuestring, expected) == 0;
}

int main(int argc, char **argv)
{
    cJSON *response;
    cJSON *dt;
    char fixture_path[512];
    char *bad_stage;
    char *crc_text;
    char *seq_text;
    char *g6_begin_text;
    cJSON *g6_begin;
    char duplicate_json[TEST_JSON_SIZE];
    int failures = 0;
    int index;
    static const int ordinary_ack[] = {2, 3, 4, 6, 7, 8, 9, 11, 12};

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s CAL_MQTT_V3_FIXTURE_DIR\n", argv[0]);
        return 2;
    }
    memset(&fake_status, 0, sizeof(fake_status));
    fake_status.state = SYS_CALIBRATION_STATE_IDLE;
    fake_status.safety_ready = BOOL_TRUE;
    fake_status.persistence_ready = BOOL_TRUE;

    failures += expect_true(
        send_fixture(argv[1], "G6_CAP.request.fixture", "R") == 0,
        "G6 CAP request is handled");
    response = last_response();
    dt = response != NULL ? cJSON_GetObjectItem(response, "DT") : NULL;
    failures += expect_true(
        response != NULL && response_has_string(response, "op", "CAP") &&
        response_has_number(response, "v", 3.0) &&
        response_has_number(response, "rc", 0.0) &&
        response_has_number(response, "st", 0.0) &&
        response_has_number(response, "capabilities", 255.0) &&
        response_has_number(response, "profileId", 50.0) &&
        response_has_number(response, "hardwareMaxMa", 1680.0) &&
        response_has_number(response, "hwMaxMa", 1400.0) &&
        response_has_number(response, "setOutcurMa", 893.0) &&
        cJSON_GetObjectItem(dt, "sid") == NULL &&
        cJSON_GetObjectItem(dt, "seq") == NULL &&
        cJSON_GetObjectItem(dt, "profilesCsv") == NULL &&
        cJSON_GetObjectItem(dt, "ack") == NULL &&
        cJSON_GetObjectItem(dt, "result") == NULL &&
        cJSON_GetObjectItem(dt, "protocolVersion") == NULL &&
        cJSON_GetObjectItem(dt, "readback") == NULL,
        "CAP returns only the current 50W Product and no session/catalog");
    cJSON_Delete(response);

    failures += expect_true(
        send_fixture(argv[1], "G6_CAP.request.fixture", "W") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL &&
        send_request("{\"v\":2,\"op\":\"CAP\"}", "R") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL &&
        send_request("{\"v\":3,\"v\":3,\"op\":\"CAP\"}", "R") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL,
        "wrong CT/version and duplicate keys are BAD_REQUEST");

    failures += expect_true(
        send_fixture(argv[1], "NEGATIVE_V2_ALIAS.request.fixture", "R") == 0,
        "legacy V2 request is consumed as a V3 error");
    response = last_response();
    failures += expect_true(
        response != NULL && response_has_number(response, "rc", 1.0),
        "legacy V2 aliases are BAD_REQUEST without compatibility fallback");
    cJSON_Delete(response);

    snprintf(fixture_path, sizeof(fixture_path), "%s/%s", argv[1],
             "G6_BEGIN.request.fixture");
    g6_begin_text = load_text(fixture_path);
    cJSON_InitHooks(NULL);
    g6_begin = g6_begin_text != NULL ? cJSON_Parse(g6_begin_text) : NULL;
    failures += expect_true(
        g6_begin != NULL &&
        cJSON_GetNumberValue(cJSON_GetObjectItem(
            g6_begin, "profileFingerprint")) == 287454020.0,
        "G6 fixed BEGIN fixture preserves the documented 0x11223344 vector");
    cJSON_Delete(g6_begin);
    free(g6_begin_text);

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":880,\"seq\":1,\"profileId\":50,\"profileFingerprint\":287454020,\"leaseMs\":30000}", "W") == 0 &&
        strstr(last_json, "\"rc\":10") != NULL &&
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":881,\"seq\":1,\"profileId\":50,\"profileFingerprint\":1122902929,\"leaseMs\":999}", "W") == 0 &&
        strstr(last_json, "\"rc\":5") != NULL && begin_calls == 0U,
        "BEGIN rejects Product mismatch and out-of-range lease before service");

    failures += expect_true(
        send_fixture(argv[1], "G6_BEGIN_RUNTIME_50W.request.fixture", "W") == 0,
        "runtime 50W BEGIN request succeeds");
    strcpy(duplicate_json, last_json);
    failures += expect_true(begin_calls == 1U &&
        send_fixture(argv[1], "G6_BEGIN_RUNTIME_50W.request.fixture", "W") == 0 &&
        begin_calls == 1U && strcmp(duplicate_json, last_json) == 0,
        "exact BEGIN retry replays the first response without side effects");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":123456,\"seq\":1,\"profileId\":50,\"profileFingerprint\":1122902929,\"leaseMs\":31000}", "W") == 0,
        "conflicting duplicate BEGIN is handled");
    response = last_response();
    failures += expect_true(begin_calls == 1U && response != NULL &&
        response_has_number(response, "rc", 1.0),
        "same sid/seq with different parameters is BAD_REQUEST");
    cJSON_Delete(response);

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"HEARTBEAT\",\"sid\":123456,\"seq\":2,\"leaseMs\":30000}", "W") == 0,
        "HEARTBEAT renews the session");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"SET_POINT\",\"sid\":123456,\"seq\":7,\"level\":21}", "W") == 0 &&
        strstr(last_json, "\"rc\":5") != NULL && set_point_calls == 0U,
        "SET_POINT rejects non-canonical levels before service");
    failures += expect_true(
        send_fixture(argv[1], "G6_SET_POINT.request.fixture", "W") == 0,
        "G6 SET_POINT succeeds");
    strcpy(duplicate_json, last_json);
    failures += expect_true(set_point_calls == 1U &&
        send_fixture(argv[1], "G6_SET_POINT.request.fixture", "W") == 0 &&
        set_point_calls == 1U && strcmp(duplicate_json, last_json) == 0,
        "exact SET_POINT retry is idempotent");

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"RAW\",\"sid\":123456,\"seq\":9}", "R") == 0,
        "ACTIVE RAW succeeds");
    response = last_response();
    failures += expect_true(response != NULL &&
        response_has_number(response, "ocoRaw", 1234.0) &&
        response_has_number(response, "blAgeMs", 25.0) &&
        response_has_number(response, "validFlags", 2047.0),
        "RAW exposes the exact flat Raw/Corrected V3 fields");
    cJSON_Delete(response);
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"HEARTBEAT\",\"sid\":123456,\"seq\":2,\"leaseMs\":30000}", "W") == 0 &&
        strstr(last_json, "\"rc\":2") != NULL,
        "old seq without a replay cache is BAD_STATE");

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"STAGE\",\"sid\":123456,\"seq\":18,\"payloadLength\":244,\"payloadCrc32\":0,\"ph\":\"00\"}", "W") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL,
        "STAGE rejects the ph alias instead of adding compatibility");
    snprintf(fixture_path, sizeof(fixture_path), "%s/%s", argv[1],
             "G6_STAGE.request.fixture");
    bad_stage = load_text(fixture_path);
    crc_text = bad_stage != NULL ? strstr(bad_stage, "1110049161") : NULL;
    seq_text = bad_stage != NULL ? strstr(bad_stage, "\"seq\":20") : NULL;
    if (crc_text != NULL)
    {
        memcpy(crc_text, "1110049162", 10U);
    }
    if (seq_text != NULL)
    {
        memcpy(seq_text, "\"seq\":19", 8U);
    }
    failures += expect_true(
        bad_stage != NULL && crc_text != NULL && seq_text != NULL &&
        send_request(bad_stage, "W") == 0 &&
        strstr(last_json, "\"rc\":7") != NULL && stage_calls == 0U,
        "STAGE rejects a payload CRC mismatch before service");
    free(bad_stage);

    failures += expect_true(
        send_fixture(argv[1], "G6_STAGE.request.fixture", "W") == 0,
        "G6 STAGE validates and stages the 244-byte payload");
    strcpy(duplicate_json, last_json);
    failures += expect_true(stage_calls == 1U &&
        send_fixture(argv[1], "G6_STAGE.request.fixture", "W") == 0 &&
        stage_calls == 1U && strcmp(duplicate_json, last_json) == 0,
        "exact STAGE retry does not restage");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"APPLY\",\"sid\":123456,\"seq\":21}", "W") == 0,
        "APPLY activates staged correction without Flash fields");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"RAW\",\"sid\":123456,\"seq\":29}", "R") == 0,
        "APPLIED RAW supports independent verification");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"SET_OUTPUT\",\"sid\":123456,\"seq\":29,\"percent\":101}", "W") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL,
        "same sid/seq with a conflicting operation is BAD_REQUEST");
    failures += expect_true(
        send_fixture(argv[1], "G6_SET_OUTPUT.request.fixture", "W") == 0,
        "G6 SET_OUTPUT uses percent and not SET_VERIFY");
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"COMMIT\",\"sid\":123456,\"seq\":31}", "W") == 0,
        "COMMIT succeeds");
    strcpy(duplicate_json, last_json);
    failures += expect_true(commit_calls == 1U &&
        send_request("{\"v\":3,\"op\":\"COMMIT\",\"sid\":123456,\"seq\":31}", "W") == 0 &&
        commit_calls == 1U && strcmp(duplicate_json, last_json) == 0,
        "exact COMMIT retry never commits Flash twice");

    failures += expect_true(
        send_fixture(argv[1], "G6_READ.request.fixture", "R") == 0,
        "single READ returns the committed payload");
    response = last_response();
    dt = response != NULL ? cJSON_GetObjectItem(response, "DT") : NULL;
    failures += expect_true(response != NULL &&
        response_has_number(response, "payloadLength", 244.0) &&
        response_has_number(response, "generation", 7.0) &&
        cJSON_IsString(cJSON_GetObjectItem(dt, "payloadHex")) &&
        strlen(cJSON_GetObjectItem(dt, "payloadHex")->valuestring) == 488U &&
        cJSON_GetObjectItem(dt, "recordCrc32") == NULL &&
        cJSON_GetObjectItem(dt, "commitWord") == NULL,
        "READ is one 244-byte payload, never a 272-byte record/chunk");
    cJSON_Delete(response);

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"DIAG\"}", "R") == 0,
        "DIAG is sessionless and read-only");
    response = last_response();
    dt = response != NULL ? cJSON_GetObjectItem(response, "DT") : NULL;
    failures += expect_true(response != NULL &&
        response_has_number(response, "validFrameCount", 101.0) &&
        response_has_number(response, "oreCount", 2.0) &&
        response_has_number(response, "blFresh", 1.0) &&
        cJSON_GetObjectItem(dt, "sid") == NULL &&
        cJSON_GetObjectItem(dt, "seq") == NULL &&
        cJSON_GetObjectItem(dt, "ocoRaw") == NULL,
        "DIAG carries UART/BL recovery counters, separate from RAW");
    cJSON_Delete(response);

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"RELEASE\",\"sid\":123456,\"seq\":33}", "W") == 0,
        "RELEASE closes a committed session");
    failures += expect_true(
        send_fixture(argv[1], "G6_CAP.request.fixture", "R") == 0,
        "CAP remains sessionless after release");
    response = last_response();
    failures += expect_true(response != NULL &&
        response_has_number(response, "hasCalibration", 1.0) &&
        response_has_number(response, "generation", 7.0) &&
        response_has_number(response, "payloadLength", 244.0) &&
        response_has_number(response, "payloadCrc32", 1110049161.0),
        "CAP reports committed generation/length/CRC after release");
    cJSON_Delete(response);

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":777,\"seq\":1,\"profileId\":50,\"profileFingerprint\":1122902929,\"leaseMs\":30000}", "W") == 0,
        "second session begins");
    fake_raw_stale = 1;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"RAW\",\"sid\":777,\"seq\":2}", "R") == 0,
        "stale RAW is returned for audit");
    response = last_response();
    failures += expect_true(response != NULL &&
        response_has_number(response, "rc", 6.0) &&
        response_has_number(response, "blAgeMs", 700.0) &&
        response_has_number(response, "validFlags", 31.0),
        "DATA_STALE preserves age and flags");
    cJSON_Delete(response);
    fake_raw_stale = 0;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"ABORT\",\"sid\":777,\"seq\":3}", "W") == 0,
        "ABORT closes an uncommitted session");

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":778,\"seq\":1,\"profileId\":50,\"profileFingerprint\":1122902929,\"leaseMs\":30000}", "W") == 0,
        "hardware-fault RAW session begins");
    fake_raw_hardware_fault = 1;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"RAW\",\"sid\":778,\"seq\":2}", "R") == 0,
        "hardware-fault RAW is returned for audit");
    response = last_response();
    dt = response != NULL ? cJSON_GetObjectItem(response, "DT") : NULL;
    failures += expect_true(
        response != NULL && dt != NULL &&
        response_has_number(response, "rc", 9.0) &&
        response_has_number(response, "ocoRaw", 1234.0) &&
        response_has_number(response, "blVoltageRaw", 230000.0) &&
        response_has_number(response, "blCurrentRaw", 150000.0) &&
        response_has_number(response, "blPowerRaw", 300000.0) &&
        response_has_number(response, "correctedOutputCurrentMa", 447.0) &&
        response_has_number(response, "correctedInputVoltage01V", 2200.0) &&
        response_has_number(response, "correctedInputCurrentMa", 100.0) &&
        response_has_number(response, "correctedInputPower01W", 250.0) &&
        response_has_number(response, "outputVoltage01V", 480.0) &&
        response_has_number(response, "blAgeMs", 25.0) &&
        response_has_number(response, "validFlags", 2047.0) &&
        response_has_number(response, "faultFlags", 1.0) &&
        cJSON_GetObjectItem(dt, "level") != NULL &&
        cJSON_GetObjectItem(dt, "actualPwm") != NULL &&
        strlen(last_json) < 768U && tx_pool_peak <= TEST_TX_POOL_SIZE &&
        tx_pool_exhausted == 0,
        "HARDWARE_FAULT rc=9 preserves every RAW field within memory budgets");
    cJSON_Delete(response);
    fake_raw_hardware_fault = 0;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"ABORT\",\"sid\":778,\"seq\":3}", "W") == 0,
        "hardware-fault RAW session aborts");

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"BEGIN\",\"sid\":779,\"seq\":1,\"profileId\":50,\"profileFingerprint\":1122902929,\"leaseMs\":30000}", "W") == 0,
        "non-RAW hardware-fault session begins");
    fake_set_point_hardware_fault = 1;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"SET_POINT\",\"sid\":779,\"seq\":2,\"level\":20}", "W") == 0,
        "SET_POINT hardware fault is returned");
    response = last_response();
    dt = response != NULL ? cJSON_GetObjectItem(response, "DT") : NULL;
    failures += expect_true(
        response != NULL && response_has_number(response, "rc", 9.0) &&
        response_has_string(response, "op", "SET_POINT") &&
        response_lacks_raw_fields(dt),
        "non-RAW HARDWARE_FAULT response contains no RAW or success fields");
    cJSON_Delete(response);
    fake_set_point_hardware_fault = 0;
    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"ABORT\",\"sid\":779,\"seq\":3}", "W") == 0,
        "non-RAW hardware-fault session aborts");

    failures += expect_true(
        send_request("{\"v\":3,\"op\":\"SET_VERIFY\",\"sid\":1,\"seq\":1}", "W") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL &&
        send_request("{\"v\":3,\"op\":\"READ_CHUNK\",\"sid\":1,\"seq\":1}", "R") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL &&
        send_request("{\"v\":3,\"op\":7,\"sid\":1,\"seq\":1}", "W") == 0 &&
        strstr(last_json, "\"rc\":1") != NULL,
        "SET_VERIFY, READ_CHUNK and numeric operation have no V3 path");

    for (index = 0; index < (int)(sizeof(ordinary_ack) / sizeof(ordinary_ack[0]));
         ++index)
    {
        failures += expect_true(max_json_length[ordinary_ack[index]] < 256U,
                                "ordinary ACK stays below 256 bytes");
    }
    failures += expect_true(max_json_length[1] < 1024U,
                            "CAP stays below 1024 bytes");
    failures += expect_true(max_json_length[5] < 768U,
                            "RAW stays below 768 bytes");
    failures += expect_true(max_json_length[10] < 1024U,
                            "READ stays below 1024 bytes");
    for (index = 1; index < 14; ++index)
    {
        failures += expect_true(max_json_length[index] > 0U &&
                                max_json_length[index] < 1536U,
                                "each V3 operation stays below 1536 bytes");
        failures += expect_true(max_pool_peak[index] > 0U &&
                                max_pool_peak[index] <= TEST_TX_POOL_SIZE,
                                "each V3 operation stays within the 4KiB cJSON pool");
        printf("V3_METRIC op=%s json=%lu pool=%lu\n",
               metric_operation_name[index],
               (unsigned long)max_json_length[index],
               (unsigned long)max_pool_peak[index]);
    }
    failures += expect_true(tx_pool_exhausted == 0,
                            "no TX Pool Exhausted event occurred");

    if (failures == 0)
    {
        printf("Calibration MQTT V3 G6/idempotency/memory tests: PASS\n");
    }
    return failures == 0 ? 0 : 1;
}
