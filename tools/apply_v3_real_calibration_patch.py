from pathlib import Path

# One-shot branch helper; remove after sys_calibration_mqtt.c is patched.
path = Path('Core/Src/sys_calibration_mqtt.c')
text = path.read_text(encoding='utf-8')


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'expected exactly one match, got {count}: {old[:120]!r}')
    text = text.replace(old, new, 1)

replace_once(
'''        case SYS_CALIBRATION_MQTT_OP_BEGIN:
            return (strcmp(key, "profileId") == 0 ||
                    strcmp(key, "profileFingerprint") == 0 ||
                    strcmp(key, "leaseMs") == 0) ? BOOL_TRUE : BOOL_FALSE;
''',
'''        case SYS_CALIBRATION_MQTT_OP_BEGIN:
            return (strcmp(key, "profileId") == 0 ||
                    strcmp(key, "profileFingerprint") == 0 ||
                    strcmp(key, "leaseMs") == 0 ||
                    strcmp(key, "calibrationVoltage01V") == 0 ||
                    strcmp(key, "calibrationSpanMa") == 0) ?
                   BOOL_TRUE : BOOL_FALSE;
''')

replace_once(
'''        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
            return strcmp(key, "level") == 0 ? BOOL_TRUE : BOOL_FALSE;
''',
'''        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
            return (strcmp(key, "level") == 0 ||
                    strcmp(key, "logicalPwm") == 0) ?
                   BOOL_TRUE : BOOL_FALSE;
''')

replace_once(
'''    cJSON *op_node;
    cJSON *payload_hex_node;
''',
'''    cJSON *op_node;
    cJSON *payload_hex_node;
    cJSON *logical_pwm_node;
''')

replace_once(
'''    u16 profile_id = 0U;
    u16 level = 0U;
    u16 payload_length = 0U;
''',
'''    u16 profile_id = 0U;
    u16 level = 0U;
    u16 logical_pwm = 0U;
    u16 calibration_voltage_01v = 0U;
    u16 calibration_span_ma = 0U;
    u16 payload_length = 0U;
''')

replace_once(
'''    u8 replay_result;
    boolean_en request_cacheable = BOOL_FALSE;
''',
'''    u8 replay_result;
    boolean_en direct_pwm_requested = BOOL_FALSE;
    boolean_en request_cacheable = BOOL_FALSE;
''')

replace_once(
'''                sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) !=
                    BOOL_TRUE || seq != 1U)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, profile_id, profile_fingerprint, lease_ms);
''',
'''                sys_calibration_mqtt_read_u32(dt, "leaseMs", &lease_ms) !=
                    BOOL_TRUE ||
                sys_calibration_mqtt_read_u16(
                    dt, "calibrationVoltage01V",
                    &calibration_voltage_01v) != BOOL_TRUE ||
                sys_calibration_mqtt_read_u16(
                    dt, "calibrationSpanMa", &calibration_span_ma) !=
                    BOOL_TRUE || seq != 1U)
            {
                break;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, profile_id, profile_fingerprint, lease_ms);
            parameter_digest = sys_calibration_mqtt_digest_u32(
                parameter_digest, calibration_voltage_01v);
            parameter_digest = sys_calibration_mqtt_digest_u32(
                parameter_digest, calibration_span_ma);
''')

