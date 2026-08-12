#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Any


ELEINFO_FIELDS = ("e", "c", "v", "f", "p", "rEc", "tEc", "oc", "ov", "op", "pwr", "lc")


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def write_jsonl(handle, event: dict[str, object]) -> None:
    handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    handle.flush()


def make_payload(imei: str, msg_id: str, dt: dict[str, object]) -> dict[str, object]:
    return {
        "SN": imei,
        "TM": now_text(),
        "SV": "ctrl",
        "ID": msg_id,
        "CT": "W",
        "DT": dt,
    }


def first_number(value: Any) -> float | None:
    if isinstance(value, list):
        if not value:
            return None
        value = value[0]
    if isinstance(value, (int, float)):
        return float(value)
    return None


def eleinfo_valid(ele_info: dict[str, Any]) -> tuple[bool, list[str]]:
    missing: list[str] = []
    invalid: list[str] = []
    for field in ELEINFO_FIELDS:
        if field not in ele_info:
            missing.append(field)
        elif first_number(ele_info[field]) is None:
            invalid.append(field)
    errors = []
    if missing:
        errors.append("missing=" + ",".join(missing))
    if invalid:
        errors.append("invalid=" + ",".join(invalid))
    return not errors, errors


def summarize(samples: list[dict[str, Any]]) -> dict[str, object]:
    summary: dict[str, object] = {"sample_count": len(samples), "fields": {}}
    fields: dict[str, object] = {}
    for field in ELEINFO_FIELDS:
        values = [first_number(sample.get(field)) for sample in samples]
        numeric = [value for value in values if value is not None]
        fields[field] = {
            "count": len(numeric),
            "min": min(numeric) if numeric else None,
            "max": max(numeric) if numeric else None,
            "last": numeric[-1] if numeric else None,
        }
    summary["fields"] = fields
    return summary


def parse_expect_ranges(values: list[str]) -> dict[str, tuple[float | None, float | None]]:
    ranges: dict[str, tuple[float | None, float | None]] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"invalid --expect-range {value!r}, expected FIELD=MIN:MAX")
        field, bounds = value.split("=", 1)
        field = field.strip()
        if field not in ELEINFO_FIELDS:
            raise ValueError(f"invalid EleInfo field {field!r}; valid fields: {','.join(ELEINFO_FIELDS)}")
        if ":" not in bounds:
            raise ValueError(f"invalid --expect-range {value!r}, expected FIELD=MIN:MAX")
        min_text, max_text = bounds.split(":", 1)
        min_value = float(min_text) if min_text.strip() else None
        max_value = float(max_text) if max_text.strip() else None
        if min_value is None and max_value is None:
            raise ValueError(f"invalid --expect-range {value!r}, at least one bound is required")
        if min_value is not None and max_value is not None and min_value > max_value:
            raise ValueError(f"invalid --expect-range {value!r}, MIN is greater than MAX")
        ranges[field] = (min_value, max_value)
    return ranges


