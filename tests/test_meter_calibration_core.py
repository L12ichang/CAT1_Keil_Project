from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from dataclasses import dataclass, replace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "Core/Src/meter_calibration.h"
SOURCE = ROOT / "Core/Src/meter_calibration.c"

Q24_SHIFT = 24
Q24_ONE = 1 << Q24_SHIFT
Q24_HALF = Q24_ONE // 2
U32_MAX = 0xFFFFFFFF
U64_MAX = 0xFFFFFFFFFFFFFFFF
CF24_MASK = 0xFFFFFF
PF_PPM_MAX = 1_000_000
FLAGS_ENERGY_CF24 = 3
CHECKPOINT_DOMAIN = 0x31464345
CHECKPOINT_SIZE = 44


@dataclass(frozen=True)
class Coefficients:
    version: int
    channel_count: int
    context_crc: int
    zero_raw: tuple[int, ...]
    factor_q24: tuple[int, ...]
    energy_gain_q24: int
    flags: int
    data_crc: int = 0


def coefficient_prefix(coefficients: Coefficients) -> bytes:
    payload = bytearray(
        struct.pack(
            "<HHI",
            coefficients.version,
            coefficients.channel_count,
            coefficients.context_crc,
        )
    )
    payload.extend(struct.pack("<6i", *coefficients.zero_raw))
    payload.extend(struct.pack("<6Q", *coefficients.factor_q24))
    payload.extend(struct.pack("<QI", coefficients.energy_gain_q24, coefficients.flags))
    return bytes(payload)


def coefficient_crc(coefficients: Coefficients) -> int:
    return zlib.crc32(coefficient_prefix(coefficients)) & U32_MAX


def with_crc(coefficients: Coefficients) -> Coefficients:
    return replace(coefficients, data_crc=coefficient_crc(coefficients))


def sample_coefficients(*, energy_gain_q24: int = 10 * Q24_ONE,
                        flags: int = FLAGS_ENERGY_CF24) -> Coefficients:
    return Coefficients(
        version=2,
        channel_count=6,
        context_crc=0x12345678,
        zero_raw=(1000, 200, -20, 0, 10, 5),
        factor_q24=(
            round(0.04 * Q24_ONE),       # input voltage: 0.04 mV/raw
            round(0.4 * Q24_ONE),        # input current: 0.4 uA/raw
            round(0.1 * Q24_ONE),        # signed WATT: 0.1 mW/raw
            1_000_000_000 * Q24_ONE,     # BL0942 reciprocal numerator
            15 * Q24_ONE,                # output voltage: 15 mV/raw
            5000 * Q24_ONE,              # output current: 5000 uA/raw
        ),
        energy_gain_q24=energy_gain_q24,
        flags=flags,
    )


def checkpoint_prefix(coefficient_data_crc: int) -> bytes:
    return struct.pack(
        "<HHIIIIIIQI",
        1,
        CHECKPOINT_SIZE,
        CHECKPOINT_DOMAIN,
        0x12345678,
        coefficient_data_crc,
        7,
        0x123456,
        Q24_HALF,
        0x1122334455667788,
        1,
    )


def checkpoint_serialized(coefficient_data_crc: int) -> bytes:
    prefix = checkpoint_prefix(coefficient_data_crc)
    return prefix + struct.pack("<I", zlib.crc32(prefix) & U32_MAX)


def numeric_version(path: Path) -> tuple[int, ...]:
    return tuple(int(item) for item in re.findall(r"\d+", path.name))


