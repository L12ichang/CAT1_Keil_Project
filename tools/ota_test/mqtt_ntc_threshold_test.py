#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
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


def make_payload(imei: str, sv: str, ct: str, msg_id: str, dt: dict[str, object]) -> dict[str, object]:
    return {
        "SN": imei,
        "TM": now_text(),
        "SV": sv,
        "ID": msg_id,
        "CT": ct,
        "DT": dt,
    }


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Exercise NTC over-temperature protection by temporarily lowering INNRE_TEMP_PRO.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--wait-after-write", type=int, default=25)
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()

    pub_topic = f"MS/{args.imei}/pcp2dev"
    topics = [
        f"MS/{args.imei}/dev2pcp",
        f"MS/{args.imei}/dev2plt",
        f"MS/{args.imei}/offline",
    ]

    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_release_minsize_ntc_threshold_{timestamp()}.jsonl"
    state: dict[str, object] = {
        "connected": False,
        "messages": 0,
        "responses": {},
        "alarms": [],
        "offline": 0,
    }

    def get_response(msg_id: str) -> dict[str, object] | None:
        responses = state["responses"]
        assert isinstance(responses, dict)
        value = responses.get(msg_id)
        return value if isinstance(value, dict) else None

    def response_ok(msg_id: str) -> bool:
        response = get_response(msg_id)
        return response is not None and int(response.get("ER", -1)) == 0

    client = mqtt.Client(client_id=f"codex_ntc_{args.imei}_{int(time.time())}", clean_session=True)
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
                if str(parsed.get("SV", "")).lower() == "rept" and str(parsed.get("CT", "")).upper() == "A":
                    alarms = state["alarms"]
                    assert isinstance(alarms, list)
                    alarms.append(parsed)
            except json.JSONDecodeError:
                pass
            write_jsonl(log, event)

        def publish(name: str, payload: dict[str, object]) -> None:
            result = client.publish(pub_topic, json.dumps(payload, ensure_ascii=False), qos=1)
            result.wait_for_publish(timeout=10)
            write_jsonl(log, {"ts": now_text(), "event": "published", "name": name, "mid": result.mid, "rc": result.rc, "topic": pub_topic, "payload": payload})

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

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "start", "pub_topic": pub_topic, "topics": topics})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        try:
            if not wait_for("mqtt connect", lambda: bool(state["connected"]), 20):
                print(log_path)
                return 2

            read_before_id = "NTC1001"
            publish("read_before", make_payload(args.imei, "prop", "R", read_before_id, {"props": ["Factory", "RunSts", "PerSts"]}))
            if not wait_for("read before response", lambda: get_response(read_before_id) is not None, args.timeout):
                print(log_path)
                return 3
            before = get_response(read_before_id) or {}
            before_dt = before.get("DT", {}) if isinstance(before.get("DT", {}), dict) else {}
            factory = before_dt.get("Factory", {}) if isinstance(before_dt.get("Factory", {}), dict) else {}
            per_sts = before_dt.get("PerSts", {}) if isinstance(before_dt.get("PerSts", {}), dict) else {}
            original_en = int(factory.get("INNRE_TEMP_PRO_EN", 1))
            original_threshold = int(factory.get("INNRE_TEMP_PRO", 85))
            temp_raw = int(per_sts.get("temp", 0))
            current_temp_c = (temp_raw + 5) // 10 if temp_raw > 0 else 0
            if current_temp_c <= 1:
                write_jsonl(log, {"ts": now_text(), "event": "abort", "reason": "invalid_temp", "per_sts": per_sts})
                print(log_path)
                return 4
            test_threshold = max(1, min(127, current_temp_c - 1))
            if test_threshold == original_threshold and current_temp_c < 127:
                test_threshold = current_temp_c
            write_jsonl(log, {
                "ts": now_text(),
                "event": "selected_threshold",
                "temp_raw": temp_raw,
                "current_temp_c": current_temp_c,
                "original_en": original_en,
                "original_threshold": original_threshold,
                "test_threshold": test_threshold,
            })

            write_test_id = "NTC1002"
            publish("write_low_threshold", make_payload(args.imei, "prop", "W", write_test_id, {"Factory": {"INNRE_TEMP_PRO_EN": 1, "INNRE_TEMP_PRO": test_threshold}}))
            if not wait_for("write low threshold ack", lambda: response_ok(write_test_id), args.timeout):
                print(log_path)
                return 5

            time.sleep(max(1, args.wait_after_write))
            read_active_id = "NTC1003"
            publish("read_active", make_payload(args.imei, "prop", "R", read_active_id, {"props": ["Factory", "RunSts", "PerSts"]}))
            if not wait_for("read active response", lambda: get_response(read_active_id) is not None, args.timeout):
                print(log_path)
                return 6
            active = get_response(read_active_id) or {}
            active_dt = active.get("DT", {}) if isinstance(active.get("DT", {}), dict) else {}
            run_sts = active_dt.get("RunSts", {}) if isinstance(active_dt.get("RunSts", {}), dict) else {}
            sts = run_sts.get("sts", []) if isinstance(run_sts.get("sts", []), list) else []
            active_pass = 51 in [int(x) for x in sts if isinstance(x, (int, float))]
            write_jsonl(log, {"ts": now_text(), "event": "active_check", "passed": active_pass, "run_sts": run_sts, "active_dt": active_dt, "alarms": state["alarms"]})

            restore_id = "NTC1004"
            publish("restore_threshold", make_payload(args.imei, "prop", "W", restore_id, {"Factory": {"INNRE_TEMP_PRO_EN": original_en, "INNRE_TEMP_PRO": original_threshold}}))
            restore_ack = wait_for("restore threshold ack", lambda: response_ok(restore_id), args.timeout)
            time.sleep(3)
            restore_read = False
            restored: dict[str, object] = {}
            for attempt in range(3):
                read_restore_id = f"NTC100{5 + attempt}"
                publish("read_restored", make_payload(args.imei, "prop", "R", read_restore_id, {"props": ["Factory", "RunSts", "PerSts"]}))
                if wait_for("read restored response", lambda msg_id=read_restore_id: response_ok(msg_id), args.timeout):
                    restored = get_response(read_restore_id) or {}
                    restore_read = True
                    break
                write_jsonl(log, {"ts": now_text(), "event": "read_restored_retry", "attempt": attempt + 1, "response": get_response(read_restore_id)})
                time.sleep(2)
            restored_dt = restored.get("DT", {}) if isinstance(restored.get("DT", {}), dict) else {}
            restored_factory = restored_dt.get("Factory", {}) if isinstance(restored_dt.get("Factory", {}), dict) else {}
            restore_pass = (
                restore_ack and
                restore_read and
                int(restored_factory.get("INNRE_TEMP_PRO_EN", -1)) == original_en and
                int(restored_factory.get("INNRE_TEMP_PRO", -1)) == original_threshold
            )
            result = {
                "active_pass": active_pass,
                "restore_pass": restore_pass,
                "original_en": original_en,
                "original_threshold": original_threshold,
                "test_threshold": test_threshold,
                "temp_raw": temp_raw,
                "offline": state["offline"],
                "log_path": str(log_path),
            }
            write_jsonl(log, {"ts": now_text(), "event": "finish", "result": result, "state": state})
            print(log_path)
            return 0 if active_pass and restore_pass else 1
        finally:
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
