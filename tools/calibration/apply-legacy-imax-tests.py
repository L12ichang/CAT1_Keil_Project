from pathlib import Path
import hashlib, json

ROOT = Path(__file__).resolve().parents[2]
FIX = ROOT / 'protocol/fixtures/CAL_MQTT_V2'

def write_json(path: Path, data) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

def shift_exchange(exchange, delta=1):
    exchange['request']['DT']['seq'] += delta
    exchange['expectedResponse']['DT']['seq'] += delta
    rb = exchange['expectedResponse']['DT'].get('readback')
    if isinstance(rb, dict) and 'lastSeq' in rb:
        rb['lastSeq'] += delta

# Add explicit old-protocol 0x24/0x07 transaction after BEGIN.
max_fixture = {
  'fixtureVersion': 1,
  'name': 'MAX_CONTEXT_50W_56V',
  'requestTopic': 'MS/TEST50W0000001/plt2dev',
  'responseTopic': 'MS/TEST50W0000001/dev2plt',
  'request': {
    'SN': 'TEST50W0000001', 'TM': '2026-08-17 10:00:01.5', 'SV': 'cal', 'ID': 'X007', 'CT': 'W',
    'DT': {'op': 'SET_MAX_CONTEXT', 'protocolVersion': 2, 'sessionId': 1234, 'seq': 2,
           'frameHex': '3A240708436600000258037AB30D0A'}
  },
  'expectedResponse': {
    'DT': {'op': 'SET_MAX_CONTEXT', 'protocolVersion': 2, 'seq': 2, 'result': 0, 'ack': True,
           'readback': {
             'profileContext': {'contextVersion': 1, 'profileId': 50, 'modelCode': 'DL-50Z-56T-MXG',
               'profileVersion': 1, 'profileFingerprint': 2809625630, 'calibrationVoltage01V': 560,
               'configuredRatedCurrentMa': 890, 'calibratedMaxCurrentMa': 890, 'tableCrc32': 0},
             'profileBindingCrc32': 2984763304}}
  }
}
write_json(FIX / 'MAX_CONTEXT_50W_56V.json', max_fixture)

stage_path = FIX / 'STAGE_50W_56V_198B.json'
stage = json.loads(stage_path.read_text(encoding='utf-8'))
shift_exchange(stage)
write_json(stage_path, stage)

commit_path = FIX / 'COMMIT_READBACK_50W_56V.json'
commit = json.loads(commit_path.read_text(encoding='utf-8'))
for step in commit['steps']:
    shift_exchange(step)
write_json(commit_path, commit)

raw_path = FIX / 'RAW_50W_SNAPSHOT.json'
raw = json.loads(raw_path.read_text(encoding='utf-8'))
shift_exchange(raw)
write_json(raw_path, raw)

names = [
  'CAPABILITIES_50W_FIRST_CAL.json', 'BEGIN_50W_56V.json', 'MAX_CONTEXT_50W_56V.json',
  'STAGE_50W_56V_198B.json', 'COMMIT_READBACK_50W_56V.json', 'RAW_50W_SNAPSHOT.json',
]
manifest = ''.join(f"{hashlib.sha256((FIX / name).read_bytes()).hexdigest()}  {name}\n" for name in names)
(FIX / 'SHA256SUMS').write_text(manifest, encoding='utf-8', newline='\n')

