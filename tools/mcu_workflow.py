from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import re
import select
import shlex
import shutil
import subprocess
import sys
import tempfile
import termios
import time
import struct
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.login_flow import get_topics

DEFAULT_CONFIG = ROOT / "config" / "mcu_workflow.json"
COMMON_H = ROOT / "Core" / "Src" / "common.h"
REPORT_TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
LEVEL_PATTERNS = (
    ("ERROR", re.compile(r"\b(error|failed|fail|crc|exception)\b", re.IGNORECASE)),
    ("WARN", re.compile(r"\b(warn|warning|timeout|retry)\b", re.IGNORECASE)),
    ("DEBUG", re.compile(r"\b(debug|trace)\b", re.IGNORECASE)),
)
MODULE_PATTERN = re.compile(r"^\[([A-Z0-9_:-]+)\]")
HEX32_PATTERN = re.compile(r"\b(?:0x)?([0-9A-Fa-f]{8})\s*[:=]\s*(?:0x)?([0-9A-Fa-f]{8})")
PC_PATTERN = re.compile(r"(?:\bPC\b|\(PC\))\s*(?:=|:)\s*(?:0x)?([0-9A-Fa-f]{8})", re.IGNORECASE)
APP_VERSION_PATTERN = re.compile(r"#define\s+APP_VERSION\s+\(u16\)(\d+)")
IOSSIOSPEED = 0x80045402
NM_LINE_PATTERN = re.compile(r"^([0-9A-Fa-f]+)\s+\S\s+(\S+)$")
READELF_SECTION_PATTERN = re.compile(
    r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+([A-Z]*)"
)
READELF_SYMBOL_PATTERN = re.compile(r"^\s*\d+:\s*([0-9A-Fa-f]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S+)$")
SIZE_SUMMARY_PATTERN = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+[0-9A-Fa-f]+\s+")
SIZE_SECTION_PATTERN = re.compile(r"^(\S+)\s+(\d+)\s+(\d+)$")
FLASH_LIMIT = 0x08024000
RAM_BASE = 0x20000000
BOOTLOADER_APP_RELEASE_FLAG = RAM_BASE
RAM_LIMIT = 0x2000C000
VTOR_REGISTER = 0xE000ED08
ARM_TOOL_HINTS = (
    Path(os.environ["TOOLCHAIN_BIN"]) if os.environ.get("TOOLCHAIN_BIN") else None,
    Path("/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin"),
    Path("/opt/homebrew/bin"),
    Path("/usr/local/bin"),
    Path.home() / ".platformio" / "packages" / "toolchain-gccarmnoneeabi" / "bin",
)


@dataclass
class StepResult:
    name: str
    passed: bool
    details: str
    metrics: dict[str, object]


def load_config(config_path: Path) -> dict:
    with config_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["config_path"] = str(config_path)
    return data


def now_text() -> str:
    return datetime.now().strftime(REPORT_TIME_FORMAT)


def print_line(message: str = "") -> None:
    print(message, flush=True)


def print_header(title: str) -> None:
    print_line("=" * 72)
    print_line(title)
    print_line("=" * 72)


def jlink_file_arg(path: Path) -> str:
    text = str(path)
    if any(ch.isspace() for ch in text):
        return '"' + text.replace('"', '\\"') + '"'
    return text


