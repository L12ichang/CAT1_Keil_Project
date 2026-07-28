#include "zk_calibration_property.h"
#include "zk_protocol_internal.h"
#include "current_calibration.h"
#include "current_cal_storage.h"
#include "factory_user_data.h"
#include "meter_runtime.h"

typedef struct
{
    const char *node_name;
    const char *action;
    const char *session_id;
    u32 seq;
    current_cal_result_en result;
} zk_cal_response_ctx_t;

static boolean_en zk_cal_json_u32(cJSON *object, const char *name, u32 *value)
{
    cJSON *item;
    double number;
    u32 converted;

    item = cJSON_GetObjectItem(object, name);
    if (item == NULL || !cJSON_IsNumber(item) || value == NULL)
    {
        return BOOL_FALSE;
    }
    number = item->valuedouble;
    if (number < 0.0 || number > 4294967295.0)
    {
        return BOOL_FALSE;
    }
    converted = (u32)number;
    if ((double)converted != number)
    {
        return BOOL_FALSE;
    }
    *value = converted;
    return BOOL_TRUE;
}

static boolean_en zk_cal_json_u16(cJSON *object, const char *name, u16 *value)
{
    u32 number;

    if (zk_cal_json_u32(object, name, &number) != BOOL_TRUE || number > 65535UL)
    {
        return BOOL_FALSE;
    }
    *value = (u16)number;
    return BOOL_TRUE;
}

static boolean_en zk_cal_json_u8(cJSON *object, const char *name, u8 *value)
{
    u32 number;

    if (zk_cal_json_u32(object, name, &number) != BOOL_TRUE || number > 255UL)
    {
        return BOOL_FALSE;
    }
    *value = (u8)number;
    return BOOL_TRUE;
}