def validate_expectations(
    samples: list[dict[str, Any]],
    expect_ranges: dict[str, tuple[float | None, float | None]],
    require_nonzero: list[str],
) -> dict[str, object]:
    range_checks: dict[str, object] = {}
    for field, (min_value, max_value) in expect_ranges.items():
        values = [first_number(sample.get(field)) for sample in samples]
        numeric = [value for value in values if value is not None]
        failures: list[float] = []
        for value in numeric:
            if min_value is not None and value < min_value:
                failures.append(value)
            elif max_value is not None and value > max_value:
                failures.append(value)
        range_checks[field] = {
            "expected_min": min_value,
            "expected_max": max_value,
            "count": len(numeric),
            "min": min(numeric) if numeric else None,
            "max": max(numeric) if numeric else None,
            "ok": bool(numeric) and not failures,
            "failures": failures,
        }

    nonzero_checks: dict[str, object] = {}
    for field in require_nonzero:
        values = [first_number(sample.get(field)) for sample in samples]
        numeric = [value for value in values if value is not None]
        nonzero_checks[field] = {
            "count": len(numeric),
            "min_abs": min([abs(value) for value in numeric]) if numeric else None,
            "ok": bool(numeric) and all(value != 0 for value in numeric),
        }

    return {
        "range_checks": range_checks,
        "nonzero_checks": nonzero_checks,
        "ok": all(bool(check["ok"]) for check in range_checks.values()) and
              all(bool(check["ok"]) for check in nonzero_checks.values()),
    }


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Collect repeated CAT.1 BL0942 EleInfo telemetry via MQTT patrol reports.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--samples", type=int, default=6)
    parser.add_argument("--interval", type=int, default=12)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument(
        "--expect-range",
        action="append",
        default=[],
        metavar="FIELD=MIN:MAX",
        help="Require each numeric sample for an EleInfo field to be inside an inclusive range. Either bound may be omitted, e.g. v=200:260 or p=1:.",
    )
    parser.add_argument(
        "--require-nonzero",
        action="append",
        choices=ELEINFO_FIELDS,
        default=[],
        help="Require every collected numeric sample for this EleInfo field to be nonzero. Repeat for multiple fields.",
    )
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()
    try:
        expect_ranges = parse_expect_ranges(args.expect_range)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    pub_topic = f"MS/{args.imei}/pcp2dev"
    topics = [
        f"MS/{args.imei}/dev2pcp",
        f"MS/{args.imei}/dev2plt",
        f"MS/{args.imei}/offline",
    ]

    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_release_minsize_bl0942_telemetry_{timestamp()}.jsonl"
    state: dict[str, object] = {
        "connected": False,
        "messages": 0,
        "offline": 0,
        "acks": {},
        "samples": [],
        "errors": [],
    }

    client = mqtt.Client(client_id=f"codex_bl0942_{args.imei}_{int(time.time())}", clean_session=True)
    if args.username:
        client.username_pw_set(args.username, args.password)

    with log_path.open("w", encoding="utf-8") as log:
        def on_connect(client, userdata, flags, rc):
            state["connected"] = rc == 0
            write_jsonl(log, {"ts": now_text(), "event": "connect", "rc": rc, "topics": topics, "pub_topic": pub_topic})
            if rc == 0:
                for topic in topics:
                    client.subscribe(topic, qos=1)

        def on_message(client, userdata, message):
            text = message.payload.decode("utf-8", errors="replace")
            event: dict[str, object] = {"ts": now_text(), "event": "message", "topic": message.topic, "payload": text}
            state["messages"] = int(state["messages"]) + 1
            if message.topic.endswith("/offline"):
                state["offline"] = int(state["offline"]) + 1
            try:
                parsed = json.loads(text)
                event["json"] = parsed
                if parsed.get("SV") == "ctrl" and parsed.get("CT") == "W":
                    acks = state["acks"]
                    assert isinstance(acks, dict)
                    acks[str(parsed.get("ID", ""))] = int(parsed.get("ER", -1))
                if parsed.get("SV") == "rept" and parsed.get("CT") == "C":
                    dt = parsed.get("DT", {})
                    if isinstance(dt, dict):
                        ele_info = dt.get("EleInfo", {})
                        if isinstance(ele_info, dict):
                            ok, errors = eleinfo_valid(ele_info)
                            if ok:
                                samples = state["samples"]
                                assert isinstance(samples, list)
                                samples.append(ele_info)
                            else:
                                err_list = state["errors"]
                                assert isinstance(err_list, list)
                                err_list.append({"id": parsed.get("ID"), "errors": errors, "eleInfo": ele_info})
            except json.JSONDecodeError:
                pass
            write_jsonl(log, event)

        def wait_for(desc: str, predicate, seconds: int) -> bool:
            deadline = time.time() + seconds
            last_emit = 0.0
            while time.time() < deadline:
                if predicate():
                    write_jsonl(log, {"ts": now_text(), "event": "wait_ok", "desc": desc})
                    return True
                if time.time() - last_emit > 10:
                    last_emit = time.time()
                    write_jsonl(log, {"ts": now_text(), "event": "wait_progress", "desc": desc, "state": state})
                time.sleep(0.2)
            write_jsonl(log, {"ts": now_text(), "event": "wait_timeout", "desc": desc, "state": state})
            return False

        def publish(name: str, payload: dict[str, object]) -> None:
            result = client.publish(pub_topic, json.dumps(payload, ensure_ascii=False), qos=1)
            result.wait_for_publish(timeout=10)
            write_jsonl(log, {"ts": now_text(), "event": "published", "name": name, "mid": result.mid, "rc": result.rc, "topic": pub_topic, "payload": payload})

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "start", "pub_topic": pub_topic, "topics": topics, "samples_requested": args.samples})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        try:
            if not wait_for("mqtt connect", lambda: bool(state["connected"]), 20):
                print(log_path)
                return 2

            for index in range(args.samples):
                before = len(state["samples"]) if isinstance(state["samples"], list) else 0
                msg_id = f"BL{1000 + index}"
                publish("patrol", make_payload(args.imei, msg_id, {"DO": "patrol"}))
                wait_for(f"patrol {index + 1} EleInfo", lambda before=before: isinstance(state["samples"], list) and len(state["samples"]) > before, args.timeout)
                if index + 1 < args.samples:
                    time.sleep(max(1, args.interval))

            samples = state["samples"]
            assert isinstance(samples, list)
            summary = summarize(samples)
            expectations = validate_expectations(samples, expect_ranges, args.require_nonzero)
            passed = (
                len(samples) >= args.samples and
                int(state["offline"]) == 0 and
                not state["errors"] and
                bool(expectations["ok"])
            )
            write_jsonl(log, {
                "ts": now_text(),
                "event": "finish",
                "passed": passed,
                "summary": summary,
                "expectations": expectations,
                "state": state,
                "log_path": str(log_path),
            })
            print(log_path)
            return 0 if passed else 1
        finally:
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
