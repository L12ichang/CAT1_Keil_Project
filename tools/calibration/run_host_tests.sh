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
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/tools/calibration/test_cal_payload_v3.c" \
  -o "${BUILD_DIR}/test_cal_payload_v3"

"${BUILD_DIR}/test_cal_payload_v3" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/G1_CRC32.fixture" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/G2_ENDIAN.fixture" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/G3_FINGERPRINT_50W_E1_1.fixture" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/G4_PAYLOAD.fixture" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/G5_RECORD_GENERATION_7.fixture"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/CJSON/cJSON.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_mqtt.c" \
  "${ROOT_DIR}/tools/calibration/test_cal_mqtt_v3.c" \
  -o "${BUILD_DIR}/test_cal_mqtt_v3"

"${BUILD_DIR}/test_cal_mqtt_v3" \
  "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3"

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
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  -x c - \
  -o "${BUILD_DIR}/test_v3_rx_capacity" <<'C'
#include "hw_gateway.h"
#include "mqtt_zk_protocol.h"

_Static_assert(ZK_JSON_RX_MAX == 2048U,
               "V3 JSON receive limit must remain 2048 bytes");
_Static_assert(RECV_BUF_LENGTH == (ZK_JSON_RX_MAX + 1U),
               "receive storage must include the trailing NUL byte");
_Static_assert(sizeof(stringBuf) == RECV_BUF_LENGTH,
               "the modem receive buffer must use the shared V3 capacity");

int main(void)
{
    return 0;
}
C

"${BUILD_DIR}/test_v3_rx_capacity"
echo "V3 MQTT receive capacity gate: PASS"

while read -r expected_sha fixture_name; do
  actual_sha="$(openssl dgst -sha256 \
    "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/${fixture_name}" | awk '{print $NF}')"
  if [[ "${actual_sha}" != "${expected_sha}" ]]; then
    echo "error: CAL_MQTT_V3 fixture SHA256 mismatch: ${fixture_name}" >&2
    exit 1
  fi
done < "${ROOT_DIR}/protocol/fixtures/CAL_MQTT_V3/SHA256SUMS"

echo "CAL_MQTT_V3 fixture SHA256 manifest: PASS"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DPRODUCT_TARGET_50W \
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
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_calibration_snapshot.c" \
  "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
  "${ROOT_DIR}/tools/calibration/test_bl0942_snapshot_freshness.c" \
  -o "${BUILD_DIR}/test_bl0942_snapshot_freshness"

"${BUILD_DIR}/test_bl0942_snapshot_freshness"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/hw_flash_page_writer.c" \
  "${ROOT_DIR}/tools/calibration/test_hw_flash_paging.c" \
  -o "${BUILD_DIR}/test_hw_flash_paging"

"${BUILD_DIR}/test_hw_flash_paging"

for target in 50 75 100 150 200 240; do
  cc -std=c11 -fsyntax-only \
    -D"PRODUCT_TARGET_${target}W" \
    -I"${ROOT_DIR}/Core/Src" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c"
done

echo "six frozen Product Target compile gates: PASS"
