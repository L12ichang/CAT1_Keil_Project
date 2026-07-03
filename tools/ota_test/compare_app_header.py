#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


CHECKSUM_OFFSET = 0x200
SIZE_OFFSET = 0x204
DEVICE_TYPE_OFFSET = 0x208


def read_header(data: bytes) -> dict[str, int]:
    return {
        "checksum": int.from_bytes(data[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 4], "little"),
        "size": int.from_bytes(data[SIZE_OFFSET : SIZE_OFFSET + 4], "little") & 0x00FFFFFF,
        "device_type": int.from_bytes(data[DEVICE_TYPE_OFFSET : DEVICE_TYPE_OFFSET + 2], "little"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare OTA package header with APP flash header dump.")
    parser.add_argument("--ota", required=True, type=Path)
    parser.add_argument("--flash", required=True, type=Path, help="savebin dump starting at APP base")
    args = parser.parse_args()

    ota_header = read_header(args.ota.read_bytes())
    flash_header = read_header(args.flash.read_bytes())
    mismatches = [
        key for key in ("checksum", "size", "device_type")
        if ota_header[key] != flash_header[key]
    ]
    payload = {
        "ota": {key: f"0x{value:08X}" if key != "device_type" else f"0x{value:04X}" for key, value in ota_header.items()},
        "flash": {key: f"0x{value:08X}" if key != "device_type" else f"0x{value:04X}" for key, value in flash_header.items()},
        "match": not mismatches,
        "mismatches": mismatches,
    }
    print(json.dumps(payload, indent=2))
    return 0 if not mismatches else 1


if __name__ == "__main__":
    raise SystemExit(main())
