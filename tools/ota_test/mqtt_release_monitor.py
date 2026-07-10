#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def write_jsonl(handle, event: dict[str, object]) -> None:
    handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    handle.flush()


def classify_message(topic: str, parsed: object, state: dict[str, object]) -> None:
    if topic.endswith("/offline"):
        state["offline_count"] = int(state["offline_count"]) + 1
        return
    if not isinstance(parsed, dict):
        return

    sv = str(parsed.get("SV", ""))
    ct = str(parsed.get("CT", ""))
    if sv == "rept" and ct == "L":
        state["login_count"] = int(state["login_count"]) + 1
        dt = parsed.get("DT", {})
        if isinstance(dt, dict):
            dev_info = dt.get("DevInfo", {})
            mdl_info = dt.get("MdlInfo", {})
            if isinstance(dev_info, dict):
                state["last_sver"] = dev_info.get("sver")
            if isinstance(mdl_info, dict):
                state["last_iccid"] = mdl_info.get("iccid")
    elif sv == "rept" and ct == "H":
        state["heartbeat_count"] = int(state["heartbeat_count"]) + 1
    elif sv == "rept" and ct in ("B", "C"):
        state["report_count"] = int(state["report_count"]) + 1
    elif sv == "rept" and ct == "A":
        state["alarm_count"] = int(state["alarm_count"]) + 1
    elif sv == "rqst":
        state["request_count"] = int(state["request_count"]) + 1


def run_reset_pin(reset_script: Path) -> tuple[int, str]:
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(reset_script),
    ]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output = result.stdout or ""
    return result.returncode, output[-3000:]


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Read-only CAT.1 MQTT startup/soak monitor.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--duration", type=int, default=240)
    parser.add_argument("--progress-interval", type=int, default=15)
    parser.add_argument("--reset-pin", action="store_true", help="Run tools/ota_test/jlink_reset.ps1 after MQTT subscribe.")
    parser.add_argument("--reset-script", type=Path, default=Path("tools/ota_test/jlink_reset.ps1"))
    parser.add_argument("--expect-login", action="store_true")
    parser.add_argument("--expect-heartbeat", action="store_true")
    parser.add_argument("--expect-report", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()

    topics = [
        f"MS/{args.imei}/dev2pcp",
        f"MS/{args.imei}/dev2plt",
        f"MS/{args.imei}/offline",
    ]
    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_release_monitor_{timestamp()}.jsonl"

    state: dict[str, object] = {
        "connected": False,
        "messages": 0,
        "login_count": 0,
        "heartbeat_count": 0,
        "report_count": 0,
        "alarm_count": 0,
        "request_count": 0,
        "offline_count": 0,
        "last_topic": "",
        "last_payload": "",
        "last_sver": None,
        "last_iccid": None,
        "first_message_ts": "",
        "last_message_ts": "",
    }

    client = mqtt.Client(client_id=f"codex_monitor_{args.imei}_{int(time.time())}", clean_session=True)
    if args.username:
        client.username_pw_set(args.username, args.password)

    with log_path.open("w", encoding="utf-8") as log:
        def on_connect(client, userdata, flags, rc):
            state["connected"] = rc == 0
            write_jsonl(log, {"ts": now_text(), "event": "connect", "rc": rc, "topics": topics})
            if rc == 0:
                for topic in topics:
                    client.subscribe(topic, qos=1)

        def on_message(client, userdata, message):
            text = message.payload.decode("utf-8", errors="replace")
            event: dict[str, object] = {"ts": now_text(), "event": "message", "topic": message.topic, "payload": text}
            state["messages"] = int(state["messages"]) + 1
            state["last_topic"] = message.topic
            state["last_payload"] = text
            if not state["first_message_ts"]:
                state["first_message_ts"] = event["ts"]
            state["last_message_ts"] = event["ts"]
            try:
                parsed = json.loads(text)
                event["json"] = parsed
                classify_message(message.topic, parsed, state)
            except json.JSONDecodeError:
                classify_message(message.topic, None, state)
            write_jsonl(log, event)

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "start", "topics": topics, "duration_sec": args.duration})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        try:
            deadline = time.time() + 20
            while time.time() < deadline and not bool(state["connected"]):
                time.sleep(0.1)
            if not bool(state["connected"]):
                write_jsonl(log, {"ts": now_text(), "event": "finish", "passed": False, "reason": "mqtt_connect_timeout", "state": state, "log_path": str(log_path)})
                print(log_path)
                return 2

            if args.reset_pin:
                rc, output_tail = run_reset_pin(args.reset_script)
                write_jsonl(log, {"ts": now_text(), "event": "reset_pin", "rc": rc, "script": str(args.reset_script), "output_tail": output_tail})
                if rc != 0:
                    write_jsonl(log, {"ts": now_text(), "event": "finish", "passed": False, "reason": "reset_pin_failed", "state": state, "log_path": str(log_path)})
                    print(log_path)
                    return 3

            start = time.time()
            last_emit = 0.0
            while time.time() - start < args.duration:
                if time.time() - last_emit >= max(1, args.progress_interval):
                    last_emit = time.time()
                    write_jsonl(log, {"ts": now_text(), "event": "progress", "elapsed_sec": int(time.time() - start), "state": state})
                time.sleep(0.2)

            checks = {
                "login": {"expected": args.expect_login, "ok": (not args.expect_login) or int(state["login_count"]) > 0},
                "heartbeat": {"expected": args.expect_heartbeat, "ok": (not args.expect_heartbeat) or int(state["heartbeat_count"]) > 0},
                "report": {"expected": args.expect_report, "ok": (not args.expect_report) or int(state["report_count"]) > 0},
                "offline": {"expected": True, "ok": int(state["offline_count"]) == 0},
            }
            passed = bool(state["connected"]) and all(bool(value["ok"]) for value in checks.values())
            write_jsonl(log, {"ts": now_text(), "event": "finish", "passed": passed, "checks": checks, "state": state, "log_path": str(log_path)})
            print(log_path)
            return 0 if passed else 1
        finally:
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
