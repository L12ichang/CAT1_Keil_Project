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


def read_le32(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def sum32_word_bytes(word: int) -> int:
    return (
        ((word >> 24) & 0xFF)
        + ((word >> 16) & 0xFF)
        + ((word >> 8) & 0xFF)
        + (word & 0xFF)
    )


def calc_checksum(data: bytearray, size: int) -> int:
    total = 0
    for index in range(size // 4):
        offset = index * 4
        if offset == CHECKSUM_OFFSET or offset == SIZE_OFFSET:
            continue
        total = (total + sum32_word_bytes(read_le32(data, offset))) & 0xFFFFFFFF
    return total


def pack(input_path: Path, output_path: Path, info_path: Path, device_type: int, max_size: int) -> dict[str, object]:
    data = bytearray(input_path.read_bytes())
    if len(data) < HEADER_MIN_LEN:
        raise SystemExit(f"input is too small for OTA header: {len(data)} bytes")
    while len(data) % 4:
        data.append(0xFF)
    if len(data) > max_size:
        raise SystemExit(f"input exceeds OTA max size: {len(data)} > {max_size}")
    if len(data) > 0x00FFFFFF:
        raise SystemExit(f"input exceeds 24-bit OTA size field: {len(data)}")
    if device_type < 0 or device_type > 0xFFFF:
        raise SystemExit(f"device_type must fit in uint16: 0x{device_type:X}")

    size = len(data)
    data[SIZE_OFFSET : SIZE_OFFSET + 4] = (size & 0x00FFFFFF).to_bytes(4, "little")
    data[DEVICE_TYPE_OFFSET : DEVICE_TYPE_OFFSET + 2] = device_type.to_bytes(2, "little")
    checksum = calc_checksum(data, size)
    if checksum == CHECKSUM_PLACEHOLDER:
        raise SystemExit("refusing to create OTA package with placeholder checksum")
    if size == SIZE_PLACEHOLDER:
        raise SystemExit("refusing to create OTA package with placeholder size")
    data[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 4] = checksum.to_bytes(4, "little")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    report = {
        "input": str(input_path),
        "output": str(output_path),
        "size": size,
        "checksum": f"0x{checksum:08X}",
        "device_type": f"0x{device_type:04X}",
        "header_offset": {
            "checksum": f"0x{CHECKSUM_OFFSET:X}",
            "size": f"0x{SIZE_OFFSET:X}",
            "device_type": f"0x{DEVICE_TYPE_OFFSET:X}",
        },
        "max_size": max_size,
    }
    info_path.parent.mkdir(parents=True, exist_ok=True)
    info_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack a Keil APP bin as a CAT.1 OTA image.")
    parser.add_argument("--input", required=True, type=Path, help="APP bin built for 0x08008000")
    parser.add_argument("--output", required=True, type=Path, help="Output OTA cat1.bin")
    parser.add_argument("--info", type=Path, help="Output JSON report path")
    parser.add_argument("--device-type", default="0x0003", help="Expected device_type written at 0x208")
    parser.add_argument("--max-size", default=str(DEFAULT_MAX_SIZE), help="Maximum APP/OTA image size in bytes")
    args = parser.parse_args()

    info_path = args.info or args.output.with_name(args.output.stem + "_ota_info.json")
    report = pack(
        input_path=args.input,
        output_path=args.output,
        info_path=info_path,
        device_type=int(args.device_type, 0),
        max_size=int(args.max_size, 0),
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
