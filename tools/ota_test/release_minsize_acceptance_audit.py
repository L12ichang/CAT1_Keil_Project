#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path
from typing import Any

from inspect_ota_bin import inspect


REQUIRED_DEFINES = (
    "APP_LOG_ENABLE=0",
    "APP_OTA_LOG_ENABLE=0",
    "APP_HEX_LOG_ENABLE=0",
    "APP_PERF_PROFILE_ENABLE=0",
    "APP_PWM_DEBUG_ENABLE=0",
    "OTA_RAW_HEX_LOG_ENABLE=0",
    "OTA_DEBUG_DOWNLOAD_ONLY=0",
    "OTA_STREAM_TO_BACKUP_DEBUG=0",
    "OTA_DEBUG_CLEAR_ALL_UFS=0",
    "ZK_ENABLE_SUNRISE_PLAN=0",
    "BL0942_USE_FLOAT_XCAP_COMPENSATION=0",
    "ZK_PROTOCOL_ONLY=1",
    "LEGACY_DATAPOINT_PROTOCOL_ENABLE=0",
    "LEGACY_APP_PROCESS_ENABLE=0",
    "LEGACY_ACTIVE_ENABLE=0",
)

FORBIDDEN_ALLOCATED_SYMBOLS = (
    "hw_uart3_init",
    "hw_uart3_process",
    "dma_printf",
    "powf",
    "sqrtf",
    "sinf",
    "cosf",
    "tanf",
    "acosf",
    "asinf",
    "fmodf",
    "floorf",
    "appProcess",
    "uploadAllDataPoint",
    "uploadDriverDataPoint",
    "uploadDriverOnedataPoint",
    "uploadfaultListDataPoint",
    "Offline_report",
    "initUploadDataPoint",
    "addIntTypeDataPoint",
    "addFaultListDataPoint",
    "finishUploadDataPoint",
)

REQUIRED_DOCS = (
    "baseline_map_summary.md",
    "docs/map_size_compare.md",
    "docs/固件体积优化_第一轮报告.md",
    "docs/实机验证记录.md",
    "docs/第一轮验收矩阵.md",
    "docs/第一轮完成度审计.md",
    "docs/当前优化镜像OTA闭环交接清单.md",
)

DOC_MARKERS = {
    "baseline_map_summary.md": ("93212", "mqtt_zk_protocol.o", "baseline_cat1.map", "baseline_serial_log.txt"),
    "docs/map_size_compare.md": ("93212", "71648", "ota.o", "hw_uart3.o", "cjson.o"),
    "docs/固件体积优化_第一轮报告.md": ("71648", "cJSON", "RSetType 2", "release_minsize_acceptance_audit"),
    "docs/实机验证记录.md": ("J-Link", "MQTT", "BL0942", "NTC", "RTC", "OTA"),
    "docs/第一轮验收矩阵.md": ("Release-MinSize", "PASS", "BL0942", "RTC", "计划"),
    "docs/第一轮完成度审计.md": ("summary=pass", "71648", "BL0942", "type=2"),
    "docs/当前优化镜像OTA闭环交接清单.md": ("release_minsize_ota_20260710_025602.bin", "mqtt_guarded_ota", "summary=pass"),
}


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_project_defines(project_path: Path, target_name: str) -> dict[str, Any]:
    root = ET.parse(project_path).getroot()
    target = None
    for item in root.findall("./Targets/Target"):
        name = item.findtext("TargetName")
        if name == target_name:
            target = item
            break
    if target is None:
        return {"status": "fail", "target": target_name, "missing": list(REQUIRED_DEFINES), "define_text": ""}

    define_text = target.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/Define") or ""
    missing = [define for define in REQUIRED_DEFINES if define not in define_text]
    return {
        "status": "pass" if not missing else "fail",
        "target": target_name,
        "missing": missing,
        "define_text": define_text,
    }


def read_allocated_map_text(map_path: Path) -> str:
    text = map_path.read_text(encoding="latin-1")
    marker = "Image Symbol Table"
    marker_index = text.find(marker)
    if marker_index < 0:
        raise ValueError(f"{marker!r} not found in {map_path}")
    return text[marker_index:]


