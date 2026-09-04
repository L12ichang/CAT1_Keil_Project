/*************************************************************
程序功能：SV=cal隔离校准会话与结果回读接口
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：黎长彩
单位名称：广东东菱电源科技有限公司
编辑日期：2026.8.4
*************************************************************/
#include "sys_calibration_mqtt.h"
#include "zk_protocol_internal.h"
#include "sys_calibration_service.h"
#include "sys_calibration_storage.h"
#include "sys_calibration_driver_protocol.h"
#include "sys_calibration_curve.h"
#include "sys_product_profile.h"
#include "factory_user_data.h"
#include "sys_calibration_safety.h"
#include "sys_calibration_snapshot.h"
#include "sys_bl0942.h"
#include "sys_Vo_Io.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define SYS_CALIBRATION_RAW_SCHEMA_VERSION 1U
#define SYS_CALIBRATION_RAW_MAX_AGE_MS     500U

#define SYS_CALIBRATION_MQTT_PROTOCOL_VERSION 2U

static const char *sys_calibration_mqtt_result_reason(
    sys_calibration_result_en result)
{
    static const char *const reason[] =
    {
        "",
        "NOT_AVAILABLE",
        "INVALID_STATE",
        "INVALID_ARGUMENT",
        "LEASE_EXPIRED",
        "BUSY",
        "PROTOCOL_ERROR",
        "SAFETY_NOT_READY",
        "DUPLICATE",
        "FLASH_GATED",
        "HARDWARE_FAULT",
        "CONTEXT_MISMATCH"
    };
    if ((u32)result >= (u32)(sizeof(reason) / sizeof(reason[0])))
    {
        return "UNKNOWN_RESULT";
    }
    return reason[(u32)result];
}

