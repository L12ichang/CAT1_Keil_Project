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


def first_int(value: Any) -> int | None:
    if isinstance(value, list):
        if not value:
            return None
        value = value[0]
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    return None


def extract_run_bri(message: dict[str, Any] | None) -> int | None:
    if not isinstance(message, dict):
        return None
    dt = message.get("DT", {})
    if not isinstance(dt, dict):
        return None
    run_sts = dt.get("RunSts", {})
    if not isinstance(run_sts, dict):
        return None
    return first_int(run_sts.get("bri"))


def build_plan(plan_id: int, schedule_time: datetime, target_bri: int) -> dict[str, object]:
    start = schedule_time.replace(hour=0, minute=0, second=0)
    end = schedule_time.replace(hour=23, minute=59, second=59)
    return {
        "id": plan_id,
        "en": 1,
        "type": 1,
        "priority": 8,
        "sDate": start.strftime("%Y-%m-%d %H:%M:%S"),
        "eDate": end.strftime("%Y-%m-%d %H:%M:%S"),
        "week": "1111111",
        "jobs": [
            {
                "cns": [1],
                "timetp": 0,
                "time": [schedule_time.strftime("%H:%M")],
                "bri": [target_bri],
            }
        ],
    }


def plan_matches(response: dict[str, Any] | None, plan_id: int, target_bri: int, schedule_time: datetime) -> bool:
    if not response_ok(response):
        return False
    dt = response.get("DT", {}) if isinstance(response, dict) else {}
    plan = dt.get("plan", {}) if isinstance(dt, dict) else {}
    if not isinstance(plan, dict):
        return False
    jobs = plan.get("jobs", [])
    if not isinstance(jobs, list) or not jobs:
        return False
    job = jobs[0]
    if not isinstance(job, dict):
        return False
    time_values = job.get("time", [])
    bri_values = job.get("bri", [])
    return (
        int(plan.get("id", -1)) == plan_id and
        int(plan.get("en", -1)) == 1 and
        int(plan.get("type", -1)) == 1 and
        isinstance(time_values, list) and
        schedule_time.strftime("%H:%M") in time_values and
        isinstance(bri_values, list) and
        target_bri in [first_int(value) for value in bri_values]
    )


def pick_target_bri(initial_bri: int | None) -> int:
    if initial_bri is None:
        return 70
    if initial_bri >= 80:
        return 70
    if initial_bri <= 20:
        return 80
    return 100


