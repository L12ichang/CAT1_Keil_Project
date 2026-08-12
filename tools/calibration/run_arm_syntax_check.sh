#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

bash "${ROOT_DIR}/tools/arm_gcc_syntax_check.sh"

if ! command -v clang >/dev/null 2>&1; then
  echo "error: clang is required for the BL0942 integration syntax check" >&2
  exit 1
fi

SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
if [[ -z "${SDK_PATH}" || ! -f "${SDK_PATH}/usr/include/stdint.h" ]]; then
  echo "error: macOS SDK stdint.h not found" >&2
  exit 1
fi

clang \
  -target arm-none-eabi \
  -fsyntax-only \
  -std=gnu11 \
  -fshort-enums \
  -D__ARM_ARCH_7M__ \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DAPROM_OFFSET \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Core/Src/gateway" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  -isystem "${SDK_PATH}/usr/include" \
  "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
  "${ROOT_DIR}/Core/Src/sys_bl0942.c" \
  "${ROOT_DIR}/Core/Src/hw_uart2.c"
