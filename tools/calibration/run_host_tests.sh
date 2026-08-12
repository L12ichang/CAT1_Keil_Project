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
