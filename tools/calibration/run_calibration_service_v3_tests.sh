#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-cal-service-v3.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -fshort-enums \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DAPP_LOG_ENABLE=0 \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_calibration_service.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_snapshot.c" \
  "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/tools/calibration/test_calibration_service_v3.c" \
  -o "${BUILD_DIR}/test_calibration_service_v3"

"${BUILD_DIR}/test_calibration_service_v3"
echo "Calibration V3 service state-machine tests: PASS"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -fshort-enums \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DAPP_LOG_ENABLE=0 \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_pwm.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/tools/calibration/test_calibration_pwm_v3.c" \
  -o "${BUILD_DIR}/test_calibration_pwm_v3"

"${BUILD_DIR}/test_calibration_pwm_v3"
echo "Calibration V3 PWM/default/fallback/safety tests: PASS"

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -fshort-enums \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DAPP_LOG_ENABLE=0 \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/sys_temp_over_protect.c" \
  "${ROOT_DIR}/Core/Src/sys_pwm.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
  "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/tools/calibration/test_temp_pwm_integration_v3.c" \
  -o "${BUILD_DIR}/test_temp_pwm_integration_v3"

"${BUILD_DIR}/test_temp_pwm_integration_v3"
echo "Production temperature/PWM integration tests: PASS"
