/*************************************************************
程序功能：SV=cal隔离校准会话与结果回读接口
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：Codex
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_mqtt.h"
#include "zk_protocol_internal.h"
#include "sys_calibration_service.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_curve.h"
#include "sys_calibration_safety.h"
#include "main.h"

#include <string.h>

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
    node = cJSON_GetObjectItem(object, key);
    if (node == NULL || !cJSON_IsNumber(node))
    {
        return BOOL_FALSE;
    }
    number = cJSON_GetNumberValue(node);
    if (number < 0.0 || number > 4294967295.0 ||
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

static boolean_en sys_calibration_mqtt_read_payload(
    cJSON *object,
    const char *key,
    u8 *payload,
    u16 capacity,
    u16 *length)
{
    cJSON *array;
    cJSON *item;
    const char *hex;
    int count;
    int index;
    double number;

    if (object == NULL || key == NULL || payload == NULL || length == NULL)
    {
        return BOOL_FALSE;
    }
    array = cJSON_GetObjectItem(object, key);
    if (array == NULL)
    {
        return BOOL_FALSE;
    }
    if (cJSON_IsString(array) && array->valuestring != NULL)
    {
        u16 hex_length = (u16)strlen(array->valuestring);
        u16 byte_length;
        u16 hex_index;
        u8 high;
        u8 low;

        if (hex_length == 0U || (hex_length & 1U) != 0U ||
            hex_length > (u16)(capacity * 2U))
        {
            return BOOL_FALSE;
        }
        byte_length = (u16)(hex_length / 2U);
        for (hex_index = 0U; hex_index < hex_length; hex_index += 2U)
        {
            hex = array->valuestring;
            if (hex[hex_index] >= '0' && hex[hex_index] <= '9')
            {
                high = (u8)(hex[hex_index] - '0');
            }
            else if (hex[hex_index] >= 'A' && hex[hex_index] <= 'F')
            {
                high = (u8)(hex[hex_index] - 'A' + 10U);
            }
            else if (hex[hex_index] >= 'a' && hex[hex_index] <= 'f')
            {
                high = (u8)(hex[hex_index] - 'a' + 10U);
            }
            else
            {
                return BOOL_FALSE;
            }
            if (hex[hex_index + 1U] >= '0' && hex[hex_index + 1U] <= '9')
            {
                low = (u8)(hex[hex_index + 1U] - '0');
            }
            else if (hex[hex_index + 1U] >= 'A' &&
                     hex[hex_index + 1U] <= 'F')
            {
                low = (u8)(hex[hex_index + 1U] - 'A' + 10U);
            }
            else if (hex[hex_index + 1U] >= 'a' &&
                     hex[hex_index + 1U] <= 'f')
            {
                low = (u8)(hex[hex_index + 1U] - 'a' + 10U);
            }
            else
            {
                return BOOL_FALSE;
            }
            payload[hex_index / 2U] = (u8)((high << 4U) | low);
        }
        *length = byte_length;
        return BOOL_TRUE;
    }
    if (!cJSON_IsArray(array))
    {
        return BOOL_FALSE;
    }
    count = cJSON_GetArraySize(array);
    if (count <= 0 || (u32)count > capacity)
    {
        return BOOL_FALSE;
    }
    for (index = 0; index < count; ++index)
    {
        item = cJSON_GetArrayItem(array, index);
        if (item == NULL || !cJSON_IsNumber(item))
        {
            return BOOL_FALSE;
        }
        number = cJSON_GetNumberValue(item);
        if (number < 0.0 || number > 255.0 ||
            number != (double)(u8)number)
        {
            return BOOL_FALSE;
        }
        payload[index] = (u8)number;
    }
    *length = (u16)count;
    return BOOL_TRUE;
}

static void sys_calibration_mqtt_add_payload(cJSON *dt)
{
    u8 payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
    char hex[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH * 2U + 1U];
    static const char digits[] = "0123456789ABCDEF";
    u16 length;
    u16 index;

    if (dt == NULL || sys_calibration_service_get_staged_payload(
                          payload, sizeof(payload), &length) != BOOL_TRUE)
    {
        return;
    }
    for (index = 0U; index < length; ++index)
    {
        hex[index * 2U] = digits[payload[index] >> 4U];
        hex[index * 2U + 1U] = digits[payload[index] & 0x0FU];
    }
    hex[length * 2U] = '\0';
    cJSON_AddStringToObject(dt, "payload", hex);
}

static void sys_calibration_mqtt_add_status(
    cJSON *dt,
    const sys_calibration_service_status_st *status)
{
    cJSON *readback;

    if (dt == NULL || status == NULL)
    {
        return;
    }
    readback = zk_cjson_create_tx_object("cal.readback");
    if (readback == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(readback, "state", status->state);
    cJSON_AddNumberToObject(readback, "sessionId", status->session_id);
    cJSON_AddNumberToObject(readback, "leaseDeadlineMs", status->lease_deadline_ms);
    cJSON_AddNumberToObject(readback, "lastSeq", status->last_request_seq);
    cJSON_AddNumberToObject(readback, "currentLevel", status->current_level);
    cJSON_AddNumberToObject(readback, "payloadLength", status->staged_length);
    cJSON_AddNumberToObject(readback, "payloadCrc32", status->staged_crc32);
    cJSON_AddBoolToObject(readback, "staged", status->staged_valid == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "safetyReady", status->safety_ready == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "bootInhibit",
                          status->boot_inhibit_active == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "commitAvailable",
                          status->commit_available == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "nonzeroOutputAllowed",
                          status->nonzero_output_allowed == BOOL_TRUE);
    cJSON_AddItemToObject(dt, "readback", readback);
    sys_calibration_mqtt_add_payload(dt);
}

static int sys_calibration_mqtt_send_response(
    const zk_message_header_t *request,
    const char *operation,
    sys_calibration_result_en result,
    const sys_calibration_service_status_st *status,
    boolean_en capabilities,
    u32 seq)
{
    cJSON *root;
    cJSON *dt;
    int send_result;

    root = zk_create_root_from_header(request, 1, (int)result);
    if (root == NULL)
    {
        return -1;
    }
    dt = zk_cjson_create_tx_object("cal.DT");
    if (dt == NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "DT", dt);
    cJSON_AddStringToObject(dt, "op", operation == NULL ? "" : operation);
    cJSON_AddNumberToObject(dt, "seq", seq);
    cJSON_AddNumberToObject(dt, "result", (int)result);
    cJSON_AddBoolToObject(dt, "ack", result == SYS_CALIBRATION_RESULT_OK);
    if (capabilities == BOOL_TRUE)
    {
        cJSON_AddNumberToObject(dt, "protocolVersion",
                                SYS_CALIBRATION_DRIVER_PROTOCOL_VERSION);
        cJSON_AddBoolToObject(dt, "protocolFrozen",
                              SYS_CALIBRATION_PROTOCOL_FROZEN != 0U);
        cJSON_AddBoolToObject(dt, "codecAvailable",
                              SYS_CALIBRATION_CODEC_AVAILABLE != 0U);
        cJSON_AddNumberToObject(dt, "tablePoints",
                                SYS_CALIBRATION_DRIVER_POINT_COUNT);
        cJSON_AddNumberToObject(dt, "levelStep",
                                SYS_CALIBRATION_DRIVER_LEVEL_STEP);
        cJSON_AddNumberToObject(dt, "mid", SYS_CALIBRATION_50W_MID);
        cJSON_AddNumberToObject(dt, "rs3Mohm", SYS_CALIBRATION_50W_RS3_MOHM);
        cJSON_AddNumberToObject(dt, "ratedCurrentMa",
                                SYS_CALIBRATION_50W_RATED_CURRENT_MA);
        cJSON_AddNumberToObject(dt, "hardFailCurrentMa",
                                SYS_CALIBRATION_ABSOLUTE_FAIL_CURRENT_MA);
        cJSON_AddBoolToObject(dt, "commitAvailable",
                              SYS_CALIBRATION_FLASH_COMMIT_ENABLED != 0U);
        cJSON_AddBoolToObject(dt, "nonzeroOutputAllowed",
                              SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED != 0U);
        cJSON_AddStringToObject(dt, "commitGate", "SHARED_2K_PAGE_MAP_HIL");
    }
    sys_calibration_mqtt_add_status(dt, status);
    send_result = zk_send_json_root(root, NULL);
    cJSON_Delete(root);
    return send_result;
}

boolean_en sys_calibration_mqtt_handle(
    cJSON *root,
    const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *op_node;
    const char *operation;
    u32 session_id = 0U;
    u32 seq = 0U;
    u32 lease_ms;
    u16 level;
    u16 length;
    u8 payload[SYS_CALIBRATION_DRIVER_TABLE_FRAME_LENGTH];
    sys_calibration_raw_direction_en direction;
    sys_calibration_result_en result;
    sys_calibration_service_status_st status;
    boolean_en capabilities = BOOL_FALSE;

    if (root == NULL || header == NULL || strcmp(header->sv, ZK_SV_CAL) != 0)
    {
        return BOOL_FALSE;
    }
    dt = cJSON_GetObjectItem(root, "DT");
    op_node = (dt != NULL) ? cJSON_GetObjectItem(dt, "op") : NULL;
    if (dt == NULL || !cJSON_IsObject(dt) || op_node == NULL ||
        !cJSON_IsString(op_node) || op_node->valuestring == NULL)
    {
        (void)sys_calibration_service_get_status(&status);
        (void)sys_calibration_mqtt_send_response(
            header, "", SYS_CALIBRATION_RESULT_PROTOCOL_ERROR, &status,
            BOOL_FALSE, 0U);
        return BOOL_TRUE;
    }
    operation = op_node->valuestring;
    (void)sys_calibration_service_get_status(&status);

    if (strcmp(operation, "CAPABILITIES") == 0)
    {
        capabilities = BOOL_TRUE;
        result = SYS_CALIBRATION_RESULT_OK;
    }
    else if (sys_calibration_mqtt_read_u32(dt, "sessionId", &session_id) !=
                 BOOL_TRUE ||
             sys_calibration_mqtt_read_u32(dt, "seq", &seq) != BOOL_TRUE)
    {
        result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    else if (strcmp(operation, "BEGIN") == 0)
    {
        if (sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_begin_seq(
                session_id, HAL_GetTick(), lease_ms, seq, &status);
        }
    }
    else if (strcmp(operation, "HEARTBEAT") == 0)
    {
        if (sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_heartbeat_seq(
                session_id, HAL_GetTick(), lease_ms, seq, &status);
        }
    }
    else if (strcmp(operation, "SET_POINT") == 0)
    {
        if (sys_calibration_mqtt_read_u16(dt, "level", &level) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_set_point_seq(
                session_id, HAL_GetTick(), seq, level, &status);
        }
    }
    else if (strcmp(operation, "RAW") == 0)
    {
        u16 direction_value = 0U;

        if (sys_calibration_mqtt_read_payload(
                dt, "frame", payload, sizeof(payload), &length) != BOOL_TRUE ||
            sys_calibration_mqtt_read_u16(dt, "direction", &direction_value) !=
                BOOL_TRUE || direction_value > 1U)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            direction = (sys_calibration_raw_direction_en)direction_value;
            result = sys_calibration_service_raw_seq(
                session_id, HAL_GetTick(), seq, payload, length, direction,
                &status);
        }
    }
    else if (strcmp(operation, "STAGE_CONFIG") == 0)
    {
        if (sys_calibration_mqtt_read_payload(
                dt, "payload", payload, sizeof(payload), &length) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_stage_config_seq(
                session_id, HAL_GetTick(), seq, payload, length, &status);
        }
    }
    else if (strcmp(operation, "APPLY") == 0)
    {
        result = sys_calibration_service_apply_seq(
            session_id, HAL_GetTick(), seq, &status);
    }
    else if (strcmp(operation, "READBACK") == 0)
    {
        result = sys_calibration_service_readback_seq(
            session_id, HAL_GetTick(), seq, &status);
    }
    else if (strcmp(operation, "COMMIT") == 0)
    {
        result = sys_calibration_service_commit_seq(
            session_id, HAL_GetTick(), seq, &status);
    }
    else if (strcmp(operation, "ABORT") == 0)
    {
        result = sys_calibration_service_abort_seq(
            session_id, HAL_GetTick(), seq, &status);
    }
    else if (strcmp(operation, "RELEASE") == 0)
    {
        result = sys_calibration_service_release_seq(
            session_id, HAL_GetTick(), seq, &status);
    }
    else
    {
        result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }

    /* 结果先由service缓存，发布忙时同一sessionId+seq可重发相同结果和回读。 */
    if (sys_calibration_mqtt_send_response(
            header, operation, result, &status, capabilities, seq) != 0)
    {
        /* 消费已完成；下一次相同seq请求会重新尝试发布缓存结果。 */
        return BOOL_TRUE;
    }
    return BOOL_TRUE;
}
