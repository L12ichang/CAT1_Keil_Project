from pathlib import Path

p = Path('tools/calibration/test_snapshot.c')
text = p.read_text(encoding='utf-8')
repls = {
    '560U, 890U, 0U, 0U,': '560U, 892U, 0U, 0U,',
    '560U, 890U, 800U,': '560U, 892U, 800U,',
    'calibrated_target_current == 890U': 'calibrated_target_current == 892U',
    'gained_pwm == 556U': 'gained_pwm == 558U',
    'legacy 0x07 accepts pre-gain Imax below SET_OUTCUR': 'legacy 0x07 accepts pre-gain Imax below theoretical I100',
}
for old, new in repls.items():
    if old not in text:
        raise SystemExit(f'missing test_snapshot fixture: {old}')
    text = text.replace(old, new)
p.write_text(text, encoding='utf-8')
print('test_snapshot 50W/56V theoretical I100 fixtures updated to 892mA')
