from pathlib import Path
import hashlib

FIX = Path('protocol/fixtures/CAL_MQTT_V2')

# The wire shape stays frozen. Only calibration-context values change from the
# 56V runtime default SET (890mA) to theoretical 50W/56V I100 (892mA).
updates = {
    'BEGIN_50W_56V.json': [
        ('"configuredRatedCurrentMa": 890', '"configuredRatedCurrentMa": 892'),
        ('"profileBindingCrc32": 2215728488', '"profileBindingCrc32": 3888210002'),
    ],
    'MAX_CONTEXT_50W_56V.json': [
        ('"configuredRatedCurrentMa": 890', '"configuredRatedCurrentMa": 892'),
        ('"profileBindingCrc32": 2984763304', '"profileBindingCrc32": 3526879890'),
    ],
    'STAGE_50W_56V_198B.json': [
        ('"configuredRatedCurrentMa": 890', '"configuredRatedCurrentMa": 892'),
        ('"profileBindingCrc32": 538152895', '"profileBindingCrc32": 1136900741'),
    ],
    'COMMIT_READBACK_50W_56V.json': [
        ('"configuredRatedCurrentMa": 890', '"configuredRatedCurrentMa": 892'),
        ('538152895', '1136900741'),
    ],
}
for name, replacements in updates.items():
    p = FIX / name
    text = p.read_text(encoding='utf-8')
    for old, new in replacements:
        if old not in text:
            raise SystemExit(f'{name}: missing {old}')
        text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')

p = Path('tools/calibration/test_cal_mqtt_v2.c')
text = p.read_text(encoding='utf-8')
for old, new in [
    ('560U, 890U, 890U,', '560U, 892U, 890U,'),
    ('== 538152895UL', '== 1136900741UL'),
    ('sys_product_profile_context_build(560U, 890U, 0U, 0U, &parsed)',
     'sys_product_profile_context_build(560U, 892U, 0U, 0U, &parsed)'),
    ('== 2215728488UL', '== 3888210002UL'),
]:
    if old not in text:
        raise SystemExit(f'test_cal_mqtt_v2.c: missing {old}')
    text = text.replace(old, new, 1)
p.write_text(text, encoding='utf-8')

# Refresh only the existing frozen fixture manifest entries, preserving order.
manifest = FIX / 'SHA256SUMS'
lines = []
for line in manifest.read_text(encoding='utf-8').splitlines():
    if not line.strip():
        continue
    _, name = line.split(None, 1)
    data = (FIX / name).read_bytes()
    lines.append(f'{hashlib.sha256(data).hexdigest()}  {name}')
manifest.write_text('\n'.join(lines) + '\n', encoding='utf-8')

print('CAL_MQTT_V2 fixture values/CRCs aligned to theoretical I100=892mA')