static void sys_calibration_mqtt_add_profile_catalog(cJSON *dt)
{
    static const u16 profile_ids[] = {50U, 75U, 100U, 150U, 200U, 240U};
    static char profiles_csv[768U];
    const sys_product_profile_st *profile;
    u32 index;
    u32 used = 0U;
    int written;

    if (dt == NULL)
    {
        return;
    }
    /* Keep CAPABILITIES inside the fixed 4 KiB cJSON pool / 2 KiB TX buffer. */
    cJSON_AddStringToObject(dt, "profilesCodec", "PROFILE_CSV_V1");
    profiles_csv[0] = '\0';
    for (index = 0U; index <
         (u32)(sizeof(profile_ids) / sizeof(profile_ids[0])); ++index)
    {
        profile = sys_product_profile_find(profile_ids[index]);
        if (profile == NULL || profile->model_code == NULL ||
            profile->block_code == NULL || used >= sizeof(profiles_csv))
        {
            profiles_csv[0] = '\0';
            break;
        }
        written = snprintf(
            &profiles_csv[used], sizeof(profiles_csv) - used,
            "%s%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%s",
            (index == 0U) ? "" : ";", profile->profile_id,
            profile->model_code, profile->mid, profile->rated_power_w,
            profile->default_runtime_current_ma, profile->rs3_mohm,
            profile->hw_max_current_ma, profile->absolute_fail_current_ma,
            profile->build_enabled == BOOL_TRUE ? 1U : 0U,
            profile->nonzero_calibration_enabled == BOOL_TRUE ? 1U : 0U,
            profile->block_code);
        if (written < 0 || (u32)written >= sizeof(profiles_csv) - used)
        {
            profiles_csv[0] = '\0';
            break;
        }
        used += (u32)written;
    }
    cJSON_AddStringToObject(
        dt, "profilesCsv",
        profiles_csv[0] == '\0' ? "CATALOG_ENCODING_FAILED" : profiles_csv);
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

static boolean_en sys_calibration_mqtt_read_context(
    cJSON *dt,
    boolean_en require_table_crc,
    sys_calibration_context_st *context)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    cJSON *object;
    cJSON *model_code;
    u32 context_version;
    u32 binding_crc32;

    if (dt == NULL || context == NULL || profile == NULL)
    {
        return BOOL_FALSE;
    }
    object = cJSON_GetObjectItem(dt, "profileContext");
    if (object == NULL || !cJSON_IsObject(object) ||
        cJSON_GetObjectItem(dt, "profileId") != NULL ||
        cJSON_GetObjectItem(dt, "modelCode") != NULL ||
        cJSON_GetObjectItem(dt, "profileVersion") != NULL ||
        cJSON_GetObjectItem(dt, "profileFingerprint") != NULL ||
        cJSON_GetObjectItem(dt, "calibrationVoltage01V") != NULL ||
        cJSON_GetObjectItem(dt, "configuredRatedCurrentMa") != NULL ||
        cJSON_GetObjectItem(dt, "calibratedMaxCurrentMa") != NULL ||
        cJSON_GetObjectItem(dt, "tableCrc32") != NULL ||
        cJSON_GetObjectItem(object, "calibrationVoltageV") != NULL ||
        sys_calibration_mqtt_read_u32(object, "contextVersion",
                                     &context_version) != BOOL_TRUE ||
        context_version != SYS_CALIBRATION_CONTEXT_VERSION ||
        sys_calibration_mqtt_read_u16(object, "profileId",
                                     &context->profile_id) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u16(object, "profileVersion",
                                     &context->profile_version) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u32(object, "profileFingerprint",
                                     &context->profile_fingerprint_crc32) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u16(object, "calibrationVoltage01V",
                                     &context->calibration_voltage_01v) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u16(object, "configuredRatedCurrentMa",
                                     &context->configured_rated_current_ma) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u16(object, "calibratedMaxCurrentMa",
                                     &context->calibrated_max_current_ma) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u32(object, "tableCrc32",
                                     &context->table_crc32) != BOOL_TRUE ||
        sys_calibration_mqtt_read_u32(dt, "profileBindingCrc32",
                                     &binding_crc32) != BOOL_TRUE)
    {
        return BOOL_FALSE;
    }
    model_code = cJSON_GetObjectItem(object, "modelCode");
    if (model_code == NULL || !cJSON_IsString(model_code) ||
        model_code->valuestring == NULL ||
        strcmp(model_code->valuestring, profile->model_code) != 0 ||
        context->configured_rated_current_ma != SET_OUTCUR)
    {
        return BOOL_FALSE;
    }
    return (sys_product_profile_context_validate(
                context, require_table_crc) == BOOL_TRUE &&
            binding_crc32 ==
                sys_product_profile_context_binding_crc32(context)) ?
           BOOL_TRUE : BOOL_FALSE;
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

static void sys_calibration_mqtt_add_profile_context(
    cJSON *parent,
    const char *key,
    const sys_calibration_context_st *context)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    cJSON *object;

    if (parent == NULL || key == NULL || context == NULL || profile == NULL)
    {
        return;
    }
    object = zk_cjson_create_tx_object("cal.profileContext");
    if (object == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(object, "contextVersion",
                            SYS_CALIBRATION_CONTEXT_VERSION);
    cJSON_AddNumberToObject(object, "profileId", context->profile_id);
    cJSON_AddStringToObject(object, "modelCode", profile->model_code);
    cJSON_AddNumberToObject(object, "profileVersion",
                            context->profile_version);
    cJSON_AddNumberToObject(object, "profileFingerprint",
                            context->profile_fingerprint_crc32);
    cJSON_AddNumberToObject(object, "calibrationVoltage01V",
                            context->calibration_voltage_01v);
    cJSON_AddNumberToObject(object, "configuredRatedCurrentMa",
                            context->configured_rated_current_ma);
    cJSON_AddNumberToObject(object, "calibratedMaxCurrentMa",
                            context->calibrated_max_current_ma);
    cJSON_AddNumberToObject(object, "tableCrc32", context->table_crc32);
    cJSON_AddItemToObject(parent, key, object);
}

static void sys_calibration_mqtt_add_payload(cJSON *readback)
{
    u8 payload[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH];
    char hex[SYS_CALIBRATION_DRIVER_TABLE_PAYLOAD_LENGTH * 2U + 1U];
    static const char digits[] = "0123456789ABCDEF";
    u16 length;
    u16 index;

    if (readback == NULL || sys_calibration_service_get_staged_payload(
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
    cJSON_AddStringToObject(readback, "payloadHex", hex);
}

static void sys_calibration_mqtt_add_status(
    cJSON *dt,
    const sys_calibration_service_status_st *status,
    boolean_en include_payload)
{
    cJSON *readback;
    sys_calibration_context_st committed_context;

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
    cJSON_AddNumberToObject(
        readback, "tableCrc32",
        status->context_valid == BOOL_TRUE ? status->context.table_crc32 : 0U);
    cJSON_AddNumberToObject(readback, "committedGeneration",
                            status->committed_generation);
    cJSON_AddBoolToObject(readback, "staged", status->staged_valid == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "committed",
                          status->committed_valid == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "safetyReady", status->safety_ready == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "bootInhibit",
                          status->boot_inhibit_active == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "persistenceReady",
                          status->persistence_ready == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "commitAvailable",
                          status->commit_available == BOOL_TRUE);
    cJSON_AddBoolToObject(readback, "contextValid",
                          status->context_valid == BOOL_TRUE);
    if (status->context_valid == BOOL_TRUE)
    {
        sys_calibration_mqtt_add_profile_context(
            readback, "profileContext", &status->context);
        cJSON_AddNumberToObject(
            readback, "profileBindingCrc32",
            sys_product_profile_context_binding_crc32(&status->context));
    }
    if (sys_calibration_service_get_committed_context(
            &committed_context) == BOOL_TRUE)
    {
        cJSON_AddNumberToObject(readback, "committedTableCrc32",
                                committed_context.table_crc32);
        cJSON_AddNumberToObject(
            readback, "committedProfileBindingCrc32",
            sys_product_profile_context_binding_crc32(&committed_context));
    }
    else
    {
        cJSON_AddNumberToObject(readback, "committedTableCrc32", 0U);
        cJSON_AddNumberToObject(readback,
                                "committedProfileBindingCrc32", 0U);
    }
    if (include_payload == BOOL_TRUE)
    {
        sys_calibration_mqtt_add_payload(readback);
    }
    cJSON_AddItemToObject(dt, "readback", readback);
}

static boolean_en sys_calibration_mqtt_add_raw_readback(
    cJSON *dt,
    const sys_calibration_service_status_st *status)
{
    cJSON *readback;

    if (dt == NULL || status == NULL)
    {
        return BOOL_FALSE;
    }
    readback = zk_cjson_create_tx_object("cal.raw.readback");
    if (readback == NULL)
    {
        return BOOL_FALSE;
    }
    cJSON_AddNumberToObject(readback, "sessionId", status->session_id);
    cJSON_AddNumberToObject(readback, "lastSeq", status->last_request_seq);
    cJSON_AddNumberToObject(readback, "currentLevel", status->current_level);
    cJSON_AddItemToObject(dt, "readback", readback);
    return (zk_cjson_tx_allocation_ok() == BOOL_TRUE &&
            cJSON_GetObjectItem(dt, "readback") == readback) ?
           BOOL_TRUE : BOOL_FALSE;
}

static boolean_en sys_calibration_mqtt_raw_snapshot_ready(
    u32 now_ms,
    const sys_calibration_service_status_st *status,
    sys_calibration_snapshot_aggregate_st *snapshot)
{
    if (status == NULL || snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    /* Meter diagnostics are best-effort; ADC/PWM are the required RAW sources. */
    (void)sys_calibration_snapshot_read_aggregate(now_ms, snapshot);
    if (status->current_level > 200U ||
        (status->current_level % 2U) != 0U ||
        (snapshot->valid_flags & SYS_CALIBRATION_AGGREGATE_ADC_PRESENT) == 0U ||
        (snapshot->valid_flags & SYS_CALIBRATION_AGGREGATE_PWM_PRESENT) == 0U ||
        (snapshot->adc.valid_flags & SYS_CALIBRATION_ADC_SAMPLE_VALID) == 0U ||
        (snapshot->pwm.valid_flags & SYS_CALIBRATION_PWM_SAMPLE_VALID) == 0U ||
        snapshot->adc_age_ms > SYS_CALIBRATION_RAW_MAX_AGE_MS ||
        snapshot->pwm.requested_percent != (status->current_level / 2U))
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

static boolean_en sys_calibration_mqtt_add_raw(
    cJSON *dt,
    const sys_calibration_snapshot_aggregate_st *snapshot)
{
    cJSON *raw;
    cJSON *meter;
    cJSON *adc;
    cJSON *pwm;

    if (dt == NULL || snapshot == NULL)
    {
        return BOOL_FALSE;
    }
    raw = zk_cjson_create_tx_object("cal.raw");
    meter = zk_cjson_create_tx_object("cal.raw.meter");
    adc = zk_cjson_create_tx_object("cal.raw.adc");
    pwm = zk_cjson_create_tx_object("cal.raw.pwm");
    if (raw == NULL || meter == NULL || adc == NULL || pwm == NULL)
    {
        cJSON_Delete(raw);
        cJSON_Delete(meter);
        cJSON_Delete(adc);
        cJSON_Delete(pwm);
        return BOOL_FALSE;
    }
    cJSON_AddNumberToObject(raw, "schemaVersion",
                            SYS_CALIBRATION_RAW_SCHEMA_VERSION);
    cJSON_AddBoolToObject(raw, "available", 1);
    cJSON_AddNumberToObject(raw, "validFlags", snapshot->valid_flags);
    cJSON_AddNumberToObject(raw, "meterAdcSkewMs", snapshot->meter_adc_skew_ms);
    cJSON_AddNumberToObject(raw, "meterPwmSkewMs", snapshot->meter_pwm_skew_ms);
    cJSON_AddNumberToObject(raw, "inputVoltage01V", ac_voltage_8209);
    cJSON_AddNumberToObject(raw, "inputCurrentMa", Z_ac_current);
    cJSON_AddNumberToObject(raw, "inputPower001W", ac_powerpa);
    cJSON_AddNumberToObject(raw, "outputVoltage01V", Vo_value);
    cJSON_AddNumberToObject(raw, "outputCurrentMa", Io_value);
    cJSON_AddNumberToObject(raw, "outputPower01W", Po_value);
    cJSON_AddNumberToObject(meter, "seq", snapshot->meter.seq);
    cJSON_AddNumberToObject(meter, "ageMs", snapshot->meter_age_ms);
    cJSON_AddNumberToObject(meter, "validFlags", snapshot->meter.valid_flags);
    cJSON_AddNumberToObject(meter, "iRmsRaw", snapshot->meter.i_rms_raw);
    cJSON_AddNumberToObject(meter, "vRmsRaw", snapshot->meter.v_rms_raw);
    cJSON_AddNumberToObject(meter, "iFastRmsRaw", snapshot->meter.i_fast_rms_raw);
    cJSON_AddNumberToObject(meter, "wattRaw", snapshot->meter.watt_raw);
    cJSON_AddNumberToObject(meter, "cfCntRaw", snapshot->meter.cf_cnt_raw);
    cJSON_AddNumberToObject(meter, "freqRaw", snapshot->meter.freq_raw);
    cJSON_AddNumberToObject(meter, "statusRaw", snapshot->meter.status_raw);
    cJSON_AddNumberToObject(meter, "frameErrors", bl0942_checksum_error_count);
    cJSON_AddNumberToObject(meter, "timeoutErrors", bl0942_timeout_count);
    cJSON_AddNumberToObject(meter, "uartErrors", bl0942_uart_error_count);
    cJSON_AddNumberToObject(meter, "compatFrames", bl0942_compat_frame_count);
    cJSON_AddNumberToObject(adc, "seq", snapshot->adc.seq);
    cJSON_AddNumberToObject(adc, "ageMs", snapshot->adc_age_ms);
    cJSON_AddNumberToObject(adc, "validFlags", snapshot->adc.valid_flags);
    cJSON_AddNumberToObject(adc, "ntcRaw", snapshot->adc.ntc_raw);
    cJSON_AddNumberToObject(adc, "voutRaw", snapshot->adc.vout_raw);
    cJSON_AddNumberToObject(adc, "leakRaw", snapshot->adc.leak_raw);
    cJSON_AddNumberToObject(adc, "ioutRaw", snapshot->adc.iout_raw);
    cJSON_AddNumberToObject(pwm, "seq", snapshot->pwm.seq);
    cJSON_AddNumberToObject(pwm, "ageMs", snapshot->pwm_age_ms);
    cJSON_AddNumberToObject(pwm, "requestedPercent",
                            snapshot->pwm.requested_percent);
    cJSON_AddNumberToObject(pwm, "protectedPercent",
                            snapshot->pwm.protected_percent);
    cJSON_AddNumberToObject(pwm, "logicalPwm", snapshot->pwm.logical_pwm);
    cJSON_AddNumberToObject(pwm, "ccr", snapshot->pwm.ccr);
    cJSON_AddNumberToObject(pwm, "ocoOn", snapshot->pwm.oco_on);
    cJSON_AddItemToObject(raw, "meter", meter);
    cJSON_AddItemToObject(raw, "adc", adc);
    cJSON_AddItemToObject(raw, "pwm", pwm);
    cJSON_AddItemToObject(dt, "raw", raw);
    return (zk_cjson_tx_allocation_ok() == BOOL_TRUE &&
            cJSON_GetObjectItem(dt, "raw") == raw) ? BOOL_TRUE : BOOL_FALSE;
}

static void sys_calibration_mqtt_add_active_profile(
    cJSON *dt,
    const sys_product_profile_st *profile,
    u16 maximum_current_ma,
    u16 calibrated_max_current_ma)
{
    cJSON *active;

    if (dt == NULL || profile == NULL)
    {
        return;
    }
    active = zk_cjson_create_tx_object("cal.activeProfile");
    if (active == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(active, "profileId", profile->profile_id);
    cJSON_AddStringToObject(active, "modelCode", profile->model_code);
    cJSON_AddNumberToObject(active, "profileVersion",
                            profile->profile_version);
    cJSON_AddNumberToObject(active, "profileFingerprint",
                            profile->fingerprint_crc32);
    cJSON_AddNumberToObject(active, "mid", profile->mid);
    cJSON_AddNumberToObject(active, "ratedPowerW", profile->rated_power_w);
    cJSON_AddNumberToObject(active, "rs3Mohm", profile->rs3_mohm);
    cJSON_AddNumberToObject(active, "hwMaxCurrentMa",
                            profile->hw_max_current_ma);
    cJSON_AddNumberToObject(active, "absoluteFailCurrentMa",
                            profile->absolute_fail_current_ma);
    cJSON_AddNumberToObject(active, "boundOutputVoltage01V",
                            BOUND_OUTPUT_VOLTAGE_01V);
    cJSON_AddNumberToObject(active, "maxCurrentAtVoltageMa",
                            maximum_current_ma);
    cJSON_AddNumberToObject(active, "configuredRatedCurrentMa", SET_OUTCUR);
    cJSON_AddNumberToObject(active, "calibratedMaxCurrentMa",
                            calibrated_max_current_ma);
    cJSON_AddBoolToObject(active, "buildEnabled",
                          profile->build_enabled == BOOL_TRUE);
    cJSON_AddItemToObject(dt, "activeProfile", active);
}

static int sys_calibration_mqtt_send_response(
    const zk_message_header_t *request,
    const char *operation,
    sys_calibration_result_en result,
    const sys_calibration_service_status_st *status,
    boolean_en capabilities,
    u32 seq)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 maximum_current_ma = 0U;
    u16 calibrated_max_current_ma = 0U;
    boolean_en runtime_profile_valid;
    sys_product_current_validation_en envelope_result;
    sys_product_current_validation_en runtime_current_result;
    boolean_en first_calibration_allowed;
    boolean_en runtime_nonzero_allowed;
    boolean_en calibration_nonzero_allowed;
    boolean_en commit_available;
    const char *block_code;
    cJSON *root;
    cJSON *dt;
    sys_calibration_snapshot_aggregate_st raw_snapshot;
    boolean_en raw_operation;
    int send_result;

    raw_operation = (operation != NULL && strcmp(operation, "RAW") == 0) ?
                    BOOL_TRUE : BOOL_FALSE;
    if (raw_operation == BOOL_TRUE && result == SYS_CALIBRATION_RESULT_OK &&
        sys_calibration_mqtt_raw_snapshot_ready(
            HAL_GetTick(), status, &raw_snapshot) != BOOL_TRUE)
    {
        result = SYS_CALIBRATION_RESULT_NOT_AVAILABLE;
    }

rebuild_response:
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
    runtime_profile_valid = sys_product_profile_runtime_matches(
        MID, OUTPUT_CUR_SENSOR, HWMAX_OUTCUR);
    envelope_result = sys_product_profile_validate_runtime_current(
        profile, BOUND_OUTPUT_VOLTAGE_01V, SET_OUTCUR);
    runtime_current_result = factory_user_validate_runtime_current(
        BOUND_OUTPUT_VOLTAGE_01V, SET_OUTCUR);
    (void)sys_product_profile_compute_i100_ma(
        profile, BOUND_OUTPUT_VOLTAGE_01V, &maximum_current_ma);
    (void)sys_calibration_service_get_calibrated_max_current_ma(
        BOUND_OUTPUT_VOLTAGE_01V, &calibrated_max_current_ma);
    cJSON_AddNumberToObject(dt, "protocolVersion",
                            SYS_CALIBRATION_MQTT_PROTOCOL_VERSION);
    if (capabilities == BOOL_TRUE)
    {
        commit_available =
            (SYS_CALIBRATION_FLASH_COMMIT_ENABLED != 0U &&
             runtime_profile_valid == BOOL_TRUE && status != NULL &&
             status->commit_available == BOOL_TRUE &&
             status->persistence_ready == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
        first_calibration_allowed =
            (SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED != 0U &&
             sys_product_profile_is_complete(profile) == BOOL_TRUE &&
             profile->nonzero_calibration_enabled == BOOL_TRUE &&
             runtime_profile_valid == BOOL_TRUE &&
             envelope_result == SYS_PRODUCT_CURRENT_VALID && status != NULL &&
             status->safety_ready == BOOL_TRUE &&
             status->persistence_ready == BOOL_TRUE &&
             status->commit_available == BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
        runtime_nonzero_allowed =
            (SYS_CALIBRATION_NONZERO_OUTPUT_ENABLED != 0U &&
             runtime_profile_valid == BOOL_TRUE &&
             runtime_current_result == SYS_PRODUCT_CURRENT_VALID &&
             sys_calibration_service_runtime_context_matches_voltage(
                 BOUND_OUTPUT_VOLTAGE_01V) == BOOL_TRUE && status != NULL &&
             status->safety_ready == BOOL_TRUE &&
             status->boot_inhibit_active != BOOL_TRUE) ? BOOL_TRUE : BOOL_FALSE;
        calibration_nonzero_allowed =
            (first_calibration_allowed == BOOL_TRUE ||
             sys_calibration_service_is_output_authorized() == BOOL_TRUE) ?
            BOOL_TRUE : BOOL_FALSE;

        cJSON_AddBoolToObject(dt, "protocolFrozen",
                              SYS_CALIBRATION_MQTT_V2_FIELDS_FROZEN != 0U);
        cJSON_AddNumberToObject(dt, "driverProtocolVersion",
                                SYS_CALIBRATION_DRIVER_PROTOCOL_VERSION);
        cJSON_AddNumberToObject(dt, "tablePoints",
                                SYS_CALIBRATION_DRIVER_POINT_COUNT);
        cJSON_AddNumberToObject(dt, "levelStep",
                                SYS_CALIBRATION_DRIVER_LEVEL_STEP);
        sys_calibration_mqtt_add_profile_catalog(dt);
        sys_calibration_mqtt_add_active_profile(
            dt, profile, maximum_current_ma, calibrated_max_current_ma);
        cJSON_AddBoolToObject(dt, "firstCalibrationAllowed",
                              first_calibration_allowed == BOOL_TRUE);
        cJSON_AddBoolToObject(dt, "runtimeNonzeroOutputAllowed",
                              runtime_nonzero_allowed == BOOL_TRUE);
        cJSON_AddBoolToObject(dt, "calibrationNonzeroOutputAllowed",
                              calibration_nonzero_allowed == BOOL_TRUE);
        cJSON_AddBoolToObject(
            dt, "configuredRatedCurrentWriteReadbackAvailable",
            runtime_profile_valid == BOOL_TRUE);
        cJSON_AddBoolToObject(dt, "validationPercentOutputAvailable",
                              SYS_CALIBRATION_CODEC_AVAILABLE != 0U);
        cJSON_AddBoolToObject(dt, "rawMeasurementSupported", 1);
        cJSON_AddNumberToObject(dt, "rawSchemaVersion",
                                SYS_CALIBRATION_RAW_SCHEMA_VERSION);
        cJSON_AddNumberToObject(dt, "rawMaxAgeMs",
                                SYS_CALIBRATION_RAW_MAX_AGE_MS);
        cJSON_AddBoolToObject(dt, "commitAvailable",
                              commit_available == BOOL_TRUE);
        cJSON_AddBoolToObject(dt, "safetyReady",
                              status != NULL &&
                              status->safety_ready == BOOL_TRUE);
        if (sys_product_profile_is_complete(profile) != BOOL_TRUE)
        {
            block_code = profile->block_code;
        }
        else if (runtime_profile_valid != BOOL_TRUE)
        {
            block_code = "RUNTIME_FACTORY_PROFILE_MISMATCH";
        }
        else if (envelope_result != SYS_PRODUCT_CURRENT_VALID)
        {
            block_code = sys_product_profile_current_validation_reason(
                envelope_result);
        }
        else if (status == NULL || status->safety_ready != BOOL_TRUE)
        {
            block_code = "SAFETY_NOT_READY";
        }
        else if (status->persistence_ready != BOOL_TRUE)
        {
            block_code = "PERSISTENCE_NOT_READY";
        }
        else if (runtime_current_result ==
                 SYS_PRODUCT_CURRENT_CALIBRATION_MAX_UNAVAILABLE)
        {
            block_code = "FIRST_CALIBRATION_REQUIRED";
        }
        else if (runtime_current_result != SYS_PRODUCT_CURRENT_VALID)
        {
            block_code = sys_product_profile_current_validation_reason(
                runtime_current_result);
        }
        else if (sys_calibration_service_runtime_context_matches_voltage(
                     BOUND_OUTPUT_VOLTAGE_01V) != BOOL_TRUE)
        {
            block_code = "CALIBRATION_CONTEXT_VOLTAGE_MISMATCH";
        }
        else
        {
            block_code = "OK";
        }
        cJSON_AddStringToObject(dt, "blockCode", block_code);
    }
    else
    {
        cJSON_AddStringToObject(dt, "blockCode",
                                sys_calibration_mqtt_result_reason(result));
    }
    if (capabilities != BOOL_TRUE)
    {
        if (raw_operation == BOOL_TRUE)
        {
            if (sys_calibration_mqtt_add_raw_readback(dt, status) != BOOL_TRUE)
            {
                cJSON_Delete(root);
                return -1;
            }
        }
        else
        {
            sys_calibration_mqtt_add_status(
                dt, status,
                (operation != NULL &&
                 strcmp(operation, "READBACK") == 0) ? BOOL_TRUE : BOOL_FALSE);
        }
    }
    if (raw_operation == BOOL_TRUE && result == SYS_CALIBRATION_RESULT_OK)
    {
        if (sys_calibration_mqtt_add_raw(dt, &raw_snapshot) != BOOL_TRUE)
        {
            cJSON_Delete(root);
            result = SYS_CALIBRATION_RESULT_NOT_AVAILABLE;
            goto rebuild_response;
        }
    }
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
    u32 protocol_version = 0U;
    u32 lease_ms;
    u16 level;
    u16 target_percent;
    u16 length;
    u8 payload[SYS_CALIBRATION_DRIVER_TABLE_FRAME_LENGTH];
    sys_calibration_context_st request_context;
    sys_calibration_context_st active_context;
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

    if (sys_calibration_mqtt_read_u32(
            dt, "protocolVersion", &protocol_version) != BOOL_TRUE ||
        protocol_version != SYS_CALIBRATION_MQTT_PROTOCOL_VERSION)
    {
        result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    else if (((strcmp(operation, "CAPABILITIES") == 0 ||
          strcmp(operation, "RAW") == 0 ||
          strcmp(operation, "READBACK") == 0) &&
         strcmp(header->ct, ZK_CT_READ) != 0) ||
        ((strcmp(operation, "CAPABILITIES") != 0 &&
          strcmp(operation, "RAW") != 0 &&
          strcmp(operation, "READBACK") != 0) &&
         strcmp(header->ct, ZK_CT_WRITE) != 0))
    {
        result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    else if (strcmp(operation, "CAPABILITIES") == 0)
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
        else if (sys_calibration_mqtt_read_context(
                     dt, BOOL_FALSE, &request_context) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
            sys_calibration_service_force_fault();
            (void)sys_calibration_service_get_status(&status);
        }
        else
        {
            result = sys_calibration_service_begin_context_seq(
                session_id, HAL_GetTick(), lease_ms, seq, &request_context,
                &status);
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
    else if (strcmp(operation, "SET_MAX_CONTEXT") == 0)
    {
        if (!cJSON_IsString(cJSON_GetObjectItem(dt, "frameHex")) ||
            sys_calibration_mqtt_read_payload(
                dt, "frameHex", payload, sizeof(payload), &length) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_raw_seq(
                session_id, HAL_GetTick(), seq, payload, length,
                SYS_CALIBRATION_RAW_SET, &status);
        }
    }
    else if (strcmp(operation, "SET_VALIDATION_PERCENT") == 0)
    {
        if (sys_calibration_mqtt_read_u16(
                dt, "targetPercent", &target_percent) != BOOL_TRUE ||
            target_percent == 0U || target_percent >= 100U)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_set_validation_percent_seq(
                session_id, HAL_GetTick(), seq, (u8)target_percent, &status);
        }
    }
    else if (strcmp(operation, "RAW") == 0)
    {
        if (cJSON_GetObjectItem(dt, "frame") != NULL ||
            cJSON_GetObjectItem(dt, "direction") != NULL)
        {
            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
        }
        else
        {
            result = sys_calibration_service_snapshot_seq(
                session_id, HAL_GetTick(), seq, &status);
        }
    }
    else if (strcmp(operation, "STAGE_CONFIG") == 0)
    {
        if (!cJSON_IsString(cJSON_GetObjectItem(dt, "payloadHex")) ||
            sys_calibration_mqtt_read_payload(
                dt, "payloadHex", payload, sizeof(payload), &length) != BOOL_TRUE ||
            sys_calibration_mqtt_read_context(
                dt, BOOL_TRUE, &request_context) != BOOL_TRUE ||
            request_context.table_crc32 !=
                sys_calibration_storage_crc32(payload, length) ||
            sys_calibration_service_get_context(&active_context) != BOOL_TRUE ||
            sys_product_profile_context_equal(
                &request_context, &active_context, BOOL_FALSE) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
            sys_calibration_service_force_fault();
            (void)sys_calibration_service_get_status(&status);
        }
        else
        {
            result = sys_calibration_service_stage_config_context_seq(
                session_id, HAL_GetTick(), seq, &request_context,
                payload, length, &status);
        }
    }
    else if (strcmp(operation, "APPLY") == 0)
    {
        if (sys_calibration_mqtt_read_context(
                dt, BOOL_TRUE, &request_context) != BOOL_TRUE ||
            sys_calibration_service_get_context(&active_context) != BOOL_TRUE ||
            sys_product_profile_context_equal(
                &request_context, &active_context, BOOL_TRUE) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
            sys_calibration_service_force_fault();
            (void)sys_calibration_service_get_status(&status);
        }
        else
        {
            result = sys_calibration_service_apply_seq(
                session_id, HAL_GetTick(), seq, &status);
        }
    }
    else if (strcmp(operation, "READBACK") == 0)
    {
        if (sys_calibration_mqtt_read_context(
                dt, BOOL_TRUE, &request_context) != BOOL_TRUE ||
            sys_calibration_service_get_context(&active_context) != BOOL_TRUE ||
            sys_product_profile_context_equal(
                &request_context, &active_context, BOOL_TRUE) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
            sys_calibration_service_force_fault();
            (void)sys_calibration_service_get_status(&status);
        }
        else
        {
            result = sys_calibration_service_readback_seq(
                session_id, HAL_GetTick(), seq, &status);
        }
    }
    else if (strcmp(operation, "COMMIT") == 0)
    {
        if (sys_calibration_mqtt_read_context(
                dt, BOOL_TRUE, &request_context) != BOOL_TRUE)
        {
            result = SYS_CALIBRATION_RESULT_CONTEXT_MISMATCH;
            sys_calibration_service_force_fault();
            (void)sys_calibration_service_get_status(&status);
        }
        else
        {
            result = sys_calibration_service_commit_context_seq(
                session_id, HAL_GetTick(), seq, &request_context, &status);
        }
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