# Service-level test: old 0x07 is mandatory before STAGE and device-current span need not equal Imax.
p = ROOT / 'tools/calibration/test_snapshot.c'
text = p.read_text(encoding='utf-8')
old = '''        const unsigned char raw_query[7U] =\n            {0x3AU, 0x26U, 0x08U, 0x00U, 0x2EU, 0x0DU, 0x0AU};\n'''
new = old + '''        const unsigned char legacy_max_frame[15U] =\n            {0x3AU, 0x24U, 0x07U, 0x08U, 0x43U, 0x66U, 0x00U, 0x00U,\n             0x02U, 0x58U, 0x03U, 0x7AU, 0xB3U, 0x0DU, 0x0AU};\n'''
if old not in text: raise SystemExit('test_snapshot raw_query block missing')
text = text.replace(old, new, 1)
old = '''        failures += expect_true(sys_product_profile_context_build(\n                                    560U, 890U, 890U,\n                                    sys_calibration_storage_crc32(\n                                        staged_payload, sizeof(staged_payload)),\n                                    &staged_context) == BOOL_TRUE,\n                                "staged context stores table-derived calibrated max");\n'''
new = old.replace('table-derived calibrated max', 'legacy 0x07 characterized Imax')
if old not in text: raise SystemExit('test_snapshot staged context block missing')
text = text.replace(old, new, 1)
old = '''        failures += expect_true(sys_calibration_service_raw_seq(\n                                    42U, 102U, 9U, raw_query, sizeof(raw_query),\n                                    SYS_CALIBRATION_RAW_QUERY, &service_status) ==\n                                    SYS_CALIBRATION_RESULT_NOT_AVAILABLE,\n                                "raw query is codec-checked then transport-gated");\n'''
new = '''        failures += expect_true(sys_calibration_service_raw_seq(\n                                    42U, 102U, 9U, legacy_max_frame,\n                                    sizeof(legacy_max_frame), SYS_CALIBRATION_RAW_SET,\n                                    &service_status) == SYS_CALIBRATION_RESULT_OK &&\n                                    service_status.context.calibrated_max_current_ma == 890U,\n                                "legacy 0x07 writes characterized Imax before STAGE");\n'''
if old not in text: raise SystemExit('test_snapshot old raw-query expectation missing')
text = text.replace(old, new, 1)
p.write_text(text, encoding='utf-8', newline='\n')

# MQTT fixture runner now includes the max-context exchange and continues with shifted seq values.
p = ROOT / 'tools/calibration/test_cal_mqtt_v2.c'
text = p.read_text(encoding='utf-8')
old = '''    failures += run_fixture(argv[1], "BEGIN_50W_56V.json");\n    failures += run_fixture(argv[1], "STAGE_50W_56V_198B.json");\n'''
new = '''    failures += run_fixture(argv[1], "BEGIN_50W_56V.json");\n    failures += run_fixture(argv[1], "MAX_CONTEXT_50W_56V.json");\n    failures += run_fixture(argv[1], "STAGE_50W_56V_198B.json");\n'''
if old not in text: raise SystemExit('test_cal_mqtt_v2 main fixture block missing')
text = text.replace(old, new, 1)
text = text.replace('\\"sessionId\\":1234,\\"seq\\":9}}";', '\\"sessionId\\":1234,\\"seq\\":10}}";', 1)
text = text.replace('\\"protocolVersion\\":2,\\"seq\\":9,', '\\"protocolVersion\\":2,\\"seq\\":10,', 1)
text = text.replace('\\"sessionId\\":1234,\\"seq\\":10}}";', '\\"sessionId\\":1234,\\"seq\\":11}}";', 1)
text = text.replace('\\"protocolVersion\\":2,\\"seq\\":10,', '\\"protocolVersion\\":2,\\"seq\\":11,', 1)
p.write_text(text, encoding='utf-8', newline='\n')

# Regression: a 36V measured Imax may be 1400mA although rated-power I100 is ~1388mA.
p = ROOT / 'tools/calibration/test_protocol_gates.c'
text = p.read_text(encoding='utf-8')
marker = '''    profile = sys_product_profile_current();\n    failures += expect_true(\n        profile != NULL && profile->profile_id == SYS_PRODUCT_PROFILE_ID_50W &&\n        profile->fingerprint_crc32 == SYS_PRODUCT_PROFILE_50W_FINGERPRINT_CRC32 &&\n        sys_product_profile_calculate_fingerprint(profile) == profile->fingerprint_crc32 &&\n        sys_product_profile_is_complete(profile) == BOOL_TRUE,\n        "selected default 50W profile is complete");\n'''
insert = marker + '''    failures += expect_true(\n        sys_product_profile_context_build(360U, 890U, 1400U, 0U,\n                                          &calibration_context) == BOOL_TRUE &&\n        calibration_context.calibrated_max_current_ma == 1400U,\n        "legacy measured Imax may exceed theoretical rated-power I100 within physical I-V");\n    failures += expect_true(\n        sys_product_profile_context_build(360U, 890U, 1401U, 0U,\n                                          &calibration_context) == BOOL_FALSE,\n        "legacy measured Imax remains bounded by physical I-V");\n'''
if marker not in text: raise SystemExit('test_protocol_gates profile marker missing')
text = text.replace(marker, insert, 1)
p.write_text(text, encoding='utf-8', newline='\n')

for rel in ['tools/calibration/apply-legacy-imax-tests.py', '.github/workflows/apply-legacy-imax-tests.yml']:
    q = ROOT / rel
    if q.exists(): q.unlink()
