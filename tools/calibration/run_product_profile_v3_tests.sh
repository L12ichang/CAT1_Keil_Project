#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-product-v3.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Src" \
  "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  "${ROOT_DIR}/tools/calibration/test_product_profile_v3.c" \
  -o "${BUILD_DIR}/test_product_profile_v3"

"${BUILD_DIR}/test_product_profile_v3" \
  "${ROOT_DIR}/protocol/fixtures/PRODUCT_PROFILE_V3/FINGERPRINT_50W_E1_1.fixture"

if cc -std=c11 -fsyntax-only \
    -I"${ROOT_DIR}/Core/Src" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    >"${BUILD_DIR}/no-target.log" 2>&1; then
  echo "error: product profile compiled without a Product Target" >&2
  exit 1
fi

if cc -std=c11 -fsyntax-only \
    -DPRODUCT_TARGET_50W \
    -DPRODUCT_TARGET_75W \
    -I"${ROOT_DIR}/Core/Src" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    >"${BUILD_DIR}/multi-target.log" 2>&1; then
  echo "error: product profile compiled with multiple Product Targets" >&2
  exit 1
fi

for target in 75 100 150 200 240; do
  if cc -std=c11 -fsyntax-only \
      -D"PRODUCT_TARGET_${target}W" \
      -I"${ROOT_DIR}/Core/Src" \
      "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
      >"${BUILD_DIR}/unfrozen-${target}.log" 2>&1; then
    echo "error: unfrozen ${target}W Product Target compiled" >&2
    exit 1
  fi
done

cc \
  -std=c11 \
  -DPRODUCT_TARGET_50W \
  -I"${ROOT_DIR}/Core/Src" \
  -c "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
  -o "${BUILD_DIR}/sys_product_profile.o"

if strings "${BUILD_DIR}/sys_product_profile.o" | \
    rg -q 'DL-(75|100|150|200|240)|UNFROZEN|_profiles'; then
  echo "error: 50W Product object contains another power profile/catalog" >&2
  exit 1
fi

if [[ "$(rg -o 'PRODUCT_TARGET_50W' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx" | wc -l | tr -d ' ')" != "2" ]] || \
   rg -q 'SYS_PRODUCT_PROFILE_SELECT' \
    "${ROOT_DIR}/MDK-ARM-8008000/project.uvprojx"; then
  echo "error: Keil targets do not exclusively select PRODUCT_TARGET_50W" >&2
  exit 1
fi

echo "Product Target compile gates and single-profile object scan: PASS"