def ensure_directory(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def find_serial_ports() -> list[str]:
    patterns = ["/dev/cu.usb*", "/dev/tty.usb*", "/dev/cu.wch*", "/dev/tty.wch*", "/dev/cu.SLAB*", "/dev/tty.SLAB*"]
    paths: list[str] = []
    for pattern in patterns:
        paths.extend(sorted(str(path) for path in Path("/dev").glob(pattern.split("/dev/")[1])))
    return sorted(dict.fromkeys(paths))


def resolve_serial_port(config: dict, requested: str | None = None) -> str:
    if requested:
        return requested
    preferred = config["serial"].get("preferred_port")
    ports = find_serial_ports()
    if preferred and preferred in ports:
        return preferred
    if ports:
        return ports[0]
    raise RuntimeError("未检测到可用串口设备")


def resolve_firmware_path(config: dict, requested: str | None = None) -> Path:
    candidates: list[Path] = []
    if requested:
        candidates.append((ROOT / requested).resolve() if not os.path.isabs(requested) else Path(requested))
    else:
        build_cfg = config["build"]
        default_path = build_cfg.get("default_firmware")
        if default_path:
            candidates.append(ROOT / default_path)
        for item in build_cfg.get("fallback_firmware", []):
            candidates.append(ROOT / item)
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError("未找到可用固件文件")


def sha256_of_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_subprocess(command: Iterable[str], cwd: Path | None = None, timeout: int | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(command),
        cwd=str(cwd or ROOT),
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"命令失败: {' '.join(shlex.quote(part) for part in command)}\n{result.stdout}\n{result.stderr}")
    return result


def resolve_arm_tool(tool_name: str) -> str:
    direct = shutil.which(tool_name)
    if direct:
        return direct
    for hint in ARM_TOOL_HINTS:
        if hint is None:
            continue
        candidate = hint / tool_name
        if candidate.exists():
            return str(candidate)
    raise RuntimeError(f"未找到工具链命令: {tool_name}")


def parse_nm_symbols(output: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for line in output.splitlines():
        match = NM_LINE_PATTERN.match(line.strip())
        if not match:
            continue
        address, name = match.groups()
        values[name] = int(address, 16)
    return values


def parse_readelf_sections(output: str) -> dict[str, dict[str, object]]:
    sections: dict[str, dict[str, object]] = {}
    for line in output.splitlines():
        match = READELF_SECTION_PATTERN.match(line)
        if not match:
            continue
        name, address, size, flags = match.groups()
        sections[name] = {
            "addr": int(address, 16),
            "size": int(size, 16),
            "flags": flags,
        }
    return sections


def parse_readelf_symbols(output: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for line in output.splitlines():
        match = READELF_SYMBOL_PATTERN.match(line)
        if not match:
            continue
        address, name = match.groups()
        if name == "UND":
            continue
        values[name] = int(address, 16)
    return values


def parse_size_summary(output: str) -> dict[str, int]:
    for line in reversed(output.splitlines()):
        match = SIZE_SUMMARY_PATTERN.match(line.strip())
        if match:
            text, data, bss, total = match.groups()
            return {
                "text": int(text),
                "data": int(data),
                "bss": int(bss),
                "total": int(total),
            }
    raise RuntimeError("无法解析 arm-none-eabi-size 输出")


def parse_size_sections(output: str) -> dict[str, dict[str, int]]:
    sections: dict[str, dict[str, int]] = {}
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":") or stripped.startswith("section") or stripped.startswith("Total"):
            continue
        match = SIZE_SECTION_PATTERN.match(stripped)
        if not match:
            continue
        name, size, address = match.groups()
        sections[name] = {
            "size": int(size),
            "addr": int(address),
        }
    return sections


def parse_map_symbol_address(map_path: Path, symbol_name: str) -> int | None:
    if not map_path.exists():
        return None
    patterns = (
        re.compile(rf"\b0x([0-9A-Fa-f]+)\s+{re.escape(symbol_name)}\b"),
        re.compile(rf"\b{re.escape(symbol_name)}\s+0x([0-9A-Fa-f]+)\b"),
    )
    text = map_path.read_text(encoding="utf-8", errors="ignore")
    for pattern in patterns:
        match = pattern.search(text)
        if match:
            return int(match.group(1), 16)
    return None


def inspect_firmware_layout(elf_path: Path, flash_base: int) -> dict[str, int]:
    nm_output = run_subprocess([resolve_arm_tool("arm-none-eabi-nm"), "-n", str(elf_path)]).stdout
    readelf_sections_output = run_subprocess([resolve_arm_tool("arm-none-eabi-readelf"), "-SW", str(elf_path)]).stdout
    readelf_symbols_output = run_subprocess([resolve_arm_tool("arm-none-eabi-readelf"), "-sW", str(elf_path)]).stdout
    size_output = run_subprocess([resolve_arm_tool("arm-none-eabi-size"), str(elf_path)]).stdout
    size_sections_output = run_subprocess([resolve_arm_tool("arm-none-eabi-size"), "-A", str(elf_path)]).stdout
    map_path = elf_path.with_suffix(".map")

    nm_symbols = parse_nm_symbols(nm_output)
    readelf_sections = parse_readelf_sections(readelf_sections_output)
    readelf_symbols = parse_readelf_symbols(readelf_symbols_output)
    size_summary = parse_size_summary(size_output)
    size_sections = parse_size_sections(size_sections_output)

    vector_section = readelf_sections.get(".isr_vector")
    if vector_section is None:
        raise RuntimeError("ELF 中缺少 .isr_vector 段")

    def symbol_address(name: str) -> int | None:
        if name in readelf_symbols:
            return readelf_symbols[name]
        if name in nm_symbols:
            return nm_symbols[name]
        return parse_map_symbol_address(map_path, name)

    metadata = {
        "vector_addr": int(vector_section["addr"]),
        "vector_size": int(vector_section["size"]),
        "vector_symbol": symbol_address("g_pfnVectors") or 0,
        "prog_checksum": symbol_address("prog_checksum") or 0,
        "prog_length": symbol_address("prog_length") or 0,
        "device_type": symbol_address("device_type") or 0,
        "estack": symbol_address("_estack") or 0,
        "flash_used": size_summary["text"] + size_summary["data"],
        "ram_used": size_summary["data"] + size_summary["bss"],
        "flash_image_end": flash_base + size_summary["text"] + size_summary["data"],
        "ram_top": RAM_LIMIT,
        "data_addr": size_sections.get(".data", {}).get("addr", 0),
        "bss_addr": size_sections.get(".bss", {}).get("addr", 0),
    }
    return metadata


def verify_firmware_layout(elf_path: Path, flash_base: int) -> dict[str, int]:
    layout = inspect_firmware_layout(elf_path, flash_base)
    expected_metadata_base = flash_base + 0x200
    errors: list[str] = []
    if layout["vector_addr"] != flash_base:
        errors.append(f".isr_vector 地址错误: 0x{layout['vector_addr']:08X}")
    if layout["vector_symbol"] != flash_base:
        errors.append(f"g_pfnVectors 地址错误: 0x{layout['vector_symbol']:08X}")
    if layout["prog_checksum"] != expected_metadata_base:
        errors.append(f"prog_checksum 地址错误: 0x{layout['prog_checksum']:08X}")
    if layout["prog_length"] != expected_metadata_base + 4:
        errors.append(f"prog_length 地址错误: 0x{layout['prog_length']:08X}")
    if layout["device_type"] != expected_metadata_base + 8:
        errors.append(f"device_type 地址错误: 0x{layout['device_type']:08X}")
    if layout["estack"] != RAM_LIMIT:
        errors.append(f"_estack 地址错误: 0x{layout['estack']:08X}")
    if layout["flash_used"] > (FLASH_LIMIT - flash_base):
        errors.append(f"Flash 超限: {layout['flash_used']} bytes")
    if layout["ram_used"] > (RAM_LIMIT - RAM_BASE):
        errors.append(f"RAM 超限: {layout['ram_used']} bytes")
    if layout["flash_image_end"] > FLASH_LIMIT:
        errors.append(f"Flash 镜像末地址越界: 0x{layout['flash_image_end']:08X}")
    if errors:
        raise RuntimeError("构建基线校验失败:\n- " + "\n- ".join(errors))
    return layout


def jlink_commander(commands: list[str], config: dict, timeout: int = 30) -> str:
    if shutil.which("JLinkExe") is None:
        raise RuntimeError("未找到 JLinkExe")
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False, encoding="utf-8") as handle:
        handle.write("\n".join(commands) + "\n")
        script_path = handle.name
    try:
        jlink_cfg = config["jlink"]
        command = [
            "JLinkExe",
            "-NoGui",
            "1",
            "-AutoConnect",
            "1",
            "-device",
            jlink_cfg["device"],
            "-if",
            jlink_cfg["interface"],
            "-speed",
            str(jlink_cfg["speed_khz"]),
            "-CommanderScript",
            script_path,
        ]
        result = run_subprocess(command, timeout=timeout)
        return result.stdout + result.stderr
    finally:
        Path(script_path).unlink(missing_ok=True)


def detect_jlink(config: dict) -> dict:
    output = jlink_commander(["showemus", "exit"], config, timeout=20)
    connected = "J-Link" in output and "Cannot connect" not in output and "No emulators" not in output
    serial_match = re.search(r"Serial number:\s*([0-9]+)", output, re.IGNORECASE)
    return {
        "connected": connected,
        "serial_number": serial_match.group(1) if serial_match else "",
        "raw_output": output,
    }


def parse_mem32_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for address, value in HEX32_PATTERN.findall(output):
        values[f"0x{address.upper()}"] = f"0x{value.upper()}"
    return values


def parse_jlink_pc(output: str) -> int | None:
    matches = PC_PATTERN.findall(output)
    if not matches:
        return None
    return int(matches[-1], 16)


def parse_jlink_word(output: str, address: int) -> int | None:
    values = parse_mem32_values(output)
    value = values.get(f"0x{address:08X}")
    if not value:
        return None
    return int(value, 16)


def format_optional_hex(value: int | None) -> str:
    return "missing" if value is None else f"0x{value:08X}"


def jlink_status_is_app(output: str, app_base: int, app_limit: int) -> tuple[bool, int | None, int | None]:
    pc = parse_jlink_pc(output)
    vtor = parse_jlink_word(output, VTOR_REGISTER)
    in_app = pc is not None and app_base <= pc < app_limit and vtor == app_base
    return in_app, pc, vtor


def read_pc_vtor_commands() -> list[str]:
    return [
        "h",
        "regs",
        f"mem32 0x{VTOR_REGISTER:08X},1",
    ]


def jlink_resume_target(config: dict) -> None:
    jlink_commander(["connect", "g", "exit"], config, timeout=30)


def jlink_reset_type_commands(config: dict) -> list[str]:
    return ["RSetType 2"] if config.get("jlink", {}).get("reset_type") == "reset-pin" else []


def release_bootloader_to_app(config: dict, app_base: int, app_limit: int = FLASH_LIMIT) -> dict[str, object]:
    first_output = jlink_commander(
        [
            "connect",
            *jlink_reset_type_commands(config),
            "r",
            "g",
            "sleep 500",
            *read_pc_vtor_commands(),
            "exit",
        ],
        config,
        timeout=60,
    )
    in_app, pc, vtor = jlink_status_is_app(first_output, app_base, app_limit)
    if in_app:
        jlink_resume_target(config)
        return {
            "release_flag_written": False,
            "pc": f"0x{pc:08X}",
            "vtor": f"0x{vtor:08X}",
        }

    second_output = jlink_commander(
        [
            "connect",
            *jlink_reset_type_commands(config),
            "h",
            f"w1 0x{BOOTLOADER_APP_RELEASE_FLAG:08X} 0x01",
            "g",
            "sleep 500",
            *read_pc_vtor_commands(),
            "g",
            "exit",
        ],
        config,
        timeout=60,
    )
    in_app, pc, vtor = jlink_status_is_app(second_output, app_base, app_limit)
    if not in_app:
        raise RuntimeError(
            "Bootloader release failed: "
            f"PC={format_optional_hex(pc)}, VTOR={format_optional_hex(vtor)}, "
            f"expected PC in 0x{app_base:08X}..0x{app_limit - 1:08X} and VTOR=0x{app_base:08X}"
        )
    return {
        "release_flag_written": True,
        "pc": f"0x{pc:08X}",
        "vtor": f"0x{vtor:08X}",
    }


def read_target_status(config: dict) -> dict:
    jlink_cfg = config["jlink"]
    output = jlink_commander(
        [
            f"device {jlink_cfg['device']}",
            f"si {jlink_cfg['interface']}",
            f"speed {jlink_cfg['speed_khz']}",
            "connect",
            "mem32 0xE0042000 1",
            "mem32 0x1FFFF7E8 3",
            "exit",
        ],
        config,
        timeout=8,
    )
    values = parse_mem32_values(output)
    idcode = values.get("0xE0042000", "")
    uid = [values.get("0x1FFFF7E8", ""), values.get("0x1FFFF7EC", ""), values.get("0x1FFFF7F0", "")]
    return {
        "idcode": idcode,
        "uid": "".join(part.replace("0x", "") for part in uid if part),
        "raw_output": output,
    }


def parse_app_version() -> int:
    if not COMMON_H.exists():
        return 0
    text = COMMON_H.read_text(encoding="utf-8", errors="ignore")
    match = APP_VERSION_PATTERN.search(text)
    return int(match.group(1)) if match else 0


def latest_build_metadata(config: dict) -> dict:
    candidates = [ROOT / path for path in config["build"].get("fallback_firmware", [])]
    if config["build"].get("default_firmware"):
        candidates.insert(0, ROOT / config["build"]["default_firmware"])
    for candidate in candidates:
        if candidate.exists():
            stat = candidate.stat()
            return {
                "path": str(candidate.resolve()),
                "size": stat.st_size,
                "mtime": datetime.fromtimestamp(stat.st_mtime).strftime(REPORT_TIME_FORMAT),
            }
    return {"path": "", "size": 0, "mtime": ""}


def baud_flag(baudrate: int) -> int:
    mapping = {}
    for value, name in (
        (9600, "B9600"),
        (19200, "B19200"),
        (38400, "B38400"),
        (57600, "B57600"),
        (115200, "B115200"),
        (230400, "B230400"),
        (460800, "B460800"),
        (921600, "B921600"),
        (1000000, "B1000000"),
    ):
        flag = getattr(termios, name, None)
        if flag is not None:
            mapping[value] = flag
    if baudrate not in mapping:
        raise RuntimeError(f"不支持的波特率: {baudrate}")
    return mapping[baudrate]


def configure_serial(fd: int, baudrate: int, data_bits: int, stop_bits: int, parity: str) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CLOCAL | termios.CREAD
    attrs[3] = 0
    custom_baud = False
    try:
        speed_flag = baud_flag(baudrate)
        attrs[4] = speed_flag
        attrs[5] = speed_flag
    except RuntimeError:
        if sys.platform == "darwin":
            # macOS may not expose B1000000, but IOSSIOSPEED can still apply it.
            attrs[4] = termios.B9600
            attrs[5] = termios.B9600
            custom_baud = True
        else:
            raise
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    attrs[2] &= ~termios.CSIZE
    if data_bits == 7:
        attrs[2] |= termios.CS7
    else:
        attrs[2] |= termios.CS8
    if stop_bits == 2:
        attrs[2] |= termios.CSTOPB
    else:
        attrs[2] &= ~termios.CSTOPB
    parity = parity.upper()
    if parity == "N":
        attrs[2] &= ~termios.PARENB
    elif parity == "E":
        attrs[2] |= termios.PARENB
        attrs[2] &= ~termios.PARODD
    elif parity == "O":
        attrs[2] |= termios.PARENB
        attrs[2] |= termios.PARODD
    else:
        raise RuntimeError(f"不支持的校验位: {parity}")
    termios.tcflush(fd, termios.TCIFLUSH)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    if custom_baud:
        try:
            fcntl.ioctl(fd, IOSSIOSPEED, struct.pack("I", baudrate))
        except OSError as error:
            raise RuntimeError(f"设置自定义波特率失败: {baudrate} ({error})") from error


def infer_level(message: str) -> str:
    for level, pattern in LEVEL_PATTERNS:
        if pattern.search(message):
            return level
    return "INFO"


def infer_module(message: str) -> str:
    match = MODULE_PATTERN.match(message)
    if match:
        return match.group(1)
    for token in ("MQTT", "OTA", "UART", "LOGIN", "FLASH", "JLINK"):
        if token.lower() in message.lower():
            return token
    return "CORE"


def parse_log_entry(line: str) -> dict:
    message = line.strip()
    return {
        "timestamp": now_text(),
        "level": infer_level(message),
        "module": infer_module(message),
        "message": message,
    }


def serial_capture(config: dict, port: str, duration: float, output_dir: Path | None = None, send_text: str | None = None, expect: str | None = None) -> dict:
    serial_cfg = config["serial"]
    output_dir = ensure_directory(output_dir) if output_dir else None
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    entries: list[dict] = []
    raw_bytes = bytearray()
    matched = False
    text_log_path = output_dir / f"serial_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log" if output_dir else None
    jsonl_path = output_dir / f"serial_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl" if output_dir else None
    try:
        configure_serial(fd, int(serial_cfg["baudrate"]), int(serial_cfg["data_bits"]), int(serial_cfg["stop_bits"]), str(serial_cfg["parity"]))
        if send_text:
            os.write(fd, send_text.encode("utf-8"))
        start = time.time()
        buffer = bytearray()
        with (text_log_path.open("w", encoding="utf-8") if text_log_path else tempfile.TemporaryFile(mode="w+", encoding="utf-8")) as text_handle:
            with (jsonl_path.open("w", encoding="utf-8") if jsonl_path else tempfile.TemporaryFile(mode="w+", encoding="utf-8")) as jsonl_handle:
                while time.time() - start < duration:
                    ready, _, _ = select.select([fd], [], [], 0.2)
                    if not ready:
                        continue
                    chunk = os.read(fd, 512)
                    if not chunk:
                        continue
                    raw_bytes.extend(chunk)
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        raw_line, _, buffer = buffer.partition(b"\n")
                        line = raw_line.decode("utf-8", errors="replace").strip()
                        if not line:
                            continue
                        entry = parse_log_entry(line)
                        entries.append(entry)
                        text_handle.write(f"{entry['timestamp']} [{entry['level']}] [{entry['module']}] {entry['message']}\n")
                        jsonl_handle.write(json.dumps(entry, ensure_ascii=False) + "\n")
                        if expect and expect in line:
                            matched = True
        return {
            "entries": entries,
            "matched": matched,
            "raw_size": len(raw_bytes),
            "text_log": str(text_log_path) if text_log_path else "",
            "jsonl_log": str(jsonl_path) if jsonl_path else "",
        }
    finally:
        os.close(fd)


def normalize_firmware_to_bin(path: Path) -> tuple[Path, bool]:
    suffix = path.suffix.lower()
    if suffix == ".bin":
        return path, False
    objcopy = resolve_arm_tool("arm-none-eabi-objcopy")
    temp_path = Path(tempfile.mkstemp(suffix=".bin")[1])
    if suffix == ".elf":
        run_subprocess([objcopy, "-O", "binary", str(path), str(temp_path)])
    elif suffix == ".hex":
        run_subprocess([objcopy, "-I", "ihex", "-O", "binary", str(path), str(temp_path)])
    elif suffix == ".axf":
        run_subprocess([objcopy, "-O", "binary", str(path), str(temp_path)])
    else:
        temp_path.unlink(missing_ok=True)
        raise RuntimeError(f"不支持的固件格式: {path.suffix}")
    return temp_path, True


def flash_firmware(config: dict, firmware_path: Path, dry_run: bool = False) -> dict:
    normalized_bin, temporary = normalize_firmware_to_bin(firmware_path)
    try:
        checksum = sha256_of_file(normalized_bin)
        size = normalized_bin.stat().st_size
        base_address = config["build"]["flash_base"]
        flash_end = int(base_address, 16) + size
        if flash_end > FLASH_LIMIT:
            raise RuntimeError(
                f"固件越过APP分区上限: end=0x{flash_end:08X}, limit=0x{FLASH_LIMIT:08X}, size={size} bytes"
            )
        print_line(f"[10%] 固件校验完成: {firmware_path}")
        print_line(f"       归一化BIN: {normalized_bin}")
        print_line(f"       大小: {size} bytes")
        print_line(f"       写入范围: {base_address}..0x{flash_end - 1:08X}")
        print_line(f"       SHA256: {checksum}")
        if dry_run:
            print_line("[100%] 已完成烧录流程演练，未写入目标板")
            return {
                "dry_run": True,
                "checksum": checksum,
                "size": size,
                "base_address": base_address,
                "flash_end": f"0x{flash_end:08X}",
                "verified": True,
                "firmware_path": str(firmware_path),
            }
        commands = [
            f"device {config['jlink']['device']}",
            f"si {config['jlink']['interface']}",
            f"speed {config['jlink']['speed_khz']}",
            "connect",
            *jlink_reset_type_commands(config),
            "r",
            "h",
            f"loadbin {jlink_file_arg(normalized_bin)},{base_address}",
            "r",
            "h",
            "exit",
        ]
        jlink_commander(commands, config, timeout=120)
        print_line("[70%] 目标烧录完成，开始回读验证")
        verify_file = Path(tempfile.mkstemp(suffix=".bin")[1])
        try:
            jlink_commander(
                [
                    f"device {config['jlink']['device']}",
                    f"si {config['jlink']['interface']}",
                    f"speed {config['jlink']['speed_khz']}",
                    "connect",
                    *jlink_reset_type_commands(config),
                    "r",
                    "h",
                    f"savebin {jlink_file_arg(verify_file)},{base_address},0x{size:X}",
                    "exit",
                ],
                config,
                timeout=120,
            )
            verified = verify_file.read_bytes()[:size] == normalized_bin.read_bytes()
        finally:
            verify_file.unlink(missing_ok=True)
        if not verified:
            raise RuntimeError("烧录后回读校验失败")
        release = release_bootloader_to_app(config, int(base_address, 0), FLASH_LIMIT)
        print_line(
            "[100%] 烧录完成、回读校验通过、App 已释放: "
            f"PC={release['pc']} VTOR={release['vtor']} "
            f"release_flag_written={release['release_flag_written']}"
        )
        return {
            "dry_run": False,
            "checksum": checksum,
            "size": size,
            "base_address": base_address,
            "flash_end": f"0x{flash_end:08X}",
            "verified": verified,
            "firmware_path": str(firmware_path),
            "release": release,
        }
    finally:
        if temporary:
            normalized_bin.unlink(missing_ok=True)


def build_project(config: dict) -> dict:
    result = run_subprocess(["make", "build"], cwd=ROOT, check=False)
    metadata = latest_build_metadata(config)
    gcc_success = result.returncode == 0
    layout: dict[str, int] = {}
    layout_error = ""
    if gcc_success and config["build"].get("default_firmware"):
        elf_path = ROOT / config["build"]["default_firmware"]
        try:
            layout = verify_firmware_layout(elf_path, int(config["build"]["flash_base"], 16))
        except Exception as error:
            gcc_success = False
            layout_error = str(error)
    return {
        "success": gcc_success,
        "gcc_success": gcc_success,
        "fallback_used": False,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "artifact": metadata,
        "layout": layout,
        "layout_error": layout_error,
    }


def generate_report(config: dict, step_results: list[StepResult], report_name: str) -> dict:
    report_dir = ensure_directory(ROOT / config["outputs"]["report_dir"])
    markdown_path = report_dir / f"{report_name}.md"
    json_path = report_dir / f"{report_name}.json"
    passed_count = sum(1 for item in step_results if item.passed)
    payload = {
        "generated_at": now_text(),
        "environment": {
            "config": config["config_path"],
            "jlink_device": config["jlink"]["device"],
            "serial_port": config["serial"]["preferred_port"],
            "serial_baudrate": config["serial"]["baudrate"],
        },
        "results": [
            {"name": item.name, "passed": item.passed, "details": item.details, "metrics": item.metrics}
            for item in step_results
        ],
        "summary": {
            "total": len(step_results),
            "passed": passed_count,
            "failed": len(step_results) - passed_count,
        },
    }
    markdown_lines = [
        "# 单片机开发调试测试报告",
        "",
        f"- 生成时间: {payload['generated_at']}",
        f"- J-Link设备: {payload['environment']['jlink_device']}",
        f"- 串口: {payload['environment']['serial_port']}",
        f"- 波特率: {payload['environment']['serial_baudrate']}",
        "",
        "## 执行结果",
        "",
    ]
    for item in step_results:
        status = "通过" if item.passed else "失败"
        markdown_lines.append(f"- {item.name}: {status} | {item.details}")
        if item.metrics:
            markdown_lines.append(f"  - 指标: {json.dumps(item.metrics, ensure_ascii=False)}")
    markdown_lines.extend(
        [
            "",
            "## 汇总",
            "",
            f"- 总用例: {payload['summary']['total']}",
            f"- 通过: {payload['summary']['passed']}",
            f"- 失败: {payload['summary']['failed']}",
        ]
    )
    markdown_path.write_text("\n".join(markdown_lines) + "\n", encoding="utf-8")
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"markdown": str(markdown_path), "json": str(json_path)}


