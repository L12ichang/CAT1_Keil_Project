#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"

if [[ -z "${SDK_PATH}" || ! -f "${SDK_PATH}/usr/include/stdint.h" ]]; then
  echo "error: macOS SDK stdint.h not found. Install or repair Xcode Command Line Tools." >&2
  exit 1
fi

if [[ "$#" -eq 0 ]]; then
  set -- \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/mqtt_zk_protocol.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/App.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/Protocol.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_alarm.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_runtime_stats.c" \
    "${ROOT_DIR}/Core/Src/LampProtocolLib/zk_property.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_snapshot.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_service.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_driver_protocol.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_dc5200.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_curve.c" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_safety.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_storage.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_boot_inhibit.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_flash.c" \
    "${ROOT_DIR}/Core/Src/sys_calibration_mqtt.c" \
    "${ROOT_DIR}/Core/Src/factory_user_data.c" \
    "${ROOT_DIR}/Core/Src/sys_bl0942.c" \
    "${ROOT_DIR}/Core/Src/sys_bl0942_frame.c" \
    "${ROOT_DIR}/Core/Src/hw_flash_page_writer.c" \
    "${ROOT_DIR}/Core/Src/hw_flash.c" \
    "${ROOT_DIR}/Core/Src/adc.c" \
    "${ROOT_DIR}/Core/Src/sys_pwm.c" \
    "${ROOT_DIR}/Core/Src/hw_tim1_pwm2.c" \
    "${ROOT_DIR}/Core/Src/hw_uart2.c" \
    "${ROOT_DIR}/Core/Src/main.c"
fi

arm-none-eabi-gcc \
  -fsyntax-only \
  -mcpu=cortex-m3 \
  -mthumb \
  -std=gnu11 \
  -DUSE_HAL_DRIVER \
  -DSTM32F103xE \
  -DAPROM_OFFSET \
  -DLEGACY_DATAPOINT_PROTOCOL_ENABLE=1 \
  -DLEGACY_APP_PROCESS_ENABLE=1 \
  -DLEGACY_ACTIVE_ENABLE=1 \
  -isystem "${ROOT_DIR}/tools/arm_syntax_shims" \
  -isystem "${SDK_PATH}/usr/include" \
  -I"${ROOT_DIR}/Core/Inc" \
  -I"${ROOT_DIR}/Core/Src" \
  -I"${ROOT_DIR}/Core/Src/CJSON" \
  -I"${ROOT_DIR}/Core/Src/LampProtocolLib" \
  -I"${ROOT_DIR}/Core/Src/gateway" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "$@"
