#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import queue
import statistics
import struct
import subprocess
import tempfile
import threading
import time
import uuid
import zlib
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


READ_ACTIONS = {"readInfo", "readStatus", "readCurveStatus"}
CAL_NODES = ("CalibrationAck", "CalibrationStatus", "CalibrationCurveStatus", "CalibrationInfo")
POINTS = tuple(range(0, 101, 5))


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def stamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def curve_crc(values: list[int], version: int = 1) -> int:
    if len(values) != 21:
        raise ValueError("curve must contain exactly 21 values")
    return zlib.crc32(struct.pack("<HH21H", version, 21, *values)) & 0xFFFFFFFF


def interpolate(values: list[int], percent: int) -> int:
    if percent <= 0:
        return 0
    if percent >= 100:
        return values[20]
    index, remainder = divmod(percent, 5)
    return values[index] + ((values[index + 1] - values[index]) * remainder + 2) // 5


def stable_samples(samples: list[float], window: int = 12) -> tuple[bool, dict[str, float]]:
    if len(samples) < window * 2:
        return False, {"count": float(len(samples))}
    checks: list[bool] = []
    averages: list[float] = []
    metrics: dict[str, float] = {}
    for index, values in enumerate((samples[-window * 2 : -window], samples[-window:]), start=1):
        average = statistics.fmean(values)
        averages.append(average)
        span = max(values) - min(values)
        span_limit = max(5.0, abs(average) * 0.005)
        slope = abs(values[-1] - values[0]) / max(1, len(values) - 1)
        slope_limit = max(1.0, abs(average) * 0.001)
        checks.append(span <= span_limit and slope <= slope_limit)
        metrics.update(
            {
                f"window{index}_average": average,
                f"window{index}_span": span,
                f"window{index}_span_limit": span_limit,
                f"window{index}_slope": slope,
                f"window{index}_slope_limit": slope_limit,
            }
        )
    mean_drift = abs(averages[1] - averages[0])
    mean_drift_limit = max(5.0, max(abs(averages[0]), abs(averages[1])) * 0.005)
    metrics["window_mean_drift_ma"] = mean_drift
    metrics["window_mean_drift_limit_ma"] = mean_drift_limit
    checks.append(mean_drift <= mean_drift_limit)
    metrics["average"] = averages[1]
    return all(checks), metrics


@dataclass
class Response:
    payload: dict[str, Any]
    calibration: dict[str, Any]