replace_once(
'''        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
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
''',
'''        case SYS_CALIBRATION_MQTT_OP_SET_POINT:
            if (sys_calibration_mqtt_read_u16(dt, "level", &level) != BOOL_TRUE)
            {
                break;
            }
            logical_pwm_node = cJSON_GetObjectItemCaseSensitive(
                dt, "logicalPwm");
            if (logical_pwm_node != NULL)
            {
                if (sys_calibration_mqtt_read_u16(
                        dt, "logicalPwm", &logical_pwm) != BOOL_TRUE)
                {
                    break;
                }
                direct_pwm_requested = BOOL_TRUE;
            }
            request_cacheable = BOOL_TRUE;
            parameter_digest = sys_calibration_mqtt_parameter_digest(
                operation, level,
                direct_pwm_requested == BOOL_TRUE ? logical_pwm : 0U,
                direct_pwm_requested == BOOL_TRUE ? 1U : 0U);
            result = (level <= SYS_CALIBRATION_LEVEL_MAX &&
                      (level % SYS_CALIBRATION_LEVEL_STEP) == 0U &&
                      (direct_pwm_requested != BOOL_TRUE ||
                       (logical_pwm <= SYS_CALIBRATION_PWM_MAX &&
                        ((level == 0U && logical_pwm == 0U) ||
                         (level != 0U && logical_pwm != 0U))))) ?
                     SYS_CALIBRATION_RESULT_OK :
                     SYS_CALIBRATION_RESULT_RANGE_ERROR;
            response->level = level;
            break;
''')

replace_once(
'''            else if (sys_calibration_payload_validate(decoded_payload) !=
                         BOOL_TRUE ||
                     sys_calibration_payload_within_product_limits(
                         decoded_payload, profile) != BOOL_TRUE)
            {
                result = SYS_CALIBRATION_RESULT_RANGE_ERROR;
            }
''',
'''            else if (sys_calibration_payload_validate(decoded_payload) !=
                         BOOL_TRUE ||
                     sys_calibration_payload_within_product_limits(
                         decoded_payload, profile) != BOOL_TRUE ||
                     decoded_payload->output[0].reference_output_current_ma != 0U ||
                     decoded_payload->output[
                         SYS_CALIBRATION_PAYLOAD_POINT_COUNT - 1U]
                         .reference_output_current_ma !=
                         sys_calibration_service_calibration_span_ma())
            {
                /* A real-calibration Output curve is always the exact 0..span
                 * target axis. Sampling references may differ within tolerance. */
                result = SYS_CALIBRATION_RESULT_RANGE_ERROR;
            }
''')

replace_once(
'''            case SYS_CALIBRATION_MQTT_OP_BEGIN:
                result = sys_calibration_service_begin_seq(
                    session_id, HAL_GetTick(), lease_ms, seq, profile_id,
                    profile_fingerprint, &response->status);
                break;
''',
'''            case SYS_CALIBRATION_MQTT_OP_BEGIN:
                result = sys_calibration_service_begin_range_seq(
                    session_id, HAL_GetTick(), lease_ms, seq, profile_id,
                    profile_fingerprint, calibration_voltage_01v,
                    calibration_span_ma, &response->status);
                break;
''')

replace_once(
'''            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                result = sys_calibration_service_set_point_seq(
                    session_id, HAL_GetTick(), seq, level, &response->status);
                break;
''',
'''            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                result = direct_pwm_requested == BOOL_TRUE ?
                    sys_calibration_service_set_point_direct_seq(
                        session_id, HAL_GetTick(), seq, level, logical_pwm,
                        &response->status) :
                    sys_calibration_service_set_point_seq(
                        session_id, HAL_GetTick(), seq, level,
                        &response->status);
                break;
''')

replace_once(
'''                cJSON_AddNumberToObject(dt, "leaseMs", response->lease_ms);
                break;
            case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
''',
'''                cJSON_AddNumberToObject(dt, "leaseMs", response->lease_ms);
                cJSON_AddNumberToObject(
                    dt, "calibrationVoltage01V",
                    response->status.calibration_voltage_01v);
                cJSON_AddNumberToObject(
                    dt, "calibrationSpanMa",
                    response->status.calibration_span_ma);
                break;
            case SYS_CALIBRATION_MQTT_OP_HEARTBEAT:
''')

replace_once(
'''            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                cJSON_AddNumberToObject(dt, "level", response->status.current_level);
                break;
''',
'''            case SYS_CALIBRATION_MQTT_OP_SET_POINT:
                cJSON_AddNumberToObject(dt, "level", response->status.current_level);
                cJSON_AddNumberToObject(dt, "actualPwm", response->status.actual_pwm);
                break;
''')

path.write_text(text, encoding='utf-8')
print('patched', path)
