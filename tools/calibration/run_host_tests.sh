#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-calibration-tests.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_calibration_snapshot.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_service.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_dc5200.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.c" \
  "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
  "${ROOT_DIR}/tools/calibration/test_snapshot.c" \
  -o "${BUILD_DIR}/test_snapshot"

"${BUILD_DIR}/test_snapshot"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -Wno-error=int-to-pointer-cast \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/CJSON/cJSON.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_mqtt.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_snapshot.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_service.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.c" \
  "${ROOT_DIR}/tools/calibration/test_cal_mqtt_v2.c" \
  -o "${BUILD_DIR}/test_cal_mqtt_v2"

"${BUILD_DIR}/test_cal_mqtt_v2" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V2"

while read -r expected_sha fixture_name; do
  actual_sha="$(openssl dgst -sha256 \
    "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V2/${fixture_name}" | awk '{print $NF}')"
  if [[ "${actual_sha}" != "${expected_sha}" ]]; then
    echo "error: fixture SHA256 mismatch: ${fixture_name}" >&2
    exit 1
  fi
done < "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V2/SHA256SUMS"

echo "CAL_MQTT_V2 fixture SHA256 manifest: PASS"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
  "${ROOT_DIR}/tools/calibration/test_bl0942_frame.c" \
  -o "${BUILD_DIR}/test_bl0942_frame"

"${BUILD_DIR}/test_bl0942_frame"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_dc5200.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_storage.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_boot_inhibit.c" \
  "${ROOT_DIR}/tools/calibration/test_protocol_gates.c" \
  -o "${BUILD_DIR}/test_protocol_gates"

"${BUILD_DIR}/test_protocol_gates"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/hw_flash_page_writer.c" \
  "${ROOT_DIR}/tools/calibration/test_hw_flash_paging.c" \
  -o "${BUILD_DIR}/test_hw_flash_paging"

"${BUILD_DIR}/test_hw_flash_paging"

for profile in 50 75 100 150 200 240; do
  cc \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -DSYS_PRODUCT_PROFILE_SELECT="${profile}" \
    -I"${ROOT_DIR}/Core/Src" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    "${ROOT_DIR}/tools/calibration/test_product_profile_matrix.c" \
    -o "${BUILD_DIR}/profile-${profile}"
  "${BUILD_DIR}/profile-${profile}"
done

echo "six legacy product profile build/runtime matrix: PASS"
