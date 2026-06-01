from __future__ import annotations

import argparse
from pathlib import Path


APP_BASE = 0x08008000
CHECKSUM_OFFSET = 0x200
LENGTH_OFFSET = 0x204
DEVICE_TYPE_OFFSET = 0x208
EXPECTED_DEVICE_TYPE = 0x0003


def read_ihex(path: Path) -> dict[int, int]:
    data: dict[int, int] = {}
    upper = 0
    for line_number, raw_line in enumerate(path.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_number}: invalid Intel HEX record")

        record = bytes.fromhex(line[1:])
        length = record[0]
        address = (record[1] << 8) | record[2]
        record_type = record[3]
        payload = record[4 : 4 + length]
        checksum = record[4 + length]
        if ((sum(record[: 4 + length]) + checksum) & 0xFF) != 0:
            raise ValueError(f"{path}:{line_number}: checksum mismatch")

        if record_type == 0x00:
            absolute = (upper << 16) + address
            for offset, value in enumerate(payload):
                data[absolute + offset] = value
        elif record_type == 0x01:
            break
        elif record_type == 0x04:
            if length != 2:
                raise ValueError(f"{path}:{line_number}: invalid extended linear address")
            upper = (payload[0] << 8) | payload[1]
        else:
            continue
    return data


def read_le32(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def write_le32(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 4] = value.to_bytes(4, "little")


def write_le16(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 2] = value.to_bytes(2, "little")


def sum32_word(word: int) -> int:
    return (
        ((word >> 24) & 0xFF)
        + ((word >> 16) & 0xFF)
        + ((word >> 8) & 0xFF)
        + (word & 0xFF)
    )


def app_checksum(data: bytearray, size: int) -> int:
    total = 0
    for index in range(size // 4):
        offset = index * 4
        if CHECKSUM_OFFSET <= offset < LENGTH_OFFSET + 4:
            continue
        total = (total + sum32_word(read_le32(data, offset))) & 0xFFFFFFFF
    return total


def hex_to_app_bin(hex_path: Path, bin_path: Path, app_base: int) -> tuple[int, int]:
    image = read_ihex(hex_path)
    if not image:
        raise ValueError(f"{hex_path}: no data records")
    min_address = min(image)
    max_address = max(image) + 1
    if min_address != app_base:
        raise ValueError(f"HEX starts at 0x{min_address:08X}, expected 0x{app_base:08X}")

    size = max_address - app_base
    if size <= DEVICE_TYPE_OFFSET + 2:
        raise ValueError("APP image is too small for metadata")
    if size % 4 != 0:
        raise ValueError(f"APP image size is not word aligned: {size}")

    data = bytearray([0xFF] * size)
    for address, value in image.items():
        if address < app_base:
            raise ValueError(f"HEX contains data before app base: 0x{address:08X}")
        data[address - app_base] = value

    write_le32(data, LENGTH_OFFSET, size)
    if int.from_bytes(data[DEVICE_TYPE_OFFSET : DEVICE_TYPE_OFFSET + 2], "little") != EXPECTED_DEVICE_TYPE:
        write_le16(data, DEVICE_TYPE_OFFSET, EXPECTED_DEVICE_TYPE)
    checksum = app_checksum(data, size)
    write_le32(data, CHECKSUM_OFFSET, checksum)

    bin_path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(data)
    return size, checksum


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert Keil APP HEX to bootloader-ready BIN.")
    parser.add_argument("--hex", required=True, type=Path, help="Input Intel HEX")
    parser.add_argument("--bin", required=True, type=Path, help="Output APP BIN")
    parser.add_argument("--app-base", default=f"0x{APP_BASE:08X}", help="Expected APP base")
    args = parser.parse_args()

    size, checksum = hex_to_app_bin(args.hex, args.bin, int(args.app_base, 0))
    print(f"Wrote {args.bin} ({size} bytes, checksum=0x{checksum:08X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
