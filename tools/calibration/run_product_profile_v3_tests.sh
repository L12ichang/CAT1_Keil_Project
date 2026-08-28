#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cat1-product-v3.XXXXXX")"
trap 'rm -rf "${BUILD_DIR}"' EXIT

for target in 50 75 100 150 200 240; do
  cc \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -D"PRODUCT_TARGET_${target}W" \
    -I"${ROOT_DIR}/Core/Src" \
    "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    "${ROOT_DIR}/tools/calibration/test_product_profile_v3.c" \
    -o "${BUILD_DIR}/test_product_profile_${target}w_v3"

  "${BUILD_DIR}/test_product_profile_${target}w_v3" \
    "${ROOT_DIR}/protocol/fixtures/PRODUCT_PROFILE_V3/FINGERPRINT_${target}W_E1_1.fixture"
done

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

for target in 50 75 100 150 200 240; do
  cc \
    -std=c11 \
    -D"PRODUCT_TARGET_${target}W" \
    -I"${ROOT_DIR}/Core/Src" \
    -c "${ROOT_DIR}/Core/Src/sys_product_profile.c" \
    -o "${BUILD_DIR}/sys_product_profile_${target}w.o"

  other_models="$(printf '%s\n' 50 75 100 150 200 240 | rg -v "^${target}$" | paste -sd '|' -)"
  if strings "${BUILD_DIR}/sys_product_profile_${target}w.o" | \
      rg -q "DL-(${other_models})Z-56T-MXG|_profiles"; then
    echo "error: ${target}W Product object contains another model profile/catalog" >&2
    exit 1
  fi
done

echo "Six Product Target compile gates and single-profile object scans: PASS"
