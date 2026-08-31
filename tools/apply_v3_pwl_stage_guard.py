from pathlib import Path

path = Path('Core/Src/sys_calibration_service.c')
text = path.read_text(encoding='utf-8')


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'expected exactly one match, got {count}: {old[:160]!r}')
    text = text.replace(old, new, 1)


replace_once(
'''sys_calibration_result_en sys_calibration_service_stage_seq(
    u32 session_id,
''',
'''static boolean_en sys_calibration_service_session_output_axis_valid(
    const u8 payload[SYS_CALIBRATION_PAYLOAD_LENGTH])
{
    u16 span_ma = sys_calibration_service_calibration_span_ma();
    u32 index;

    if (payload == NULL || span_ma == 0U)
    {
        return BOOL_FALSE;
    }
    for (index = 0U; index < SYS_CALIBRATION_POINT_COUNT; ++index)
    {
        u32 offset = CALP_OUTPUT_OFFSET + index * CALP_OUTPUT_POINT_SIZE;
        u16 expected_ma = (u16)(((u32)span_ma * index +
                                 (SYS_CALIBRATION_POINT_COUNT - 1U) / 2U) /
                                (SYS_CALIBRATION_POINT_COUNT - 1U));
        u16 payload_ma = sys_calibration_get_u16_le(payload + offset + 2U);
        if (payload_ma != expected_ma)
        {
            return BOOL_FALSE;
        }
    }
    return BOOL_TRUE;
}

sys_calibration_result_en sys_calibration_service_stage_seq(
    u32 session_id,
''')

replace_once(
'''        result = sys_calibration_service_validate_payload(payload, length,
                                                            payload_crc32);
        sys_calibration_service_safe_off();
''',
'''        result = sys_calibration_service_validate_payload(payload, length,
                                                            payload_crc32);
        if (result == SYS_CALIBRATION_RESULT_OK &&
            sys_calibration_service_session_output_axis_valid(payload) != BOOL_TRUE)
        {
            /* Output X is the requested physical target current axis. The
             * external measured current belongs to the OCO/measurement tables,
             * never here. Require the exact 11-point 0..session-span axis. */
            result = SYS_CALIBRATION_RESULT_RANGE_ERROR;
        }
        sys_calibration_service_safe_off();
''')

path.write_text(text, encoding='utf-8')
print('patched', path)