def check_map(map_path: Path, max_rom_size: int) -> dict[str, Any]:
    allocated = read_allocated_map_text(map_path)
    forbidden_hits = [
        symbol for symbol in FORBIDDEN_ALLOCATED_SYMBOLS
        if re.search(rf"\b{re.escape(symbol)}\b", allocated)
    ]
    total_match = re.search(r"Total ROM Size.*?(\d+)\s*\(", allocated)
    total_rom = int(total_match.group(1)) if total_match else None
    base_ok = "0x08008000" in allocated
    return {
        "status": "pass" if not forbidden_hits and total_rom is not None and total_rom <= max_rom_size and base_ok else "fail",
        "path": str(map_path),
        "total_rom": total_rom,
        "max_rom_size": max_rom_size,
        "app_base_08008000_seen": base_ok,
        "forbidden_allocated_hits": forbidden_hits,
    }


def read_jsonl_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def read_text_any(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8", errors="replace")


def latest_event(events: list[dict[str, Any]], name: str) -> dict[str, Any] | None:
    for event in reversed(events):
        if event.get("event") == name:
            return event
    return None


def check_guarded_ota_log(log_path: Path) -> dict[str, Any]:
    events = read_jsonl_events(log_path)
    preflight_event = latest_event(events, "preflight")
    abort_event = latest_event(events, "abort_mismatch_no_publish")
    match_event = latest_event(events, "preflight_match_no_publish") or latest_event(events, "preflight_match_publish_allowed")
    finish_event = latest_event(events, "finish")
    if preflight_event is None:
        return {"status": "fail", "path": str(log_path), "reason": "missing preflight event"}

    preflight = preflight_event.get("preflight", {})
    if match_event is not None:
        finish_state = finish_event.get("state", {}) if finish_event else {}
        live_checks = {}
        if finish_event is not None:
            live_checks = {
                "published": finish_state.get("published") is True,
                "progress": finish_state.get("progress") is True,
                "online_after_ota": finish_state.get("online_after_ota") is True,
                "error_false": finish_state.get("error") is False,
                "offline_zero": finish_state.get("offline") == 0,
            }
        status = "pass" if finish_event is None or all(live_checks.values()) else "fail"
        conclusion = "url_matches_current_image" if finish_event is None else "url_matches_current_image_and_live_ota_passed"
    elif abort_event is not None and preflight.get("match") is False:
        status = "blocked"
        conclusion = "url_still_points_to_noncurrent_image"
        live_checks = {}
        finish_state = {}
    else:
        status = "fail"
        conclusion = "preflight_did_not_reach_clear_match_or_safe_abort"
        live_checks = {}
        finish_state = {}

    return {
        "status": status,
        "path": str(log_path),
        "conclusion": conclusion,
        "match": preflight.get("match"),
        "mismatch_reasons": preflight.get("mismatch_reasons", []),
        "expected": preflight.get("expected", {}),
        "candidate": preflight.get("candidate", {}),
        "live_checks": live_checks,
        "finish_state": finish_state,
    }


def check_docs(root: Path) -> dict[str, Any]:
    missing = [path for path in REQUIRED_DOCS if not (root / path).exists()]
    marker_checks: dict[str, Any] = {}
    for doc_path, markers in DOC_MARKERS.items():
        path = root / doc_path
        if not path.exists():
            marker_checks[doc_path] = {"status": "fail", "reason": "missing"}
            continue
        text = read_text_any(path)
        checks = {marker: marker in text for marker in markers}
        marker_checks[doc_path] = {
            "status": "pass" if all(checks.values()) else "fail",
            "checks": checks,
        }
    failed_markers = [path for path, item in marker_checks.items() if item["status"] != "pass"]
    return {
        "status": "pass" if not missing and not failed_markers else "fail",
        "missing": missing,
        "failed_markers": failed_markers,
        "required": list(REQUIRED_DOCS),
        "marker_checks": marker_checks,
    }


def check_artifact_file(path_text: str, expected_size: int | None = None) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing artifact"}
    actual_size = path.stat().st_size
    checks = {"exists": True}
    if expected_size is not None:
        checks["size_matches"] = actual_size == expected_size
    return {
        "status": "pass" if all(checks.values()) else "fail",
        "path": path_text,
        "checks": checks,
        "actual_size": actual_size,
        "expected_size": expected_size,
    }


def check_build_log(path_text: str, expected: dict[str, int]) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing build log"}
    text = read_text_any(path)
    size_match = re.search(
        r"Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)",
        text,
    )
    wrote_match = re.search(r"Wrote .+?\((\d+) bytes, checksum=(0x[0-9A-Fa-f]+)\)", text)
    actual = {
        "code": int(size_match.group(1)) if size_match else None,
        "ro_data": int(size_match.group(2)) if size_match else None,
        "rw_data": int(size_match.group(3)) if size_match else None,
        "zi_data": int(size_match.group(4)) if size_match else None,
        "rom_bytes": int(wrote_match.group(1)) if wrote_match else None,
        "checksum": wrote_match.group(2).upper() if wrote_match else None,
        "zero_errors_warnings": "0 Error(s), 0 Warning(s)" in text,
    }
    checks = {
        "program_size_present": size_match is not None,
        "wrote_line_present": wrote_match is not None,
        "zero_errors_warnings": actual["zero_errors_warnings"],
    }
    for key, expected_value in expected.items():
        actual_value = actual.get(key)
        checks[f"{key}_matches"] = actual_value == expected_value if isinstance(expected_value, int) else actual_value == str(expected_value).upper()
    return {
        "status": "pass" if all(checks.values()) else "fail",
        "path": path_text,
        "checks": checks,
        "actual": actual,
        "expected": expected,
    }


def check_serial_log(path_text: str, required_markers: tuple[str, ...]) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing serial log"}
    text = read_text_any(path)
    checks = {marker: marker in text for marker in required_markers}
    return {"status": "pass" if all(checks.values()) else "fail", "path": path_text, "checks": checks}


def check_jlink_flash_log(path_text: str, expected_bytes: int) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing J-Link log"}
    text = read_text_any(path)
    checks = {
        "loadbin_seen": "loadbin" in text,
        "verify_successful": "Verify successful." in text,
        "expected_read_bytes": f"Reading {expected_bytes} bytes" in text,
        "app_address_seen": "0x08008000" in text,
    }
    return {"status": "pass" if all(checks.values()) else "fail", "path": path_text, "checks": checks}


def nested_get(value: dict[str, Any], path: tuple[str, ...], default: Any = None) -> Any:
    current: Any = value
    for item in path:
        if not isinstance(current, dict):
            return default
        current = current.get(item, default)
    return current


def check_json_finish(path_text: str, predicate, details) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing log"}
    events = read_jsonl_events(path)
    finish = latest_event(events, "finish")
    if finish is None:
        return {"status": "fail", "path": path_text, "reason": "missing finish event"}
    ok = bool(predicate(finish))
    return {
        "status": "pass" if ok else "fail",
        "path": path_text,
        "details": details(finish),
    }


def check_reset_pin_probe(path_text: str) -> dict[str, Any]:
    path = Path(path_text)
    if not path.exists():
        return {"status": "fail", "path": path_text, "reason": "missing log"}
    text = read_text_any(path)
    checks = {
        "reset_pin": "RSetType 2" in text and "Reset type RESETPIN" in text,
        "pc_in_app": re.search(r"\bPC\s*=\s*080[0-9A-Fa-f]{5}", text) is not None,
        "vtor_app": "E000ED08 = 08008000" in text,
        "cfsr_zero": "E000ED28 = 00000000" in text,
        "hfsr_zero": "E000ED2C = 00000000" in text,
        "rcc_zero": "40021024 = 00000000" in text,
    }
    return {"status": "pass" if all(checks.values()) else "fail", "path": path_text, "checks": checks}


def check_live_evidence(paths: dict[str, str]) -> dict[str, Any]:
    logs: dict[str, Any] = {
        "release_monitor": check_json_finish(
            paths["release_monitor"],
            lambda finish: finish.get("passed") is True and
            nested_get(finish, ("checks", "login")) is True and
            nested_get(finish, ("checks", "heartbeat")) is True and
            nested_get(finish, ("checks", "offline")) is True and
            nested_get(finish, ("state", "last_sver")) == 22 and
            nested_get(finish, ("state", "offline_count")) == 0,
            lambda finish: {"passed": finish.get("passed"), "checks": finish.get("checks"), "state": finish.get("state")},
        ),
        "control_query": check_json_finish(
            paths["control_query"],
            lambda finish: nested_get(finish, ("state", "connected")) is True and
            nested_get(finish, ("state", "patrol_ack")) is True and
            nested_get(finish, ("state", "patrol_report")) is True and
            nested_get(finish, ("state", "heartbeat")) is True,
            lambda finish: finish.get("state"),
        ),
        "control_dim": check_json_finish(
            paths["control_dim"],
            lambda finish: all(nested_get(finish, ("state", key)) is True for key in ("ack80", "ack100", "bri80_report", "bri100_report")),
            lambda finish: finish.get("state"),
        ),
        "control_switch": check_json_finish(
            paths["control_switch"],
            lambda finish: all(nested_get(finish, ("state", key)) is True for key in ("ack_off", "ack_on", "off_report", "on_report")),
            lambda finish: finish.get("state"),
        ),
        "param_persist": check_json_finish(
            paths["param_persist"],
            lambda finish: nested_get(finish, ("summary", "passed")) is True and
            nested_get(finish, ("summary", "before_reset", "sBri")) == nested_get(finish, ("summary", "temp_sBri")) and
            nested_get(finish, ("summary", "after_reset", "sBri")) == nested_get(finish, ("summary", "temp_sBri")),
            lambda finish: finish.get("summary"),
        ),
        "ntc": check_json_finish(
            paths["ntc"],
            lambda finish: nested_get(finish, ("result", "active_pass")) is True and
            nested_get(finish, ("result", "restore_pass")) is True and
            nested_get(finish, ("result", "offline")) == 0,
            lambda finish: finish.get("result"),
        ),
        "bl0942_unloaded": check_json_finish(
            paths["bl0942_unloaded"],
            lambda finish: finish.get("passed") is True and
            nested_get(finish, ("summary", "sample_count"), 0) >= 1 and
            nested_get(finish, ("state", "offline")) == 0 and
            not nested_get(finish, ("state", "errors"), []),
            lambda finish: {"passed": finish.get("passed"), "summary": finish.get("summary"), "state": finish.get("state")},
        ),
        "bl0942_loaded": check_json_finish(
            paths["bl0942_loaded"],
            lambda finish: finish.get("passed") is True and
            nested_get(finish, ("summary", "sample_count"), 0) >= 6 and
            nested_get(finish, ("expectations", "ok")) is True and
            nested_get(finish, ("expectations", "nonzero_checks", "v", "ok")) is True and
            nested_get(finish, ("expectations", "nonzero_checks", "c", "ok")) is True and
            nested_get(finish, ("expectations", "nonzero_checks", "p", "ok")) is True and
            nested_get(finish, ("state", "offline")) == 0 and
            not nested_get(finish, ("state", "errors"), []),
            lambda finish: {"passed": finish.get("passed"), "summary": finish.get("summary"), "expectations": finish.get("expectations"), "state": finish.get("state")},
        ),
        "rtc": check_json_finish(
            paths["rtc"],
            lambda finish: finish.get("passed") is True and
            all(nested_get(finish, ("result", "checks", key)) is True for key in ("rtc_read_before", "rtc_write", "rtc_read_after", "plan_nid_read", "plan_now_read", "offline")),
            lambda finish: finish.get("result"),
        ),
        "plan_exec": check_json_finish(
            paths["plan_exec"],
            lambda finish: finish.get("passed") is True and
            all(nested_get(finish, ("result", key)) is True for key in ("write_ok", "readback_ok", "execution_ok", "plan_now_ok", "cleanup_delete_ok", "cleanup_nid_ok", "restore_ack")) and
            nested_get(finish, ("result", "offline")) == 0,
            lambda finish: finish.get("result"),
        ),
        "current_image_ota": check_json_finish(
            paths["current_image_ota"],
            lambda finish: nested_get(finish, ("state", "published")) is True and
            nested_get(finish, ("state", "progress")) is True and
            nested_get(finish, ("state", "online_after_ota")) is True and
            nested_get(finish, ("state", "error")) is False and
            nested_get(finish, ("state", "offline")) == 0,
            lambda finish: finish.get("state"),
        ),
        "old_image_ota_regression": check_json_finish(
            paths["old_image_ota"],
            lambda finish: nested_get(finish, ("state", "response")) is True and
            nested_get(finish, ("state", "progress")) is True and
            nested_get(finish, ("state", "online")) is True and
            nested_get(finish, ("state", "error")) is False,
            lambda finish: finish.get("state"),
        ),
        "reset_pin_probe": check_reset_pin_probe(paths["reset_pin_probe"]),
    }
    failing = [name for name, item in logs.items() if item["status"] != "pass"]
    return {"status": "pass" if not failing else "fail", "failing": failing, "logs": logs}


def summarize_status(checks: dict[str, Any]) -> str:
    statuses = [value.get("status") for value in checks.values() if isinstance(value, dict)]
    if any(status == "fail" for status in statuses):
        return "fail"
    if any(status == "blocked" for status in statuses):
        return "blocked"
    return "pass"


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit Release-MinSize acceptance evidence from current build artifacts.")
    parser.add_argument("--project", type=Path, default=Path("MDK-ARM-8008000/project.uvprojx"))
    parser.add_argument("--target", default="Release-MinSize")
    parser.add_argument("--map", type=Path, default=Path("output/release_minsize_final_cat1.map"))
    parser.add_argument("--image", type=Path, default=Path("output/release_minsize_final_cat1.bin"))
    parser.add_argument("--guarded-ota-log", type=Path, default=Path("tools/ota_test/logs/mqtt_guarded_ota_20260710_142819.jsonl"))
    parser.add_argument("--device-type", default="0x0003")
    parser.add_argument("--max-size", default="0x1C000")
    parser.add_argument("--max-rom", default="0x16000", help="Release target ceiling for this round; default is 88 KB.")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()

    max_size = int(args.max_size, 0)
    max_rom = int(args.max_rom, 0)
    image_report, image_errors = inspect(args.image, int(args.device_type, 0), max_size)
    image_check = {
        "status": "pass" if not image_errors else "fail",
        "report": dict(image_report, errors=image_errors, sha256=sha256_file(args.image) if args.image.exists() else None),
    }

    checks: dict[str, Any] = {
        "release_defines": check_project_defines(args.project, args.target),
        "release_map": check_map(args.map, max_rom),
        "image_header": image_check,
        "guarded_current_ota": check_guarded_ota_log(args.guarded_ota_log),
        "required_docs": check_docs(Path(".")),
        "required_artifacts": {
            "status": "pass",
            "baseline_bin": check_artifact_file("output/baseline_cat1.bin", 93212),
            "baseline_hex": check_artifact_file("output/baseline_cat1.hex"),
            "baseline_map": check_artifact_file("output/baseline_cat1.map"),
            "baseline_axf": check_artifact_file("output/baseline_cat1.axf"),
            "release_bin": check_artifact_file("output/release_minsize_final_cat1.bin", 71648),
            "release_hex": check_artifact_file("output/release_minsize_final_cat1.hex"),
            "release_map_artifact": check_artifact_file("output/release_minsize_final_cat1.map"),
            "release_axf": check_artifact_file("output/release_minsize_final_cat1.axf"),
            "debug_bin": check_artifact_file("output/debug_final_cat1.bin", 93212),
            "debug_hex": check_artifact_file("output/debug_final_cat1.hex"),
            "debug_map": check_artifact_file("output/debug_final_cat1.map"),
            "debug_axf": check_artifact_file("output/debug_final_cat1.axf"),
            "ota_package": check_artifact_file("tools/ota_test/out/release_minsize_ota_20260710_025602.bin", 71648),
        },
        "build_logs": {
            "status": "pass",
            "baseline": check_build_log("output/baseline_rebuild.log", {
                "code": 87906,
                "ro_data": 5098,
                "rw_data": 1684,
                "zi_data": 33684,
                "rom_bytes": 93212,
                "checksum": "0x0092A57B",
            }),
            "release": check_build_log("output/release_minsize_final_build.log", {
                "code": 69154,
                "ro_data": 2286,
                "rw_data": 1612,
                "zi_data": 32724,
                "rom_bytes": 71648,
                "checksum": "0x0070A0EE",
            }),
            "debug": check_build_log("output/debug_final_build.log", {
                "code": 87906,
                "ro_data": 5098,
                "rw_data": 1684,
                "zi_data": 33684,
                "rom_bytes": 93212,
                "checksum": "0x0092A530",
            }),
        },
        "serial_logs": {
            "status": "pass",
            "baseline_boot": check_serial_log(
                "output/baseline_serial_log.txt",
                ("boot startup1", "from aprom", "checksum ok", "PLL from HSE"),
            ),
            "release_boot": check_serial_log(
                "output/release_minsize_resetpin_serial_20260710_024056.txt",
                ("from power on or reset", "checksum ok", "PLL from HSE"),
            ),
            "debug_uart": check_serial_log(
                "output/debug_final_resetpin_serial_20260710_025029.txt",
                ("from power on or reset", "checksum ok", "IMEI=864512081541939", "[ICCID] ok", "[MQTT] publish ack", "[RTC] startup force sync complete"),
            ),
        },
        "baseline_jlink_flash": check_jlink_flash_log("output/baseline_jlink_flash.log", 93212),
        "jlink_flash": check_jlink_flash_log("output/release_minsize_final_jlink_flash.log", 71648),
        "representative_live_logs": check_live_evidence({
            "release_monitor": "tools/ota_test/logs/mqtt_release_monitor_20260710_081125.jsonl",
            "control_query": "tools/ota_test/logs/mqtt_release_minsize_control_query_20260710_024341.jsonl",
            "control_dim": "tools/ota_test/logs/mqtt_release_minsize_control_dim_20260710_024557.jsonl",
            "control_switch": "tools/ota_test/logs/mqtt_release_minsize_control_switch_20260710_024853.jsonl",
            "param_persist": "tools/ota_test/logs/mqtt_release_minsize_param_persist_20260710_025449.jsonl",
            "ntc": "tools/ota_test/logs/mqtt_release_minsize_ntc_threshold_20260710_031702.jsonl",
            "bl0942_unloaded": "tools/ota_test/logs/mqtt_release_minsize_bl0942_telemetry_20260710_090815.jsonl",
            "bl0942_loaded": "tools/ota_test/logs/mqtt_release_minsize_bl0942_telemetry_20260710_143039.jsonl",
            "rtc": "tools/ota_test/logs/mqtt_release_minsize_rtc_plan_20260710_085552.jsonl",
            "plan_exec": "tools/ota_test/logs/mqtt_release_minsize_plan_exec_20260710_090547.jsonl",
            "current_image_ota": "tools/ota_test/logs/mqtt_guarded_ota_20260710_142819.jsonl",
            "old_image_ota": "tools/ota_test/logs/mqtt_ota_20260710_032918.jsonl",
            "reset_pin_probe": "tools/ota_test/logs/jlink_reset_20260710_080330.log",
        }),
    }
    for aggregate_key in ("required_artifacts", "build_logs", "serial_logs"):
        aggregate = checks[aggregate_key]
        child_statuses = [value.get("status") for value in aggregate.values() if isinstance(value, dict)]
        aggregate["status"] = "pass" if all(status == "pass" for status in child_statuses) else "fail"
    summary = summarize_status(checks)
    follow_up_items = [
        {
            "id": "native_type2_sunrise_sunset",
            "reason": "Release-MinSize disables device-side astronomy by design per the first-round size plan",
            "next_step": "keep using platform-converted ordinary concrete-time plans, or rebuild/test native type=2 support if product requirements change",
        },
        {
            "id": "post_ota_jlink_readback",
            "reason": "current live OTA passed over MQTT, but the later J-Link-only verify attempt could not connect to the probe",
            "next_step": "rerun tools/ota_test/jlink_verify.ps1 after the probe USB connection is restored if post-OTA byte-for-byte readback is required",
        },
    ]
    payload = {
        "ts": now_text(),
        "summary": summary,
        "checks": checks,
        "follow_up_items": follow_up_items,
    }

    args.log_dir.mkdir(parents=True, exist_ok=True)
    out_path = args.log_dir / f"release_minsize_acceptance_audit_{timestamp()}.json"
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(out_path)
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 1 if summary == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
