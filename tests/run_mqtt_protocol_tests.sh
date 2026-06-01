#!/usr/bin/env bash
set -euo pipefail
python3 -m unittest \
  tests/test_mqtt_protocol_refactor.py \
  tests/test_login_validation.py \
  tests/test_mqtt_password_contract.py \
  tests/test_phase2_legacy_cleanup.py \
  tests/test_phase3_json_skeleton.py \
  tests/test_phase4_login_heartbeat.py \
  -v
