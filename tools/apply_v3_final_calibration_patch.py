from pathlib import Path

path = Path('Core/Src/sys_pwm.c')
text = path.read_text(encoding='utf-8')


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'expected exactly one match, got {count}: {old[:120]!r}')
    text = text.replace(old, new, 1)

# BUILD/characterization SET_POINT must always exercise the mature uncalibrated
# transfer.  Otherwise recalibration would characterize an already committed
# correction curve and the inverse PWL table would be built from the wrong
# plant response.  Runtime pwm_output() still uses the committed/APPLIED curve
# first and falls back to the mature SET/HWMAX expression outside coverage.
replace_once(
'''    u16 logical_pwm;\n    u16 protected_pwm;\n    boolean_en calibrated_path;\n''',
'''    u16 logical_pwm;\n    u16 protected_pwm;\n''')

replace_once(
'''    calibrated_path = sys_calibration_service_output_pwm_for_current(\n        target_current_ma, &logical_pwm);\n    if (calibrated_path != BOOL_TRUE &&\n        sys_pwm_default_for_target(target_current_ma, &logical_pwm) != BOOL_TRUE)\n    {\n        sys_pwm_force_safe_off();\n        return BOOL_FALSE;\n    }\n''',
'''    /* BUILD/characterization intentionally bypasses any committed curve.\n     * The first 11-point sweep must measure the raw mature SET/HWMAX transfer.\n     * STAGE/APPLY verification uses SET_OUTPUT and therefore the staged curve. */\n    if (sys_pwm_default_for_target(target_current_ma, &logical_pwm) != BOOL_TRUE)\n    {\n        sys_pwm_force_safe_off();\n        return BOOL_FALSE;\n    }\n''')

replace_once(
'''    if (calibrated_path == BOOL_TRUE)\n    {\n        hw_tim1_pwm2_set_calibration_PWM_OUT(logical_pwm);\n    }\n    else\n    {\n        hw_tim1_pwm2_set_calibration_default_PWM_OUT(logical_pwm);\n    }\n''',
'''    hw_tim1_pwm2_set_calibration_default_PWM_OUT(logical_pwm);\n''')

path.write_text(text, encoding='utf-8')
print('patched', path)
