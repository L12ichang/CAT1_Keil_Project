from pathlib import Path
p = Path('tools/calibration/test_protocol_gates.c')
text = p.read_text(encoding='utf-8')
for old, new in [
    ('sys_product_profile_context_build(360U, 890U, 1400U, 0U,',
     'sys_product_profile_context_build(360U, 1388U, 1400U, 0U,'),
    ('sys_product_profile_context_build(360U, 890U, 1401U, 0U,',
     'sys_product_profile_context_build(360U, 1388U, 1401U, 0U,'),
    ('sys_product_profile_context_build(\n                                560U, 890U, 890U,',
     'sys_product_profile_context_build(\n                                560U, 892U, 890U,'),
    ('"calibration context binds old table maximum and CRC"',
     '"calibration context binds theoretical I100, old measured Imax and CRC"'),
]:
    if old not in text:
        raise SystemExit(f'missing protocol gate fixture: {old}')
    text = text.replace(old, new, 1)
p.write_text(text, encoding='utf-8')
print('protocol/storage fixtures aligned to selected-CV theoretical I100')
