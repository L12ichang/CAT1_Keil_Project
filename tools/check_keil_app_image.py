from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PROJECT = ROOT / "MDK-ARM-8008000" / "project.uvprojx"
APP_BASE = 0x08008000
APP_SAFE_END = 0x08024000
APP_MAX_SIZE = APP_SAFE_END - APP_BASE
DATA_MAIN = 0x08005000
DATA_BACKUP = 0x08006800
ZK_JSON_RX_MIN = 2048
ZK_JSON_TX_MIN = 2048
SYS_DATA_EXPECTED_SIZE = 408
CHECKSUM_OFFSET = 0x200
LENGTH_OFFSET = 0x204
DEVICE_TYPE_OFFSET = 0x208
EXPECTED_DEVICE_TYPE = 0x0003
DEMO_CHECKSUM_PLACEHOLDER = 0x12345678
DEMO_LENGTH_PLACEHOLDER = 0x89ABCDEF
FAKE_IMEI_FALLBACK = b"000000000000128"


@dataclass
class CheckReport:
    project: str
    output_name: str = "cat1"
    app_base: int = APP_BASE
    safe_end: int = APP_SAFE_END
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    details: dict[str, object] = field(default_factory=dict)

    @property
    def passed(self) -> bool:
        return not self.errors


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_hex(value: str) -> int:
    return int(value, 16)


def normalize_project_text(text: str) -> str:
    return text.replace("\\", "/").lower()


def parse_c_define_int(
    source: str, name: str, resolving: frozenset[str] = frozenset()
) -> int | None:
    if name in resolving:
        return None

    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([^\r\n]+)",
        source,
        flags=re.MULTILINE,
    )
    if not match:
        return None

    expression = re.split(r"//|/\*", match.group(1), maxsplit=1)[0].strip()
    expression = re.sub(r"\(\s*[us](?:8|16|32|64)\s*\)", "", expression)
    while expression.startswith("(") and expression.endswith(")"):
        expression = expression[1:-1].strip()

    literal = re.fullmatch(r"(0x[0-9A-Fa-f]+|\d+)[uUlL]*", expression)
    if literal:
        return int(literal.group(1), 0)

    alias = re.fullmatch(r"[A-Za-z_]\w*", expression)
    if alias:
        return parse_c_define_int(source, alias.group(0), resolving | {name})
    return None


def parse_output_name(project_text: str) -> str:
    match = re.search(r"<OutputName>([^<]+)</OutputName>", project_text)
    return match.group(1).strip() if match else "cat1"


def parse_sct_base(sct_text: str) -> tuple[int | None, int | None]:
    match = re.search(r"\bLR_IROM1\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)", sct_text)
    if not match:
        return None, None
    return parse_hex(match.group(1)), parse_hex(match.group(2))