class MqttStation:
    def __init__(self, args: argparse.Namespace, log_handle: Any):
        try:
            import paho.mqtt.client as mqtt
        except ImportError as exc:
            raise SystemExit("paho-mqtt is required") from exc

        self.args = args
        self.log_handle = log_handle
        self.pub_topic = args.pub_topic or f"MS/{args.imei}/plt2dev"
        self.sub_topic = args.sub_topic or f"MS/{args.imei}/dev2plt"
        self.responses: queue.Queue[dict[str, Any]] = queue.Queue()
        self.next_id = int(time.time()) % 1_000_000
        self.connected = threading.Event()
        self.client = mqtt.Client(
            client_id=f"cal_station_{args.imei}_{uuid.uuid4().hex[:8]}", clean_session=True
        )
        if args.username:
            self.client.username_pw_set(args.username, args.password)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

    def log(self, event: str, **fields: Any) -> None:
        record = {"ts": now_text(), "event": event, **fields}
        self.log_handle.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.log_handle.flush()

    def _on_connect(self, client: Any, userdata: Any, flags: Any, rc: int) -> None:
        self.log("connect", rc=rc, sub_topic=self.sub_topic)
        if rc == 0:
            client.subscribe(self.sub_topic, qos=1)
            self.connected.set()

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        text = message.payload.decode("utf-8", errors="replace")
        self.log("message", topic=message.topic, payload=text)
        try:
            payload = json.loads(text)
        except json.JSONDecodeError:
            return
        self.responses.put(payload)

    def connect(self) -> None:
        self.client.connect(self.args.host, self.args.port, keepalive=60)
        self.client.loop_start()
        if not self.connected.wait(self.args.timeout):
            raise TimeoutError("MQTT connect timeout")

    def close(self) -> None:
        self.client.loop_stop()
        self.client.disconnect()

    def _message_id(self) -> str:
        self.next_id = (self.next_id + 1) % 1_000_000
        return f"{self.next_id:06d}"

    def request(
        self,
        action: str,
        fields: dict[str, Any] | None = None,
        *,
        message_id: str | None = None,
    ) -> Response:
        message_id = message_id or self._message_id()
        calibration = {"action": action, **(fields or {})}
        payload = {
            "SN": self.args.imei,
            "TM": now_text(),
            "SV": "prop",
            "ID": message_id,
            "CT": "R" if action in READ_ACTIONS else "W",
            "DT": {"Calibration": calibration},
        }
        self.log("publish", topic=self.pub_topic, payload=payload)
        publish = self.client.publish(
            self.pub_topic, json.dumps(payload, ensure_ascii=False, separators=(",", ":")), qos=1
        )
        publish.wait_for_publish(timeout=10)
        deadline = time.time() + self.args.timeout
        while time.time() < deadline:
            try:
                response = self.responses.get(timeout=min(0.5, max(0.01, deadline - time.time())))
            except queue.Empty:
                continue
            if str(response.get("ID", "")) != message_id:
                continue
            dt = response.get("DT") or {}
            for node in CAL_NODES:
                value = dt.get(node)
                if isinstance(value, dict):
                    return Response(response, value)
        raise TimeoutError(f"no calibration response for {action} ID={message_id}")


def require_result(response: Response, expected: int = 0) -> dict[str, Any]:
    actual = int(response.calibration.get("result", -1))
    if actual != expected:
        raise RuntimeError(
            f"action={response.calibration.get('action')} result={actual}, expected={expected}: "
            f"{response.calibration}"
        )
    return response.calibration


def read_meter_samples(prompt: str, minimum: int = 24) -> tuple[list[float], dict[str, float]]:
    while True:
        text = input(f"{prompt}\n输入至少 {minimum} 个标准仪表 mA 读数（逗号分隔，q 中止）：\n> ").strip()
        if text.lower() in {"q", "quit", "abort"}:
            raise KeyboardInterrupt("operator aborted")
        try:
            samples = [float(part.strip()) for part in text.replace(";", ",").split(",") if part.strip()]
        except ValueError:
            print("读数格式错误，请重试。")
            continue
        stable, metrics = stable_samples(samples)
        if len(samples) < minimum or not stable:
            print(f"样本未达到连续双窗口稳定条件：{metrics}")
            continue
        return samples, metrics


def output_gate(args: argparse.Namespace, percent: int) -> None:
    if percent <= 0:
        return
    if not args.enable_output:
        raise PermissionError("non-zero output requires --enable-output")
    if percent > args.low_power_limit and not args.enable_high_power:
        raise PermissionError(
            f"{percent}% exceeds {args.low_power_limit}% low-power limit; use --enable-high-power"
        )


def run_info(station: MqttStation) -> dict[str, Any]:
    info = require_result(station.request("readInfo", {"seq": 1}))
    print(json.dumps(info, ensure_ascii=False, indent=2))
    return info


def run_smoke(station: MqttStation, session: str) -> None:
    info = run_info(station)
    profile = int(info["profileCrc"])
    require_result(
        station.request(
            "enter", {"sessionId": session, "seq": 1, "profileCrc": profile, "timeoutSec": 30}
        )
    )
    require_result(
        station.request(
            "setPwm",
            {"sessionId": session, "seq": 2, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0},
        )
    )
    status = require_result(station.request("readStatus", {"sessionId": session, "seq": 3}))
    if int(status.get("outputEnabled", 1)) != 0 or int(status.get("logicalPwm", -1)) != 0:
        raise RuntimeError(f"zero-output smoke test failed: {status}")
    require_result(station.request("abort", {"sessionId": session, "seq": 4, "reason": 0}))


