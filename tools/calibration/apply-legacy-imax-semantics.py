from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding='utf-8', newline='\n')

def replace(path: str, old: str, new: str) -> None:
    text = read(path)
    if old not in text:
        raise SystemExit(f'expected block not found in {path}: {old[:140]!r}')
    write(path, text.replace(old, new, 1))

def replace_between(path: str, start: str, end: str, new: str) -> None:
    text = read(path)
    a = text.find(start)
    if a < 0:
        raise SystemExit(f'start marker not found in {path}: {start!r}')
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f'end marker not found in {path}: {end!r}')
    write(path, text[:a] + new + text[b:])

# Persisted record layout is unchanged, but format semantics changed: calibrated_max_current_ma
# now carries the legacy 0x07 characterized Imax. Reject old format-3 records fail-closed.
replace(
    'Core/Src/sys_calibration_storage.h',
    '#define SYS_CALIBRATION_STORAGE_FORMAT_VERSION 3U',
    '#define SYS_CALIBRATION_STORAGE_FORMAT_VERSION 4U',
)

# Product context: characterized Imax is a physical endpoint, not rated-power I100.
profile_helper = r'''static boolean_en sys_product_profile_validate_legacy_i_max(
    const sys_product_profile_st *profile,
    u16 calibration_voltage_01v,
    u16 characterized_i_max_ma)
{
    u32 index;
    u16 iv_limit_ma = 0U;

    if (characterized_i_max_ma == 0U)
    {
        return BOOL_TRUE;
    }
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        calibration_voltage_01v < profile->minimum_voltage_01v ||
        calibration_voltage_01v > profile->maximum_voltage_01v ||
        calibration_voltage_01v == profile->special_test_voltage_01v)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (calibration_voltage_01v >= profile->iv_limits[index].voltage_01v)
        {
            iv_limit_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (iv_limit_ma == 0U || characterized_i_max_ma > iv_limit_ma ||
        characterized_i_max_ma > profile->hw_max_current_ma ||
        characterized_i_max_ma >= profile->absolute_fail_current_ma)
    {
        return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

'''
replace(
    'Core/Src/sys_product_profile.c',
    'boolean_en sys_product_profile_context_build(\n',
    profile_helper + 'boolean_en sys_product_profile_context_build(\n',
)
replace_between(
    'Core/Src/sys_product_profile.c',
    'boolean_en sys_product_profile_context_build(\n',
    'boolean_en sys_product_profile_context_validate(\n',
    r'''boolean_en sys_product_profile_context_build(
    u16 calibration_voltage_01v,
    u16 configured_rated_current_ma,
    u16 calibrated_max_current_ma,
    u32 table_crc32,
    sys_calibration_context_st *context)
{
    const sys_product_profile_st *profile = sys_product_profile_current();

    if (context == NULL ||
        sys_product_profile_validate_runtime_current(
            profile, calibration_voltage_01v,
            configured_rated_current_ma) != SYS_PRODUCT_CURRENT_VALID ||
        sys_product_profile_validate_legacy_i_max(
            profile, calibration_voltage_01v,
            calibrated_max_current_ma) != BOOL_TRUE ||
        (calibrated_max_current_ma != 0U &&
         configured_rated_current_ma > calibrated_max_current_ma))
    {
        return BOOL_FALSE;
    }
    context->profile_id = profile->profile_id;
    context->profile_version = profile->profile_version;
    context->profile_fingerprint_crc32 = profile->fingerprint_crc32;
    context->calibration_voltage_01v = calibration_voltage_01v;
    context->configured_rated_current_ma = configured_rated_current_ma;
    /* Legacy protocol meaning: after 0x24/0x07 this is the measured Imax.
       It is not the theoretical rated-power I100 and not SET_OUTCUR. */
    context->calibrated_max_current_ma = calibrated_max_current_ma;
    context->table_crc32 = table_crc32;
    return BOOL_TRUE;
}

''',
)
replace(
    'Core/Src/sys_product_profile.c',
    "        (require_table_crc != BOOL_TRUE &&\n         (context->table_crc32 != 0U || context->calibrated_max_current_ma != 0U)))",
    "        (require_table_crc != BOOL_TRUE && context->table_crc32 != 0U))",
)