def find_host_toolchain() -> tuple[Path, Path, list[Path], list[Path]] | None:
    roots: list[Path] = []
    configured = os.environ.get("VSINSTALLDIR")
    if configured:
        roots.append(Path(configured))
    for base in (Path(r"C:\Program Files\Microsoft Visual Studio\2022"),
                 Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022")):
        if base.is_dir():
            roots.extend(path for path in base.iterdir() if path.is_dir())

    candidates: list[tuple[tuple[int, ...], Path, Path]] = []
    for root in roots:
        tools_root = root / r"VC\Tools\MSVC"
        if not tools_root.is_dir():
            continue
        for version_root in tools_root.iterdir():
            compiler_bin = version_root / r"bin\Hostx64\x64"
            if (compiler_bin / "cl.exe").is_file() and \
                    (compiler_bin / "link.exe").is_file():
                candidates.append((numeric_version(version_root), root,
                                   version_root))
    if not candidates:
        return None
    _, visual_studio, version_root = max(candidates, key=lambda item: item[0])
    compiler_bin = version_root / r"bin\Hostx64\x64"

    include_paths: list[Path] = []
    library_paths: list[Path] = []
    if (version_root / "include").is_dir() and (version_root / r"lib\x64").is_dir():
        kits_root = Path(r"C:\Program Files (x86)\Windows Kits\10")
        kit_versions = sorted(
            (path for path in (kits_root / "Include").iterdir()
             if path.is_dir()), key=numeric_version, reverse=True
        ) if (kits_root / "Include").is_dir() else []
        for kit_version in kit_versions:
            lib_version = kits_root / "Lib" / kit_version.name
            proposed_includes = [
                version_root / "include", kit_version / "ucrt",
                kit_version / "shared", kit_version / "um",
            ]
            proposed_libraries = [
                version_root / r"lib\x64", lib_version / r"ucrt\x64",
                lib_version / r"um\x64",
            ]
            if all(path.is_dir() for path in
                   [*proposed_includes, *proposed_libraries]):
                include_paths = proposed_includes
                library_paths = proposed_libraries
                break

    if not include_paths:
        scope_sdk = visual_studio / r"SDK\ScopeCppSDK\vc15"
        proposed_includes = [
            scope_sdk / r"VC\include", scope_sdk / r"SDK\include\ucrt",
            scope_sdk / r"SDK\include\shared", scope_sdk / r"SDK\include\um",
        ]
        proposed_libraries = [scope_sdk / r"VC\lib", scope_sdk / r"SDK\lib"]
        if not all(path.is_dir() for path in
                   [*proposed_includes, *proposed_libraries]):
            return None
        include_paths = proposed_includes
        library_paths = proposed_libraries

    return (compiler_bin / "cl.exe", compiler_bin / "link.exe",
            include_paths, library_paths)


def find_armclang() -> Path | None:
    candidates: list[Path] = []
    configured = os.environ.get("ARMCLANG_PATH")
    if configured:
        candidates.append(Path(configured))
    discovered = shutil.which("armclang")
    if discovered:
        candidates.append(Path(discovered))
    candidates.extend([
        Path(r"D:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe"),
        Path(r"C:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe"),
    ])
    return next((path for path in candidates if path.is_file()), None)


HOST_TYPES = r"""
#ifndef METER_CALIBRATION_HOST_TYPES_H
#define METER_CALIBRATION_HOST_TYPES_H
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;
typedef enum { BOOL_FALSE = 0, BOOL_TRUE = 1 } boolean_en;
#endif
"""


def host_harness(expected_crc: int, golden_serialized: bytes,
                 golden_checkpoint: bytes) -> str:
    golden_initializer = ", ".join(f"0x{value:02x}U" for value in golden_serialized)
    checkpoint_initializer = ", ".join(
        f"0x{value:02x}U" for value in golden_checkpoint
    )
    return rf"""
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "meter_calibration.h"

#define CHECK(condition) do {{ if (!(condition)) {{ return __LINE__; }} }} while (0)

static const u8 golden_serialized[METER_CAL_COEFFICIENT_SERIALIZED_SIZE] = {{
    {golden_initializer}
}};
static const u8 golden_checkpoint[METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE] = {{
    {checkpoint_initializer}
}};

u32 current_cal_crc32(const u8 *data, u32 length)
{{
    u32 crc = 0xffffffffUL;
    u32 index;
    u8 bit;
    for (index = 0U; index < length; ++index)
    {{
        crc ^= (u32)data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {{
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320UL : 0U);
        }}
    }}
    return ~crc;
}}

static void make_coefficients(meter_cal_coefficients_t *coefficients)
{{
    static const s32 zeros[6] = {{1000, 200, -20, 0, 10, 5}};
    static const u64 factors[6] = {{
        671089ULL,
        6710886ULL,
        1677722ULL,
        16777216000000000ULL,
        251658240ULL,
        83886080000ULL
    }};
    u32 index;
    (void)memset(coefficients, 0, sizeof(*coefficients));
    coefficients->version = METER_CAL_COEFFICIENT_VERSION;
    coefficients->channel_count = METER_CAL_CHANNEL_COUNT;
    coefficients->context_crc = 0x12345678UL;
    for (index = 0U; index < 6U; ++index)
    {{
        coefficients->zero_raw[index] = zeros[index];
        coefficients->factor_q24[index] = factors[index];
    }}
    coefficients->energy_gain_q24 = 10ULL * METER_CAL_Q24_ONE;
    coefficients->flags = METER_CAL_FLAGS_ENERGY_CF24;
    coefficients->data_crc = meter_calibration_coefficients_crc(coefficients);
}}

static int coefficients_are_zero(const meter_cal_coefficients_t *coefficients)
{{
    const u8 *bytes = (const u8 *)coefficients;
    u32 index;
    for (index = 0U; index < (u32)sizeof(*coefficients); ++index)
    {{
        if (bytes[index] != 0U)
        {{
            return 0;
        }}
    }}
    return 1;
}}

static int bytes_are_zero(const void *object, u32 size)
{{
    const u8 *bytes = (const u8 *)object;
    u32 index;
    for (index = 0U; index < size; ++index)
    {{
        if (bytes[index] != 0U)
        {{
            return 0;
        }}
    }}
    return 1;
}}

int main(void)
{{
    meter_cal_coefficients_t coefficients;
    meter_cal_coefficients_t disabled;
    meter_cal_coefficients_t decoded;
    meter_cal_coefficients_t invalid;
    meter_cal_energy_accumulator_t accumulator;
    meter_cal_energy_accumulator_t restored;
    meter_cal_energy_checkpoint_t checkpoint;
    meter_cal_energy_checkpoint_t decoded_checkpoint;
    meter_cal_result_en result;
    u8 encoded[METER_CAL_COEFFICIENT_SERIALIZED_SIZE];
    u8 corrupted[METER_CAL_COEFFICIENT_SERIALIZED_SIZE];
    u8 checkpoint_bytes[METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE];
    u8 checkpoint_corrupted[METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE];
    u32 index;
    u32 value = 0U;
    u64 rounded = 0ULL;

    CHECK(sizeof(meter_cal_coefficients_t) == 96U);
    make_coefficients(&coefficients);
    CHECK(coefficients.data_crc == 0x{expected_crc:08X}UL);
    CHECK(meter_calibration_coefficients_validate(&coefficients,
                                                   0x12345678UL) == METER_CAL_OK);
    CHECK(meter_calibration_coefficients_encode(&coefficients, 0x12345678UL,
          encoded, sizeof(encoded)) == METER_CAL_OK);
    for (index = 0U; index < METER_CAL_COEFFICIENT_SERIALIZED_SIZE; ++index)
    {{
        CHECK(encoded[index] == golden_serialized[index]);
    }}
    CHECK(meter_calibration_coefficients_decode(encoded, sizeof(encoded),
          0x12345678UL, &decoded) == METER_CAL_OK);
    CHECK(decoded.zero_raw[2] == -20);
    CHECK(decoded.factor_q24[5] == 83886080000ULL);
    CHECK(decoded.data_crc == coefficients.data_crc);

    (void)memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(meter_calibration_coefficients_decode(encoded, sizeof(encoded) - 1U,
          0x12345678UL, &decoded) == METER_CAL_SERIALIZED_SIZE_INVALID);
    CHECK(coefficients_are_zero(&decoded));
    (void)memcpy(corrupted, encoded, sizeof(corrupted));
    corrupted[METER_CAL_COEFFICIENT_SERIALIZED_SIZE - 1U] ^= 1U;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(meter_calibration_coefficients_decode(corrupted, sizeof(corrupted),
          0x12345678UL, &decoded) == METER_CAL_DATA_CRC_MISMATCH);
    CHECK(coefficients_are_zero(&decoded));
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(meter_calibration_coefficients_decode(encoded, sizeof(encoded),
          0x87654321UL, &decoded) == METER_CAL_CONTEXT_MISMATCH);
    CHECK(coefficients_are_zero(&decoded));

    (void)memset(encoded, 0xa5, sizeof(encoded));
    CHECK(meter_calibration_coefficients_encode(&coefficients, 0x12345678UL,
          encoded, sizeof(encoded) - 1U) == METER_CAL_BUFFER_TOO_SMALL);
    for (index = 0U; index < sizeof(encoded) - 1U; ++index)
    {{
        CHECK(encoded[index] == 0U);
    }}
    CHECK(encoded[sizeof(encoded) - 1U] == 0xa5U);
    invalid = coefficients;
    invalid.factor_q24[0] = 1ULL;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    (void)memset(encoded, 0xa5, sizeof(encoded));
    CHECK(meter_calibration_coefficients_encode(&invalid, 0x12345678UL,
          encoded, sizeof(encoded)) == METER_CAL_FACTOR_OUT_OF_RANGE);
    for (index = 0U; index < sizeof(encoded); ++index)
    {{
        CHECK(encoded[index] == 0U);
    }}

    CHECK(meter_calibration_sign_extend_s24(0x000000UL) == 0);
    CHECK(meter_calibration_sign_extend_s24(0x7fffffUL) == 8388607);
    CHECK(meter_calibration_sign_extend_s24(0x800000UL) == -8388608);
    CHECK(meter_calibration_sign_extend_s24(0xffffffUL) == -1);

    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW, 0xffffffUL, &value);
    CHECK(result == METER_CAL_OK && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW, 0x800000UL, &value);
    CHECK(result == METER_CAL_OK && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW, 0U, &value);
    CHECK(result == METER_CAL_OK && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_ACTIVE_POWER_MW, 100U, &value);
    CHECK(result == METER_CAL_OK && value == 12U);

    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ, 20000U, &value);
    CHECK(result == METER_CAL_OK && value == 50000U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ, 16667U, &value);
    CHECK(result == METER_CAL_OK && value == 59999U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ, 0U, &value);
    CHECK(result == METER_CAL_RAW_OUT_OF_RANGE && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ, 9999U, &value);
    CHECK(result == METER_CAL_RAW_OUT_OF_RANGE && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_FREQUENCY_MILLIHZ, 40001U, &value);
    CHECK(result == METER_CAL_RAW_OUT_OF_RANGE && value == 0U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_INPUT_VOLTAGE_MV, 101000U, &value);
    CHECK(result == METER_CAL_OK && value == 4000U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_OUTPUT_CURRENT_UA, 1005U, &value);
    CHECK(result == METER_CAL_OK && value == 5000000U);
    result = meter_calibration_convert(&coefficients, 0x12345678UL,
        METER_CAL_CHANNEL_OUTPUT_CURRENT_UA, 0x1000U, &value);
    CHECK(result == METER_CAL_RAW_OUT_OF_RANGE && value == 0U);

    invalid = coefficients;
    invalid.factor_q24[0] = 1ULL;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_FACTOR_OUT_OF_RANGE);
    invalid = coefficients;
    invalid.flags = 4U;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_FLAGS_INVALID);
    invalid = coefficients;
    invalid.flags = METER_CAL_FLAG_ENERGY_ENABLED;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ENERGY_CONFIGURATION_INVALID);
    invalid = coefficients;
    invalid.flags = 0U;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ENERGY_CONFIGURATION_INVALID);
    invalid = coefficients;
    invalid.energy_gain_q24 = 1ULL;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ENERGY_CONFIGURATION_INVALID);
    invalid = coefficients;
    invalid.factor_q24[5] = ULLONG_MAX;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_FACTOR_OUT_OF_RANGE);
    invalid = coefficients;
    invalid.zero_raw[0] = 0x00fffffeL;
    invalid.factor_q24[0] = ULLONG_MAX;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ZERO_OUT_OF_RANGE);
    invalid = coefficients;
    invalid.zero_raw[4] = 4094L;
    invalid.factor_q24[4] = ULLONG_MAX;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ZERO_OUT_OF_RANGE);
    invalid = coefficients;
    invalid.zero_raw[2] = 8388606L;
    invalid.factor_q24[2] = ULLONG_MAX;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_ZERO_OUT_OF_RANGE);
    invalid = coefficients;
    invalid.zero_raw[0] = 900000L;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_RAW_SPAN_TOO_SMALL);
    invalid = coefficients;
    invalid.zero_raw[4] = 700L;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_RAW_SPAN_TOO_SMALL);
    invalid = coefficients;
    invalid.zero_raw[2] = 100000L;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_RAW_SPAN_TOO_SMALL);
    invalid = coefficients;
    invalid.factor_q24[2] = ULLONG_MAX;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    CHECK(meter_calibration_coefficients_validate(&invalid, 0x12345678UL) ==
          METER_CAL_FACTOR_OUT_OF_RANGE);

    CHECK(meter_calibration_output_power_mw(56000U, 890000U) == 49840U);
    CHECK(meter_calibration_output_power_mw(UINT_MAX, UINT_MAX) == UINT_MAX);
    CHECK(meter_calibration_input_pf_ppm(103500U, 230000U, 500000U) == 900000U);
    CHECK(meter_calibration_input_pf_ppm(UINT_MAX, UINT_MAX, UINT_MAX) == 233U);

    /* Persistent checkpoint v1 golden vector and fail-closed decoding. */
    (void)memset(&checkpoint, 0, sizeof(checkpoint));
    checkpoint.version = METER_CAL_ENERGY_CHECKPOINT_VERSION;
    checkpoint.serialized_size = METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE;
    checkpoint.domain = METER_CAL_ENERGY_CHECKPOINT_DOMAIN;
    checkpoint.context_crc = 0x12345678UL;
    checkpoint.coefficient_data_crc = coefficients.data_crc;
    checkpoint.continuity_epoch = 7U;
    checkpoint.previous_cf24 = 0x123456UL;
    checkpoint.remainder_q24 = METER_CAL_Q24_HALF;
    checkpoint.total_energy_uwh = 0x1122334455667788ULL;
    checkpoint.initialized = 1U;
    checkpoint.data_crc = meter_calibration_energy_checkpoint_crc(&checkpoint);
    CHECK(meter_calibration_energy_checkpoint_validate(&checkpoint,
          0x12345678UL, coefficients.data_crc, 7U) == METER_CAL_OK);
    CHECK(meter_calibration_energy_checkpoint_encode(&checkpoint,
          0x12345678UL, coefficients.data_crc, 7U, checkpoint_bytes,
          sizeof(checkpoint_bytes)) == METER_CAL_OK);
    for (index = 0U; index < sizeof(checkpoint_bytes); ++index)
    {{
        CHECK(checkpoint_bytes[index] == golden_checkpoint[index]);
    }}
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes), 0x12345678UL, coefficients.data_crc, 7U,
          &decoded_checkpoint) == METER_CAL_OK);
    CHECK(decoded_checkpoint.total_energy_uwh == 0x1122334455667788ULL);
    CHECK(meter_calibration_energy_checkpoint_rounded_uwh(&decoded_checkpoint,
          0x12345678UL, coefficients.data_crc, 7U, &rounded) == METER_CAL_OK);
    CHECK(rounded == 0x1122334455667789ULL);

    (void)memcpy(checkpoint_corrupted, checkpoint_bytes,
                 sizeof(checkpoint_corrupted));
    checkpoint_corrupted[28] ^= 1U;
    (void)memset(&decoded_checkpoint, 0xa5, sizeof(decoded_checkpoint));
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_corrupted,
          sizeof(checkpoint_corrupted), 0x12345678UL, coefficients.data_crc,
          7U, &decoded_checkpoint) == METER_CAL_CHECKPOINT_CRC_MISMATCH);
    CHECK(bytes_are_zero(&decoded_checkpoint, sizeof(decoded_checkpoint)));
    (void)memset(&decoded_checkpoint, 0xa5, sizeof(decoded_checkpoint));
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes), 0x87654321UL, coefficients.data_crc, 7U,
          &decoded_checkpoint) == METER_CAL_CHECKPOINT_CONTEXT_MISMATCH);
    CHECK(bytes_are_zero(&decoded_checkpoint, sizeof(decoded_checkpoint)));
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes), 0x12345678UL, coefficients.data_crc ^ 1U,
          7U, &decoded_checkpoint) == METER_CAL_CHECKPOINT_COEFFICIENT_MISMATCH);
    CHECK(bytes_are_zero(&decoded_checkpoint, sizeof(decoded_checkpoint)));
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes), 0x12345678UL, coefficients.data_crc, 8U,
          &decoded_checkpoint) == METER_CAL_CHECKPOINT_EPOCH_MISMATCH);
    CHECK(bytes_are_zero(&decoded_checkpoint, sizeof(decoded_checkpoint)));
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes) - 1U, 0x12345678UL, coefficients.data_crc,
          7U, &decoded_checkpoint) == METER_CAL_SERIALIZED_SIZE_INVALID);
    CHECK(bytes_are_zero(&decoded_checkpoint, sizeof(decoded_checkpoint)));

    (void)memset(&restored, 0xa5, sizeof(restored));
    CHECK(meter_calibration_energy_accumulator_restore(&restored, &checkpoint,
          0x12345678UL, coefficients.data_crc, 8U) ==
          METER_CAL_CHECKPOINT_EPOCH_MISMATCH);
    CHECK(bytes_are_zero(&restored, sizeof(restored)));
    CHECK(meter_calibration_energy_accumulator_restore(&restored, &checkpoint,
          0x12345678UL, coefficients.data_crc, 7U) == METER_CAL_OK);
    CHECK(restored.previous_cf24 == 0x123456UL);
    CHECK(restored.continuity_epoch == 7U);

    /* Fraction preservation and a true natural 24-bit wrap. */
    coefficients.energy_gain_q24 = METER_CAL_Q24_ONE / 2U;
    coefficients.data_crc = meter_calibration_coefficients_crc(&coefficients);
    meter_calibration_energy_accumulator_init(&accumulator, 0ULL, 7U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients, 0x12345678UL,
          0xfffffeUL, 100U, 7U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_OK && value == 0U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients, 0x12345678UL,
          0xffffffUL, 100U, 7U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_OK && value == 0U);
    CHECK(accumulator.remainder_q24 == METER_CAL_Q24_HALF);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients, 0x12345678UL,
          0U, 100U, 7U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_OK && value == 1U);

    /* Capture/reboot/restore and recover exactly 50 retained CF counts. */
    coefficients.energy_gain_q24 = METER_CAL_Q24_ONE;
    coefficients.data_crc = meter_calibration_coefficients_crc(&coefficients);
    meter_calibration_energy_accumulator_init(&accumulator, 100ULL, 7U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients, 0x12345678UL,
          100U, 100U, 7U, BOOL_TRUE, &accumulator, &value) == METER_CAL_OK);
    CHECK(meter_calibration_energy_checkpoint_capture(&coefficients,
          0x12345678UL, &accumulator, &checkpoint) == METER_CAL_OK);
    CHECK(meter_calibration_energy_checkpoint_encode(&checkpoint,
          0x12345678UL, coefficients.data_crc, 7U, checkpoint_bytes,
          sizeof(checkpoint_bytes)) == METER_CAL_OK);
    CHECK(meter_calibration_energy_checkpoint_decode(checkpoint_bytes,
          sizeof(checkpoint_bytes), 0x12345678UL, coefficients.data_crc, 7U,
          &decoded_checkpoint) == METER_CAL_OK);
    CHECK(meter_calibration_energy_accumulator_restore(&restored,
          &decoded_checkpoint, 0x12345678UL, coefficients.data_crc, 7U) ==
          METER_CAL_OK);
    CHECK(meter_calibration_energy_resume_cf24(&coefficients, 0x12345678UL,
          150U, 100U, 7U, BOOL_TRUE, &restored, &value) == METER_CAL_OK);
    CHECK(value == 50U && restored.total_energy_uwh == 150ULL);

    /* A reset near wrap can resemble delta=4; explicit discontinuity wins. */
    accumulator.previous_cf24 = 0xfffffeUL;
    accumulator.remainder_q24 = METER_CAL_Q24_HALF;
    accumulator.total_energy_uwh = 500ULL;
    accumulator.continuity_epoch = 7U;
    accumulator.initialized = BOOL_TRUE;
    CHECK(meter_calibration_energy_resume_cf24(&coefficients, 0x12345678UL,
          2U, 100U, 8U, BOOL_FALSE, &accumulator, &value) ==
          METER_CAL_COUNTER_DISCONTINUITY);
    CHECK(value == 0U && accumulator.total_energy_uwh == 500ULL);
    CHECK(accumulator.previous_cf24 == 2U &&
          accumulator.continuity_epoch == 8U);

    /* Long missing interval exceeds caller proof and is never accumulated. */
    accumulator.previous_cf24 = 100U;
    accumulator.remainder_q24 = 0U;
    accumulator.total_energy_uwh = 500ULL;
    accumulator.continuity_epoch = 8U;
    accumulator.initialized = BOOL_TRUE;
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients,
          0x12345678UL, 1000U, 100U, 8U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_COUNTER_DISCONTINUITY);
    CHECK(value == 0U && accumulator.total_energy_uwh == 500ULL);
    CHECK(accumulator.previous_cf24 == 1000U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients,
          0x12345678UL, 1001U, 10U, 9U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_CHECKPOINT_EPOCH_MISMATCH);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients,
          0x12345678UL, 1001U, 10U, 8U, (boolean_en)2, &accumulator, &value) ==
          METER_CAL_CONTINUITY_INVALID);

    disabled = coefficients;
    disabled.flags = 0U;
    disabled.energy_gain_q24 = 0ULL;
    disabled.data_crc = meter_calibration_coefficients_crc(&disabled);
    CHECK(meter_calibration_coefficients_validate(&disabled, 0x12345678UL) ==
          METER_CAL_OK);
    CHECK(meter_calibration_energy_accumulate_cf24(&disabled, 0x12345678UL,
          20U, 100U, 8U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_ENERGY_DISABLED && value == 0U);
    (void)memset(&checkpoint, 0xa5, sizeof(checkpoint));
    CHECK(meter_calibration_energy_checkpoint_capture(&disabled,
          0x12345678UL, &accumulator, &checkpoint) ==
          METER_CAL_ENERGY_DISABLED);
    CHECK(bytes_are_zero(&checkpoint, sizeof(checkpoint)));

    accumulator.previous_cf24 = 1U;
    accumulator.remainder_q24 = METER_CAL_Q24_ONE;
    accumulator.total_energy_uwh = 0ULL;
    accumulator.continuity_epoch = 8U;
    accumulator.initialized = BOOL_TRUE;
    (void)memset(&checkpoint, 0xa5, sizeof(checkpoint));
    CHECK(meter_calibration_energy_checkpoint_capture(&coefficients,
          0x12345678UL, &accumulator, &checkpoint) ==
          METER_CAL_CHECKPOINT_INVALID);
    CHECK(bytes_are_zero(&checkpoint, sizeof(checkpoint)));

    accumulator.previous_cf24 = 10U;
    accumulator.remainder_q24 = 0U;
    accumulator.total_energy_uwh = ULLONG_MAX - 1ULL;
    accumulator.continuity_epoch = 8U;
    accumulator.initialized = BOOL_TRUE;
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients,
          0x12345678UL, 12U, 10U, 8U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_ACCUMULATOR_OVERFLOW && value == 1U);
    CHECK(accumulator.total_energy_uwh == ULLONG_MAX);

    /* A u32 compatibility report must not discard a valid u64 increment. */
    coefficients.energy_gain_q24 = 1677721600000ULL; /* 100,000 uWh/CF. */
    coefficients.data_crc = meter_calibration_coefficients_crc(&coefficients);
    meter_calibration_energy_accumulator_init(&accumulator, 0ULL, 8U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients, 0x12345678UL,
          0U, METER_CAL_CF_COUNTER_MASK, 8U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_OK);
    result = meter_calibration_energy_accumulate_cf24(&coefficients,
        0x12345678UL, 42950U, METER_CAL_CF_COUNTER_MASK, 8U, BOOL_TRUE,
        &accumulator, &value);
    CHECK(result == METER_CAL_CONVERSION_SATURATED && value == UINT_MAX);
    CHECK(accumulator.total_energy_uwh == 4295000000ULL);
    CHECK(accumulator.remainder_q24 == 0U);
    CHECK(accumulator.previous_cf24 == 42950U);
    CHECK(meter_calibration_energy_accumulate_cf24(&coefficients,
          0x12345678UL, 42951U, METER_CAL_CF_COUNTER_MASK, 8U, BOOL_TRUE,
          &accumulator, &value) == METER_CAL_OK);
    CHECK(value == 100000U && accumulator.total_energy_uwh == 4295100000ULL);

    /* Total accepts every representable uWh, then saturates without wrap. */
    accumulator.previous_cf24 = 0U;
    accumulator.remainder_q24 = 0U;
    accumulator.total_energy_uwh = ULLONG_MAX - 4294967296ULL;
    accumulator.continuity_epoch = 8U;
    accumulator.initialized = BOOL_TRUE;
    result = meter_calibration_energy_accumulate_cf24(&coefficients,
        0x12345678UL, 42950U, METER_CAL_CF_COUNTER_MASK, 8U, BOOL_TRUE,
        &accumulator, &value);
    CHECK(result == METER_CAL_ACCUMULATOR_OVERFLOW && value == UINT_MAX);
    CHECK(accumulator.total_energy_uwh == ULLONG_MAX);
    CHECK(accumulator.remainder_q24 == 0U && accumulator.previous_cf24 == 42950U);

    /* Invalid conversion configuration is transactional: no baseline advance. */
    invalid = coefficients;
    invalid.energy_gain_q24 = 1677721600001ULL;
    invalid.data_crc = meter_calibration_coefficients_crc(&invalid);
    accumulator.previous_cf24 = 77U;
    accumulator.remainder_q24 = 0U;
    accumulator.total_energy_uwh = 321ULL;
    accumulator.continuity_epoch = 8U;
    accumulator.initialized = BOOL_TRUE;
    CHECK(meter_calibration_energy_accumulate_cf24(&invalid, 0x12345678UL,
          78U, 10U, 8U, BOOL_TRUE, &accumulator, &value) ==
          METER_CAL_ENERGY_CONFIGURATION_INVALID && value == 0U);
    CHECK(accumulator.previous_cf24 == 77U && accumulator.remainder_q24 == 0U &&
          accumulator.total_energy_uwh == 321ULL);

    return 0;
}}
"""


class MeterCalibrationContractTests(unittest.TestCase):
    def test_serialization_is_fixed_96_byte_little_endian_vector(self) -> None:
        coefficients = sample_coefficients()
        prefix = coefficient_prefix(coefficients)
        serialized = prefix + struct.pack("<I", coefficient_crc(coefficients))
        self.assertEqual(len(prefix), 92)
        self.assertEqual(len(serialized), 96)
        self.assertEqual(coefficient_crc(coefficients), 0xF6193FB8)
        self.assertEqual(
            prefix.hex(),
            "0200060078563412e8030000c8000000ecffffff000000000a00000005000000"
            "713d0a000000000066666600000000009a9919000000000000000000ca9a3b00"
            "0000000f0000000000000088130000000000000a0000000003000000",
        )
        self.assertEqual(serialized[-4:].hex(), "b83f19f6")

    def test_checkpoint_is_fixed_44_byte_little_endian_golden_vector(self) -> None:
        serialized = checkpoint_serialized(coefficient_crc(sample_coefficients()))
        self.assertEqual(len(serialized), 44)
        self.assertEqual(
            serialized.hex(),
            "01002c004543463178563412b83f19f6070000005634120000008000"
            "887766554433221101000000dc698b28",
        )
        self.assertEqual(zlib.crc32(serialized[:40]) & U32_MAX, 0x288B69DC)

    def test_source_contract_has_high_resolution_fixed_transforms(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        combined = header + "\n" + source

        self.assertIsNone(re.search(r"\b(?:float|double)\b", combined))
        self.assertNotIn("MID", combined)
        self.assertIn("u64 factor_q24[METER_CAL_CHANNEL_COUNT]", header)
        self.assertIn("u64 energy_gain_q24", header)
        self.assertIn("sizeof(meter_cal_coefficients_t) == 96U", source)
        self.assertIn("METER_CAL_COEFFICIENT_CRC_SIZE       92U", source)
        self.assertIn("meter_calibration_sign_extend_s24", source)
        self.assertIn("meter_cal_apply_reciprocal_q24", source)
        self.assertIn("METER_CAL_COUNTER_DISCONTINUITY", header)
        self.assertIn("maximum_trusted_cf_delta", header)
        self.assertNotIn("energy_from_cf_delta", header)
        self.assertIn("METER_CAL_COEFFICIENT_SERIALIZED_SIZE 96U", header)
        self.assertIn("meter_calibration_coefficients_encode", source)
        self.assertIn("meter_calibration_coefficients_decode", source)
        self.assertIn("METER_CAL_ENERGY_CHECKPOINT_SERIALIZED_SIZE 44U", header)
        self.assertIn("meter_calibration_energy_checkpoint_encode", source)
        self.assertIn("meter_calibration_energy_checkpoint_decode", source)
        self.assertIn("meter_cal_validate_linear_anchors", source)
        self.assertIn("anchor_percent[3] = {25U, 50U, 100U}", source)
        self.assertIn("less than\n * one complete 24-bit counter revolution", header)
        self.assertIn("sequence-stamped input snapshot", header)
        for unit_name in (
            "INPUT_VOLTAGE_MV",
            "INPUT_CURRENT_UA",
            "INPUT_ACTIVE_POWER_MW",
            "INPUT_FREQUENCY_MILLIHZ",
            "OUTPUT_VOLTAGE_MV",
            "OUTPUT_CURRENT_UA",
        ):
            self.assertIn(unit_name, header)

    def test_crc_valid_garbage_factors_are_represented_in_regression_vectors(self) -> None:
        baseline = sample_coefficients()
        low = list(baseline.factor_q24)
        low[0] = 1
        high = list(baseline.factor_q24)
        high[5] = U64_MAX
        low_coefficients = with_crc(replace(baseline, factor_q24=tuple(low)))
        high_coefficients = with_crc(replace(baseline, factor_q24=tuple(high)))
        self.assertEqual(low_coefficients.data_crc, coefficient_crc(low_coefficients))
        self.assertEqual(high_coefficients.data_crc, coefficient_crc(high_coefficients))
        self.assertNotEqual(low_coefficients.data_crc, high_coefficients.data_crc)

    def test_direct_c_harness_compiles_and_runs_all_public_vectors(self) -> None:
        toolchain = find_host_toolchain()
        if toolchain is None:
            self.fail("MSVC host compiler/linker/runtime libraries are required")
        compiler, linker, include_paths, library_paths = toolchain

        expected_crc = coefficient_crc(sample_coefficients())
        temporary_paths: list[Path] = []

        def reserve_file(suffix: str) -> Path:
            handle = tempfile.NamedTemporaryFile(
                prefix="meter_cal_host_", suffix=suffix, delete=False
            )
            handle.close()
            path = Path(handle.name)
            temporary_paths.append(path)
            return path

        types_header = reserve_file("_types.h")
        harness = reserve_file("_harness.c")
        source_object = reserve_file("_source.obj")
        harness_object = reserve_file("_harness.obj")
        executable = reserve_file("_harness.exe")
        try:
            types_header.write_text(HOST_TYPES, encoding="ascii")
            golden_serialized = (
                coefficient_prefix(sample_coefficients()) +
                struct.pack("<I", expected_crc)
            )
            golden_checkpoint = checkpoint_serialized(expected_crc)
            harness.write_text(
                host_harness(expected_crc, golden_serialized,
                             golden_checkpoint),
                encoding="ascii",
            )
            source_object.unlink()
            harness_object.unlink()
            executable.unlink()

            compiler_environment = os.environ.copy()
            compiler_environment["INCLUDE"] = os.pathsep.join(
                str(path) for path in include_paths
            )
            compiler_environment["LIB"] = os.pathsep.join(
                str(path) for path in library_paths
            )
            compiler_environment["PATH"] = (
                str(compiler.parent) + os.pathsep +
                compiler_environment.get("PATH", "")
            )

            common_options = [
                "/nologo", "/c", "/TC", "/std:c11", "/W4", "/WX",
                "/wd5105", "/wd4127",
                "/DMETER_CALIBRATION_HOST_TEST", f'/FI{types_header}',
                f'/I{HEADER.parent}',
            ]
            commands = [
                [str(compiler), *common_options, str(SOURCE), f'/Fo{source_object}'],
                [str(compiler), *common_options, str(harness), f'/Fo{harness_object}'],
                [str(linker), "/nologo", str(source_object), str(harness_object),
                 f'/out:{executable}'],
            ]
            for command in commands:
                compiled = subprocess.run(
                    command,
                    cwd=ROOT,
                    env=compiler_environment,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    capture_output=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(
                    compiled.returncode,
                    0,
                    msg=(f"host build failed: {command[0]}\n"
                         f"{compiled.stdout}\n{compiled.stderr}"),
                )

            executed = subprocess.run(
                [str(executable)], cwd=ROOT, text=True, encoding="utf-8",
                errors="replace", capture_output=True,
                timeout=15, check=False,
            )
            self.assertEqual(
                executed.returncode,
                0,
                msg=f"C harness failed:\n{executed.stdout}\n{executed.stderr}",
            )
        finally:
            for path in temporary_paths:
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass

    def test_armclang_cortex_m3_is_warning_clean(self) -> None:
        compiler = find_armclang()
        if compiler is None:
            self.fail("ARMClang is required for Cortex-M3 warning-clean validation")

        handle = tempfile.NamedTemporaryFile(
            prefix="meter_cal_arm_", suffix="_types.h", delete=False
        )
        types_header = Path(handle.name)
        try:
            handle.write(HOST_TYPES.encode("ascii"))
            handle.close()
            compiled = subprocess.run(
                [
                    str(compiler), "--target=arm-arm-none-eabi",
                    "-mcpu=cortex-m3", "-std=c99", "-Wall", "-Wextra",
                    "-Werror", "-DMETER_CALIBRATION_HOST_TEST",
                    "-include", str(types_header), "-I", str(HEADER.parent),
                    "-fsyntax-only", str(SOURCE),
                ],
                cwd=ROOT,
                text=True,
                encoding="utf-8",
                errors="replace",
                capture_output=True,
                timeout=60,
                check=False,
            )
            self.assertEqual(
                compiled.returncode,
                0,
                msg=f"ARMClang failed:\n{compiled.stdout}\n{compiled.stderr}",
            )
        finally:
            handle.close()
            try:
                types_header.unlink()
            except FileNotFoundError:
                pass

    def test_python_boundary_oracle_covers_rounding_wrap_and_extremes(self) -> None:
        self.assertEqual(round(1_000_000_000 / 20_000), 50_000)
        self.assertEqual((0x000002 - 0xFFFFFE) & CF24_MASK, 4)
        self.assertGreater((10 - 100_000) & CF24_MASK, 100)
        self.assertEqual(min((U32_MAX * U32_MAX + 500_000) // 1_000_000,
                             U32_MAX), U32_MAX)
        denominator = U32_MAX * U32_MAX
        expected_pf = round((U32_MAX * 1_000_000 * PF_PPM_MAX) / denominator)
        self.assertEqual(expected_pf, 233)


if __name__ == "__main__":
    unittest.main(verbosity=2)
