/*************************************************************
程序功能：Calibration MQTT Protocol V3字符串合同
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
*************************************************************/
#include "sys_calibration_mqtt.h"
#include "zk_protocol_internal.h"
#include "sys_calibration_service.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_product_profile.h"
#include "sys_bl0942.h"
#include "factory_user_data.h"
#include "main.h"

#include <string.h>

#define SYS_CALIBRATION_MQTT_CAPABILITIES       0x00FFU
#define SYS_CALIBRATION_MQTT_RAW_MAX_AGE_MS       500U
#define SYS_CALIBRATION_MQTT_OP_TEXT_LENGTH         12U

typedef enum
{
    SYS_CALIBRATION_MQTT_OP_INVALID = 0,
    SYS_CALIBRATION_MQTT_OP_CAP,
    SYS_CALIBRATION_MQTT_OP_BEGIN,
    SYS_CALIBRATION_MQTT_OP_HEARTBEAT,
    SYS_CALIBRATION_MQTT_OP_SET_POINT,
    SYS_CALIBRATION_MQTT_OP_RAW,
    SYS_CALIBRATION_MQTT_OP_STAGE,
    SYS_CALIBRATION_MQTT_OP_APPLY,
    SYS_CALIBRATION_MQTT_OP_SET_OUTPUT,
    SYS_CALIBRATION_MQTT_OP_COMMIT,
    SYS_CALIBRATION_MQTT_OP_READ,
    SYS_CALIBRATION_MQTT_OP_ABORT,
    SYS_CALIBRATION_MQTT_OP_RELEASE,
    SYS_CALIBRATION_MQTT_OP_DIAG
} sys_calibration_mqtt_operation_en;

typedef struct
{
    sys_calibration_mqtt_operation_en operation;
    char operation_text[SYS_CALIBRATION_MQTT_OP_TEXT_LENGTH];
    sys_calibration_result_en result;
    sys_calibration_service_status_st status;
    u32 session_id;
    u32 seq;
    u32 lease_ms;
    u32 payload_crc32;
    u32 generation;
    u16 level;
    u16 payload_length;
    u8 percent;
    boolean_en raw_valid;
    boolean_en payload_valid;
    boolean_en diag_valid;
    sys_calibration_raw_st raw;
    sys_bl0942_diag_st diag;
    u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH];
} sys_calibration_mqtt_response_st;

typedef struct
{
    boolean_en valid;
    u32 session_id;
    u32 seq;
    u32 parameter_digest;
    sys_calibration_mqtt_operation_en operation;
    sys_calibration_mqtt_response_st response;
} sys_calibration_mqtt_replay_st;

static sys_calibration_mqtt_replay_st _replay;
/* MQTT dispatch is synchronous. Keeping the large request/response work area
 * static avoids a roughly 1.1 KiB handler stack frame on the Cortex-M3. */
static sys_calibration_mqtt_response_st _working_response;
static sys_calibration_payload_st _working_decoded_payload;

static const char *sys_calibration_mqtt_operation_text(
    sys_calibration_mqtt_operation_en operation)
{
    static const char *const text[] =
    {
        "",
        "CAP",
        "BEGIN",
        "HEARTBEAT",
        "SET_POINT",
        "RAW",
        "STAGE",
        "APPLY",
        "SET_OUTPUT",
        "COMMIT",
        "READ",
        "ABORT",
        "RELEASE",
        "DIAG"
    };

    if ((u32)operation >= (u32)(sizeof(text) / sizeof(text[0])))
    {
        return "";
    }
    return text[(u32)operation];
}

static sys_calibration_mqtt_operation_en sys_calibration_mqtt_parse_operation(
    const char *text)
{
    sys_calibration_mqtt_operation_en operation;

    if (text == NULL)
    {
        return SYS_CALIBRATION_MQTT_OP_INVALID;
    }
    for (operation = SYS_CALIBRATION_MQTT_OP_CAP;
         operation <= SYS_CALIBRATION_MQTT_OP_DIAG;
         operation = (sys_calibration_mqtt_operation_en)((u32)operation + 1U))
    {
        if (strcmp(text, sys_calibration_mqtt_operation_text(operation)) == 0)
        {
            return operation;
        }
    }
    return SYS_CALIBRATION_MQTT_OP_INVALID;
}

static boolean_en sys_calibration_mqtt_is_sessionless(
    sys_calibration_mqtt_operation_en operation)
{
    return (operation == SYS_CALIBRATION_MQTT_OP_CAP ||
            operation == SYS_CALIBRATION_MQTT_OP_DIAG) ?
           BOOL_TRUE : BOOL_FALSE;
}