static boolean_en zk_cal_json_context_crc(cJSON *object, u32 *value)
{
    cJSON *context_item;
    cJSON *profile_item;
    u32 context_crc;
    u32 profile_crc;

    context_item = cJSON_GetObjectItem(object, "contextCrc");
    profile_item = cJSON_GetObjectItem(object, "profileCrc");
    if (context_item == NULL && profile_item == NULL)
    {
        return BOOL_FALSE;
    }
    if (context_item != NULL &&
        zk_cal_json_u32(object, "contextCrc", &context_crc) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (profile_item != NULL &&
        zk_cal_json_u32(object, "profileCrc", &profile_crc) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    if (context_item != NULL && profile_item != NULL && context_crc != profile_crc)
    {
        return BOOL_FALSE;
    }
    *value = (context_item != NULL) ? context_crc : profile_crc;
    return BOOL_TRUE;
}

static u32 zk_cal_hash_bytes(u32 hash, const u8 *data, u32 length)
{
    u32 i;

    for (i = 0U; i < length; ++i)
    {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash;
}

static u32 zk_cal_command_digest(cJSON *calibration)
{
    static const char *numeric_names[] = {
        "contextCrc", "profileCrc", "timeoutSec", "pointIndex", "targetPercent",
        "logicalPwm", "curveVersion", "curveCrc", "startIndex", "percent",
        "calibrationMaxCurrentMa", "reason", "meterVersion",
        "meterDataCrc", "startOffset"
    };
    cJSON *item;
    cJSON *values;
    const char *text;
    u32 hash;
    u32 value;
    int i;
    int count;

    hash = 2166136261UL;
    item = cJSON_GetObjectItem(calibration, "action");
    text = (item != NULL && cJSON_IsString(item)) ? item->valuestring : "";
    hash = zk_cal_hash_bytes(hash, (const u8 *)text, strlen(text));
    item = cJSON_GetObjectItem(calibration, "sessionId");
    text = (item != NULL && cJSON_IsString(item)) ? item->valuestring : "";
    hash = zk_cal_hash_bytes(hash, (const u8 *)text, strlen(text));

    count = (int)(sizeof(numeric_names) / sizeof(numeric_names[0]));
    for (i = 0; i < count; ++i)
    {
        item = cJSON_GetObjectItem(calibration, numeric_names[i]);
        value = 0xffffffffUL;
        if (item != NULL && cJSON_IsNumber(item) && item->valuedouble >= 0.0 &&
            item->valuedouble <= 4294967295.0)
        {
            value = (u32)item->valuedouble;
            if ((double)value != item->valuedouble)
            {
                value = 0xffffffffUL;
            }
        }
        hash = zk_cal_hash_bytes(hash, (const u8 *)&value, sizeof(value));
    }
    values = cJSON_GetObjectItem(calibration, "values");
    if (values != NULL && cJSON_IsArray(values))
    {
        count = cJSON_GetArraySize(values);
        for (i = 0; i < count; ++i)
        {
            item = cJSON_GetArrayItem(values, i);
            value = 0xffffffffUL;
            if (item != NULL && cJSON_IsNumber(item) &&
                item->valuedouble >= 0.0 &&
                item->valuedouble <= 4294967295.0)
            {
                value = (u32)item->valuedouble;
                if ((double)value != item->valuedouble)
                {
                    value = 0xffffffffUL;
                }
            }
            hash = zk_cal_hash_bytes(hash, (const u8 *)&value, sizeof(value));
        }
    }
    return hash;
}

static const char *zk_cal_curve_state(const current_cal_status_t *status)
{
    if (status->state == CAL_STATE_COMMITTED)
    {
        return "VALID";
    }
    if (status->state == CAL_STATE_TEMP_APPLIED)
    {
        return "TEMP_APPLIED";
    }
    if (status->pending_valid == BOOL_TRUE)
    {
        return "PENDING";
    }
    if (status->received_bitmap != 0U)
    {
        return "RECEIVING";
    }
    if (status->active_curve_valid == BOOL_TRUE)
    {
        return "VALID";
    }
    return "EMPTY";
}

static int zk_cal_build_response(cJSON *dt, void *context)
{
    zk_cal_response_ctx_t *response;
    current_cal_status_t status;
    meter_runtime_calibration_snapshot_t meter;
    cJSON *node;

    response = (zk_cal_response_ctx_t *)context;
    if (dt == NULL || response == NULL)
    {
        return -1;
    }
    current_calibration_get_status(&status);
    node = zk_cjson_create_tx_object(response->node_name);
    if (node == NULL)
    {
        return -1;
    }
    cJSON_AddItemToObject(dt, response->node_name, node);
    cJSON_AddStringToObject(node, "action", response->action);
    if (response->session_id != NULL && response->session_id[0] != '\0')
    {
        cJSON_AddStringToObject(node, "sessionId", response->session_id);
    }
    cJSON_AddNumberToObject(node, "seq", (double)response->seq);
    cJSON_AddNumberToObject(node, "result", (double)response->result);
    cJSON_AddStringToObject(node, "state", current_calibration_state_name(status.state));
    cJSON_AddNumberToObject(node, "contextCrc", (double)status.context_crc);
    if (strcmp(response->node_name, "CalibrationMeterSample") == 0)
    {
        (void)meter_runtime_get_calibration_snapshot(&meter);
        cJSON_AddNumberToObject(node, "runtimeMode", meter.mode);
        cJSON_AddNumberToObject(node, "coefficientResult",
                                meter.coefficient_result);
        cJSON_AddNumberToObject(node, "storageMeterStatus",
                                meter.storage_status);
        cJSON_AddNumberToObject(node, "inputVoltageRaw",
                                (double)meter.input_voltage_raw);
        cJSON_AddNumberToObject(node, "inputCurrentRaw",
                                (double)meter.input_current_raw);
        cJSON_AddNumberToObject(node, "inputFastCurrentRaw",
                                (double)meter.input_fast_current_raw);
        cJSON_AddNumberToObject(node, "inputWattRaw24",
                                (double)meter.input_watt_raw24);
        cJSON_AddNumberToObject(node, "inputWattSigned",
                                (double)meter.input_watt_signed);
        cJSON_AddNumberToObject(node, "inputPeriodRaw",
                                (double)meter.input_period_raw);
        cJSON_AddNumberToObject(node, "inputCfRaw24",
                                (double)meter.input_cf_raw24);
        cJSON_AddNumberToObject(node, "inputStatus", meter.input_status);
        cJSON_AddNumberToObject(node, "inputSequence",
                                (double)meter.input_sequence);
        cJSON_AddNumberToObject(node, "inputTick", (double)meter.input_tick);
        cJSON_AddNumberToObject(node, "inputAgeMs",
                                (double)meter.input_age_ms);
        cJSON_AddNumberToObject(node, "inputValid", meter.input_valid);
        cJSON_AddNumberToObject(node, "outputVoltageRaw",
                                (double)meter.output_voltage_raw);
        cJSON_AddNumberToObject(node, "outputCurrentRaw",
                                (double)meter.output_current_raw);
        cJSON_AddNumberToObject(node, "outputProtectCode",
                                meter.output_protect_code);
        cJSON_AddNumberToObject(node, "outputSequence",
                                (double)meter.output_sequence);
        cJSON_AddNumberToObject(node, "outputTick",
                                (double)meter.output_tick);
        cJSON_AddNumberToObject(node, "outputAgeMs",
                                (double)meter.output_age_ms);
        cJSON_AddNumberToObject(node, "outputValid", meter.output_valid);
        return 0;
    }
    cJSON_AddNumberToObject(node, "profileCrc", (double)status.context_crc);
    cJSON_AddNumberToObject(node, "legacyProfileCrc", (double)status.legacy_profile_crc);
    cJSON_AddNumberToObject(node, "curveCrc", (double)status.curve_crc);
    cJSON_AddNumberToObject(node, "curveVersion", status.curve_version);
    cJSON_AddNumberToObject(node, "requiredCurveVersion", CURRENT_CAL_CURVE_VERSION);
    cJSON_AddNumberToObject(node, "storageFormatVersion", 2);
    cJSON_AddNumberToObject(node, "pwmLogicalMax", (double)current_cal_pwm_logical_max());
    cJSON_AddNumberToObject(node, "sid", SID);
    cJSON_AddNumberToObject(node, "mid", MID);
    cJSON_AddNumberToObject(node, "driverVersion", DRV_VERSION);
    cJSON_AddNumberToObject(node, "ratedCurrentMa", SET_OUTCUR);
    cJSON_AddNumberToObject(node, "calibrationMaxCurrentMa",
                            (double)status.calibration_max_current_ma);
    cJSON_AddNumberToObject(node, "calMaxCurrentMa",
                            (double)status.calibration_max_current_ma);
    cJSON_AddNumberToObject(node, "hardwareMaxCurrentMa", HWMAX_OUTCUR);
    cJSON_AddNumberToObject(node, "outputCurrentSensorMohm", OUTPUT_CUR_SENSOR);
    cJSON_AddNumberToObject(node, "pwmOffset", OP_PWM_OFFSET);
    cJSON_AddNumberToObject(node, "setOutCurrentMa", SET_OUTCUR);
    cJSON_AddNumberToObject(node, "hwMaxOutCurrentMa", HWMAX_OUTCUR);
    cJSON_AddNumberToObject(node, "opPwmOffset", OP_PWM_OFFSET);
    cJSON_AddNumberToObject(node, "pointCount", CURRENT_CAL_POINT_COUNT);
    cJSON_AddStringToObject(node, "curveState", zk_cal_curve_state(&status));
    cJSON_AddNumberToObject(node, "storageSequence", (double)status.storage_sequence);
    cJSON_AddNumberToObject(node, "receivedBitmap", (double)status.received_bitmap);
    cJSON_AddNumberToObject(node, "missingBitmap", (double)status.missing_bitmap);
    cJSON_AddNumberToObject(node, "receivedCount", status.received_count);
    cJSON_AddNumberToObject(node, "curveValid", status.pending_valid);
    cJSON_AddNumberToObject(node, "activeCurveValid", status.active_curve_valid);
    cJSON_AddNumberToObject(node, "stored", status.active_curve_valid);
    cJSON_AddNumberToObject(node, "committedThisSession",
                            status.state == CAL_STATE_COMMITTED);
    cJSON_AddNumberToObject(node, "pointIndex", status.point_index);
    cJSON_AddNumberToObject(node, "targetPercent", status.target_percent);
    cJSON_AddNumberToObject(node, "lastError", status.last_error);
    cJSON_AddNumberToObject(node, "logicalPwm", status.pwm.applied_logical_pwm);
    cJSON_AddNumberToObject(node, "requestedLogicalPwm", status.pwm.requested_logical_pwm);
    cJSON_AddNumberToObject(node, "compareValue", status.pwm.compare_value);
    cJSON_AddNumberToObject(node, "outputEnabled", status.pwm.output_enabled);
    cJSON_AddNumberToObject(node, "outputLimited", status.pwm.limited);
    cJSON_AddNumberToObject(node, "protectCode", status.pwm.protect_code);
    cJSON_AddNumberToObject(node, "timeoutRemainingMs", (double)status.timeout_remaining_ms);
    cJSON_AddNumberToObject(node, "requiredMeterVersion",
                            METER_CAL_COEFFICIENT_VERSION);
    cJSON_AddNumberToObject(node, "meterPayloadSize",
                            METER_CAL_COEFFICIENT_SERIALIZED_SIZE);
    cJSON_AddNumberToObject(node, "meterChunkMax", 32);
    cJSON_AddNumberToObject(node, "meterVersion", status.meter_version);
    cJSON_AddNumberToObject(node, "meterDataCrc",
                            (double)status.meter_data_crc);
    cJSON_AddNumberToObject(node, "meterReceivedCount",
                            status.meter_received_count);
    cJSON_AddNumberToObject(node, "meterMissingCount",
                            status.meter_missing_count);
    cJSON_AddNumberToObject(node, "meterComplete", status.meter_complete);
    cJSON_AddNumberToObject(node, "meterValidated", status.meter_validated);
    cJSON_AddNumberToObject(node, "meterValidationResult",
                            status.meter_validation_result);
    cJSON_AddNumberToObject(node, "storageMeterStatus",
                            status.meter_storage_status);
    cJSON_AddNumberToObject(node, "runtimeMode", status.meter_runtime_mode);
    cJSON_AddNumberToObject(node, "coefficientResult",
                            status.meter_runtime_coefficient_result);
    return 0;
}

static void zk_cal_publish(const zk_message_header_t *header,
                           const char *node_name,
                           const char *action,
                           const char *session_id,
                           u32 seq,
                           current_cal_result_en result)
{
    zk_cal_response_ctx_t response;

    response.node_name = node_name;
    response.action = action;
    response.session_id = session_id;
    response.seq = seq;
    response.result = result;
    (void)zk_publish_response_with_dt(header, 0, zk_cal_build_response, &response);
}

static current_cal_action_en zk_cal_action_id(const char *action)
{
    if (strcmp(action, "enter") == 0) return CAL_ACTION_ENTER;
    if (strcmp(action, "setPwm") == 0) return CAL_ACTION_SET_PWM;
    if (strcmp(action, "readStatus") == 0) return CAL_ACTION_READ_STATUS;
    if (strcmp(action, "writeCurveChunk") == 0) return CAL_ACTION_WRITE_CURVE_CHUNK;
    if (strcmp(action, "readCurveStatus") == 0) return CAL_ACTION_READ_CURVE_STATUS;
    if (strcmp(action, "applyTemporary") == 0) return CAL_ACTION_APPLY_TEMPORARY;
    if (strcmp(action, "setTestPercent") == 0) return CAL_ACTION_SET_TEST_PERCENT;
    if (strcmp(action, "commit") == 0) return CAL_ACTION_COMMIT;
    if (strcmp(action, "abort") == 0) return CAL_ACTION_ABORT;
    if (strcmp(action, "exit") == 0) return CAL_ACTION_EXIT;
    if (strcmp(action, "readMeterInfo") == 0) return CAL_ACTION_READ_METER_INFO;
    if (strcmp(action, "readMeterSample") == 0) return CAL_ACTION_READ_METER_SAMPLE;
    if (strcmp(action, "readMeterStatus") == 0) return CAL_ACTION_READ_METER_STATUS;
    if (strcmp(action, "writeMeterChunk") == 0) return CAL_ACTION_WRITE_METER_CHUNK;
    if (strcmp(action, "commitMeter") == 0) return CAL_ACTION_COMMIT_METER;
    if (strcmp(action, "beginMeter") == 0) return CAL_ACTION_BEGIN_METER;
    return (current_cal_action_en)0;
}

static boolean_en zk_cal_action_is_read(const char *action)
{
    return (strcmp(action, "readInfo") == 0 ||
            strcmp(action, "readStatus") == 0 ||
            strcmp(action, "readCurveStatus") == 0 ||
            strcmp(action, "readMeterInfo") == 0 ||
            strcmp(action, "readMeterSample") == 0 ||
            strcmp(action, "readMeterStatus") == 0) ? BOOL_TRUE : BOOL_FALSE;
}

boolean_en zk_handle_calibration_property(cJSON *root, const zk_message_header_t *header)
{
    cJSON *dt;
    cJSON *calibration;
    cJSON *item;
    cJSON *values_node;
    const char *action;
    const char *session_id;
    const char *response_node;
    current_cal_action_en action_id;
    current_cal_result_en result;
    boolean_en duplicate;
    u32 seq;
    u32 digest;
    u32 profile_crc;
    u32 curve_crc;
    u32 meter_data_crc;
    u32 calibration_max_current_ma;
    u16 timeout_sec;
    u16 logical_pwm;
    u16 curve_version;
    u16 meter_version;
    u16 values[7];
    u8 meter_values[32];
    u8 point_index;
    u8 target_percent;
    u8 start_index;
    u8 start_offset;
    u8 percent;
    int value_count;
    int i;

    if (root == NULL || header == NULL || strcmp(header->sv, ZK_SV_PROP) != 0)
    {
        return BOOL_FALSE;
    }
    dt = cJSON_GetObjectItem(root, "DT");
    calibration = (dt != NULL) ? cJSON_GetObjectItem(dt, "Calibration") : NULL;
    if (calibration == NULL)
    {
        return BOOL_FALSE;
    }
    if (!cJSON_IsObject(calibration))
    {
        zk_cal_publish(header, "CalibrationAck", "", "", 0U, CAL_INVALID_PARAM);
        return BOOL_TRUE;
    }
    item = cJSON_GetObjectItem(calibration, "action");
    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL)
    {
        zk_cal_publish(header, "CalibrationAck", "", "", 0U, CAL_INVALID_ACTION);
        return BOOL_TRUE;
    }
    action = item->valuestring;
    if ((zk_cal_action_is_read(action) == BOOL_TRUE && strcmp(header->ct, ZK_CT_READ) != 0) ||
        (zk_cal_action_is_read(action) != BOOL_TRUE && strcmp(header->ct, ZK_CT_WRITE) != 0))
    {
        zk_cal_publish(header, "CalibrationAck", action, "", 0U, CAL_INVALID_ACTION);
        return BOOL_TRUE;
    }
    if (strcmp(action, "readInfo") == 0 ||
        strcmp(action, "readMeterInfo") == 0)
    {
        const char *info_node;

        info_node = (strcmp(action, "readMeterInfo") == 0) ?
                    "CalibrationMeterInfo" : "CalibrationInfo";
        if (zk_cal_json_u32(calibration, "seq", &seq) != BOOL_TRUE || seq == 0U)
        {
            zk_cal_publish(header, info_node, action, "", 0U,
                           CAL_INVALID_PARAM);
        }
        else
        {
            zk_cal_publish(header, info_node, action, "", seq, CAL_OK);
        }
        return BOOL_TRUE;
    }

    item = cJSON_GetObjectItem(calibration, "sessionId");
    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0' || strlen(item->valuestring) > CURRENT_CAL_SESSION_ID_MAX ||
        zk_cal_json_u32(calibration, "seq", &seq) != BOOL_TRUE || seq == 0U)
    {
        zk_cal_publish(header, "CalibrationAck", action, "", 0U, CAL_INVALID_PARAM);
        return BOOL_TRUE;
    }
    session_id = item->valuestring;
    digest = zk_cal_command_digest(calibration);
    action_id = zk_cal_action_id(action);
    if (action_id == 0)
    {
        zk_cal_publish(header, "CalibrationAck", action, session_id, seq, CAL_INVALID_ACTION);
        return BOOL_TRUE;
    }

    response_node = "CalibrationAck";
    if (action_id == CAL_ACTION_READ_STATUS) response_node = "CalibrationStatus";
    if (action_id == CAL_ACTION_READ_CURVE_STATUS) response_node = "CalibrationCurveStatus";
    if (action_id == CAL_ACTION_READ_METER_SAMPLE) response_node = "CalibrationMeterSample";
    if (action_id == CAL_ACTION_READ_METER_STATUS) response_node = "CalibrationMeterStatus";

    duplicate = BOOL_FALSE;
    if (action_id == CAL_ACTION_ENTER)
    {
        if (current_calibration_is_active() == BOOL_TRUE)
        {
            result = current_calibration_prepare_command(session_id, seq, action_id, digest, &duplicate);
            if (duplicate == BOOL_TRUE)
            {
                zk_cal_publish(header, response_node, action, session_id, seq, result);
                return BOOL_TRUE;
            }
            if (result != CAL_OK)
            {
                zk_cal_publish(header, response_node, action, session_id, seq, result);
                return BOOL_TRUE;
            }
        }
        timeout_sec = 0U;
        if (zk_cal_json_context_crc(calibration, &profile_crc) != BOOL_TRUE)
        {
            result = CAL_INVALID_PARAM;
        }
        else
        {
            item = cJSON_GetObjectItem(calibration, "timeoutSec");
            if (item != NULL && zk_cal_json_u16(calibration, "timeoutSec", &timeout_sec) != BOOL_TRUE)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_enter(session_id, seq, digest, profile_crc,
                                                   timeout_sec, zk_ota_is_busy());
            }
        }
        if (result != CAL_OK && current_calibration_is_active() == BOOL_TRUE)
        {
            current_calibration_complete_command(seq, action_id, digest, result);
        }
        zk_cal_publish(header, response_node, action, session_id, seq, result);
        return BOOL_TRUE;
    }

    result = current_calibration_prepare_command(session_id, seq, action_id, digest, &duplicate);
    if (duplicate == BOOL_TRUE || result != CAL_OK)
    {
        zk_cal_publish(header, response_node, action, session_id, seq, result);
        return BOOL_TRUE;
    }

    switch (action_id)
    {
        case CAL_ACTION_SET_PWM:
            if (zk_cal_json_u8(calibration, "pointIndex", &point_index) != BOOL_TRUE ||
                zk_cal_json_u8(calibration, "targetPercent", &target_percent) != BOOL_TRUE ||
                zk_cal_json_u16(calibration, "logicalPwm", &logical_pwm) != BOOL_TRUE)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_set_pwm(point_index, target_percent, logical_pwm);
            }
            break;
        case CAL_ACTION_READ_STATUS:
        case CAL_ACTION_READ_CURVE_STATUS:
        case CAL_ACTION_READ_METER_SAMPLE:
        case CAL_ACTION_READ_METER_STATUS:
            result = CAL_OK;
            break;
        case CAL_ACTION_WRITE_CURVE_CHUNK:
            values_node = cJSON_GetObjectItem(calibration, "values");
            value_count = (values_node != NULL && cJSON_IsArray(values_node)) ?
                          cJSON_GetArraySize(values_node) : 0;
            if (zk_cal_json_u16(calibration, "curveVersion", &curve_version) != BOOL_TRUE ||
                zk_cal_json_context_crc(calibration, &profile_crc) != BOOL_TRUE ||
                zk_cal_json_u32(calibration, "curveCrc", &curve_crc) != BOOL_TRUE ||
                zk_cal_json_u32(calibration, "calibrationMaxCurrentMa",
                                &calibration_max_current_ma) != BOOL_TRUE ||
                zk_cal_json_u8(calibration, "startIndex", &start_index) != BOOL_TRUE ||
                value_count < 1 || value_count > 7)
            {
                result = CAL_INVALID_PARAM;
                break;
            }
            for (i = 0; i < value_count; ++i)
            {
                item = cJSON_GetArrayItem(values_node, i);
                if (item == NULL || !cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
                    item->valuedouble > 65535.0 || (double)(u16)item->valuedouble != item->valuedouble)
                {
                    break;
                }
                values[i] = (u16)item->valuedouble;
            }
            if (i != value_count)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_write_curve_chunk(curve_version, profile_crc,
                                                               curve_crc,
                                                               calibration_max_current_ma,
                                                               start_index,
                                                               values, (u8)value_count);
            }
            break;
        case CAL_ACTION_WRITE_METER_CHUNK:
            values_node = cJSON_GetObjectItem(calibration, "values");
            value_count = (values_node != NULL && cJSON_IsArray(values_node)) ?
                          cJSON_GetArraySize(values_node) : 0;
            if (zk_cal_json_u16(calibration, "meterVersion",
                                &meter_version) != BOOL_TRUE ||
                zk_cal_json_context_crc(calibration, &profile_crc) != BOOL_TRUE ||
                zk_cal_json_u32(calibration, "meterDataCrc",
                                &meter_data_crc) != BOOL_TRUE ||
                zk_cal_json_u8(calibration, "startOffset",
                               &start_offset) != BOOL_TRUE ||
                value_count < 1 || value_count > 32)
            {
                result = CAL_INVALID_PARAM;
                break;
            }
            for (i = 0; i < value_count; ++i)
            {
                item = cJSON_GetArrayItem(values_node, i);
                if (item == NULL || !cJSON_IsNumber(item) ||
                    item->valuedouble < 0.0 || item->valuedouble > 255.0 ||
                    (double)(u8)item->valuedouble != item->valuedouble)
                {
                    break;
                }
                meter_values[i] = (u8)item->valuedouble;
            }
            if (i != value_count)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_write_meter_chunk(
                    meter_version, profile_crc, meter_data_crc,
                    start_offset, meter_values, (u8)value_count);
            }
            break;
        case CAL_ACTION_BEGIN_METER:
            result = current_calibration_begin_meter();
            break;
        case CAL_ACTION_APPLY_TEMPORARY:
            result = (zk_cal_json_u32(calibration, "curveCrc", &curve_crc) == BOOL_TRUE) ?
                     current_calibration_apply_temporary(curve_crc) : CAL_INVALID_PARAM;
            break;
        case CAL_ACTION_SET_TEST_PERCENT:
            result = (zk_cal_json_u8(calibration, "percent", &percent) == BOOL_TRUE) ?
                     current_calibration_set_test_percent(percent) : CAL_INVALID_PARAM;
            break;
        case CAL_ACTION_COMMIT:
            if (zk_cal_json_context_crc(calibration, &profile_crc) != BOOL_TRUE ||
                zk_cal_json_u32(calibration, "curveCrc", &curve_crc) != BOOL_TRUE)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_commit(profile_crc, curve_crc);
            }
            break;
        case CAL_ACTION_COMMIT_METER:
            if (zk_cal_json_context_crc(calibration, &profile_crc) != BOOL_TRUE ||
                zk_cal_json_u32(calibration, "meterDataCrc",
                                &meter_data_crc) != BOOL_TRUE)
            {
                result = CAL_INVALID_PARAM;
            }
            else
            {
                result = current_calibration_commit_meter(profile_crc,
                                                          meter_data_crc);
            }
            break;
        case CAL_ACTION_ABORT:
            result = current_calibration_abort();
            break;
        case CAL_ACTION_EXIT:
            result = current_calibration_exit();
            break;
        default:
            result = CAL_INVALID_ACTION;
            break;
    }
    current_calibration_complete_command(seq, action_id, digest, result);
    zk_cal_publish(header, response_node, action, session_id, seq, result);
    return BOOL_TRUE;
}