def run_python_unittests(test_modules: list[str]) -> dict:
    command = [sys.executable, "-m", "unittest", *test_modules]
    result = run_subprocess(command, cwd=ROOT, check=False)
    return {
        "success": result.returncode == 0,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }


def step_status(config: dict, serial_probe_seconds: float) -> StepResult:
    jlink_info = detect_jlink(config)
    target_info = {"idcode": "", "uid": "", "query_error": ""}
    if jlink_info["connected"]:
        try:
            target_info.update(read_target_status(config))
        except Exception as error:
            target_info["query_error"] = str(error)
    firmware = latest_build_metadata(config)
    version = parse_app_version()
    serial_port = resolve_serial_port(config)
    serial_info = {
        "raw_size": 0,
        "text_log": "",
        "jsonl_log": "",
        "matched": False,
        "error": "",
    }
    try:
        serial_info.update(serial_capture(config, serial_port, serial_probe_seconds))
    except Exception as error:
        serial_info["error"] = str(error)
    active_state = "运行中" if serial_info["raw_size"] > 0 else "空闲或无日志"
    if serial_info["error"]:
        active_state = f"串口检测失败: {serial_info['error']}"
    passed = bool(jlink_info["connected"] and serial_port)
    details = f"ChipID={target_info['idcode'] or '未知'} UID={target_info['uid'] or '未知'} 版本={version} 运行状态={active_state}"
    metrics = {
        "firmware_version": version,
        "chip_id": target_info["idcode"],
        "uid": target_info["uid"],
        "build_artifact": firmware["path"],
        "serial_bytes": serial_info["raw_size"],
        "serial_port": serial_port,
        "serial_error": serial_info["error"],
        "target_query_error": target_info["query_error"],
    }
    return StepResult("状态检测", passed, details, metrics)


