#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.check_keil_app_image import APP_BASE, APP_SAFE_END, DEFAULT_PROJECT, run_checks
from tools.mcu_workflow import flash_firmware


DEFAULT_BIN = ROOT / "MDK-ARM-8008000" / "out" / "cat1.bin"


def build_config(args: argparse.Namespace) -> dict:
    return {
        "build": {
            "flash_base": f"0x{APP_BASE:08X}",
            "default_firmware": str(DEFAULT_BIN.relative_to(ROOT)),
        },
        "jlink": {
            "device": args.jlink_device,
            "interface": args.interface,
            "speed_khz": args.speed,
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate, burn, verify, and release the Keil app image at 0x08008000."
    )
    parser.add_argument("--project", default=str(DEFAULT_PROJECT), help="Keil project.uvprojx path")
    parser.add_argument("--bin", default=str(DEFAULT_BIN), help="BIN file to load at 0x08008000")
    parser.add_argument("--jlink-device", default="STM32F103RC", help="J-Link target device")
    parser.add_argument("--interface", default="SWD", help="J-Link interface")
    parser.add_argument("--speed", type=int, default=4000, help="J-Link speed in kHz")
    parser.add_argument("--skip-freshness", action="store_true", help="Allow stale Keil out files")
    parser.add_argument("--dry-run", action="store_true", help="Validate only; do not write target flash")
    parser.add_argument("--json", action="store_true", help="Print machine-readable result")
    args = parser.parse_args(argv)

    report = run_checks(
        project_path=Path(args.project),
        app_base=APP_BASE,
        safe_end=APP_SAFE_END,
        require_fresh=not args.skip_freshness,
    )
    if not report.passed:
        if args.json:
            print(json.dumps({"passed": False, "errors": report.errors, "details": report.details}, indent=2))
        else:
            for error in report.errors:
                print(f"ERROR: {error}")
        return 1

    firmware = Path(args.bin).resolve()
    if not firmware.exists():
        print(f"ERROR: BIN does not exist: {firmware}")
        return 1

    result = flash_firmware(build_config(args), firmware, dry_run=args.dry_run)
    if args.json:
        print(json.dumps({"passed": True, "image": report.details, "flash": result}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