static const char *sys_calibration_mqtt_expected_ct(
    sys_calibration_mqtt_operation_en operation)
{
    if (operation == SYS_CALIBRATION_MQTT_OP_CAP ||
        operation == SYS_CALIBRATION_MQTT_OP_RAW ||
        operation == SYS_CALIBRATION_MQTT_OP_READ ||
        operation == SYS_CALIBRATION_MQTT_OP_DIAG)
    {
        return ZK_CT_READ;
    }
    return ZK_CT_WRITE;
}

static void sys_calibration_mqtt_copy_operation(
    char destination[SYS_CALIBRATION_MQTT_OP_TEXT_LENGTH],
    const char *source)
{
    u8 index = 0U;

    if (source != NULL)
    {
        while (source[index] != '\0' &&
               index + 1U < SYS_CALIBRATION_MQTT_OP_TEXT_LENGTH)
        {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static boolean_en sys_calibration_mqtt_read_u32(
    cJSON *object,
    const char *key,
    u32 *value)
{
    cJSON *node;
    double number;

    if (object == NULL || key == NULL || value == NULL)
    {
        return BOOL_FALSE;
    }
    node = cJSON_GetObjectItemCaseSensitive(object, key);
    if (node == NULL || !cJSON_IsNumber(node))
    {
        return BOOL_FALSE;
    }
    number = cJSON_GetNumberValue(node);
    if (number != number || number < 0.0 || number > 4294967295.0 ||
        number != (double)(u32)number)
    {
        return BOOL_FALSE;
    }
    *value = (u32)number;
    return BOOL_TRUE;
}

static boolean_en sys_calibration_mqtt_read_u16(
    cJSON *object,
    const char *key,
    u16 *value)
{
    u32 number;

    if (sys_calibration_mqtt_read_u32(object, key, &number) != BOOL_TRUE ||
        number > 65535U)
    {
        return BOOL_FALSE;
    }
    *value = (u16)number;
    return BOOL_TRUE;
}

static boolean_en sys_calibration_mqtt_read_u8(
    cJSON *object,
    const char *key,
    u8 *value)
{
    u32 number;

    if (sys_calibration_mqtt_read_u32(object, key, &number) != BOOL_TRUE ||
        number > 255U)
    {
        return BOOL_FALSE;
    }
    *value = (u8)number;
    return BOOL_TRUE;
}

static boolean_en sys_calibration_mqtt_field_allowed(
    sys_calibration_mqtt_operation_en operation,
    const char *key)
{
    if (key == NULL)
    {
        return BOOL_FALSE;
    }
    if (strcmp(key, "v") == 0 || strcmp(key, "op") == 0)
    {
        return BOOL_TRUE;
    }
    if (sys_calibration_mqtt_is_sessionless(operation) != BOOL_TRUE &&
        (strcmp(key, "sid") == 0 || strcmp(key, "seq") == 0))
    {
        return BOOL_TRUE;
    }
    switch (operation)
    {
        case SYS_CALIBRATION_MQTT_OP_BEGIN:
            return (strcmp(key, "profileId") == 0 ||
                    strcmp(key, "profileFingerprint") == 0 ||
                    strcmp(key, "leaseMs") == 0) ? BOOL_TRUE : BOOL_FALSE;
        case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
            return strcmp(key, "leaseMs") == 0 ? BOOL_TRUE : BOOL_FALSE;
        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
            return strcmp(key, "level") == 0 ? BOOL_TRUE : BOOL_FALSE;
        case SYS_CALIBRATION_MQTT_OP_STAGE:
            return (strcmp(key, "payloadLength") == 0 ||
                    strcmp(key, "payloadCrc32") == 0 ||
                    strcmp(key, "payloadHex") == 0) ? BOOL_TRUE : BOOL_FALSE;
        case SYS_CALIBRATION_MQTT_OP_SET_OUTPUT:
            return strcmp(key, "percent") == 0 ? BOOL_TRUE : BOOL_FALSE;
        default:
            return BOOL_FALSE;
    }
}

static boolean_en sys_calibration_mqtt_request_fields_valid(
    cJSON *dt,
    sys_calibration_mqtt_operation_en operation)
{
    cJSON *node;
    cJSON *previous;

    if (dt == NULL)
    {
        return BOOL_FALSE;
    }
    for (node = dt->child; node != NULL; node = node->next)
    {
        if (sys_calibration_mqtt_field_allowed(
                operation, node->string) != BOOL_TRUE)
        {
            return BOOL_FALSE;
        }
        for (previous = dt->child; previous != node; previous = previous->next)
        {
            if (previous->string != NULL && node->string != NULL &&
                strcmp(previous->string, node->string) == 0)
            {
                return BOOL_FALSE;
            }
        }
    }
    return BOOL_TRUE;
}

static u32 sys_calibration_mqtt_digest_byte(u32 digest, u8 value)
{
    digest ^= value;
    return digest * 16777619UL;
}

static u32 sys_calibration_mqtt_digest_u32(u32 digest, u32 value)
{
    digest = sys_calibration_mqtt_digest_byte(digest, (u8)value);
    digest = sys_calibration_mqtt_digest_byte(digest, (u8)(value >> 8U));
    digest = sys_calibration_mqtt_digest_byte(digest, (u8)(value >> 16U));
    return sys_calibration_mqtt_digest_byte(digest, (u8)(value >> 24U));
}

static u32 sys_calibration_mqtt_parameter_digest(
    sys_calibration_mqtt_operation_en operation,
    u32 first,
    u32 second,
    u32 third)
{
    u32 digest = 2166136261UL;

    digest = sys_calibration_mqtt_digest_u32(digest, (u32)operation);
    digest = sys_calibration_mqtt_digest_u32(digest, first);
    digest = sys_calibration_mqtt_digest_u32(digest, second);
    return sys_calibration_mqtt_digest_u32(digest, third);
}

static void sys_calibration_mqtt_response_init(
    sys_calibration_mqtt_response_st *response,
    sys_calibration_mqtt_operation_en operation,
    const char *operation_text,
    u32 session_id,
    u32 seq,
    const sys_calibration_service_status_st *status)
{
    memset(response, 0, sizeof(*response));
    response->operation = operation;
    response->session_id = session_id;
    response->seq = seq;
    response->result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
    sys_calibration_mqtt_copy_operation(response->operation_text,
                                        operation_text);
    if (status != NULL)
    {
        response->status = *status;
    }
}

static boolean_en sys_calibration_mqtt_add_common_response(
    cJSON *dt,
    const sys_calibration_mqtt_response_st *response)
{
    if (dt == NULL || response == NULL)
    {
        return BOOL_FALSE;
    }
    cJSON_AddNumberToObject(dt, "v", SYS_CALIBRATION_PROTOCOL_VERSION);
    cJSON_AddStringToObject(dt, "op", response->operation_text);
    if (sys_calibration_mqtt_is_sessionless(response->operation) != BOOL_TRUE)
    {
        cJSON_AddNumberToObject(dt, "sid", response->session_id);
        cJSON_AddNumberToObject(dt, "seq", response->seq);
    }
    cJSON_AddNumberToObject(dt, "rc", response->result);
    cJSON_AddNumberToObject(dt, "st", response->status.state);
    return zk_cjson_tx_allocation_ok();
}

static void sys_calibration_mqtt_add_cap(
    cJSON *dt,
    const sys_calibration_mqtt_response_st *response)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    boolean_en has_calibration = response->status.committed_valid;

    cJSON_AddNumberToObject(dt, "profileId", profile->profile_id);
    cJSON_AddNumberToObject(dt, "profileVersion", profile->profile_version);
    cJSON_AddNumberToObject(dt, "profileFingerprint", profile->fingerprint_crc32);
    cJSON_AddNumberToObject(dt, "mid", profile->mid);
    cJSON_AddNumberToObject(dt, "hardwareRevision", profile->hardware_revision);
    cJSON_AddNumberToObject(dt, "ratedPowerW", profile->rated_power_w);
    cJSON_AddNumberToObject(dt, "rs3Mohm", profile->rs3_mohm);
    cJSON_AddNumberToObject(dt, "hardwareMaxMa", profile->hw_max_current_ma);
    cJSON_AddNumberToObject(dt, "hwMaxMa", HWMAX_OUTCUR);
    cJSON_AddNumberToObject(dt, "setOutcurMa", SET_OUTCUR);
    cJSON_AddNumberToObject(dt, "pwmFullScale", profile->pwm_full_scale);
    cJSON_AddNumberToObject(dt, "pwmPolarity", profile->pwm_polarity);
    cJSON_AddNumberToObject(dt, "ocoHardwareRevision",
                            profile->oco_hardware_revision);
    cJSON_AddNumberToObject(dt, "pointCount", SYS_CALIBRATION_POINT_COUNT);
    cJSON_AddNumberToObject(dt, "levelStep", SYS_CALIBRATION_LEVEL_STEP);
    cJSON_AddNumberToObject(dt, "payloadVersion",
                            SYS_CALIBRATION_PAYLOAD_VERSION);
    cJSON_AddNumberToObject(dt, "storageFormatVersion", 4U);
    cJSON_AddNumberToObject(dt, "rawMaxAgeMs",
                            SYS_CALIBRATION_MQTT_RAW_MAX_AGE_MS);
    cJSON_AddNumberToObject(dt, "capabilities",
                            SYS_CALIBRATION_MQTT_CAPABILITIES);
    cJSON_AddNumberToObject(dt, "hasCalibration",
                            has_calibration == BOOL_TRUE ? 1U : 0U);
    cJSON_AddNumberToObject(
        dt, "generation",
        has_calibration == BOOL_TRUE ? response->status.committed_generation : 0U);
    cJSON_AddNumberToObject(
        dt, "payloadLength",
        has_calibration == BOOL_TRUE ? response->status.committed_length : 0U);
    cJSON_AddNumberToObject(
        dt, "payloadCrc32",
        has_calibration == BOOL_TRUE ? response->status.committed_crc32 : 0U);
    cJSON_AddNumberToObject(dt, "safetyReady",
                            response->status.safety_ready == BOOL_TRUE ? 1U : 0U);
    cJSON_AddNumberToObject(
        dt, "persistenceReady",
        response->status.persistence_ready == BOOL_TRUE ? 1U : 0U);
    cJSON_AddNumberToObject(dt, "faultFlags", response->status.fault_flags);
}

static void sys_calibration_mqtt_add_raw(
    cJSON *dt,
    const sys_calibration_raw_st *raw)
{
    cJSON_AddNumberToObject(dt, "level", raw->level);
    cJSON_AddNumberToObject(dt, "actualPwm", raw->actual_pwm);
    cJSON_AddNumberToObject(dt, "ocoRaw", raw->oco_raw);
    cJSON_AddNumberToObject(dt, "blVoltageRaw", raw->bl_voltage_raw);
    cJSON_AddNumberToObject(dt, "blCurrentRaw", raw->bl_current_raw);
    cJSON_AddNumberToObject(dt, "blPowerRaw", raw->bl_power_raw);
    cJSON_AddNumberToObject(dt, "correctedOutputCurrentMa",
                            raw->corrected_output_current_ma);
    cJSON_AddNumberToObject(dt, "correctedInputVoltage01V",
                            raw->corrected_input_voltage_01v);
    cJSON_AddNumberToObject(dt, "correctedInputCurrentMa",
                            raw->corrected_input_current_ma);
    cJSON_AddNumberToObject(dt, "correctedInputPower01W",
                            raw->corrected_input_power_01w);
    cJSON_AddNumberToObject(dt, "outputVoltage01V", raw->output_voltage_01v);
    cJSON_AddNumberToObject(dt, "blAgeMs", raw->bl_age_ms);
    cJSON_AddNumberToObject(dt, "validFlags", raw->valid_flags);
    cJSON_AddNumberToObject(dt, "faultFlags", raw->fault_flags);
}

static void sys_calibration_mqtt_add_diag(
    cJSON *dt,
    const sys_bl0942_diag_st *diag)
{
    cJSON_AddNumberToObject(dt, "validFrameCount", diag->valid_frame_count);
    cJSON_AddNumberToObject(dt, "oreCount", diag->ore_count);
    cJSON_AddNumberToObject(dt, "feCount", diag->fe_count);
    cJSON_AddNumberToObject(dt, "neCount", diag->ne_count);
    cJSON_AddNumberToObject(dt, "timeoutCount", diag->timeout_count);
    cJSON_AddNumberToObject(dt, "uartErrorCount", diag->uart_error_count);
    cJSON_AddNumberToObject(dt, "recoveryCount", diag->recovery_count);
    cJSON_AddNumberToObject(dt, "recoveryFailCount", diag->recovery_fail_count);
    cJSON_AddNumberToObject(dt, "lastValidFrameTick",
                            diag->last_valid_frame_tick);
    cJSON_AddNumberToObject(dt, "blAgeMs", diag->bl_age_ms);
    cJSON_AddNumberToObject(dt, "blFresh", diag->bl_fresh);
    cJSON_AddNumberToObject(dt, "uartGState", diag->uart_g_state);
    cJSON_AddNumberToObject(dt, "uartRxState", diag->uart_rx_state);
    cJSON_AddNumberToObject(dt, "uartErrorCode", diag->uart_error_code);
}

static boolean_en sys_calibration_mqtt_add_operation_response(
    cJSON *dt,
    const sys_calibration_mqtt_response_st *response)
{
    char payload_hex[SYS_CALIBRATION_PAYLOAD_HEX_LENGTH + 1U];

    if (response->operation == SYS_CALIBRATION_MQTT_OP_CAP)
    {
        sys_calibration_mqtt_add_cap(dt, response);
    }
    else if (response->operation == SYS_CALIBRATION_MQTT_OP_DIAG &&
             response->result == SYS_CALIBRATION_RESULT_OK &&
             response->diag_valid == BOOL_TRUE)
    {
        sys_calibration_mqtt_add_diag(dt, &response->diag);
    }
    else if (response->operation == SYS_CALIBRATION_MQTT_OP_RAW &&
             (response->result == SYS_CALIBRATION_RESULT_OK ||
              response->result == SYS_CALIBRATION_RESULT_DATA_STALE ||
              response->result == SYS_CALIBRATION_RESULT_HARDWARE_FAULT) &&
             response->raw_valid == BOOL_TRUE)
    {
        sys_calibration_mqtt_add_raw(dt, &response->raw);
    }
    else if (response->result == SYS_CALIBRATION_RESULT_OK)
    {
        switch (response->operation)
        {
            case SYS_CALIBRATION_MQTT_OP_BEGIN:
                cJSON_AddNumberToObject(dt, "profileId",
                    sys_product_profile_current()->profile_id);
                cJSON_AddNumberToObject(dt, "profileFingerprint",
                    sys_product_profile_current()->fingerprint_crc32);
                cJSON_AddNumberToObject(dt, "leaseMs", response->lease_ms);
                break;
            case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
                cJSON_AddNumberToObject(dt, "leaseMs", response->lease_ms);
                break;
            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                cJSON_AddNumberToObject(dt, "level", response->status.current_level);
                break;
            case SYS_CALIBRATION_MQTT_OP_STAGE:
            case SYS_CALIBRATION_MQTT_OP_APPLY:
                cJSON_AddNumberToObject(dt, "payloadLength",
                                        response->payload_length);
                cJSON_AddNumberToObject(dt, "payloadCrc32",
                                        response->payload_crc32);
                break;
            case SYS_CALIBRATION_MQTT_OP_SET_OUTPUT:
                cJSON_AddNumberToObject(dt, "percent", response->percent);
                cJSON_AddNumberToObject(dt, "actualPwm", response->status.actual_pwm);
                break;
            case SYS_CALIBRATION_MQTT_OP_COMMIT:
                cJSON_AddNumberToObject(dt, "generation", response->generation);
                cJSON_AddNumberToObject(dt, "payloadLength",
                                        response->payload_length);
                cJSON_AddNumberToObject(dt, "payloadCrc32",
                                        response->payload_crc32);
                break;
            case SYS_CALIBRATION_MQTT_OP_READ:
                if (response->payload_valid != BOOL_TRUE ||
                    sys_calibration_payload_hex_encode(
                        response->payload, response->payload_length,
                        payload_hex, (u16)sizeof(payload_hex)) != BOOL_TRUE)
                {
                    return BOOL_FALSE;
                }
                cJSON_AddNumberToObject(dt, "generation", response->generation);
                cJSON_AddNumberToObject(dt, "payloadLength",
                                        response->payload_length);
                cJSON_AddNumberToObject(dt, "payloadCrc32",
                                        response->payload_crc32);
                cJSON_AddStringToObject(dt, "payloadHex", payload_hex);
                break;
            default:
                break;
        }
    }
    return zk_cjson_tx_allocation_ok();
}

static int sys_calibration_mqtt_send_response(
    const zk_message_header_t *header,
    const sys_calibration_mqtt_response_st *response)
{
    cJSON *root;
    cJSON *dt;
    int result;

    root = zk_create_root_from_header(header, 1, (int)response->result);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("cal.v3.DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "DT", dt);
    if (sys_calibration_mqtt_add_common_response(dt, response) != BOOL_TRUE ||
        sys_calibration_mqtt_add_operation_response(dt, response) != BOOL_TRUE ||
        zk_cjson_tx_allocation_ok() != BOOL_TRUE)
    {
        cJSON_Delete(root);
        return -1;
    }
    result = zk_send_json_root(root, NULL);
    cJSON_Delete(root);
    return result;
}

static void sys_calibration_mqtt_cache_response(
    u32 parameter_digest,
    const sys_calibration_mqtt_response_st *response)
{
    _replay.valid = BOOL_TRUE;
    _replay.session_id = response->session_id;
    _replay.seq = response->seq;
    _replay.parameter_digest = parameter_digest;
    _replay.operation = response->operation;
    _replay.response = *response;
}

static u8 sys_calibration_mqtt_replay_check(
    sys_calibration_mqtt_operation_en operation,
    u32 session_id,
    u32 seq,
    u32 parameter_digest)
{
    if (_replay.valid != BOOL_TRUE || _replay.session_id != session_id)
    {
        return 0U;
    }
    if (_replay.seq == seq)
    {
        return (_replay.operation == operation &&
                _replay.parameter_digest == parameter_digest) ? 1U : 2U;
    }
    return (seq < _replay.seq) ? 3U : 0U;
}

static boolean_en sys_calibration_mqtt_session_fields_valid(
    cJSON *dt,
    u32 *session_id,
    u32 *seq)
{
    return (sys_calibration_mqtt_read_u32(dt, "sid", session_id) == BOOL_TRUE &&
            sys_calibration_mqtt_read_u32(dt, "seq", seq) == BOOL_TRUE &&
            *session_id != 0U && *seq != 0U) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en sys_calibration_mqtt_handle(
    cJSON *root,
    const zk_message_header_t *header)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    cJSON *dt;
    cJSON *op_node;
    cJSON *payload_hex_node;
    const char *operation_text = "";
    sys_calibration_mqtt_operation_en operation = SYS_CALIBRATION_MQTT_OP_INVALID;
    sys_calibration_mqtt_response_st *response = &_working_response;
    sys_calibration_service_status_st status;
    sys_calibration_payload_st *decoded_payload = &_working_decoded_payload;
    sys_calibration_result_en result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
    u32 version = 0U;
    u32 session_id = 0U;
    u32 seq = 0U;
    u32 lease_ms = 0U;
    u32 profile_fingerprint = 0U;
    u32 payload_crc32 = 0U;
    u32 generation = 0U;
    u32 parameter_digest = 0U;
    u16 profile_id = 0U;
    u16 level = 0U;
    u16 payload_length = 0U;
    u16 read_length = 0U;
    u8 percent = 0U;
    u8 replay_result;
    boolean_en request_cacheable = BOOL_FALSE;

    if (root == NULL || header == NULL || strcmp(header->sv, ZK_SV_CAL) != 0)
    {
        return BOOL_FALSE;
    }
    memset(&status, 0, sizeof(status));
    status.state = SYS_CALIBRATION_STATE_IDLE;
    (void)sys_calibration_service_get_status(&status);

    dt = cJSON_GetObjectItemCaseSensitive(root, "DT");
    op_node = dt != NULL ?
              cJSON_GetObjectItemCaseSensitive(dt, "op") : NULL;
    if (dt != NULL && cJSON_IsObject(dt) && op_node != NULL &&
        cJSON_IsString(op_node) && op_node->valuestring != NULL)
    {
        operation_text = op_node->valuestring;
        operation = sys_calibration_mqtt_parse_operation(operation_text);
    }
    sys_calibration_mqtt_response_init(response, operation, operation_text,
                                       0U, 0U, &status);

    if (dt == NULL || !cJSON_IsObject(dt) ||
        sys_calibration_mqtt_read_u32(dt, "v", &version) != BOOL_TRUE ||
        version != SYS_CALIBRATION_PROTOCOL_VERSION ||
        operation == SYS_CALIBRATION_MQTT_OP_INVALID ||
        strcmp(header->ct, sys_calibration_mqtt_expected_ct(operation)) != 0 ||
        sys_calibration_mqtt_request_fields_valid(dt, operation) != BOOL_TRUE)
    {
        (void)sys_calibration_mqtt_send_response(header, response);
        return BOOL_TRUE;
    }

    if (sys_calibration_mqtt_is_sessionless(operation) == BOOL_TRUE)
    {
        if (cJSON_GetObjectItemCaseSensitive(dt, "sid") != NULL ||
            cJSON_GetObjectItemCaseSensitive(dt, "seq") != NULL)
        {
            (void)sys_calibration_mqtt_send_response(header, response);
            return BOOL_TRUE;
        }
        response->result = SYS_CALIBRATION_RESULT_OK;
        if (operation == SYS_CALIBRATION_MQTT_OP_DIAG)
        {
            response->diag_valid = sys_bl0942_get_diag(HAL_GetTick(),
                                                        &response->diag);
            if (response->diag_valid != BOOL_TRUE)
            {
                response->result = SYS_CALIBRATION_RESULT_HARDWARE_FAULT;
            }
        }
        (void)sys_calibration_mqtt_send_response(header, response);
        return BOOL_TRUE;
    }

    if (sys_calibration_mqtt_session_fields_valid(
            dt, &session_id, &seq) != BOOL_TRUE)
    {
        (void)sys_calibration_mqtt_send_response(header, response);
        return BOOL_TRUE;
    }
    response->session_id = session_id;
    response->seq = seq;

    switch (operation)
    {
        case SYS_CALIBRATION_MQTT_OP_BEGIN:
            if (sys_calibration_mqtt_read_u16(dt, "profileId", &profile_id) !=
                    BOOL_TRUE ||
                sys_calibration_mqtt_read_u32(
                    dt, "profileFingerprint", &profile_fingerprint) != BOOL_TRUE ||
                sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) !=
                    BOOL_TRUE || seq != 1U)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, profile_id, profile_fingerprint, lease_ms);
            if (lease_ms < SYS_CALIBRATION_LEASE_MIN_MS ||
                lease_ms > SYS_CALIBRATION_LEASE_MAX_MS)
            {
                result = SYS_CALIBRATION_RESULT_RANGE_ERROR;
            }
            else if (profile_id != profile->profile_id ||
                     profile_fingerprint != profile->fingerprint_crc32)
            {
                result = SYS_CALIBRATION_RESULT_PROFILE_MISMATCH;
            }
            else
            {
                result = SYS_CALIBRATION_RESULT_OK;
            }
            response->lease_ms = lease_ms;
            break;
        case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
            if (sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) !=
                BOOL_TRUE)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, lease_ms, 0U, 0U);
            result = (lease_ms >= SYS_CALIBRATION_LEASE_MIN_MS &&
                      lease_ms <= SYS_CALIBRATION_LEASE_MAX_MS) ?
                     SYS_CALIBRATION_RESULT_OK :
                     SYS_CALIBRATION_RESULT_RANGE_ERROR;
            response->lease_ms = lease_ms;
            break;
        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
            if (sys_calibration_mqtt_read_u16(dt, "level", &level) != BOOL_TRUE)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, level, 0U, 0U);
            result = (level <= SYS_CALIBRATION_LEVEL_MAX &&
                      (level % SYS_CALIBRATION_LEVEL_STEP) == 0U) ?
                     SYS_CALIBRATION_RESULT_OK :
                     SYS_CALIBRATION_RESULT_RANGE_ERROR;
            response->level = level;
            break;
        case SYS_CALIBRATION_MQTT_OP_STAGE:
            payload_hex_node = cJSON_GetObjectItemCaseSensitive(
                dt, "payloadHex");
            if (sys_calibration_mqtt_read_u16(
                    dt, "payloadLength", &payload_length) != BOOL_TRUE ||
                sys_calibration_mqtt_read_u32(
                    dt, "payloadCrc32", &payload_crc32) != BOOL_TRUE ||
                payload_hex_node == NULL || !cJSON_IsString(payload_hex_node) ||
                payload_hex_node->valuestring == NULL ||
                payload_length != SYS_CALIBRATION_PAYLOAD_LENGTH ||
                sys_calibration_payload_hex_decode(
                    payload_hex_node->valuestring, response->payload,
                    (u16)sizeof(response->payload)) != BOOL_TRUE)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, payload_length, payload_crc32,
                sys_calibration_payload_crc32_iso_hdlc(
                    response->payload, SYS_CALIBRATION_PAYLOAD_LENGTH));
            if (sys_calibration_payload_crc32_iso_hdlc(
                    response->payload, SYS_CALIBRATION_PAYLOAD_LENGTH) !=
                    payload_crc32)
            {
                result = SYS_CALIBRATION_RESULT_CRC_ERROR;
            }
            else if (sys_calibration_payload_decode(
                         response->payload, SYS_CALIBRATION_PAYLOAD_LENGTH,
                         decoded_payload) != BOOL_TRUE)
            {
                result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
            }
            else if (sys_calibration_payload_validate(decoded_payload) !=
                         BOOL_TRUE ||
                     sys_calibration_payload_within_product_limits(
                         decoded_payload, profile) != BOOL_TRUE)
            {
                result = SYS_CALIBRATION_RESULT_RANGE_ERROR;
            }
            else if (sys_calibration_payload_matches_product(
                         decoded_payload, profile) != BOOL_TRUE)
            {
                result = SYS_CALIBRATION_RESULT_PROFILE_MISMATCH;
            }
            else
            {
                result = SYS_CALIBRATION_RESULT_OK;
            }
            response->payload_length = payload_length;
            response->payload_crc32 = payload_crc32;
            break;
        case SYS_CALIBRATION_MQTT_OP_SET_OUTPUT:
            if (sys_calibration_mqtt_read_u8(dt, "percent", &percent) !=
                BOOL_TRUE)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, percent, 0U, 0U);
            result = percent <= 100U ? SYS_CALIBRATION_RESULT_OK :
                                      SYS_CALIBRATION_RESULT_RANGE_ERROR;
            response->percent = percent;
            break;
        default:
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, 0U, 0U, 0U);
            result = SYS_CALIBRATION_RESULT_OK;
            break;
    }

    if (request_cacheable != BOOL_TRUE)
    {
        response->result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
        (void)sys_calibration_mqtt_send_response(header, response);
        return BOOL_TRUE;
    }

    replay_result = sys_calibration_mqtt_replay_check(
        operation, session_id, seq, parameter_digest);
    if (replay_result == 1U)
    {
        if (_replay.response.status.state != SYS_CALIBRATION_STATE_IDLE)
        {
            (void)sys_calibration_service_timer(HAL_GetTick(), &status);
            if (status.state == SYS_CALIBRATION_STATE_IDLE)
            {
                response->result = SYS_CALIBRATION_RESULT_SESSION_EXPIRED;
                response->status = status;
                sys_calibration_mqtt_cache_response(parameter_digest,
                                                     response);
                (void)sys_calibration_mqtt_send_response(header, response);
                return BOOL_TRUE;
            }
        }
        (void)sys_calibration_mqtt_send_response(header, &_replay.response);
        return BOOL_TRUE;
    }
    if (replay_result == 2U || replay_result == 3U)
    {
        response->result = replay_result == 2U ?
                          SYS_CALIBRATION_RESULT_BAD_REQUEST :
                          SYS_CALIBRATION_RESULT_BAD_STATE;
        (void)sys_calibration_mqtt_send_response(header, response);
        return BOOL_TRUE;
    }

    if (result == SYS_CALIBRATION_RESULT_OK)
    {
        switch (operation)
        {
            case SYS_CALIBRATION_MQTT_OP_BEGIN:
                result = sys_calibration_service_begin_seq(
                    session_id, HAL_GetTick(), lease_ms, seq, profile_id,
                    profile_fingerprint, &response->status);
                break;
            case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
                result = sys_calibration_service_heartbeat_seq(
                    session_id, HAL_GetTick(), lease_ms, seq, &response->status);
                response->lease_ms = response->status.lease_ms;
                break;
            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                result = sys_calibration_service_set_point_seq(
                    session_id, HAL_GetTick(), seq, level, &response->status);
                break;
            case SYS_CALIBRATION_MQTT_OP_RAW:
                result = sys_calibration_service_raw_seq(
                    session_id, HAL_GetTick(), seq, &response->raw,
                    &response->status);
                response->raw_valid =
                    (result == SYS_CALIBRATION_RESULT_OK ||
                     result == SYS_CALIBRATION_RESULT_DATA_STALE ||
                     result == SYS_CALIBRATION_RESULT_HARDWARE_FAULT) ?
                    BOOL_TRUE : BOOL_FALSE;
                break;
            case SYS_CALIBRATION_MQTT_OP_STAGE:
                result = sys_calibration_service_stage_seq(
                    session_id, HAL_GetTick(), seq, response->payload,
                    payload_length, payload_crc32, &response->status);
                break;
            case SYS_CALIBRATION_MQTT_OP_APPLY:
                result = sys_calibration_service_apply_seq(
                    session_id, HAL_GetTick(), seq, &response->status);
                response->payload_length = response->status.staged_length;
                response->payload_crc32 = response->status.staged_crc32;
                break;
            case SYS_CALIBRATION_MQTT_OP_SET_OUTPUT:
                result = sys_calibration_service_set_output_seq(
                    session_id, HAL_GetTick(), seq, percent, &response->status);
                break;
            case SYS_CALIBRATION_MQTT_OP_COMMIT:
                result = sys_calibration_service_commit_seq(
                    session_id, HAL_GetTick(), seq, &response->status);
                response->generation = response->status.committed_generation;
                response->payload_length = response->status.committed_length;
                response->payload_crc32 = response->status.committed_crc32;
                break;
            case SYS_CALIBRATION_MQTT_OP_READ:
                result = sys_calibration_service_read_seq(
                    session_id, HAL_GetTick(), seq, response->payload,
                    (u16)sizeof(response->payload), &read_length,
                    &payload_crc32, &generation, &response->status);
                response->payload_length = read_length;
                response->payload_crc32 = payload_crc32;
                response->generation = generation;
                response->payload_valid = result == SYS_CALIBRATION_RESULT_OK ?
                                         BOOL_TRUE : BOOL_FALSE;
                break;
            case SYS_CALIBRATION_MQTT_OP_ABORT:
                result = sys_calibration_service_abort_seq(
                    session_id, HAL_GetTick(), seq, &response->status);
                break;
            case SYS_CALIBRATION_MQTT_OP_RELEASE:
                result = sys_calibration_service_release_seq(
                    session_id, HAL_GetTick(), seq, &response->status);
                break;
            default:
                result = SYS_CALIBRATION_RESULT_BAD_REQUEST;
                break;
        }
    }
    response->result = result;
    sys_calibration_mqtt_cache_response(parameter_digest, response);
    (void)sys_calibration_mqtt_send_response(header, response);
    return BOOL_TRUE;
}
