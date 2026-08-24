#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-persistent-v3.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DSYS_DATA_HOST_TEST \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Core/Src/gateway" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_persistent_record.c" \
  "${ROOT_DIR}/Core/Src/sys_persistent_storage.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.c" \
  "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_runtime_stats.c" \
  "${ROOT_DIR}/tools/calibration/test_persistent_v3.c" \
  -o "${BUILD_DIR}/test_persistent_v3"

"${BUILD_DIR}/test_persistent_v3"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DSYS_DATA_HOST_TEST \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/factory_user_data.c" \
  "${ROOT_DIR}/tools/calibration/test_factory_config_v3.c" \
  -o "${BUILD_DIR}/test_factory_config_v3"

"${BUILD_DIR}/test_factory_config_v3"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DSYS_DATA_HOST_TEST \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Core/Src/gateway" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_data.c" \
  "${ROOT_DIR}/Core/Src/sys_temp_over_protect.c" \
  "${ROOT_DIR}/tools/calibration/test_sys_data_temp_v3.c" \
  -o "${BUILD_DIR}/test_sys_data_temp_v3"

"${BUILD_DIR}/test_sys_data_temp_v3"

PRODUCTION_FILES=(
  "${ROOT_DIR}/Core/Src/main.c"
  "${ROOT_DIR}/Core/Src/LampProtocolLib/ota.c"
  "${ROOT_DIR}/Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
  "${ROOT_DIR}/Core/Src/sys_data.c"
  "${ROOT_DIR}/Core/Src/sys_calibration_flash.c"
  "${ROOT_DIR}/Core/Src/sys_calibration_flash.h"
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.c"
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.h"
  "${ROOT_DIR}/Core/Src/flash_address_assignment.h"
)

PYTHONDONTWRITEBYTECODE=1 python3 - "${ROOT_DIR}" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
source = (root / "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c").read_text()
erase_start = source.index("HAL_StatusTypeDef HAL_FLASHEx_Erase")
erase_lock = source.index("__HAL_LOCK(&pFlash)", erase_start)
hk32_gate = source.index("0x400220D0U", erase_start)
assert hk32_gate < erase_lock, "HK32 Flash operation gate must be cleared before page erase"
PY

if rg -n \
    'CAT1_FLASH_OTA_REPORT|ZK_OTA_REPORT_FLASH|DATAROM_STARTADDR|BAKDATAROM_STARTADDR|sys_data\.sn|sys_calibration_flash_commit[[:space:]]*\(' \
    "${PRODUCTION_FILES[@]}"; then
  echo "error: reachable V2 Persistent/OTA writer remains" >&2
  exit 1
fi
if rg -n 'sys_data_store[[:space:]]*\(' \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/ota.c"; then
  echo "error: OTA still routes Boot flag/error state through legacy sys_data" >&2
  exit 1
fi

verified_line="$(rg -n 'zk_ota_report_mark_verified' \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/ota.c" | head -1 | cut -d: -f1)"
flag_line="$(rg -n 'sys_persistent_ota_flag_mark' \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/ota.c" | head -1 | cut -d: -f1)"
reset_line="$(rg -n 'iap_jump2boot\(\);' \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/ota.c" | head -1 | cut -d: -f1)"
if [[ -z "${verified_line}" || -z "${flag_line}" || -z "${reset_line}" ||
      "${verified_line}" -ge "${flag_line}" || "${flag_line}" -ge "${reset_line}" ]]; then
  echo "error: OTA activation must persist RUN1, mark flag, then reset" >&2
  exit 1
fi

config_line="$(rg -n '^[[:space:]]*sys_data_load\(\);' "${ROOT_DIR}/Core/Src/main.c" | cut -d: -f1)"
boot_line="$(rg -n 'sys_calibration_flash_boot_load_v3' "${ROOT_DIR}/Core/Src/main.c" | cut -d: -f1)"
if [[ -z "${config_line}" || -z "${boot_line}" || "${config_line}" -ge "${boot_line}" ]]; then
  echo "error: main must initialize/load V3 Config before boot_load_v3" >&2
  exit 1
fi
if ! rg -q 'sys_persistent_ota_flag_clear_preserving_config' \
    "${ROOT_DIR}/Core/Src/main.c"; then
  echo "error: new APP startup does not safely clear the Boot OTA flag" >&2
  exit 1
fi
if ! rg -q 'sys_persistent_config_update_sections' \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_property.c" ||
   rg -n 'validate_calibrated|calibrated_max' \
    "${ROOT_DIR}/Core/Src/factory_user_data.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_property.c"; then
  echo "error: Factory/User Config is not a single decoupled CFG1 transaction" >&2
  exit 1
fi

for source_name in sys_persistent_record.c sys_persistent_storage.c; do
  if [[ "$(rg -c "<FileName>${source_name}</FileName>" \
      "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ]]; then
    echo "error: ${source_name} is not present in both Keil targets" >&2
    exit 1
  fi
done

