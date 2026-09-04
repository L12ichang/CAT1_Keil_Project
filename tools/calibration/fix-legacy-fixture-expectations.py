from pathlib import Path
import hashlib, json

ROOT = Path(__file__).resolve().parents[2]
FIX = ROOT / 'protocol/fixtures/CAL_MQTT_V2'

cap_path = FIX / 'CAPABILITIES_50W_FIRST_CAL.json'
cap = json.loads(cap_path.read_text(encoding='utf-8'))
cap['expectedResponse']['DT']['profilesCsv'] = (
    '50,DL-50Z-56T-MXG,1,50,890,120,1680,1680,1,1,OK;'
    '75,DL-75Z-56T-MXG,2,75,1360,50,2150,2150,1,1,OK;'
    '100,DL-100Z-56T-MXG,3,100,1780,50,2800,2800,1,1,OK;'
    '150,DL-150Z-56T-MXG,4,150,2700,30,4500,4500,1,1,OK;'
    '200,DL-200Z-56T-MXG,5,200,3600,15,6000,6000,1,1,OK;'
    '240,DL-240Z-56T-MXG,6,240,4300,15,7000,7000,1,1,OK'
)
cap_path.write_text(json.dumps(cap, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

p = ROOT / 'tools/calibration/test_cal_mqtt_v2.c'
text = p.read_text(encoding='utf-8')
text = text.replace(
    '\\"sessionId\\":1234,\\"lastSeq\\":9}}}";',
    '\\"sessionId\\":1234,\\"lastSeq\\":11}}}";',
    1,
)
text = text.replace(
    '\\"sessionId\\":1234,\\"seq\\":10}}}";',
    '\\"sessionId\\":1234,\\"seq\\":12}}}";',
    1,
)
text = text.replace(
    '\\"protocolVersion\\":2,\\"seq\\":10,',
    '\\"protocolVersion\\":2,\\"seq\\":12,',
    1,
)
p.write_text(text, encoding='utf-8', newline='\n')

names = [
    'CAPABILITIES_50W_FIRST_CAL.json', 'BEGIN_50W_56V.json', 'MAX_CONTEXT_50W_56V.json',
    'STAGE_50W_56V_198B.json', 'COMMIT_READBACK_50W_56V.json', 'RAW_50W_SNAPSHOT.json',
]
manifest = ''.join(f"{hashlib.sha256((FIX / name).read_bytes()).hexdigest()}  {name}\n" for name in names)
(FIX / 'SHA256SUMS').write_text(manifest, encoding='utf-8', newline='\n')

for rel in ['tools/calibration/fix-legacy-fixture-expectations.py', '.github/workflows/fix-legacy-fixture-expectations.yml']:
    path = ROOT / rel
    if path.exists():
        path.unlink()