def main() -> int:
    try:
        import paho.mqtt.client as mqtt
    except ImportError as exc:
        raise SystemExit("paho-mqtt is required: python -m pip install paho-mqtt") from exc

    parser = argparse.ArgumentParser(description="Write a temporary CAT.1 plan, prove timed brightness execution, then clean it up.")
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--execution-timeout", type=int, default=210)
    parser.add_argument("--delay-minutes", type=int, default=2, help="Schedule the temporary plan at least this many RTC minutes ahead.")
    parser.add_argument("--preferred-plan-id", type=int, default=8)
    parser.add_argument("--target-bri", type=int)
    parser.add_argument("--skip-restore-brightness", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/ota_test/logs"))
    args = parser.parse_args()

    if args.delay_minutes < 1:
        raise SystemExit("--delay-minutes must be >= 1")
    if args.preferred_plan_id < 1 or args.preferred_plan_id > 8:
        raise SystemExit("--preferred-plan-id must be in 1..8")
    if args.target_bri is not None and not (args.target_bri == 0 or 10 <= args.target_bri <= 100):
        raise SystemExit("--target-bri must be 0 or 10..100")

    pub_topic = f"MS/{args.imei}/pcp2dev"
    topics = [
        f"MS/{args.imei}/dev2pcp",
        f"MS/{args.imei}/dev2plt",
        f"MS/{args.imei}/offline",
    ]

    args.log_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.log_dir / f"mqtt_release_minsize_plan_exec_{timestamp()}.jsonl"
    state: dict[str, object] = {
        "connected": False,
        "messages": 0,
        "responses": {},
        "reports": [],
        "offline": 0,
    }

    def get_response(msg_id: str) -> dict[str, Any] | None:
        responses = state["responses"]
        assert isinstance(responses, dict)
        value = responses.get(msg_id)
        return value if isinstance(value, dict) else None

    def any_report_bri(value: int, after_wall_ts: float) -> bool:
        reports = state["reports"]
        assert isinstance(reports, list)
        for report in reports:
            if not isinstance(report, dict):
                continue
            if float(report.get("wall_ts", 0.0)) < after_wall_ts:
                continue
            if first_int(report.get("bri")) == value:
                return True
        return False

    client = mqtt.Client(client_id=f"codex_plan_exec_{args.imei}_{int(time.time())}", clean_session=True)
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
                if parsed.get("SV") == "rept" and parsed.get("CT") in ("B", "C"):
                    bri = extract_run_bri(parsed)
                    if bri is not None:
                        reports = state["reports"]
                        assert isinstance(reports, list)
                        reports.append({
                            "wall_ts": time.time(),
                            "id": parsed.get("ID"),
                            "ct": parsed.get("CT"),
                            "tm": parsed.get("TM"),
                            "bri": bri,
                            "message": parsed,
                        })
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

        def request(name: str, payload: dict[str, object], seconds: int | None = None) -> dict[str, Any] | None:
            msg_id = str(payload["ID"])
            publish(name, payload)
            wait_for(f"{msg_id} response", lambda: get_response(msg_id) is not None, seconds or args.timeout)
            return get_response(msg_id)

        client.on_connect = on_connect
        client.on_message = on_message
        write_jsonl(log, {"ts": now_text(), "event": "start", "pub_topic": pub_topic, "topics": topics})
        client.connect(args.host, args.port, keepalive=60)
        client.loop_start()

        cleanup_needed = False
        selected_plan_id = None
        initial_bri = None
        restore_ack = False
        cleanup_delete_ok = False
        cleanup_nid_ok = False
        execution_ok = False
        write_ok = False
        readback_ok = False
        plan_now_ok = False

        try:
            if not wait_for("mqtt connect", lambda: bool(state["connected"]), 20):
                print(log_path)
                return 2

            rtc_response = request("rtc_read", make_payload(args.imei, "prop", "R", "PLE1001", {"props": ["RTC", "RunSts"]}))
            if not response_ok(rtc_response):
                print(log_path)
                return 3
            rtc_dt = rtc_response.get("DT", {}) if isinstance(rtc_response, dict) and isinstance(rtc_response.get("DT", {}), dict) else {}
            device_time = parse_rtc_text(rtc_dt.get("RTC"))
            if device_time is None:
                write_jsonl(log, {"ts": now_text(), "event": "abort", "reason": "missing_rtc", "response": rtc_response})
                print(log_path)
                return 4
            initial_bri = extract_run_bri(rtc_response)
            target_bri = args.target_bri if args.target_bri is not None else pick_target_bri(initial_bri)
            if initial_bri is not None and target_bri == initial_bri:
                target_bri = pick_target_bri(initial_bri)

            nid_response = request("plan_nid_before", make_payload(args.imei, "plan", "R", "PLE1002", {"DO": "nid"}))
            if not response_ok(nid_response):
                print(log_path)
                return 5
            nid_dt = nid_response.get("DT", {}) if isinstance(nid_response, dict) and isinstance(nid_response.get("DT", {}), dict) else {}
            active_ids = nid_dt.get("nid", [])
            if not isinstance(active_ids, list):
                print(log_path)
                return 6
            active_id_set = {first_int(value) for value in active_ids}

            candidate_order = [args.preferred_plan_id] + [idx for idx in range(8, 0, -1) if idx != args.preferred_plan_id]
            for candidate in candidate_order:
                if candidate in active_id_set:
                    continue
                probe_id = f"PLEID{candidate}"
                response = request(f"probe_plan_id_{candidate}", make_payload(args.imei, "plan", "R", probe_id, {"id": candidate}))
                if response is not None and int(response.get("ER", -1)) != 0:
                    selected_plan_id = candidate
                    break
            if selected_plan_id is None:
                write_jsonl(log, {"ts": now_text(), "event": "abort", "reason": "no_unused_plan_slot", "active_ids": active_ids})
                print(log_path)
                return 7

            schedule_time = (device_time + timedelta(minutes=args.delay_minutes)).replace(second=0)
            plan = build_plan(selected_plan_id, schedule_time, target_bri)
            write_jsonl(log, {
                "ts": now_text(),
                "event": "selected_plan",
                "initial_bri": initial_bri,
                "target_bri": target_bri,
                "device_time": device_time.strftime("%Y-%m-%d %H:%M:%S"),
                "schedule_time": schedule_time.strftime("%Y-%m-%d %H:%M:%S"),
                "plan": plan,
            })

            write_response = request("plan_write", make_payload(args.imei, "plan", "W", "PLE1003", {"plan": plan}))
            write_ok = response_ok(write_response)
            cleanup_needed = write_ok
            if not write_ok:
                print(log_path)
                return 8

            readback_response = request("plan_readback", make_payload(args.imei, "plan", "R", "PLE1004", {"id": selected_plan_id}))
            readback_ok = plan_matches(readback_response, selected_plan_id, target_bri, schedule_time)
            if not readback_ok:
                print(log_path)
                return 9

            watch_start = time.time()
            next_patrol = 0.0
            deadline = time.time() + args.execution_timeout
            while time.time() < deadline:
                if any_report_bri(target_bri, watch_start):
                    execution_ok = True
                    break
                if time.time() >= next_patrol:
                    next_patrol = time.time() + 12
                    patrol_id = f"PLEP{int(time.time()) % 100000}"
                    publish("patrol_during_plan_wait", make_payload(args.imei, "ctrl", "W", patrol_id, {"DO": "patrol"}))
                time.sleep(0.5)
            if not execution_ok:
                write_jsonl(log, {"ts": now_text(), "event": "execution_not_observed", "target_bri": target_bri, "state": state})

            plan_now_response = request("plan_now_after", make_payload(args.imei, "plan", "R", "PLE1005", {"now": 1}))
            plan_now_ok = response_ok(plan_now_response)

            delete_response = request("plan_delete", make_payload(args.imei, "plan", "W", "PLE1006", {"del": [selected_plan_id]}))
            cleanup_delete_ok = response_ok(delete_response)
            cleanup_needed = False

            nid_after = request("plan_nid_after", make_payload(args.imei, "plan", "R", "PLE1007", {"DO": "nid"}))
            if response_ok(nid_after):
                after_dt = nid_after.get("DT", {}) if isinstance(nid_after, dict) and isinstance(nid_after.get("DT", {}), dict) else {}
                after_ids = after_dt.get("nid", [])
                if isinstance(after_ids, list):
                    cleanup_nid_ok = selected_plan_id not in {first_int(value) for value in after_ids}

            if not args.skip_restore_brightness and initial_bri is not None:
                restore_payload = {"cnCtrl": [{"cns": 1, "bri": initial_bri}]}
                restore_watch_start = time.time()
                restore_response = request("restore_brightness", make_payload(args.imei, "ctrl", "W", "PLE1008", restore_payload))
                restore_ack = response_ok(restore_response)
                wait_for("restored brightness report", lambda: any_report_bri(initial_bri, restore_watch_start), 30)
            else:
                restore_ack = True

            passed = (
                write_ok and
                readback_ok and
                execution_ok and
                plan_now_ok and
                cleanup_delete_ok and
                cleanup_nid_ok and
                restore_ack and
                int(state["offline"]) == 0
            )
            write_jsonl(log, {
                "ts": now_text(),
                "event": "finish",
                "passed": passed,
                "result": {
                    "plan_id": selected_plan_id,
                    "initial_bri": initial_bri,
                    "target_bri": target_bri,
                    "schedule_time": schedule_time.strftime("%Y-%m-%d %H:%M:%S"),
                    "write_ok": write_ok,
                    "readback_ok": readback_ok,
                    "execution_ok": execution_ok,
                    "plan_now_ok": plan_now_ok,
                    "cleanup_delete_ok": cleanup_delete_ok,
                    "cleanup_nid_ok": cleanup_nid_ok,
                    "restore_ack": restore_ack,
                    "offline": state["offline"],
                },
                "state": state,
                "log_path": str(log_path),
            })
            print(log_path)
            return 0 if passed else 1
        finally:
            if cleanup_needed and selected_plan_id is not None:
                try:
                    publish("plan_delete_finally", make_payload(args.imei, "plan", "W", "PLE1999", {"del": [selected_plan_id]}))
                    time.sleep(2)
                except Exception as exc:
                    write_jsonl(log, {"ts": now_text(), "event": "cleanup_exception", "error": str(exc)})
            client.loop_stop()
            client.disconnect()


if __name__ == "__main__":
    raise SystemExit(main())