def step_serial_test(config: dict, duration: float, expect: str | None = None) -> StepResult:
    serial_port = resolve_serial_port(config)
    output_dir = ROOT / config["outputs"]["log_dir"]
    try:
        result = serial_capture(config, serial_port, duration, output_dir=output_dir, expect=expect)
    except Exception as error:
        return StepResult(
            "串口测试",
            False,
            f"串口测试失败: {error}",
            {
                "serial_port": serial_port,
                "raw_size": 0,
                "text_log": "",
                "jsonl_log": "",
                "matched": False,
                "error": str(error),
            },
        )
    passed = result["raw_size"] > 0 or expect is None
    if expect is not None:
        passed = result["matched"]
    details = f"串口已配置为 {config['serial']['baudrate']}-{config['serial']['data_bits']}-{config['serial']['parity']}-{config['serial']['stop_bits']}，采集 {result['raw_size']} bytes"
    metrics = {
        "serial_port": serial_port,
        "raw_size": result["raw_size"],
        "text_log": result["text_log"],
        "jsonl_log": result["jsonl_log"],
        "line_count": len(result["entries"]),
    }
    return StepResult("串口测试", passed, details, metrics)


def step_login_tests(config: dict) -> StepResult:
    tests = config["tests"]["unit_modules"]
    result = run_python_unittests(tests)
    passed = result["success"]
    details = "登录流程、权限验证、异常处理单元测试完成"
    metrics = {
        "modules": tests,
        "stdout_lines": len(result["stdout"].splitlines()),
        "stderr_lines": len(result["stderr"].splitlines()),
    }
    return StepResult("登录与异常测试", passed, details, metrics)


