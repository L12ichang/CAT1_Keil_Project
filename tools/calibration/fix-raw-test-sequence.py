from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = ROOT / 'tools/calibration/test_cal_mqtt_v2.c'
text = p.read_text(encoding='utf-8')
start = text.index('static int validate_tx_exhaustion_raw_rejected(void)')
end = text.index('static int validate_rejected_wire_shapes(void)', start)
block = text[start:end]
needle = '\\"seq\\\":10'
if needle not in block:
    raise SystemExit('expected seq=10 not found inside TX exhaustion test')
block = block.replace(needle, '\\"seq\\\":12')
text = text[:start] + block + text[end:]
p.write_text(text, encoding='utf-8', newline='\n')
for rel in ['tools/calibration/fix-raw-test-sequence.py', '.github/workflows/fix-raw-test-sequence.yml']:
    q = ROOT / rel
    if q.exists():
        q.unlink()
