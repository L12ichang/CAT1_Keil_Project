#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import time
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any

from inspect_ota_bin import inspect


DEFAULT_URL = "http://47.120.15.220:3915/system/mediaInfo/download/1524861027026722816"
DEFAULT_EXPECTED_IMAGE = Path("tools/ota_test/out/release_minsize_ota_20260710_025602.bin")


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def write_jsonl(handle, event: dict[str, object]) -> None:
    handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    handle.flush()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_url(url: str, dest: Path, timeout: int) -> dict[str, object]:
    request = urllib.request.Request(url, headers={"User-Agent": "codex-cat1-ota-guard/1.0"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = response.read()
        headers = dict(response.headers.items())
        status = getattr(response, "status", None)
    dest.write_bytes(data)
    return {"status": status, "headers": headers, "bytes": len(data)}


def make_ota_payload(imei: str, url: str, msg_id: str) -> dict[str, object]:
    return {
        "SN": imei,
        "TM": now_text(),
        "SV": "ota",
        "ID": msg_id,
        "CT": "W",
        "DT": {"url": url},
    }


def compare_images(expected: dict[str, Any], candidate: dict[str, Any], expected_sha: str, candidate_sha: str) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    for field in ("valid", "size", "checksum", "device_type", "calculated_checksum"):
        if expected.get(field) != candidate.get(field):
            reasons.append(f"{field}: expected {expected.get(field)} got {candidate.get(field)}")
    if expected_sha != candidate_sha:
        reasons.append(f"sha256: expected {expected_sha} got {candidate_sha}")
    return not reasons, reasons


def publish_and_monitor(args: argparse.Namespace, log, payload: dict[str, object]) -> tuple[int, dict[str, object]]:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    pub_topic = args.pub_topic or f"MS/{args.imei}/pcp2dev"
    sub_topic = args.sub_topic or f"MS/{args.imei}/dev2pcp"
    online_topic = args.online_topic or f"MS/{args.imei}/dev2plt"
    offline_topic = args.offline_topic or f"MS/{args.imei}/offline"
    sub_topics: list[str] = []
    for topic in (sub_topic, online_topic, offline_topic):
        if topic and topic not in sub_topics:
            sub_topics.append(topic)

    state: dict[str, object] = {
        "connected": False,
        "published": False,
        "progress": False,
        "error": False,
        "online_after_ota": False,
        "offline": 0,
        "messages": 0,
        "ota_messages": [],
        "last_payload": "",
    }

    client = mqtt.Client(client_id=f"codex_guarded_ota_{args.imei}_{int(time.time())}", clean_session=True)
    if args.username:
        client.username_pw_set(args.username, args.password)

    def on_connect(client, userdata, flags, rc):
        state["connected"] = rc == 0
        write_jsonl(log, {"ts": now_text(), "event": "mqtt_connect", "rc": rc, "sub_topics": sub_topics, "pub_topic": pub_topic})
        if rc == 0:
            for topic in sub_topics:
                client.subscribe(topic, qos=1)

    def on_message(client, userdata, message):
        text = message.payload.decode("utf-8", errors="replace")
        event: dict[str, object] = {"ts": now_text(), "event": "message", "topic": message.topic, "payload": text}
        state["messages"] = int(state["messages"]) + 1
        state["last_payload"] = text
        if message.topic == offline_topic:
            state["offline"] = int(state["offline"]) + 1
        try:
            parsed = json.loads(text)
            event["json"] = parsed
            sv = str(parsed.get("SV", "")).lower()
            ct = str(parsed.get("CT", "")).upper()
            if sv == "ota":
                ota_messages = state["ota_messages"]
                assert isinstance(ota_messages, list)
                ota_messages.append(parsed)
                if ct == "P":
                    state["progress"] = True
                if ct == "E":
                    state["error"] = True
            if sv in ("rept", "prop") and ct in ("L", "H", "C", "B"):
                state["online_after_ota"] = True
        except json.JSONDecodeError:
            pass
        write_jsonl(log, event)

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.host, args.port, keepalive=60)
    client.loop_start()
    try:
        deadline = time.time() + args.timeout
        while time.time() < deadline and not state["connected"]:
            time.sleep(0.1)
        if not state["connected"]:
            write_jsonl(log, {"ts": now_text(), "event": "mqtt_connect_timeout", "state": state})
            return 2, state

        result = client.publish(pub_topic, json.dumps(payload, ensure_ascii=False), qos=1)
        result.wait_for_publish(timeout=10)
        state["published"] = result.rc == 0
        write_jsonl(log, {"ts": now_text(), "event": "published", "mid": result.mid, "rc": result.rc, "topic": pub_topic, "payload": payload})

        while time.time() < deadline:
            if state["error"]:
                break
            if state["progress"] and state["online_after_ota"]:
                break
            time.sleep(0.5)
        write_jsonl(log, {"ts": now_text(), "event": "monitor_finish", "state": state})
        return 1 if state["error"] else 0, state
    finally:
        client.loop_stop()
        client.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Guard CAT.1 OTA live validation by verifying the URL image matches the current optimized package before publishing.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--expected-image", type=Path, default=DEFAULT_EXPECTED_IMAGE)
    parser.add_argument("--id", default=f"OTA{int(time.time()) % 1000000}")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--download-timeout", type=int, default=60)
    parser.add_argument("--device-type", default="0x0003")
    parser.add_argument("--max-size", default="0x1C000")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    parser.add_argument("--out-dir", type=Path, default=Path("tools/ota_test/out"))
    parser.add_argument("--preflight-only", action="store_true", help="Download and compare only; do not publish OTA even on match.")
    parser.add_argument("--pub-topic")
    parser.add_argument("--sub-topic")
    parser.add_argument("--online-topic")
    parser.add_argument("--offline-topic")
    args = parser.parse_args()

    args.log_dir.mkdir(parents=True, exist_ok=True)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    stamp = timestamp()
    log_path = args.log_dir / f"mqtt_guarded_ota_{stamp}.jsonl"
    candidate_path = args.out_dir / f"guarded_ota_download_{stamp}.bin"

    with log_path.open("w", encoding="utf-8") as log:
        write_jsonl(log, {
            "ts": now_text(),
            "event": "start",
            "url": args.url,
            "expected_image": str(args.expected_image),
            "candidate_path": str(candidate_path),
            "preflight_only": args.preflight_only,
        })
        try:
            download_meta = download_url(args.url, candidate_path, args.download_timeout)
        except Exception as exc:  # noqa: BLE001 - log external download failure plainly for test evidence.
            write_jsonl(log, {"ts": now_text(), "event": "download_error", "error": str(exc), "log_path": str(log_path)})
            print(log_path)
            return 2

        expected_report, expected_errors = inspect(args.expected_image, int(args.device_type, 0), int(args.max_size, 0))
        candidate_report, candidate_errors = inspect(candidate_path, int(args.device_type, 0), int(args.max_size, 0))
        expected_sha = sha256_file(args.expected_image)
        candidate_sha = sha256_file(candidate_path)
        match, mismatch_reasons = compare_images(expected_report, candidate_report, expected_sha, candidate_sha)
        preflight = {
            "download": download_meta,
            "expected": dict(expected_report, errors=expected_errors, sha256=expected_sha),
            "candidate": dict(candidate_report, errors=candidate_errors, sha256=candidate_sha),
            "match": match,
            "mismatch_reasons": mismatch_reasons,
        }
        write_jsonl(log, {"ts": now_text(), "event": "preflight", "preflight": preflight})

        if not match:
            write_jsonl(log, {"ts": now_text(), "event": "abort_mismatch_no_publish", "preflight": preflight, "log_path": str(log_path)})
            print(log_path)
            return 3
        if args.preflight_only:
            write_jsonl(log, {"ts": now_text(), "event": "preflight_match_no_publish", "preflight": preflight, "log_path": str(log_path)})
            print(log_path)
            return 0

        payload = make_ota_payload(args.imei, args.url, args.id)
        write_jsonl(log, {"ts": now_text(), "event": "preflight_match_publish_allowed", "payload": payload})
        rc, state = publish_and_monitor(args, log, payload)
        write_jsonl(log, {"ts": now_text(), "event": "finish", "rc": rc, "state": state, "log_path": str(log_path)})
        print(log_path)
        return rc


if __name__ == "__main__":
    raise SystemExit(main())
