from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
p = ROOT / 'tools/calibration/test_cal_mqtt_v2.c'
text = p.read_text(encoding='utf-8')
old_request = '''static int validate_tx_exhaustion_raw_rejected(void)\n{\n    static const char request_text[] =\n        "{\\\"SN\\\":\\\"TEST50W0000001\\\",\\\"TM\\\":\\\"2026-08-19 06:50:45\\\","\n        "\\\"SV\\\":\\\"cal\\\",\\\"ID\\\":\\\"W010\\\",\\\"CT\\\":\\\"R\\\",\\\"DT\\\":{"\n        "\\\"op\\\":\\\"RAW\\\",\\\"protocolVersion\\\":2,"\n        "\\\"sessionId\\\":1234,\\\"seq\\\":10}}";\n    static const char expected_text[] =\n        "{\\\"DT\\\":{\\\"op\\\":\\\"RAW\\\",\\\"protocolVersion\\\":2,\\\"seq\\\":10,"\n        "\\\"result\\\":1,\\\"ack\\\":false}}";\n'''
new_request = old_request.replace('\\\"seq\\\":10', '\\\"seq\\\":12')
if old_request not in text:
    raise SystemExit('validate_tx_exhaustion_raw_rejected old sequence block not found')
text = text.replace(old_request, new_request, 1)
p.write_text(text, encoding='utf-8', newline='\n')
for rel in ['tools/calibration/fix-raw-test-sequence.py', '.github/workflows/fix-raw-test-sequence.yml']:
    q = ROOT / rel
    if q.exists(): q.unlink()