def cmd_info(config: dict, _: argparse.Namespace) -> int:
    print_header("单片机开发调试工作流")
    print_line(f"项目根目录: {ROOT}")
    print_line("主构建入口: python3 -m platformio run -e hk32f103cct6a")
    print_line(f"主Keil工程: {config['project']['keil_project']}")
    print_line(f"兼容 wrapper: {config['project']['gcc_makefile']}")
    print_line(f"默认串口: {config['serial']['preferred_port']}")
    print_line(f"默认波特率: {config['serial']['baudrate']}")
    print_line(f"J-Link设备: {config['jlink']['device']}")
    print_line("")
    print_line("可用命令:")
    for item in ("info", "jlink-detect", "serial-detect", "status", "build", "flash", "monitor", "serial-test", "gdb-server", "login-test", "auto-debug", "validate", "all"):
        print_line(f"  {item}")
    return 0


def cmd_jlink_detect(config: dict, _: argparse.Namespace) -> int:
    info = detect_jlink(config)
    print_line(json.dumps(info, ensure_ascii=False, indent=2))
    return 0 if info["connected"] else 1


def cmd_serial_detect(config: dict, _: argparse.Namespace) -> int:
    ports = find_serial_ports()
    print_line(json.dumps({"ports": ports}, ensure_ascii=False, indent=2))
    return 0 if ports else 1


