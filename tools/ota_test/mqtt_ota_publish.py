#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path


DEFAULT_URL = "http://47.120.15.220:3915/system/mediaInfo/download/1522561582713004032"


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def write_jsonl(handle, event: dict[str, object]) -> None:
    handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    handle.flush()


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Publish CAT.1 MQTT OTA command and log device responses.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--id", default=str(int(time.time()) % 1000000))
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--wait-progress", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    parser.add_argument("--pub-topic")
    parser.add_argument("--sub-topic")
    parser.add_argument("--online-topic")
    parser.add_argument("--offline-topic")
    args = parser.parse_args()

    pub_topic = args.pub_topic or f"MS/{args.imei}/pcp2dev"
    sub_topic = args.sub_topic or f"MS/{args.imei}/dev2pcp"
    online_topic = args.online_topic or f"MS/{args.imei}/dev2plt"
    offline_topic = args.offline_topic or f"MS/{args.imei}/offline"
    sub_topics = []
    for topic in (sub_topic, online_topic, offline_topic):
        if topic and topic not in sub_topics:
            sub_topics.append(topic)
    payload = {
        "SN": args.imei,
        "TM": now_text(),
        "SV": "ota",
        "ID": args.id,
        "CT": "W",
        "DT": {"url": args.url},
    }

    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_ota_{timestamp()}.jsonl"
    done = {"connected": False, "progress": False, "error": False, "response": False, "online": False}

    client = mqtt.Client(client_id=f"codex_ota_{args.imei}_{int(time.time())}", clean_session=True)
    if args.username:
        client.username_pw_set(args.username, args.password)

    with log_path.open("w", encoding="utf-8") as log:
        def on_connect(client, userdata, flags, rc):
            done["connected"] = rc == 0
            write_jsonl(log, {"ts": now_text(), "event": "connect", "rc": rc, "sub_topics": sub_topics})
            if rc == 0:
                for topic in sub_topics:
                    client.subscribe(topic, qos=1)

        def on_message(client, userdata, message):
            text = message.payload.decode("utf-8", errors="replace")
            event = {"ts": now_text(), "event": "message", "topic": message.topic, "payload": text}
            try:
                parsed = json.loads(text)
                event["json"] = parsed
                sv = str(parsed.get("SV", "")).lower()
                ct = str(parsed.get("CT", "")).upper()
                if sv == "ota":
                    if ct == "P":
                        done["progress"] = True
                    if ct == "E":
                        done["error"] = True
                    done["response"] = True
                if message.topic == online_topic and sv in ("rept", "prop", "login", "heartbeat", "devinfo"):
                    done["online"] = True
                if sv in ("rept", "prop") and ct in ("L", "H", "C", "B"):
                    done["online"] = True
            except json.JSONDecodeError:
                pass
            write_jsonl(log, event)

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "publish_plan", "pub_topic": pub_topic, "sub_topics": sub_topics, "payload": payload})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        deadline = time.time() + args.timeout
        while time.time() < deadline and not done["connected"]:
            time.sleep(0.1)
        if not done["connected"]:
            write_jsonl(log, {"ts": now_text(), "event": "connect_timeout"})
            client.loop_stop()
            client.disconnect()
            return 2

        result = client.publish(pub_topic, json.dumps(payload, ensure_ascii=False), qos=1)
        result.wait_for_publish(timeout=10)
        write_jsonl(log, {"ts": now_text(), "event": "published", "mid": result.mid, "rc": result.rc})

        while time.time() < deadline:
            if done["error"]:
                break
            if args.wait_progress and done["progress"] and done["online"]:
                break
            if not args.wait_progress and done["response"]:
                break
            time.sleep(0.2)

        write_jsonl(log, {"ts": now_text(), "event": "finish", "state": done, "log_path": str(log_path)})
        client.loop_stop()
        client.disconnect()

    print(log_path)
    return 1 if done["error"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
