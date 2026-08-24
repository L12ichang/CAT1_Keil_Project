#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def utc_text() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def write_jsonl(handle, event: dict[str, object]) -> None:
    handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    handle.flush()


def make_payload(imei: str, sv: str, ct: str, msg_id: str, dt: dict[str, object]) -> dict[str, object]:
    return {
        "SN": imei,
        "TM": utc_text(),
        "SV": sv,
        "ID": msg_id,
        "CT": ct,
        "DT": dt,
    }


def parse_rtc_text(value: object) -> datetime | None:
    if not isinstance(value, str):
        return None
    try:
        return datetime.strptime(value, "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None


def response_ok(response: dict[str, Any] | None) -> bool:
    return response is not None and int(response.get("ER", -1)) == 0


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Validate CAT.1 RTC property read/write and safe plan read probes over MQTT.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--rtc-tolerance-sec", type=int, default=120)
    parser.add_argument("--timezone-hours", type=int, default=8, help="Device local timezone offset used for DT.RTC when --set-time=now.")
    parser.add_argument("--set-time", default="now", help="RTC value to write, or 'now' for current local time.")
    parser.add_argument("--skip-rtc-write", action="store_true", help="Only read RTC; do not write a new time.")
    parser.add_argument("--probe-sr", action="store_true", help="Also probe plan DO=sr; CAT1_50W release may return an error when local sunrise math is disabled.")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()

    pub_topic = f"MS/{args.imei}/pcp2dev"
    topics = [
        f"MS/{args.imei}/dev2pcp",
        f"MS/{args.imei}/dev2plt",
        f"MS/{args.imei}/offline",
    ]

    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_cat1_50w_rtc_plan_{timestamp()}.jsonl"
    state: dict[str, object] = {
        "connected": False,
        "messages": 0,
        "responses": {},
        "offline": 0,
    }

    def get_response(msg_id: str) -> dict[str, Any] | None:
        responses = state["responses"]
        assert isinstance(responses, dict)
        value = responses.get(msg_id)
        return value if isinstance(value, dict) else None

    client = mqtt.Client(client_id=f"codex_rtc_plan_{args.imei}_{int(time.time())}", clean_session=True)
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
                msg_id = str(parsed.get("ID", ""))
                if msg_id:
                    responses = state["responses"]
                    assert isinstance(responses, dict)
                    responses[msg_id] = parsed
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

        def read_rtc(msg_id: str) -> dict[str, Any] | None:
            publish("rtc_read", make_payload(args.imei, "prop", "R", msg_id, {"props": ["RTC"]}))
            if not wait_for(f"{msg_id} response", lambda: get_response(msg_id) is not None, args.timeout):
                return None
            return get_response(msg_id)

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "start", "pub_topic": pub_topic, "topics": topics})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        try:
            if not wait_for("mqtt connect", lambda: bool(state["connected"]), 20):
                print(log_path)
                return 2

            before = read_rtc("RTC1001")
            before_dt = before.get("DT", {}) if isinstance(before, dict) and isinstance(before.get("DT", {}), dict) else {}
            before_text = before_dt.get("RTC")
            before_time = parse_rtc_text(before_text)

            write_ack = None
            set_time_text = None
            set_time = None
            if not args.skip_rtc_write:
                if args.set_time == "now":
                    set_time = datetime.now(timezone.utc).replace(tzinfo=None) + timedelta(hours=args.timezone_hours)
                    set_time_text = set_time.strftime("%Y-%m-%d %H:%M:%S")
                else:
                    set_time = parse_rtc_text(args.set_time)
                    if set_time is None:
                        write_jsonl(log, {"ts": now_text(), "event": "finish", "passed": False, "reason": "invalid_set_time", "set_time": args.set_time, "log_path": str(log_path)})
                        print(log_path)
                        return 3
                    set_time_text = args.set_time

                publish("rtc_write", make_payload(args.imei, "prop", "W", "RTC1002", {"RTC": set_time_text}))
                if not wait_for("RTC1002 response", lambda: get_response("RTC1002") is not None, args.timeout):
                    print(log_path)
                    return 4
                write_ack = get_response("RTC1002")
                time.sleep(2)

            after = read_rtc("RTC1003")
            after_dt = after.get("DT", {}) if isinstance(after, dict) and isinstance(after.get("DT", {}), dict) else {}
            after_text = after_dt.get("RTC")
            after_time = parse_rtc_text(after_text)

            plan_nid_id = "PLN1001"
            publish("plan_nid_read", make_payload(args.imei, "plan", "R", plan_nid_id, {"DO": "nid"}))
            if not wait_for("plan nid response", lambda: get_response(plan_nid_id) is not None, args.timeout):
                print(log_path)
                return 5
            plan_nid = get_response(plan_nid_id)

            plan_now_id = "PLN1002"
            publish("plan_now_read", make_payload(args.imei, "plan", "R", plan_now_id, {"now": 1}))
            if not wait_for("plan now response", lambda: get_response(plan_now_id) is not None, args.timeout):
                print(log_path)
                return 6
            plan_now = get_response(plan_now_id)

            plan_sr = None
            if args.probe_sr:
                plan_sr_id = "PLN1003"
                publish("plan_sr_read", make_payload(args.imei, "plan", "R", plan_sr_id, {"DO": "sr"}))
                if not wait_for("plan sr response", lambda: get_response(plan_sr_id) is not None, args.timeout):
                    print(log_path)
                    return 7
                plan_sr = get_response(plan_sr_id)

            rtc_after_ok = response_ok(after) and after_time is not None
            rtc_write_ok = True
            rtc_delta_sec = None
            if not args.skip_rtc_write:
                rtc_write_ok = response_ok(write_ack)
                if after_time is not None and set_time is not None:
                    rtc_delta_sec = abs((after_time - set_time).total_seconds())
                    rtc_after_ok = rtc_after_ok and rtc_delta_sec <= args.rtc_tolerance_sec

            checks = {
                "rtc_read_before": response_ok(before) and before_time is not None,
                "rtc_write": rtc_write_ok,
                "rtc_read_after": rtc_after_ok,
                "plan_nid_read": response_ok(plan_nid),
                "plan_now_read": response_ok(plan_now),
                "offline": int(state["offline"]) == 0,
            }
            passed = all(checks.values())
            result = {
                "checks": checks,
                "before_rtc": before_text,
                "after_rtc": after_text,
                "set_time": set_time_text,
                "rtc_delta_sec": rtc_delta_sec,
                "plan_nid": plan_nid,
                "plan_now": plan_now,
                "plan_sr": plan_sr,
                "offline": state["offline"],
            }
            write_jsonl(log, {"ts": now_text(), "event": "finish", "passed": passed, "result": result, "state": state, "log_path": str(log_path)})
            print(log_path)
            return 0 if passed else 1
        finally:
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
