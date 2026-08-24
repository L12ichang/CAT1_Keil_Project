#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-hw-pwm-seq.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -fshort-enums \
  -ffunction-sections \
  -fdata-sections \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DPRODUCT_TARGET_50W \
  -DAPP_LOG_ENABLE=0 \
  -DHW_TIM1_PWM2_SEQUENCE_TEST=1 \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/hw_tim1_pwm2.c" \
  "${ROOT_DIR}/tools/calibration/test_hw_tim1_pwm2_sequence.c" \
  -o "${BUILD_DIR}/test_hw_tim1_pwm2_sequence"

"${BUILD_DIR}/test_hw_tim1_pwm2_sequence"
echo "TIM1 PWM/CCR/OCO sequencing tests: PASS"