def cmd_status(config: dict, args: argparse.Namespace) -> int:
    result = step_status(config, args.probe)
    print_line(json.dumps({"passed": result.passed, "details": result.details, "metrics": result.metrics}, ensure_ascii=False, indent=2))
    return 0 if result.passed else 1


def cmd_build(config: dict, _: argparse.Namespace) -> int:
    result = build_project(config)
    print_line(result["stdout"])
    if result["stderr"]:
        print_line(result["stderr"])
    if result["layout_error"]:
        print_line(result["layout_error"])
    print_line(
        json.dumps(
            {
                "gcc_success": result["gcc_success"],
                "fallback_used": result["fallback_used"],
                "artifact": result["artifact"],
                "layout": result["layout"],
                "layout_error": result["layout_error"],
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0 if result["success"] else 1


def cmd_flash(config: dict, args: argparse.Namespace) -> int:
    firmware = resolve_firmware_path(config, args.file)
    result = flash_firmware(config, firmware, dry_run=args.dry_run)
    print_line(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


def cmd_monitor(config: dict, args: argparse.Namespace) -> int:
    serial_port = resolve_serial_port(config, args.port)
    output_dir = ROOT / config["outputs"]["log_dir"]
    result = serial_capture(config, serial_port, args.duration, output_dir=output_dir, expect=args.expect)
    for entry in result["entries"]:
        print_line(f"{entry['timestamp']} [{entry['level']}] [{entry['module']}] {entry['message']}")
    print_line(json.dumps({"raw_size": result["raw_size"], "text_log": result["text_log"], "jsonl_log": result["jsonl_log"]}, ensure_ascii=False, indent=2))
    return 0


def cmd_serial_test(config: dict, args: argparse.Namespace) -> int:
    result = step_serial_test(config, args.duration, expect=args.expect)
    print_line(json.dumps({"passed": result.passed, "details": result.details, "metrics": result.metrics}, ensure_ascii=False, indent=2))
    return 0 if result.passed else 1


def cmd_gdb_server(config: dict, _: argparse.Namespace) -> int:
    jlink_cfg = config["jlink"]
    command = [
        "JLinkGDBServer",
        "-device",
        jlink_cfg["device"],
        "-if",
        jlink_cfg["interface"],
        "-speed",
        str(jlink_cfg["speed_khz"]),
        "-port",
        str(config["debug"]["gdb_port"]),
    ]
    process = subprocess.Popen(command, cwd=str(ROOT))
    print_line(f"GDB Server PID={process.pid}")
    return process.wait()


def cmd_login_test(config: dict, _: argparse.Namespace) -> int:
    result = step_login_tests(config)
    detail = {"passed": result.passed, "details": result.details, "metrics": result.metrics}
    print_line(json.dumps(detail, ensure_ascii=False, indent=2))
    return 0 if result.passed else 1


def cmd_validate(config: dict, args: argparse.Namespace) -> int:
    steps: list[StepResult] = []
    if args.build_first:
        build_result = build_project(config)
        steps.append(
            StepResult(
                "构建检查",
                build_result["success"],
                "PlatformIO 主线构建与布局校验通过" if build_result["gcc_success"] else (build_result["layout_error"] or "PlatformIO 主线构建失败"),
                {
                    "artifact": build_result["artifact"]["path"],
                    "gcc_success": build_result["gcc_success"],
                    "fallback_used": build_result["fallback_used"],
                    "layout": build_result["layout"],
                    "layout_error": build_result["layout_error"],
                },
            )
        )
    steps.append(step_status(config, args.status_probe))
    if args.flash:
        firmware = resolve_firmware_path(config, args.file)
        flash_result = flash_firmware(config, firmware, dry_run=args.dry_run)
        steps.append(
            StepResult(
                "烧录验证",
                flash_result["verified"],
                "烧录前校验、烧录执行、烧录后回读校验已完成" if not flash_result["dry_run"] else "已完成烧录干运行校验",
                flash_result,
            )
        )
    steps.append(step_serial_test(config, args.serial_duration, expect=args.expect))
    steps.append(step_login_tests(config))
    report = generate_report(config, steps, args.report_name)
    print_line(json.dumps({"report": report, "passed": all(item.passed for item in steps)}, ensure_ascii=False, indent=2))
    return 0 if all(item.passed for item in steps) else 1


def cmd_auto_debug(config: dict, args: argparse.Namespace) -> int:
    last_code = 1
    for index in range(1, args.retries + 1):
        print_line(f"[TRY {index}/{args.retries}] 开始自动调试")
        validate_args = argparse.Namespace(
            build_first=args.build_first,
            flash=args.flash,
            dry_run=args.dry_run,
            file=args.file,
            serial_duration=args.serial_duration,
            status_probe=args.status_probe,
            expect=args.expect,
            report_name=f"{args.report_name}_try{index}",
        )
        last_code = cmd_validate(config, validate_args)
        if last_code == 0:
            return 0
        time.sleep(args.retry_interval)
    return last_code


def cmd_all(config: dict, args: argparse.Namespace) -> int:
    build_code = cmd_build(config, args)
    if build_code != 0 and not args.allow_build_fail:
        return build_code
    flash_code = cmd_flash(config, argparse.Namespace(file=args.file, dry_run=args.dry_run))
    if flash_code != 0:
        return flash_code
    return cmd_monitor(config, argparse.Namespace(port=args.port, duration=args.duration, expect=args.expect))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mcu_workflow", description="单片机开发调试工作流")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG))
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("info")
    subparsers.add_parser("jlink-detect")
    subparsers.add_parser("serial-detect")

    status_parser = subparsers.add_parser("status")
    status_parser.add_argument("--probe", type=float, default=1.5)

    subparsers.add_parser("build")

    flash_parser = subparsers.add_parser("flash")
    flash_parser.add_argument("--file")
    flash_parser.add_argument("--dry-run", action="store_true")

    monitor_parser = subparsers.add_parser("monitor")
    monitor_parser.add_argument("--port")
    monitor_parser.add_argument("--duration", type=float, default=10.0)
    monitor_parser.add_argument("--expect")

    serial_test_parser = subparsers.add_parser("serial-test")
    serial_test_parser.add_argument("--duration", type=float, default=5.0)
    serial_test_parser.add_argument("--expect")

    subparsers.add_parser("gdb-server")
    subparsers.add_parser("login-test")

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--build-first", action="store_true")
    validate_parser.add_argument("--flash", action="store_true")
    validate_parser.add_argument("--dry-run", action="store_true")
    validate_parser.add_argument("--file")
    validate_parser.add_argument("--serial-duration", type=float, default=5.0)
    validate_parser.add_argument("--status-probe", type=float, default=1.5)
    validate_parser.add_argument("--expect")
    validate_parser.add_argument("--report-name", default="latest_validation_report")

    auto_parser = subparsers.add_parser("auto-debug")
    auto_parser.add_argument("--build-first", action="store_true")
    auto_parser.add_argument("--flash", action="store_true")
    auto_parser.add_argument("--dry-run", action="store_true")
    auto_parser.add_argument("--file")
    auto_parser.add_argument("--serial-duration", type=float, default=5.0)
    auto_parser.add_argument("--status-probe", type=float, default=1.5)
    auto_parser.add_argument("--expect")
    auto_parser.add_argument("--report-name", default="auto_debug_report")
    auto_parser.add_argument("--retries", type=int, default=3)
    auto_parser.add_argument("--retry-interval", type=float, default=2.0)

    all_parser = subparsers.add_parser("all")
    all_parser.add_argument("--file")
    all_parser.add_argument("--dry-run", action="store_true")
    all_parser.add_argument("--duration", type=float, default=20.0)
    all_parser.add_argument("--port")
    all_parser.add_argument("--expect")
    all_parser.add_argument("--allow-build-fail", action="store_true")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    config = load_config(Path(args.config))
    config["serial"].setdefault("preferred_port", resolve_serial_port(config, config["serial"].get("preferred_port")) if find_serial_ports() else config["serial"].get("preferred_port", ""))
    config["serial"].setdefault("publish_topic", "")
    config["serial"].setdefault("subscribe_topic", "")
    get_topics(config["tests"]["imei"])
    commands = {
        "info": cmd_info,
        "jlink-detect": cmd_jlink_detect,
        "serial-detect": cmd_serial_detect,
        "status": cmd_status,
        "build": cmd_build,
        "flash": cmd_flash,
        "monitor": cmd_monitor,
        "serial-test": cmd_serial_test,
        "gdb-server": cmd_gdb_server,
        "login-test": cmd_login_test,
        "validate": cmd_validate,
        "auto-debug": cmd_auto_debug,
        "all": cmd_all,
    }
    return commands[args.command](config, args)


if __name__ == "__main__":
    sys.exit(main())