def run_protocol(station: MqttStation, session: str) -> None:
    info = run_info(station)
    profile = int(info["profileCrc"])
    response = station.request(
        "setPwm",
        {"sessionId": session, "seq": 1, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0},
    )
    require_result(response, 4)
    enter_fields = {"sessionId": session, "seq": 1, "profileCrc": profile, "timeoutSec": 30}
    require_result(station.request("enter", enter_fields))
    command = {"sessionId": session, "seq": 2, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0}
    require_result(station.request("setPwm", command))
    require_result(station.request("setPwm", command))
    conflict = {**command, "logicalPwm": 1}
    require_result(station.request("setPwm", conflict), 18)
    require_result(station.request("abort", {"sessionId": session, "seq": 3, "reason": 0}))


def run_fuzz(station: MqttStation, session: str) -> None:
    info = run_info(station)
    profile = int(info["profileCrc"])
    require_result(
        station.request(
            "setPwm",
            {"sessionId": session, "seq": 1, "pointIndex": 0,
             "targetPercent": 0, "logicalPwm": 0},
        ),
        4,
    )
    require_result(
        station.request(
            "enter", {"sessionId": session, "seq": 1, "profileCrc": profile, "timeoutSec": 30}
        )
    )
    invalid = {
        "sessionId": session, "seq": 2, "pointIndex": 1,
        "targetPercent": 6, "logicalPwm": 0,
    }
    require_result(station.request("setPwm", invalid), 2)
    require_result(station.request("setPwm", invalid), 2)
    require_result(station.request("setPwm", {**invalid, "logicalPwm": 1}), 18)
    curve_fields = {
        "sessionId": session, "curveVersion": 1,
        "profileCrc": profile, "curveCrc": 0,
    }
    for seq, length in ((3, 0), (4, 8), (5, 256), (6, 257)):
        require_result(
            station.request(
                "writeCurveChunk",
                {**curve_fields, "seq": seq, "startIndex": 0, "values": [0] * length},
            ),
            2,
        )
    require_result(
        station.request(
            "writeCurveChunk",
            {**curve_fields, "seq": 7, "startIndex": 20, "values": [0, 1]},
        ),
        2,
    )
    require_result(
        station.request("applyTemporary", {"sessionId": session, "seq": 8, "curveCrc": 0}), 6
    )
    require_result(station.request("exit", {"sessionId": session, "seq": 9}), 6)
    require_result(
        station.request(
            "writeCurveChunk",
            {**curve_fields, "seq": 10, "startIndex": 0, "values": [0]},
        )
    )
    require_result(
        station.request(
            "writeCurveChunk",
            {**curve_fields, "seq": 11, "startIndex": 1, "values": list(range(1, 8))},
        )
    )
    require_result(station.request("abort", {"sessionId": session, "seq": 12, "reason": 0}))


def run_regression(station: MqttStation, session: str) -> None:
    run_smoke(station, f"{session}-S")
    run_protocol(station, f"{session}-P")
    run_fuzz(station, f"{session}-F")


