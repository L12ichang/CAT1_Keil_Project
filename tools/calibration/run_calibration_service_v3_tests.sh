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
  -I"${ROOT_DIR}/Core/Src/gateway" \
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
  -I"${ROOT_DIR}/Core/Src/gateway" \
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
  -I"${ROOT_DIR}/Core/Src/gateway" \
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
  -I"${ROOT_DIR}/Core/Src/gateway" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc" \
  -I"${ROOT_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include" \
  -I"${ROOT_DIR}/Drivers/CMSIS/Include" \
  "${ROOT_DIR}/Core/Src/gateway/net_dim.c" \
  -x c - \
  -o "${BUILD_DIR}/test_net_dim_pending" <<'C'
#include <stdio.h>

#include "net_dim.h"
#include "sys_Vo_Io.h"

u8 dim_bak_to_low_acin;
u8 net_dim_to_protect;
u8 net_entery_flag;

static u32 output_count;
static u8 last_percent;

int dma_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

void sys_pwm_normal_output(u8 percent)
{
    ++output_count;
    last_percent = percent;
}

static int expect_true(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failures = 0;

    dim_level = 75U;
    dim_bak_to_low_acin = 75U;
    net_dim_to_protect = 75U;
    dim_ready();
    net_dim_clear_pending();
    uart_diam_process();
    failures += expect_true(
        output_count == 0U && dim_level == 0U &&
            dim_bak_to_low_acin == 0U && net_dim_to_protect == 0U,
        "safe off removes the complete pending dimming request");

    dim_level = 40U;
    dim_ready();
    uart_diam_process();
    failures += expect_true(
        output_count == 1U && last_percent == 40U,
        "a new ordinary dimming request still runs after safe off");

    return failures == 0 ? 0 : 1;
}
C

"${BUILD_DIR}/test_net_dim_pending"
echo "Pending network dimming cancellation tests: PASS"