# Service table validation is anchored to the characterized 0x07 Imax.
replace_between(
    'Core/Src/sys_calibration_service.c',
    'static boolean_en sys_calibration_service_validate_table(\n',
    'static boolean_en sys_calibration_service_get_runtime_table(\n',
    r'''static boolean_en sys_calibration_service_validate_table(
    const sys_calibration_driver_table_st *table,
    u16 *calibrated_max_current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 characterized_i_max_ma;
    u32 minimum_span_current_ma;
    u32 maximum_span_current_ma;
    u8 index;

    if (table == NULL || calibrated_max_current_ma == NULL ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE ||
        sys_product_profile_context_validate(&_service.status.context,
            (_service.status.context.table_crc32 != 0U) ?
                BOOL_TRUE : BOOL_FALSE) != BOOL_TRUE ||
        _service.status.context.calibrated_max_current_ma == 0U)
    {
        return BOOL_FALSE;
    }
    characterized_i_max_ma =
        _service.status.context.calibrated_max_current_ma;
    for (index = 1U; index < SYS_CALIBRATION_DRIVER_POINT_COUNT; ++index)
    {
        if (table->point[index].instrument_output_current_ma <
                table->point[index - 1U].instrument_output_current_ma ||
            table->point[index].instrument_output_power_01w <
                table->point[index - 1U].instrument_output_power_01w ||
            table->point[index].device_output_current_ma <
                table->point[index - 1U].device_output_current_ma ||
            table->point[index].device_output_power_01w <
                table->point[index - 1U].device_output_power_01w ||
            table->point[index].input_current_ma <
                table->point[index - 1U].input_current_ma ||
            table->point[index].input_power_01w <
                table->point[index - 1U].input_power_01w ||
            table->point[index].input_current_ad <
                table->point[index - 1U].input_current_ad)
        {
            return BOOL_FALSE;
        }
    }
    minimum_span_current_ma =
        ((u32)characterized_i_max_ma *
         (1000U - profile->calibration_span_tolerance_permille)) / 1000U;
    maximum_span_current_ma =
        ((u32)characterized_i_max_ma *
         (1000U + profile->calibration_span_tolerance_permille) + 999U) /
        1000U;

    /* The old protocol defines full scale by the external instrument Imax
       measured in the 50mA->Umax->CV pre-stage. Device current is not used
       as the span reference because the 11-point table exists to correct it. */
    if (table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma >= profile->absolute_fail_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .device_output_current_ma >= profile->absolute_fail_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma < minimum_span_current_ma ||
        table->point[SYS_CALIBRATION_DRIVER_POINT_COUNT - 1U]
            .instrument_output_current_ma > maximum_span_current_ma)
    {
        return BOOL_FALSE;
    }
    *calibrated_max_current_ma = characterized_i_max_ma;
    return BOOL_TRUE;
}

''',
)

