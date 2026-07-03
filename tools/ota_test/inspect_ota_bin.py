#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


CHECKSUM_OFFSET = 0x200
SIZE_OFFSET = 0x204
DEVICE_TYPE_OFFSET = 0x208
HEADER_MIN_LEN = DEVICE_TYPE_OFFSET + 2
DEFAULT_MAX_SIZE = 0x1C000
CHECKSUM_PLACEHOLDER = 0x12345678
SIZE_PLACEHOLDER = 0x89ABCDEF


def read_le32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def read_le16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def sum32_word_bytes(word: int) -> int:
    return (
        ((word >> 24) & 0xFF)
        + ((word >> 16) & 0xFF)
        + ((word >> 8) & 0xFF)
        + (word & 0xFF)
    )


def calc_checksum(data: bytes, size: int) -> int:
    total = 0
    for index in range(size // 4):
        offset = index * 4
        if offset == CHECKSUM_OFFSET or offset == SIZE_OFFSET:
            continue
        total = (total + sum32_word_bytes(read_le32(data, offset))) & 0xFFFFFFFF
    return total


def inspect(path: Path, expected_device_type: int, max_size: int) -> tuple[dict[str, object], list[str]]:
    data = path.read_bytes()
    errors: list[str] = []
    if len(data) < HEADER_MIN_LEN:
        return {"path": str(path), "file_size": len(data)}, ["file too small for OTA header"]

    checksum = read_le32(data, CHECKSUM_OFFSET)
    raw_size = read_le32(data, SIZE_OFFSET)
    size = raw_size & 0x00FFFFFF
    device_type = read_le16(data, DEVICE_TYPE_OFFSET)
    calculated = calc_checksum(data, size) if size <= len(data) and size % 4 == 0 else None

    if checksum == CHECKSUM_PLACEHOLDER:
        errors.append("checksum is placeholder 0x12345678")
    if raw_size == SIZE_PLACEHOLDER:
        errors.append("size is placeholder 0x89ABCDEF")
    if size == 0:
        errors.append("size is zero")
    if size != len(data):
        errors.append(f"header size {size} does not match file size {len(data)}")
    if size > max_size:
        errors.append(f"size {size} exceeds max {max_size}")
    if size % 4 != 0:
        errors.append(f"size {size} is not 4-byte aligned")
    if device_type != expected_device_type:
        errors.append(f"device_type 0x{device_type:04X} != expected 0x{expected_device_type:04X}")
    if calculated is not None and checksum != calculated:
        errors.append(f"checksum 0x{checksum:08X} != calculated 0x{calculated:08X}")

    return (
        {
            "path": str(path),
            "file_size": len(data),
            "checksum": f"0x{checksum:08X}",
            "raw_size": f"0x{raw_size:08X}",
            "size": size,
            "device_type": f"0x{device_type:04X}",
            "calculated_checksum": f"0x{calculated:08X}" if calculated is not None else None,
            "max_size": max_size,
            "valid": not errors,
        },
        errors,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect CAT.1 OTA image header and checksum.")
    parser.add_argument("path", type=Path)
    parser.add_argument("--device-type", default="0x0003")
    parser.add_argument("--max-size", default=str(DEFAULT_MAX_SIZE))
    args = parser.parse_args()

    report, errors = inspect(args.path, int(args.device_type, 0), int(args.max_size, 0))
    payload = dict(report)
    payload["errors"] = errors
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