def parse_map_values(map_text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    vectors = re.search(r"\b__Vectors\s+0x([0-9A-Fa-f]+)\b", map_text)
    if vectors:
        values["vectors"] = parse_hex(vectors.group(1))
    load_region = re.search(r"Load Region LR_IROM1 \(Base: 0x([0-9A-Fa-f]+), Size: 0x([0-9A-Fa-f]+)", map_text)
    if load_region:
        values["load_base"] = parse_hex(load_region.group(1))
        values["load_size"] = parse_hex(load_region.group(2))
    exec_region = re.search(r"Execution Region ER_IROM1 \(Exec base: 0x([0-9A-Fa-f]+), Load base: 0x([0-9A-Fa-f]+)", map_text)
    if exec_region:
        values["exec_base"] = parse_hex(exec_region.group(1))
        values["exec_load_base"] = parse_hex(exec_region.group(2))
    return values


def read_le32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def read_le16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def sum32_word(word: int) -> int:
    return (
        ((word >> 24) & 0xFF)
        + ((word >> 16) & 0xFF)
        + ((word >> 8) & 0xFF)
        + (word & 0xFF)
    )


def user_flash_checksum(data: bytes, size: int) -> int:
    total = 0
    for index in range(size // 4):
        offset = index * 4
        if CHECKSUM_OFFSET <= offset < LENGTH_OFFSET + 4:
            continue
        total = (total + sum32_word(read_le32(data, offset))) & 0xFFFFFFFF
    return total


def check_bin_metadata(bin_path: Path, data: bytes, report: CheckReport) -> None:
    if len(data) < DEVICE_TYPE_OFFSET + 2:
        report.errors.append(f"BIN is too small for app metadata: {bin_path}")
        return

    prog_checksum = read_le32(data, CHECKSUM_OFFSET)
    prog_length = read_le32(data, LENGTH_OFFSET)
    device_type = read_le16(data, DEVICE_TYPE_OFFSET)
    stored_size = prog_length & 0x00FFFFFF

    report.details["prog_checksum"] = f"0x{prog_checksum:08X}"
    report.details["prog_length"] = f"0x{prog_length:08X}"
    report.details["prog_length_size"] = stored_size
    report.details["device_type"] = f"0x{device_type:04X}"

    if prog_checksum == DEMO_CHECKSUM_PLACEHOLDER:
        report.errors.append("App metadata still contains demo checksum placeholder 0x12345678")
    if prog_length == DEMO_LENGTH_PLACEHOLDER:
        report.errors.append("App metadata still contains demo length placeholder 0x89ABCDEF")
    if device_type != EXPECTED_DEVICE_TYPE:
        report.errors.append(f"device_type is not 0x{EXPECTED_DEVICE_TYPE:04X}: 0x{device_type:04X}")
    if stored_size != len(data):
        report.errors.append(f"prog_length lower 24 bits ({stored_size}) do not match BIN size ({len(data)})")
    if stored_size >= APP_MAX_SIZE:
        report.errors.append(f"prog_length lower 24 bits crosses APP size limit: {stored_size}")
    if stored_size % 4 != 0:
        report.errors.append(f"prog_length lower 24 bits is not word aligned: {stored_size}")
        return
    if stored_size <= len(data) and stored_size % 4 == 0:
        calculated = user_flash_checksum(data, stored_size)
        report.details["calculated_checksum"] = f"0x{calculated:08X}"
        if prog_checksum != calculated:
            report.errors.append(
                f"prog_checksum mismatch: stored=0x{prog_checksum:08X}, calculated=0x{calculated:08X}"
            )
    if FAKE_IMEI_FALLBACK in data:
        report.errors.append("BIN still contains fake IMEI fallback string 000000000000128")


def newest_source_mtime(root: Path, project_path: Path) -> float:
    newest = project_path.stat().st_mtime
    for pattern in ("Core/Src/**/*.c", "Core/Src/**/*.h"):
        for path in root.glob(pattern):
            if path.is_file():
                newest = max(newest, path.stat().st_mtime)
    return newest


def check_project(project_path: Path, report: CheckReport) -> str:
    if not project_path.exists():
        report.errors.append(f"Project does not exist: {project_path}")
        return ""

    project_text = read_text(project_path)
    normalized = normalize_project_text(project_text)
    report.output_name = parse_output_name(project_text)
    report.details["project_path"] = str(project_path)
    report.details["output_name"] = report.output_name

    if project_path.parent.name != "MDK-ARM-8008000":
        report.errors.append("Wrong Keil project: use MDK-ARM-8008000/project.uvprojx for the app image")
    if not re.search(r"<Define>[^<]*\bAPROM_OFFSET\b[^<]*</Define>", project_text):
        report.errors.append("APROM_OFFSET is not defined; the app would not use VTOR 0x08008000")
    for required in (
        "../core/src/lampprotocollib/mqtt_zk_protocol.c",
        "../core/src/gateway/net_dim.c",
    ):
        if required not in normalized:
            report.errors.append(f"Project is missing source file: {required}")
    if "<RunUserProg2>1</RunUserProg2>" not in project_text:
        report.warnings.append("Post-build UserProg2 is not enabled; cat1.bin may not be regenerated by Keil")
    if "hex2bin_arm.bat" not in normalized:
        report.warnings.append("hex2bin_arm.bat is not configured; verify bin generation manually")

    return project_text


def check_source_contracts(report: CheckReport) -> None:
    sys_data_text = read_text(ROOT / "Core" / "Src" / "sys_data.h")
    flash_layout_text = read_text(ROOT / "Core" / "Src" / "flash_address_assignment.h")
    flash_contract_text = f"{sys_data_text}\n{flash_layout_text}"
    mqtt_header = read_text(ROOT / "Core" / "Src" / "LampProtocolLib" / "mqtt_zk_protocol.h")
    mqtt_source = read_text(ROOT / "Core" / "Src" / "LampProtocolLib" / "mqtt_zk_protocol.c")
    json_source = read_text(ROOT / "Core" / "Src" / "LampProtocolLib" / "Json_Protocol.c")

    expected_values = {
        "DATAROM_STARTADDR": DATA_MAIN,
        "BAKDATAROM_STARTADDR": DATA_BACKUP,
        "APROM_STARTADDR": APP_BASE,
        "APROM_SAFE_ENDADDR": APP_SAFE_END,
        "OTABAKROM_STARTADDR": 0x08024000,
    }
    source_values: dict[str, str] = {}
    for name, expected in expected_values.items():
        actual = parse_c_define_int(flash_contract_text, name)
        source_values[name] = f"0x{actual:08X}" if actual is not None else "missing"
        if actual != expected:
            actual_text = source_values[name]
            report.errors.append(f"{name} is not 0x{expected:08X}: {actual_text}")
    report.details["source_flash_contract"] = source_values

    actual_size = parse_c_define_int(sys_data_text, "SYS_DATA_ST_EXPECTED_SIZE")
    report.details["sys_data_expected_size"] = actual_size
    if actual_size != SYS_DATA_EXPECTED_SIZE:
        report.errors.append(f"SYS_DATA_ST_EXPECTED_SIZE is not {SYS_DATA_EXPECTED_SIZE}: {actual_size}")
    if "sys_data_st_size_must_remain_408" not in sys_data_text:
        report.errors.append("sys_data_st compile-time size guard is missing")

    rx_max = parse_c_define_int(mqtt_header, "ZK_JSON_RX_MAX")
    tx_size = parse_c_define_int(mqtt_header, "ZK_JSON_BUF_SIZE")
    pool_size = parse_c_define_int(mqtt_header, "ZK_CJSON_POOL_SIZE")
    report.details["zk_json_rx_max"] = rx_max
    report.details["zk_json_tx_size"] = tx_size
    report.details["zk_cjson_pool_size"] = pool_size
    if rx_max is None or rx_max < ZK_JSON_RX_MIN:
        report.errors.append(f"ZK_JSON_RX_MAX must be at least {ZK_JSON_RX_MIN}: {rx_max}")
    if tx_size is None or tx_size < ZK_JSON_TX_MIN:
        report.errors.append(f"ZK_JSON_BUF_SIZE must be at least {ZK_JSON_TX_MIN}: {tx_size}")
    if pool_size is None or pool_size < 4096:
        report.errors.append(f"ZK_CJSON_POOL_SIZE must be at least 4096: {pool_size}")

    for required in (
        "cJSON_InitHooks(&hooks);",
        "void zk_cjson_prepare_parse(void)",
        "zk_cjson_pool_offset = 0;",
        "zk_parse_message_header_from_root",
        "zk_message_header_matches_device",
    ):
        if required not in mqtt_source and required not in json_source:
            report.errors.append(f"ZK source contract missing: {required}")
    for forbidden in ("cJSON_Print(", "cJSON_PrintUnformatted("):
        if forbidden in mqtt_source:
            report.errors.append(f"Forbidden dynamic JSON print in mqtt_zk_protocol.c: {forbidden}")
    if re.search(r"(?<![A-Za-z0-9_])free\s*\(\s*(json_str|str)\s*\)\s*;", json_source):
        report.errors.append("Json_Protocol.c still frees cJSON allocations with free()")


def check_keil_outputs(project_path: Path, report: CheckReport, require_fresh: bool) -> None:
    out_dir = project_path.parent / "out"
    output_name = report.output_name
    sct_path = out_dir / f"{output_name}.sct"
    map_path = out_dir / f"{output_name}.map"
    bin_path = out_dir / f"{output_name}.bin"
    hex_path = out_dir / f"{output_name}.hex"
    report.details["out_dir"] = str(out_dir)

    required_outputs = (sct_path, map_path, bin_path, hex_path)
    if any(not path.exists() for path in required_outputs):
        missing = [str(path) for path in required_outputs if not path.exists()]
        report.details["keil_outputs_present"] = False
        report.details["missing_keil_outputs"] = missing
        if require_fresh:
            for path in missing:
                report.errors.append(f"Keil output missing: {path}")
        else:
            report.warnings.append("Keil outputs are absent; rebuild in Keil before burning firmware")
        return
    report.details["keil_outputs_present"] = True

    sct_base, sct_size = parse_sct_base(read_text(sct_path))
    report.details["sct_base"] = f"0x{sct_base:08X}" if sct_base is not None else ""
    report.details["sct_size"] = f"0x{sct_size:08X}" if sct_size is not None else ""
    if sct_base != report.app_base:
        report.errors.append(f"Scatter LR_IROM1 base is not 0x{report.app_base:08X}: {report.details['sct_base']}")

    map_values = parse_map_values(read_text(map_path))
    report.details["map_values"] = {key: f"0x{value:08X}" for key, value in map_values.items()}
    for key in ("vectors", "load_base", "exec_base", "exec_load_base"):
        if map_values.get(key) != report.app_base:
            actual = map_values.get(key)
            actual_text = f"0x{actual:08X}" if actual is not None else "missing"
            report.errors.append(f"Map {key} is not 0x{report.app_base:08X}: {actual_text}")

    bin_data = bin_path.read_bytes()
    bin_size = len(bin_data)
    flash_end = report.app_base + bin_size
    report.details["bin_path"] = str(bin_path)
    report.details["bin_size"] = bin_size
    report.details["flash_range"] = f"0x{report.app_base:08X}..0x{flash_end - 1:08X}"
    if flash_end > report.safe_end:
        report.errors.append(
            f"BIN crosses app safe end: end=0x{flash_end:08X}, limit=0x{report.safe_end:08X}, size={bin_size}"
        )
    check_bin_metadata(bin_path, bin_data, report)

    if require_fresh:
        latest_source = newest_source_mtime(ROOT, project_path)
        # The scatter file is a linker input and is not rewritten by every Keil
        # rebuild. Only generated outputs participate in the freshness gate.
        generated_outputs = (map_path, bin_path, hex_path)
        stale = [path for path in generated_outputs if path.stat().st_mtime < latest_source]
        report.details["freshness_checked"] = True
        if stale:
            names = ", ".join(str(path) for path in stale)
            report.errors.append(f"Keil outputs are older than source/project files; rebuild before burning: {names}")
    else:
        report.details["freshness_checked"] = False


def run_checks(
    project_path: Path = DEFAULT_PROJECT,
    app_base: int = APP_BASE,
    safe_end: int = APP_SAFE_END,
    require_fresh: bool = True,
) -> CheckReport:
    project_path = project_path.resolve()
    report = CheckReport(project=str(project_path), app_base=app_base, safe_end=safe_end)
    check_project(project_path, report)
    check_source_contracts(report)
    if project_path.exists():
        check_keil_outputs(project_path, report, require_fresh=require_fresh)
    return report


def print_human(report: CheckReport) -> None:
    status = "PASS" if report.passed else "FAIL"
    print(f"Keil app image check: {status}")
    print(f"Project: {report.project}")
    print(f"Expected app base: 0x{report.app_base:08X}")
    print(f"Safe end: 0x{report.safe_end:08X}")
    for key, value in report.details.items():
        print(f"{key}: {value}")
    for warning in report.warnings:
        print(f"WARNING: {warning}")
    for error in report.errors:
        print(f"ERROR: {error}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate the Keil app image before burning.")
    parser.add_argument("--project", default=str(DEFAULT_PROJECT), help="Path to project.uvprojx")
    parser.add_argument("--app-base", default=f"0x{APP_BASE:08X}", help="Expected app base address")
    parser.add_argument("--safe-end", default=f"0x{APP_SAFE_END:08X}", help="Exclusive app safe end address")
    parser.add_argument("--skip-freshness", action="store_true", help="Do not fail when Keil outputs are older than sources")
    parser.add_argument("--json", action="store_true", help="Print JSON report")
    args = parser.parse_args(argv)

    report = run_checks(
        project_path=Path(args.project),
        app_base=int(args.app_base, 0),
        safe_end=int(args.safe_end, 0),
        require_fresh=not args.skip_freshness,
    )
    if args.json:
        payload = {
            "passed": report.passed,
            "project": report.project,
            "output_name": report.output_name,
            "app_base": f"0x{report.app_base:08X}",
            "safe_end": f"0x{report.safe_end:08X}",
            "errors": report.errors,
            "warnings": report.warnings,
            "details": report.details,
        }
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print_human(report)
    return 0 if report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