# Re-enable the frozen legacy 0x07 frame as the only RAW_SET side effect.
replace_between(
    'Core/Src/sys_calibration_service.c',
    'sys_calibration_result_en sys_calibration_service_raw_seq(\n',
    'sys_calibration_result_en sys_calibration_service_snapshot_seq(\n',
    r'''sys_calibration_result_en sys_calibration_service_raw_seq(
    u32 session_id,
    u32 now_ms,
    u32 seq,
    const u8 *frame,
    u16 frame_length,
    sys_calibration_raw_direction_en direction,
    sys_calibration_service_status_st *status)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    sys_calibration_driver_message_st message;
    sys_calibration_driver_max_context_st max_context;
    sys_calibration_result_en result;
    u16 iv_limit_ma = 0U;
    u32 index;

    if (sys_calibration_service_check_replay(
            session_id, seq, SYS_CALIBRATION_OP_RAW, &result, status) ==
        BOOL_TRUE)
    {
        return result;
    }
    if (direction != SYS_CALIBRATION_RAW_SET || frame == NULL ||
        sys_calibration_driver_decode(frame, frame_length, &message) != BOOL_TRUE ||
        message.command != SYS_CALIBRATION_DRIVER_CMD_SET ||
        message.offset != SYS_CALIBRATION_DRIVER_OFFSET_MAX_CONTEXT ||
        sys_calibration_driver_max_context_decode(
            message.data, message.length, &max_context) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_PROTOCOL_ERROR,
                                       status);
        return SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;
    }
    result = sys_calibration_service_require_session(
        session_id, now_ms, SYS_CALIBRATION_OP_RAW, seq, status);
    if (result != SYS_CALIBRATION_RESULT_OK)
    {
        return result;
    }
    if (_service.status.state != SYS_CALIBRATION_STATE_ACTIVE ||
        _service.status.context.calibrated_max_current_ma != 0U ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_INVALID_STATE,
                                       status);
        return SYS_CALIBRATION_RESULT_INVALID_STATE;
    }
    for (index = 0U; index < profile->iv_limit_count; ++index)
    {
        if (_service.status.context.calibration_voltage_01v >=
                profile->iv_limits[index].voltage_01v)
        {
            iv_limit_ma = profile->iv_limits[index].current_ma;
        }
        else
        {
            break;
        }
    }
    if (max_context.input_ac_voltage_float_bits == 0U ||
        max_context.maximum_output_voltage_01v == 0U ||
        max_context.maximum_output_current_ma == 0U ||
        iv_limit_ma == 0U ||
        max_context.maximum_output_current_ma <
            _service.status.context.configured_rated_current_ma ||
        max_context.maximum_output_current_ma > iv_limit_ma ||
        max_context.maximum_output_current_ma > profile->hw_max_current_ma ||
        max_context.maximum_output_current_ma >= profile->absolute_fail_current_ma)
    {
        sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                       SYS_CALIBRATION_RESULT_SAFETY_NOT_READY,
                                       status);
        return SYS_CALIBRATION_RESULT_SAFETY_NOT_READY;
    }

    /* Persisted context field keeps its wire name for compatibility, but in
       the legacy flow its semantic value is exactly the measured 0x07 Imax. */
    _service.status.context.calibrated_max_current_ma =
        max_context.maximum_output_current_ma;
    sys_calibration_service_finish(session_id, seq, SYS_CALIBRATION_OP_RAW,
                                   SYS_CALIBRATION_RESULT_OK, status);
    return SYS_CALIBRATION_RESULT_OK;
}

''',
)
replace(
    'Core/Src/sys_calibration_service.c',
    "    if (session_id == 0U || context == NULL || context->table_crc32 != 0U ||\n",
    "    if (session_id == 0U || context == NULL || context->table_crc32 != 0U ||\n        context->calibrated_max_current_ma != 0U ||\n",
)

# MQTT transport wrapper: the payload remains the literal old 15-byte 0x24/0x07 frame.
replace(
    'Core/Src/sys_calibration_mqtt.c',
    '''    else if (strcmp(operation, "SET_VALIDATION_PERCENT") == 0)\n''',
    '''    else if (strcmp(operation, "SET_MAX_CONTEXT") == 0)\n    {\n        if (!cJSON_IsString(cJSON_GetObjectItem(dt, "frameHex")) ||\n            sys_calibration_mqtt_read_payload(\n                dt, "frameHex", payload, sizeof(payload), &length) != BOOL_TRUE)\n        {\n            result = SYS_CALIBRATION_RESULT_PROTOCOL_ERROR;\n        }\n        else\n        {\n            result = sys_calibration_service_raw_seq(\n                session_id, HAL_GetTick(), seq, payload, length,\n                SYS_CALIBRATION_RAW_SET, &status);\n        }\n    }\n    else if (strcmp(operation, "SET_VALIDATION_PERCENT") == 0)\n''',
)

# Remove one-shot patch machinery from resulting branch commit.
for rel in [
    'tools/calibration/apply-legacy-imax-semantics.py',
    '.github/workflows/apply-legacy-imax-semantics.yml',
]:
    p = ROOT / rel
    if p.exists():
        p.unlink()