if [[ "$(rg -c '<TargetName>CAT1_50W</TargetName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c '<TargetName>CAT1_50W_Debug</TargetName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c '<OutputName>CAT1_50W</OutputName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c '<OutputName>CAT1_50W_Debug</OutputName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ]]; then
  echo "error: Keil release/debug TargetName and OutputName must be unique 50W names" >&2
  exit 1
fi
if [[ "$(rg -c '<TargetName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ||
      "$(rg -c '<OutputName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ||
      "$(rg -c '<targetInfo name="CAT1_50W"/>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c '<targetInfo name="CAT1_50W_Debug"/>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c '<targetInfo name=' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ]]; then
  echo "error: Keil Target/Output/RTE CMSIS bindings are not the unique 50W pair" >&2
  exit 1
fi
if [[ "$(rg -c '<TargetName>CAT1_50W</TargetName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvoptx")" -ne 1 ||
      "$(rg -c '<TargetName>CAT1_50W_Debug</TargetName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvoptx")" -ne 1 ||
      "$(rg -c 'hex2bin_arm\.bat CAT1_50W</UserProg2Name>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ||
      "$(rg -c 'hex2bin_arm\.bat CAT1_50W_Debug</UserProg2Name>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 1 ]]; then
  echo "error: Keil target options/post-build conversion do not use unique 50W names" >&2
  exit 1
fi
if ! rg -q 'APP_BIN=%~dp0out\\CAT1_50W\.bin' \
      "${ROOT_DIR}/MDK-ARM-8008000/flash_app_keil.bat" ||
   ! rg -q -- '--target", default="CAT1_50W"' \
      "${ROOT_DIR}/tools/ota_test/release_minsize_acceptance_audit.py" ||
   ! rg -q 'MDK-ARM-8008000/out/CAT1_50W\.bin' \
      "${ROOT_DIR}/tools/ota_test/release_minsize_acceptance_audit.py" ||
   rg -n 'Release-MinSize|out[\\/]cat1\.(bin|hex|axf|map)' \
      "${ROOT_DIR}/tools/ota_test/release_minsize_acceptance_audit.py" \
      "${ROOT_DIR}/tools/ota_test/mqtt_rtc_plan_probe.py" \
      "${ROOT_DIR}/MDK-ARM-8008000/flash_app_keil.bat" \
      "${ROOT_DIR}/MDK-ARM-8008000/out/hex2bin_arm.bat" \
      "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx" \
      "${ROOT_DIR}/MDK-ARM-8008000/project.uvoptx"; then
  echo "error: release tool still uses a legacy Keil target/output name" >&2
  exit 1
fi
if ! rg -q '"default_firmware": "MDK-ARM-8008000/out/CAT1_50W\.bin"' \
      "${ROOT_DIR}/config/mcu_workflow.json" ||
   ! rg -q '"fallback_firmware": \[\]' \
      "${ROOT_DIR}/config/mcu_workflow.json" ||
   ! rg -q 'mqtt_cat1_50w_rtc_plan_' \
      "${ROOT_DIR}/tools/ota_test/mqtt_rtc_plan_probe.py" ||
   rg -n 'out/cat1\.bin|mqtt_release_minsize_rtc_plan_' \
      "${ROOT_DIR}/config/mcu_workflow.json" \
      "${ROOT_DIR}/tools/ota_test/mqtt_rtc_plan_probe.py" ||
   rg -n 'program|Release-MinSize' \
      "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx" \
      "${ROOT_DIR}/MDK-ARM-8008000/project.uvoptx"; then
  echo "error: active workflow/probe/Keil metadata retains a legacy default" >&2
  exit 1
fi

heartbeat_block="$(sed -n \
  '/^sys_calibration_result_en sys_calibration_service_heartbeat_seq/,/^sys_calibration_result_en sys_calibration_service_set_point_seq/p' \
  "${ROOT_DIR}/Core/Src/sys_calibration_service.c")"
if printf '%s\n' "${heartbeat_block}" | \
   rg -n '_set_inhibit|sys_persistent|zk_runtime|runtime_commit'; then
  echo "error: HEARTBEAT must not write RUN1 or calibration inhibit" >&2
  exit 1
fi

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="${ROOT_DIR}/tools" \
python3 - "${ROOT_DIR}" <<'PY'
import sys
from pathlib import Path

from check_keil_app_image import run_checks

root = Path(sys.argv[1])
project = root / "MDK-ARM-8008000" / "project.uvprojx"
source_report = run_checks(project_path=project, require_fresh=False)
assert source_report.passed, source_report.errors
assert source_report.output_name == "CAT1_50W", source_report.output_name

fresh_report = run_checks(project_path=project, require_fresh=True)
if not fresh_report.passed:
    assert all(
        error.startswith("Keil output missing:") or
        error.startswith("Keil outputs are older than source/project files")
        for error in fresh_report.errors
    ), fresh_report.errors
PY
if [[ "$(rg -c 'PRODUCT_TARGET_[A-Za-z0-9_]+' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ||
      "$(rg -c 'PRODUCT_TARGET_50W' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx")" -ne 2 ]]; then
  echo "error: each Keil target must define only PRODUCT_TARGET_50W" >&2
  exit 1
fi
if rg -q '<FileName>data_backup.c</FileName>' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx"; then
  echo "error: retired legacy data_backup writer remains in a Keil target" >&2
  exit 1
fi

echo "Persistent V3 ownership/static integration gates: PASS"