def search_point(
    station: MqttStation,
    args: argparse.Namespace,
    session: str,
    seq: int,
    index: int,
    previous_pwm: int,
    logical_max: int,
    initial_pwm: int,
    target_ma: float,
    csv_writer: csv.DictWriter,
) -> tuple[int, int]:
    low = previous_pwm + 1
    high = logical_max
    candidate = min(high, max(low, initial_pwm))
    best: tuple[float, int] | None = None
    tolerance = max(target_ma * 0.01, 10.0)

    for iteration in range(1, args.max_iterations + 1):
        percent = index * 5
        output_gate(args, percent)
        seq += 1
        response = require_result(
            station.request(
                "setPwm",
                {
                    "sessionId": session,
                    "seq": seq,
                    "pointIndex": index,
                    "targetPercent": percent,
                    "logicalPwm": candidate,
                },
            )
        )
        if int(response.get("outputLimited", 0)) or int(response.get("protectCode", 0)):
            raise RuntimeError(f"protection/limit active at {percent}%: {response}")
        time.sleep(args.settle_ms / 1000.0)
        seq += 1
        status = require_result(
            station.request("readStatus", {"sessionId": session, "seq": seq})
        )
        if (int(status.get("outputEnabled", 0)) != 1 or
                int(status.get("logicalPwm", -1)) != candidate or
                int(status.get("protectCode", 0)) != 0):
            raise RuntimeError(f"unsafe/unstable device status at {percent}%: {status}")
        samples, metrics = read_meter_samples(
            f"点位 {percent}% / 目标 {target_ma:.1f}mA / logicalPwm={candidate}"
        )
        measured = metrics["average"]
        error = abs(measured - target_ma)
        csv_writer.writerow(
            {
                "ts": now_text(),
                "phase": "search",
                "percent": percent,
                "target_ma": target_ma,
                "logical_pwm": candidate,
                "measured_ma": measured,
                "error_ma": error,
                "iteration": iteration,
                "samples": json.dumps(samples),
            }
        )
        if best is None or error < best[0]:
            best = (error, candidate)
        if error <= tolerance:
            return candidate, seq
        if measured < target_ma:
            low = candidate + 1
            if low > high:
                break
            step = max(2, candidate // 2)
            candidate = min(high, max(low, candidate + step))
        else:
            high = candidate - 1
            if low > high:
                break
            candidate = low + (high - low) // 2
    if best is not None and best[0] <= tolerance:
        return best[1], seq
    raise RuntimeError(f"point {index * 5}% failed tolerance; best={best}, tolerance={tolerance}")


def run_calibrate(
    station: MqttStation,
    args: argparse.Namespace,
    session: str,
    csv_writer: csv.DictWriter,
    report_path: Path,
) -> None:
    if not args.rated_current_ma:
        raise ValueError("--rated-current-ma is required for calibrate mode")
    info = run_info(station)
    profile = int(info["profileCrc"])
    logical_max = int(info["pwmLogicalMax"])
    hw_max_current = max(1, int(info.get("hardwareMaxCurrentMa", args.rated_current_ma)))
    estimated_full_pwm = max(20, round(logical_max * args.rated_current_ma / hw_max_current))
    seq = 1
    committed = False
    entered = False
    curve = [0]

    try:
        require_result(
            station.request(
                "enter", {"sessionId": session, "seq": seq, "profileCrc": profile, "timeoutSec": 300}
            )
        )
        entered = True
        seq += 1
        require_result(
            station.request(
                "setPwm",
                {"sessionId": session, "seq": seq, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0},
            )
        )
        time.sleep(args.settle_ms / 1000.0)
        leakage_samples, leakage_metrics = read_meter_samples("0% 硬关断泄漏验证")
        if leakage_metrics["average"] > args.leakage_max_ma:
            raise RuntimeError(f"0% leakage {leakage_metrics['average']:.2f}mA exceeds limit")
        csv_writer.writerow(
            {
                "ts": now_text(), "phase": "zero", "percent": 0, "target_ma": 0,
                "logical_pwm": 0, "measured_ma": leakage_metrics["average"],
                "error_ma": leakage_metrics["average"], "iteration": 1,
                "samples": json.dumps(leakage_samples),
            }
        )

        for index in range(1, 21):
            target = (args.rated_current_ma * index * 5 + 50) // 100
            initial = round(estimated_full_pwm * index * 5 / 100)
            point_pwm, seq = search_point(
                station, args, session, seq, index, curve[-1], logical_max,
                initial, float(target), csv_writer,
            )
            curve.append(point_pwm)

        crc = curve_crc(curve)
        for start in (0, 7, 14):
            seq += 1
            require_result(
                station.request(
                    "writeCurveChunk",
                    {
                        "sessionId": session, "seq": seq, "curveVersion": 1,
                        "profileCrc": profile, "curveCrc": crc, "startIndex": start,
                        "values": curve[start : start + 7],
                    },
                )
            )
        seq += 1
        curve_status = require_result(
            station.request("readCurveStatus", {"sessionId": session, "seq": seq})
        )
        if int(curve_status.get("receivedBitmap", 0)) != 0x1FFFFF or not int(curve_status.get("curveValid", 0)):
            raise RuntimeError(f"device rejected complete curve: {curve_status}")
        seq += 1
        require_result(
            station.request("applyTemporary", {"sessionId": session, "seq": seq, "curveCrc": crc})
        )

        for index, percent in enumerate(POINTS):
            output_gate(args, percent)
            seq += 1
            require_result(
                station.request(
                    "setTestPercent", {"sessionId": session, "seq": seq, "percent": percent}
                )
            )
            time.sleep(args.settle_ms / 1000.0)
            seq += 1
            status = require_result(
                station.request("readStatus", {"sessionId": session, "seq": seq})
            )
            expected_pwm = interpolate(curve, percent)
            expected_enabled = int(expected_pwm != 0)
            if (int(status.get("outputEnabled", -1)) != expected_enabled or
                    int(status.get("logicalPwm", -1)) != expected_pwm or
                    int(status.get("protectCode", 0)) != 0):
                raise RuntimeError(f"preview device status failed at {percent}%: {status}")
            samples, metrics = read_meter_samples(f"临时曲线复测 {percent}%")
            target = (args.rated_current_ma * percent + 50) // 100
            tolerance = args.leakage_max_ma if percent == 0 else max(target * 0.01, 10.0)
            error = abs(metrics["average"] - target)
            csv_writer.writerow(
                {
                    "ts": now_text(), "phase": "preview", "percent": percent,
                    "target_ma": target, "logical_pwm": curve[index],
                    "measured_ma": metrics["average"], "error_ma": error,
                    "iteration": 1, "samples": json.dumps(samples),
                }
            )
            if error > tolerance:
                raise RuntimeError(f"preview {percent}% failed: error={error}, tolerance={tolerance}")

        seq += 1
        zero_status = require_result(
            station.request("setTestPercent", {"sessionId": session, "seq": seq, "percent": 0})
        )
        if int(zero_status.get("outputEnabled", 1)) != 0:
            raise RuntimeError(f"pre-commit zero output failed: {zero_status}")

        report_path.write_text(
            json.dumps(
                {
                    "sessionId": session, "profileCrc": profile, "curveCrc": crc,
                    "curveVersion": 1, "ratedCurrentMa": args.rated_current_ma,
                    "logicalPwm": curve, "createdAt": now_text(),
                    "meterId": args.meter_id, "meterCalibrationDate": args.meter_cal_date,
                    "environmentC": args.environment_c, "previewPassed": True,
                },
                ensure_ascii=False, indent=2,
            ),
            encoding="utf-8",
        )
        if args.commit:
            seq += 1
            require_result(
                station.request(
                    "commit", {"sessionId": session, "seq": seq, "profileCrc": profile, "curveCrc": crc}
                )
            )
            committed = True
            seq += 1
            require_result(station.request("exit", {"sessionId": session, "seq": seq}))
            entered = False
        else:
            print("预览通过，但未指定 --commit；将 abort，不写 Flash。")
    finally:
        if entered:
            try:
                seq += 1
                station.request("abort", {"sessionId": session, "seq": seq, "reason": 1})
            except Exception as exc:
                print(f"warning: abort failed: {exc}")
        if committed:
            print(f"校准已提交：{report_path}")


def ensure_output_off(info: dict[str, Any]) -> None:
    if int(info.get("outputEnabled", 1)) != 0 or int(info.get("logicalPwm", -1)) != 0:
        raise RuntimeError("J-Link operation refused because output is not confirmed off")


def jlink_command(args: argparse.Namespace, commands: list[str], log_dir: Path, name: str) -> Path:
    executable = Path(args.jlink_exe)
    if not executable.exists():
        raise FileNotFoundError(executable)
    script_path = log_dir / f"{name}_{stamp()}.jlink"
    log_path = script_path.with_suffix(".log")
    script_path.write_text("\n".join(commands + ["exit", ""]), encoding="ascii")
    completed = subprocess.run(
        [str(executable), "-CommanderScript", str(script_path)],
        text=True, capture_output=True, timeout=60, check=False,
    )
    log_path.write_text(completed.stdout + completed.stderr, encoding="utf-8")
    if completed.returncode != 0 or "Cannot connect" in completed.stdout:
        raise RuntimeError(f"J-Link failed; see {log_path}")
    return log_path


def run_jlink_read(station: MqttStation, args: argparse.Namespace, log_dir: Path) -> None:
    info = run_info(station)
    ensure_output_off(info)
    slot_a = log_dir / f"cal_slot_a_{stamp()}.bin"
    slot_b = log_dir / f"cal_slot_b_{stamp()}.bin"
    log = jlink_command(
        args,
        [
            f"device {args.jlink_device}", "if SWD", f"speed {args.jlink_speed}", "connect",
            f"savebin {slot_a},0x08005C00,0x100", f"savebin {slot_b},0x08007400,0x100",
        ],
        log_dir,
        "jlink_cal_slots",
    )
    print(json.dumps({"slotA": str(slot_a), "slotB": str(slot_b), "log": str(log)}, indent=2))


def run_jlink_reset(station: MqttStation, args: argparse.Namespace, log_dir: Path) -> None:
    info = run_info(station)
    ensure_output_off(info)
    log = jlink_command(
        args,
        [f"device {args.jlink_device}", "if SWD", f"speed {args.jlink_speed}", "connect", "RSetType 2", "r", "go"],
        log_dir,
        "jlink_cal_reset",
    )
    print(log)


def run_jlink_flash(station: MqttStation, args: argparse.Namespace, log_dir: Path) -> None:
    info = run_info(station)
    ensure_output_off(info)
    if not args.allow_reset_output:
        raise PermissionError(
            "flashing resets the product and its existing boot policy may enable output; "
            "use --allow-reset-output only on a current-limited safe bench"
        )
    if args.firmware is None:
        raise ValueError("--firmware is required for jlink-flash")
    firmware = args.firmware.resolve()
    if firmware.suffix.lower() != ".bin" or not firmware.is_file():
        raise ValueError("--firmware must be an existing .bin image")
    if firmware.stat().st_size <= 0 or firmware.stat().st_size > 0x1C000:
        raise ValueError("firmware exceeds the 112 KB APP partition")
    log = jlink_command(
        args,
        [
            f"device {args.jlink_device}", "if SWD", f"speed {args.jlink_speed}", "connect",
            "RSetType 2", "r", "h", f"loadbin {firmware},0x08008000",
            f"verifybin {firmware},0x08008000", "r", "go",
        ],
        log_dir,
        "jlink_cal_flash",
    )
    verification_log = log.read_text(encoding="utf-8", errors="ignore")
    if "Verify successful" not in verification_log and "O.K." not in verification_log:
        raise RuntimeError(f"J-Link verification success was not observed; see {log}")
    print(log)


def start_serial_logger(args: argparse.Namespace, log_dir: Path) -> tuple[threading.Event, threading.Thread] | None:
    if not args.serial_port:
        return None
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for --serial-port") from exc
    stop = threading.Event()
    path = log_dir / f"serial_{stamp()}.log"

    def worker() -> None:
        with serial.Serial(args.serial_port, args.serial_baud, timeout=0.2) as port, path.open("wb") as handle:
            while not stop.is_set():
                data = port.read(4096)
                if data:
                    handle.write(data)
                    handle.flush()

    thread = threading.Thread(target=worker, name="cal-serial", daemon=True)
    thread.start()
    return stop, thread


def main() -> int:
    parser = argparse.ArgumentParser(description="CAT1 21-point current calibration station")
    parser.add_argument(
        "--mode",
        choices=("info", "smoke", "protocol", "fuzz", "regression", "calibrate",
                 "jlink-read", "jlink-reset", "jlink-flash"),
        default="info",
    )
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei", required=True)
    parser.add_argument("--pub-topic")
    parser.add_argument("--sub-topic")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--rated-current-ma", type=int)
    parser.add_argument("--settle-ms", type=int, default=500)
    parser.add_argument("--max-iterations", type=int, default=16)
    parser.add_argument("--leakage-max-ma", type=float, default=10.0)
    parser.add_argument("--low-power-limit", type=int, default=25)
    parser.add_argument("--enable-output", action="store_true")
    parser.add_argument("--enable-high-power", action="store_true")
    parser.add_argument("--commit", action="store_true")
    parser.add_argument("--meter-id")
    parser.add_argument("--meter-cal-date")
    parser.add_argument("--environment-c", type=float)
    parser.add_argument("--serial-port")
    parser.add_argument("--serial-baud", type=int, default=1_000_000)
    parser.add_argument("--jlink-exe", default=r"D:\Keil_v5\ARM\Segger\JLink.exe")
    parser.add_argument("--jlink-device", default="STM32F103RC")
    parser.add_argument("--jlink-speed", type=int, default=4000)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--allow-reset-output", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/current_calibration/logs"))
    args = parser.parse_args()

    if args.enable_high_power and not args.enable_output:
        parser.error("--enable-high-power also requires --enable-output")
    if args.mode == "calibrate" and args.commit and (not args.meter_id or not args.meter_cal_date):
        parser.error("committed calibration requires --meter-id and --meter-cal-date")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    run_stamp = stamp()
    jsonl_path = args.log_dir / f"calibration_{args.mode}_{run_stamp}.jsonl"
    csv_path = args.log_dir / f"calibration_{args.mode}_{run_stamp}.csv"
    report_path = args.log_dir / f"calibration_curve_{run_stamp}.json"
    session = f"CAL-{run_stamp}"
    serial_logger = start_serial_logger(args, args.log_dir)

    try:
        with jsonl_path.open("w", encoding="utf-8") as log_handle, csv_path.open(
            "w", encoding="utf-8-sig", newline=""
        ) as csv_handle:
            fieldnames = (
                "ts", "phase", "percent", "target_ma", "logical_pwm", "measured_ma",
                "error_ma", "iteration", "samples",
            )
            writer = csv.DictWriter(csv_handle, fieldnames=fieldnames)
            writer.writeheader()
            station = MqttStation(args, log_handle)
            station.connect()
            try:
                if args.mode == "info":
                    run_info(station)
                elif args.mode == "smoke":
                    run_smoke(station, session)
                elif args.mode == "protocol":
                    run_protocol(station, session)
                elif args.mode == "fuzz":
                    run_fuzz(station, session)
                elif args.mode == "regression":
                    run_regression(station, session)
                elif args.mode == "calibrate":
                    run_calibrate(station, args, session, writer, report_path)
                elif args.mode == "jlink-read":
                    run_jlink_read(station, args, args.log_dir)
                elif args.mode == "jlink-reset":
                    run_jlink_reset(station, args, args.log_dir)
                elif args.mode == "jlink-flash":
                    run_jlink_flash(station, args, args.log_dir)
            finally:
                station.close()
    finally:
        if serial_logger is not None:
            serial_logger[0].set()
            serial_logger[1].join(timeout=2)
    print(json.dumps({"jsonl": str(jsonl_path), "csv": str(csv_path)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
