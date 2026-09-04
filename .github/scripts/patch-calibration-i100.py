from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding='utf-8')
    if old not in text:
        raise SystemExit(f'expected block not found in {path}: {old[:120]!r}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

# 1) The frozen CAL_MQTT field name stays configuredRatedCurrentMa, but its
# calibration meaning is the theoretical I100 at the selected calibration CV.
replace(
    'Core/Src/sys_product_profile.c',
    '''boolean_en sys_product_profile_context_build(\n    u16 calibration_voltage_01v,\n    u16 configured_rated_current_ma,\n    u16 calibrated_max_current_ma,\n    u32 table_crc32,\n    sys_calibration_context_st *context)\n{\n    const sys_product_profile_st *profile = sys_product_profile_current();\n\n    if (context == NULL ||\n        sys_product_profile_validate_runtime_current(\n            profile, calibration_voltage_01v,\n            configured_rated_current_ma) != SYS_PRODUCT_CURRENT_VALID ||\n        sys_product_profile_validate_legacy_i_max(\n            profile, calibration_voltage_01v,\n            calibrated_max_current_ma) != BOOL_TRUE)\n    {\n        return BOOL_FALSE;\n    }\n''',
    '''boolean_en sys_product_profile_context_build(\n    u16 calibration_voltage_01v,\n    u16 configured_rated_current_ma,\n    u16 calibrated_max_current_ma,\n    u32 table_crc32,\n    sys_calibration_context_st *context)\n{\n    const sys_product_profile_st *profile = sys_product_profile_current();\n    u16 theoretical_i100_ma;\n\n    /* Keep the frozen wire field name, but bind it to the product algorithm:\n       calibration target I100 = rated power / selected CV, limited by the\n       existing I-V table and Hardware Max. It is not the 56V default SET. */\n    if (context == NULL ||\n        sys_product_profile_compute_i100_ma(\n            profile, calibration_voltage_01v,\n            &theoretical_i100_ma) != BOOL_TRUE ||\n        configured_rated_current_ma != theoretical_i100_ma ||\n        sys_product_profile_validate_runtime_current(\n            profile, calibration_voltage_01v,\n            configured_rated_current_ma) != SYS_PRODUCT_CURRENT_VALID ||\n        sys_product_profile_validate_legacy_i_max(\n            profile, calibration_voltage_01v,\n            calibrated_max_current_ma) != BOOL_TRUE)\n    {\n        return BOOL_FALSE;\n    }\n''')
replace(
    'Core/Src/sys_product_profile.c',
    '''    context->configured_rated_current_ma = configured_rated_current_ma;\n    /* Legacy protocol meaning: after 0x24/0x07 this is the measured Imax.\n       It is not the theoretical rated-power I100 and not SET_OUTCUR. */\n''',
    '''    /* Frozen field name; calibration semantics are theoretical I100 at\n       calibration_voltage_01v, not the runtime/default SET_OUTCUR value. */\n    context->configured_rated_current_ma = configured_rated_current_ma;\n    /* Legacy protocol meaning: after 0x24/0x07 this is the measured Imax.\n       It is not the theoretical rated-power I100 and not SET_OUTCUR. */\n''')

# 2) BEGIN/profile context is no longer required to equal runtime SET_OUTCUR.
# Context validation above proves the value is the theoretical I100.
replace(
    'Core/Src/sys_calibration_mqtt.c',
    '''    if (model_code == NULL || !cJSON_IsString(model_code) ||\n        model_code->valuestring == NULL ||\n        strcmp(model_code->valuestring, profile->model_code) != 0 ||\n        context->configured_rated_current_ma != SET_OUTCUR)\n''',
    '''    if (model_code == NULL || !cJSON_IsString(model_code) ||\n        model_code->valuestring == NULL ||\n        strcmp(model_code->valuestring, profile->model_code) != 0)\n''')

# 3) Calibration CV comes from the calibration session / electronic-load CV.
# Do not require it to equal the device's normal bound voltage during a session.
replace(
    'Core/Src/sys_calibration_service.c',
    '''        sys_product_profile_context_validate(\n            &_service.status.context,\n            (_service.status.state == SYS_CALIBRATION_STATE_STAGED ||\n             _service.status.state == SYS_CALIBRATION_STATE_APPLIED) ?\n                BOOL_TRUE : BOOL_FALSE) != BOOL_TRUE ||\n        _service.get_bound_voltage == NULL ||\n        _service.status.context.calibration_voltage_01v !=\n            _service.get_bound_voltage())\n''',
    '''        sys_product_profile_context_validate(\n            &_service.status.context,\n            (_service.status.state == SYS_CALIBRATION_STATE_STAGED ||\n             _service.status.state == SYS_CALIBRATION_STATE_APPLIED) ?\n                BOOL_TRUE : BOOL_FALSE) != BOOL_TRUE)\n''')
replace(
    'Core/Src/sys_calibration_service.c',
    '''    if (session_id == 0U || context == NULL || context->table_crc32 != 0U ||\n        context->calibrated_max_current_ma != 0U ||\n        sys_product_profile_context_validate(context, BOOL_FALSE) != BOOL_TRUE ||\n        _service.get_bound_voltage == NULL ||\n        context->calibration_voltage_01v != _service.get_bound_voltage() ||\n        sys_calibration_service_validate_lease(lease_ms) != BOOL_TRUE)\n''',
    '''    if (session_id == 0U || context == NULL || context->table_crc32 != 0U ||\n        context->calibrated_max_current_ma != 0U ||\n        sys_product_profile_context_validate(context, BOOL_FALSE) != BOOL_TRUE ||\n        sys_calibration_service_validate_lease(lease_ms) != BOOL_TRUE)\n''')
replace(
    'Core/Src/sys_calibration_service.c',
    '''    /* 0x07 Imax is the pre-gain Level200 measurement. After it is written,\n       firmware applies the per-device full-scale gain before the formal\n       11-point sweep, so Level200 must land near configured SET_OUTCUR. */\n''',
    '''    /* 0x07 Imax is the pre-gain Level200 measurement. After it is written,\n       firmware applies the per-device full-scale gain before the formal\n       11-point sweep, so Level200 must land near the theoretical I100 for\n       this calibration voltage. */\n''')
replace(
    'Core/Src/sys_calibration_service.c',
    '''    /* Persist the raw pre-gain Level200 Imax. The gain is derived from\n       SET_OUTCUR/Imax at runtime, so no new wire field or Flash coefficient\n       is required and the old 0x07 protocol remains unchanged. */\n''',
    '''    /* Persist the raw pre-gain Level200 Imax. The gain is derived from\n       theoretical calibration I100 / Imax, so no new wire field or Flash\n       coefficient is required and the old 0x07 protocol remains unchanged. */\n''')

# 4) Normal calibrated output must target the same voltage-derived I100 used
# to build the table. The table remains voltage-bound at runtime.
replace(
    'Core/Src/sys_pwm.c',
    '''          if (persent > 0U)\n          {\n              if (sys_calibration_service_correct_output_percent(\n                      persent, (u16)SET_OUTCUR,\n                      &calibrated_percent) == BOOL_TRUE &&\n                  sys_product_profile_compute_i100_ma(\n                      sys_product_profile_current(),\n                      BOUND_OUTPUT_VOLTAGE_01V,\n                      &pwm_current_reference_ma) == BOOL_TRUE)\n              {\n''',
    '''          if (persent > 0U)\n          {\n              if (sys_product_profile_compute_i100_ma(\n                      sys_product_profile_current(),\n                      BOUND_OUTPUT_VOLTAGE_01V,\n                      &pwm_current_reference_ma) == BOOL_TRUE &&\n                  sys_calibration_service_correct_output_percent(\n                      persent, pwm_current_reference_ma,\n                      &calibrated_percent) == BOOL_TRUE)\n              {\n''')
replace(
    'Core/Src/sys_pwm.c',
    '''        sys_product_profile_compute_i100_ma(\n            profile, context.calibration_voltage_01v,\n            &calibration_max_current_ma) != BOOL_TRUE ||\n        sys_calibration_service_runtime_context_matches_voltage(\n            BOUND_OUTPUT_VOLTAGE_01V) != BOOL_TRUE ||\n        Error_1_OL != 0U || Error_Out_LV != 0U ||\n''',
    '''        sys_product_profile_compute_i100_ma(\n            profile, context.calibration_voltage_01v,\n            &calibration_max_current_ma) != BOOL_TRUE ||\n        Error_1_OL != 0U || Error_Out_LV != 0U ||\n''')

print('firmware calibration I100/CV semantics patched')
