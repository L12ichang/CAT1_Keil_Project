#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import queue
import re
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
from typing import Any, Callable, Protocol


READ_ACTIONS = {"readInfo", "readStatus", "readCurveStatus"}
CAL_NODES = ("CalibrationAck", "CalibrationStatus", "CalibrationCurveStatus", "CalibrationInfo")
POINTS = tuple(range(0, 101, 5))
SCPI_CURRENT_QUERY = "MEAS:CURR?"
MAX_CONSECUTIVE_METER_ERRORS = 3
UINT32_MAX = 0xFFFFFFFF
FACTORY_OUTCUR_MAX_MA = 10_000
PWM_LOGICAL_MAX_LIMIT = 65_535
CURVE_VERSION = 2
STORAGE_FORMAT_VERSION = 2


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def stamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def curve_crc(
    values: list[int], calibration_max_current_ma: int, version: int = CURVE_VERSION
) -> int:
    if len(values) != 21:
        raise ValueError("curve must contain exactly 21 values")
    version = exact_json_integer(version, "curveVersion", CURVE_VERSION, CURVE_VERSION)
    calibration_max_current_ma = exact_json_integer(
        calibration_max_current_ma,
        "calibrationMaxCurrentMa",
        1,
        FACTORY_OUTCUR_MAX_MA,
    )
    if any(isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFF
           for value in values):
        raise ValueError("curve PWM values must be uint16 integers")
    payload = struct.pack(
        "<HHI21H", version, len(values), calibration_max_current_ma, *values
    )
    return zlib.crc32(payload) & UINT32_MAX


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


class CurrentMeter(Protocol):
    def read_current_ma(self) -> float:
        ...


def validate_eload_query(query: str) -> str:
    """Return the only supported, read-only electronic-load current query."""
    if query != SCPI_CURRENT_QUERY:
        raise ValueError(f"electronic-load query must be exactly {SCPI_CURRENT_QUERY!r}")
    return query


def parse_current_response(text: str, unit: str = "A") -> float:
    """Parse the electronic load response and return milliamps."""
    value_text = text.strip()
    if not value_text:
        raise ValueError("electronic load returned an empty current response")
    if not re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", value_text):
        raise ValueError(f"invalid electronic load current response: {text!r}")
    value = float(value_text)
    if not math.isfinite(value):
        raise ValueError(f"non-finite electronic load current response: {text!r}")
    if unit == "A":
        return value * 1000.0
    if unit == "mA":
        return value
    raise ValueError(f"unsupported electronic load unit: {unit}")


class ScpiCurrentMeter:
    """Serial current reader compatible with the supplied electronic-load tool."""

    def __init__(
        self,
        port_name: str,
        baud: int = 9600,
        timeout: float = 1.0,
        unit: str = "A",
    ) -> None:
        try:
            import serial
        except ImportError as exc:
            raise SystemExit("pyserial is required for --eload-port") from exc
        self.query = validate_eload_query(SCPI_CURRENT_QUERY)
        self.unit = unit
        self.port = serial.Serial(
            port_name,
            baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            write_timeout=timeout,
        )

    def close(self) -> None:
        self.port.close()

    def read_current_ma(self) -> float:
        self.port.reset_input_buffer()
        self.port.write((self.query + "\n").encode("ascii"))
        self.port.flush()
        response = self.port.readline()
        if not response:
            raise TimeoutError("electronic load current query timed out")
        return parse_current_response(response.decode("ascii", errors="replace"), self.unit)


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


def exact_json_integer(value: Any, field: str, minimum: int, maximum: int) -> int:
    """Accept a JSON number only when it represents an exact integer in range."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a JSON integer")
    if isinstance(value, float):
        if not math.isfinite(value) or not value.is_integer():
            raise ValueError(f"{field} must be a finite exact integer")
    integer = int(value)
    if integer < minimum or integer > maximum:
        raise ValueError(f"{field}={integer} is outside [{minimum}, {maximum}]")
    return integer


def require_result(response: Response, expected: int = 0) -> dict[str, Any]:
    try:
        actual = exact_json_integer(response.calibration.get("result"), "result", 0, UINT32_MAX)
    except ValueError as exc:
        raise RuntimeError(f"invalid calibration result: {response.calibration}") from exc
    if actual != expected:
        raise RuntimeError(
            f"action={response.calibration.get('action')} result={actual}, expected={expected}: "
            f"{response.calibration}"
        )
    return response.calibration


def read_meter_samples(
    prompt: str,
    minimum: int = 24,
    meter: CurrentMeter | None = None,
    sample_interval_s: float = 0.1,
    sample_timeout_s: float = 25.0,
    sample_validator: Callable[[float], None] | None = None,
    on_sample_failure: Callable[[Exception], None] | None = None,
    max_consecutive_errors: int = MAX_CONSECUTIVE_METER_ERRORS,
) -> tuple[list[float], dict[str, float]]:
    if max_consecutive_errors < 1:
        raise ValueError("max_consecutive_errors must be positive")

    def fail(exc: Exception) -> None:
        if on_sample_failure is not None:
            try:
                on_sample_failure(exc)
            except Exception as shutdown_exc:
                raise RuntimeError(
                    f"sampling failed ({exc}); emergency output-off also failed ({shutdown_exc})"
                ) from exc
        raise exc

    if meter is not None:
        print(f"{prompt}\n正在从电子负载自动读取电流（至少 {minimum} 个样本）……")
        samples: list[float] = []
        metrics: dict[str, float] = {"count": 0.0}
        last_error: Exception | None = None
        consecutive_errors = 0
        deadline = time.monotonic() + sample_timeout_s
        while time.monotonic() < deadline:
            try:
                value = float(meter.read_current_ma())
            except Exception as exc:
                last_error = exc
                consecutive_errors += 1
                if consecutive_errors >= max_consecutive_errors:
                    fail(RuntimeError(
                        f"electronic-load returned {consecutive_errors} consecutive invalid readings: {exc}"
                    ))
            else:
                try:
                    if sample_validator is not None:
                        sample_validator(value)
                except Exception as exc:
                    fail(exc)
                samples.append(value)
                last_error = None
                consecutive_errors = 0
            if len(samples) >= minimum:
                stable, metrics = stable_samples(samples)
                if stable:
                    print(
                        f"电子负载采样完成：{len(samples)} 点，"
                        f"稳定值 {metrics['average']:.3f} mA"
                    )
                    return samples, metrics
            remaining = deadline - time.monotonic()
            if remaining > 0 and sample_interval_s > 0:
                time.sleep(min(sample_interval_s, remaining))
        detail = f"，最后错误：{last_error}" if last_error is not None else ""
        raise TimeoutError(
            f"电子负载在 {sample_timeout_s:.1f}s 内未获得稳定的 {minimum} 点读数；"
            f"有效样本={len(samples)}，稳定性={metrics}{detail}"
        )

    while True:
        text = input(f"{prompt}\n输入至少 {minimum} 个标准仪表 mA 读数（逗号分隔，q 中止）：\n> ").strip()
        if text.lower() in {"q", "quit", "abort"}:
            raise KeyboardInterrupt("operator aborted")
        try:
            samples = [float(part.strip()) for part in text.replace(";", ",").split(",") if part.strip()]
        except ValueError:
            print("读数格式错误，请重试。")
            continue
        try:
            if sample_validator is not None:
                for value in samples:
                    sample_validator(value)
        except Exception as exc:
            fail(exc)
        stable, metrics = stable_samples(samples)
        if len(samples) < minimum or not stable:
            print(f"样本未达到连续双窗口稳定条件：{metrics}")
            continue
        return samples, metrics


def collect_current_samples(
    args: argparse.Namespace,
    meter: CurrentMeter | None,
    prompt: str,
    *,
    nonzero_output: bool = False,
    maximum_ma: float | None = None,
    on_sample_failure: Callable[[Exception], None] | None = None,
) -> tuple[list[float], dict[str, float]]:
    def validate_sample(value: float) -> None:
        validate_current_sample(
            args,
            value,
            prompt,
            nonzero_output=nonzero_output,
            maximum_ma=maximum_ma,
        )

    return read_meter_samples(
        prompt,
        minimum=args.meter_samples,
        meter=meter,
        sample_interval_s=args.meter_sample_interval_ms / 1000.0,
        sample_timeout_s=args.meter_sample_timeout,
        sample_validator=(validate_sample if (nonzero_output or on_sample_failure is not None) else None),
        on_sample_failure=on_sample_failure,
    )


def check_measured_power(args: argparse.Namespace, measured_ma: float, context: str) -> float | None:
    if args.load_voltage_v is None or args.power_limit_w is None:
        return None
    power_w = args.load_voltage_v * measured_ma / 1000.0
    if power_w > args.power_limit_w:
        raise RuntimeError(
            f"{context}: measured output power {power_w:.3f}W exceeds "
            f"limit {args.power_limit_w:.3f}W"
        )
    return power_w


def validate_current_sample(
    args: argparse.Namespace,
    measured_ma: float,
    context: str,
    *,
    nonzero_output: bool,
    maximum_ma: float | None,
) -> None:
    """Reject unsafe meter readings before they can influence curve fitting."""
    if not math.isfinite(measured_ma):
        raise RuntimeError(f"{context}: non-finite current sample")
    if nonzero_output:
        if measured_ma <= 0:
            raise RuntimeError(f"{context}: non-zero PWM produced non-positive current {measured_ma:.3f}mA")
        if maximum_ma is None or not math.isfinite(maximum_ma) or maximum_ma <= 0:
            raise ValueError(f"{context}: a positive hardware current limit is required")
        if measured_ma > maximum_ma:
            raise RuntimeError(
                f"{context}: current sample {measured_ma:.3f}mA exceeds hardware limit "
                f"{maximum_ma:.3f}mA"
            )
        check_measured_power(args, measured_ma, context)
        return

    # At 0% a small signed offset is normal for some meters, but a real output is not.
    if abs(measured_ma) > args.leakage_max_ma:
        raise RuntimeError(
            f"{context}: zero-output current sample {measured_ma:.3f}mA exceeds leakage limit "
            f"{args.leakage_max_ma:.3f}mA"
        )


def resume_manifest_path(csv_path: Path) -> Path:
    return csv_path.with_suffix(".manifest.json")


def build_report_provenance(
    run_csv: Path,
    run_manifest: Path,
    resume_csvs: list[Path] | None,
    calibration_context: dict[str, Any],
    manifest_exists: Callable[[Path], bool] | None = None,
) -> dict[str, Any]:
    exists = manifest_exists or (lambda path: path.is_file())
    resume_inputs: list[dict[str, Any]] = []
    for csv_path in resume_csvs or []:
        manifest_path = resume_manifest_path(csv_path)
        present = bool(exists(manifest_path))
        resume_inputs.append(
            {
                "csv": str(csv_path),
                "manifest": str(manifest_path) if present else None,
                "manifestStatus": "present" if present else "missing",
            }
        )
    context_fields = (
        "imei", "contextCrc", "profileCrc", "legacyProfileCrc",
        "ratedCurrentMa", "calibrationMaxCurrentMa",
        "hardwareMaxCurrentMa", "pwmLogicalMax", "requiredCurveVersion",
        "storageFormatVersion",
    )
    return {
        "runCsv": str(run_csv),
        "runManifest": str(run_manifest),
        "resumeInputs": resume_inputs,
        "calibrationContext": {
            field: calibration_context.get(field) for field in context_fields
        },
    }


def write_resume_manifest(csv_path: Path, context: dict[str, int | str]) -> Path:
    required = (
        "imei", "contextCrc", "profileCrc", "ratedCurrentMa",
        "calibrationMaxCurrentMa", "hardwareMaxCurrentMa", "pwmLogicalMax",
        "requiredCurveVersion", "storageFormatVersion",
    )
    if any(key not in context for key in required):
        raise ValueError("resume context is incomplete")
    manifest = {
        "format": 2,
        "csvFile": csv_path.name,
        "createdAt": now_text(),
        **context,
    }
    path = resume_manifest_path(csv_path)
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return path


def load_resume_manifest(csv_path: Path, expected: dict[str, int | str]) -> None:
    path = resume_manifest_path(csv_path)
    if not path.is_file():
        raise ValueError(f"resume CSV {csv_path} has no required manifest {path}")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid resume manifest {path}: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != 2:
        raise ValueError(f"unsupported resume manifest {path}")
    if manifest.get("csvFile") != csv_path.name:
        raise ValueError(f"resume manifest {path} is not bound to {csv_path.name}")
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(
                f"resume manifest {path} does not match current {key}: "
                f"{manifest.get(key)!r} != {value!r}"
            )


def load_resume_curve(
    path: Path | list[Path],
    calibration_max_current_ma: int,
    context: dict[str, int | str],
) -> list[int]:
    best: dict[int, tuple[float, int]] = {}
    paths = [path] if isinstance(path, Path) else path
    for item in paths:
        load_resume_manifest(item, context)
        with item.open("r", encoding="utf-8-sig", newline="") as handle:
            for row in csv.DictReader(handle):
                if row.get("phase") != "search":
                    continue
                try:
                    percent = int(row["percent"])
                    measured = float(row["measured_ma"])
                    pwm = int(row["logical_pwm"])
                except (KeyError, TypeError, ValueError) as exc:
                    raise ValueError(f"invalid resume row in {item}: {row}") from exc
                if percent <= 0 or percent > 100 or percent % 5:
                    raise ValueError(f"invalid resume percent in {item}: {percent}")
                if not math.isfinite(measured) or measured <= 0:
                    raise ValueError(f"invalid resume current in {item}: {measured!r}")
                if measured > int(context["hardwareMaxCurrentMa"]):
                    raise ValueError(f"resume current exceeds hardware limit in {item}: {measured}")
                if pwm <= 0 or pwm > int(context["pwmLogicalMax"]):
                    raise ValueError(f"invalid resume PWM in {item}: {pwm}")
                index = percent // 5
                target = (calibration_max_current_ma * percent + 50) // 100
                error = abs(measured - target)
                if index not in best or error < best[index][0]:
                    best[index] = (error, pwm)

    curve = [0]
    for index in range(1, 21):
        target = (calibration_max_current_ma * index * 5 + 50) // 100
        tolerance = max(target * 0.01, 10.0)
        selected = best.get(index)
        if selected is None or selected[0] > tolerance:
            break
        if selected[1] <= curve[-1]:
            raise ValueError(f"resume curve is not monotonic at {index * 5}%")
        curve.append(selected[1])
    print(
        f"从 {', '.join(str(item) for item in paths)} 恢复 {len(curve) - 1} 个连续点，"
        f"下一点为 {len(curve) * 5}%"
    )
    return curve


def apply_curve_overrides(curve: list[int], overrides: list[str], logical_max: int) -> list[int]:
    logical_max = exact_json_integer(
        logical_max, "pwmLogicalMax", 1, PWM_LOGICAL_MAX_LIMIT
    )
    result = list(curve)
    if not result:
        raise ValueError("curve must start with the 0% point")
    if any(isinstance(pwm, bool) or not isinstance(pwm, int) for pwm in result):
        raise ValueError("curve PWM values must be integers")
    if any(pwm < 0 or pwm > logical_max for pwm in result):
        raise ValueError(f"curve PWM values must stay within [0, {logical_max}]")
    for override in overrides:
        try:
            percent_text, pwm_text = override.split("=", 1)
            percent = int(percent_text)
            pwm = int(pwm_text)
        except ValueError as exc:
            raise ValueError(f"invalid curve override {override!r}; expected PERCENT=PWM") from exc
        if percent < 0 or percent > 100 or percent % 5:
            raise ValueError(f"invalid override percent: {percent}")
        index = percent // 5
        if index >= len(result):
            raise ValueError(f"override {percent}% is not present in the resumed curve")
        if pwm < 0 or pwm > logical_max:
            raise ValueError(f"override PWM {pwm} is outside [0, {logical_max}]")
        result[index] = pwm
    if result[0] != 0 or any(lhs >= rhs for lhs, rhs in zip(result, result[1:])):
        raise ValueError("curve overrides must preserve zero and strict monotonicity")
    if overrides:
        print(f"应用曲线点修正：{', '.join(overrides)}")
    return result


def output_gate(args: argparse.Namespace, percent: int) -> None:
    if percent <= 0:
        return
    if not args.enable_output:
        raise PermissionError("non-zero output requires --enable-output")
    if percent > args.low_power_limit and not args.enable_high_power:
        raise PermissionError(
            f"{percent}% exceeds {args.low_power_limit}% low-power limit; use --enable-high-power"
        )


def require_pwm_v2_capability(info: dict[str, Any]) -> None:
    try:
        required_curve_version = exact_json_integer(
            info["requiredCurveVersion"], "requiredCurveVersion", 0, 0xFFFF
        )
        storage_format_version = exact_json_integer(
            info["storageFormatVersion"], "storageFormatVersion", 0, 0xFFFF
        )
        context_crc = exact_json_integer(info["contextCrc"], "contextCrc", 0, UINT32_MAX)
        profile_crc = exact_json_integer(info["profileCrc"], "profileCrc", 0, UINT32_MAX)
    except (KeyError, ValueError) as exc:
        raise RuntimeError(
            "device does not advertise the required PWM calibration v2 capability"
        ) from exc
    if (required_curve_version != CURVE_VERSION or
            storage_format_version != STORAGE_FORMAT_VERSION or
            context_crc != profile_crc):
        raise RuntimeError(
            f"unsupported device calibration protocol: curve={required_curve_version}, "
            f"storage={storage_format_version}, context/profile={context_crc}/{profile_crc}; "
            f"required={CURVE_VERSION}/{STORAGE_FORMAT_VERSION}"
        )


def run_info(station: MqttStation) -> dict[str, Any]:
    info = require_result(station.request("readInfo", {"seq": 1}))
    require_pwm_v2_capability(info)
    print(json.dumps(info, ensure_ascii=False, indent=2))
    return info


def validate_cli_numeric_args(args: argparse.Namespace) -> None:
    """Validate numeric CLI values before opening serial, MQTT, or output paths."""
    if args.rated_current_ma is not None:
        exact_json_integer(
            args.rated_current_ma, "--rated-current-ma", 1, FACTORY_OUTCUR_MAX_MA
        )
    if getattr(args, "calibration_max_current_ma", None) is not None:
        exact_json_integer(
            args.calibration_max_current_ma,
            "--calibration-max-current-ma",
            1,
            FACTORY_OUTCUR_MAX_MA,
        )
    integer_ranges = (
        ("meter_samples", "--meter-samples", 24, 10_000),
        ("meter_sample_interval_ms", "--meter-sample-interval-ms", 0, 60_000),
        ("settle_ms", "--settle-ms", 0, 600_000),
        ("max_iterations", "--max-iterations", 1, 1_000),
        ("low_power_limit", "--low-power-limit", 0, 100),
    )
    for attribute, field, minimum, maximum in integer_ranges:
        exact_json_integer(getattr(args, attribute), field, minimum, maximum)

    finite_ranges = (
        ("timeout", "--timeout", 0.001, 3_600.0),
        ("eload_timeout", "--eload-timeout", 0.001, 3_600.0),
        ("meter_sample_timeout", "--meter-sample-timeout", 0.001, 3_600.0),
        ("leakage_max_ma", "--leakage-max-ma", 0.0, 1_000.0),
    )
    for attribute, field, minimum, maximum in finite_ranges:
        value = getattr(args, attribute)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{field} must be numeric")
        value = float(value)
        if not math.isfinite(value) or value < minimum or value > maximum:
            raise ValueError(f"{field} must be finite and within [{minimum}, {maximum}]")

    if args.environment_c is not None:
        if (isinstance(args.environment_c, bool) or
                not isinstance(args.environment_c, (int, float)) or
                not math.isfinite(float(args.environment_c)) or
                not -80.0 <= float(args.environment_c) <= 150.0):
            raise ValueError("--environment-c must be finite and within [-80, 150]")

    if (args.load_voltage_v is None) != (args.power_limit_w is None):
        raise ValueError("--load-voltage-v and --power-limit-w must be supplied together")
    for attribute, field, maximum in (
        ("load_voltage_v", "--load-voltage-v", 1_000.0),
        ("power_limit_w", "--power-limit-w", 10_000.0),
    ):
        value = getattr(args, attribute)
        if value is not None and (
            isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(float(value)) or not 0.0 < float(value) <= maximum
        ):
            raise ValueError(f"{field} must be finite and within (0, {maximum}]")


def validate_calibration_info(
    info: dict[str, Any], args: argparse.Namespace
) -> dict[str, int | str | None]:
    """Bind this run to immutable device limits before any non-zero PWM is allowed."""
    try:
        profile = exact_json_integer(info["profileCrc"], "profileCrc", 0, UINT32_MAX)
        context_crc = exact_json_integer(
            info.get("contextCrc", profile), "contextCrc", 0, UINT32_MAX
        )
        rated = exact_json_integer(
            info["ratedCurrentMa"], "ratedCurrentMa", 1, FACTORY_OUTCUR_MAX_MA
        )
        hardware_max = exact_json_integer(
            info["hardwareMaxCurrentMa"], "hardwareMaxCurrentMa", 1,
            FACTORY_OUTCUR_MAX_MA,
        )
        logical_max = exact_json_integer(
            info["pwmLogicalMax"], "pwmLogicalMax", 1, PWM_LOGICAL_MAX_LIMIT
        )
        requested_rated = exact_json_integer(
            args.rated_current_ma, "--rated-current-ma", 1, FACTORY_OUTCUR_MAX_MA
        )
        required_curve_version = exact_json_integer(
            info["requiredCurveVersion"], "requiredCurveVersion", 0, 0xFFFF
        )
        storage_format_version = exact_json_integer(
            info["storageFormatVersion"], "storageFormatVersion", 0, 0xFFFF
        )
    except (KeyError, ValueError) as exc:
        raise ValueError(
            "readInfo must expose PWM v2 context, limits, requiredCurveVersion, and storageFormatVersion"
        ) from exc
    if required_curve_version != CURVE_VERSION or storage_format_version != STORAGE_FORMAT_VERSION:
        raise ValueError(
            f"unsupported calibration protocol: curveVersion={required_curve_version}, "
            f"storageFormatVersion={storage_format_version}; this station requires "
            f"v{CURVE_VERSION}/v{STORAGE_FORMAT_VERSION}"
        )
    if profile != context_crc:
        raise ValueError(
            f"readInfo profileCrc compatibility alias {profile} does not match contextCrc {context_crc}"
        )
    if rated > hardware_max:
        raise ValueError(
            f"invalid readInfo calibration limits: rated={rated}, hardwareMax={hardware_max}, "
            f"logicalMax={logical_max}, profile={profile}"
        )
    if requested_rated != rated:
        raise ValueError(
            f"--rated-current-ma={requested_rated} does not match device ratedCurrentMa={rated}"
        )
    calibration_max_arg = getattr(args, "calibration_max_current_ma", None)
    requested_calibration_max = exact_json_integer(
        requested_rated if calibration_max_arg is None else calibration_max_arg,
        "--calibration-max-current-ma",
        1,
        FACTORY_OUTCUR_MAX_MA,
    )
    if not requested_rated <= requested_calibration_max <= hardware_max:
        raise ValueError(
            "current limits must satisfy ratedCurrentMa <= calibrationMaxCurrentMa "
            f"<= hardwareMaxCurrentMa, got {requested_rated} <= "
            f"{requested_calibration_max} <= {hardware_max}"
        )
    if args.load_voltage_v is not None and args.power_limit_w is not None:
        calibration_power_w = args.load_voltage_v * requested_calibration_max / 1000.0
        if calibration_power_w > args.power_limit_w:
            raise ValueError(
                f"calibration output power {calibration_power_w:.3f}W exceeds requested power limit "
                f"{args.power_limit_w:.3f}W"
            )
    calibration_max_value = info.get(
        "calibrationMaxCurrentMa", info.get("calMaxCurrentMa")
    )
    calibration_max = (
        exact_json_integer(
            calibration_max_value, "calibrationMaxCurrentMa", 0, FACTORY_OUTCUR_MAX_MA
        )
        if calibration_max_value is not None else None
    )
    legacy_profile_value = info.get("legacyProfileCrc")
    legacy_profile = (
        exact_json_integer(legacy_profile_value, "legacyProfileCrc", 0, UINT32_MAX)
        if legacy_profile_value is not None else None
    )
    return {
        "imei": str(args.imei),
        "contextCrc": context_crc,
        "profileCrc": profile,
        "legacyProfileCrc": legacy_profile,
        "ratedCurrentMa": rated,
        "calibrationMaxCurrentMa": requested_calibration_max,
        "deviceCalibrationMaxCurrentMa": calibration_max,
        "hardwareMaxCurrentMa": hardware_max,
        "pwmLogicalMax": logical_max,
        "requiredCurveVersion": required_curve_version,
        "storageFormatVersion": storage_format_version,
    }


def run_smoke(station: MqttStation, session: str) -> None:
    info = run_info(station)
    context_crc = int(info["contextCrc"])
    require_result(
        station.request(
            "enter", {"sessionId": session, "seq": 1, "contextCrc": context_crc, "timeoutSec": 30}
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
    context_crc = int(info["contextCrc"])
    response = station.request(
        "setPwm",
        {"sessionId": session, "seq": 1, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0},
    )
    require_result(response, 4)
    enter_fields = {"sessionId": session, "seq": 1, "contextCrc": context_crc, "timeoutSec": 30}
    require_result(station.request("enter", enter_fields))
    command = {"sessionId": session, "seq": 2, "pointIndex": 0, "targetPercent": 0, "logicalPwm": 0}
    require_result(station.request("setPwm", command))
    require_result(station.request("setPwm", command))
    conflict = {**command, "logicalPwm": 1}
    require_result(station.request("setPwm", conflict), 18)
    require_result(station.request("abort", {"sessionId": session, "seq": 3, "reason": 0}))


def run_fuzz(station: MqttStation, session: str) -> None:
    info = run_info(station)
    context_crc = int(info["contextCrc"])
    calibration_max_current_ma = int(info["ratedCurrentMa"])
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
            "enter", {"sessionId": session, "seq": 1, "contextCrc": context_crc, "timeoutSec": 30}
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
        "sessionId": session, "curveVersion": CURVE_VERSION,
        "contextCrc": context_crc, "curveCrc": 0,
        "calibrationMaxCurrentMa": calibration_max_current_ma,
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
    meter: CurrentMeter | None,
    session: str,
    seq: int,
    index: int,
    previous_pwm: int,
    logical_max: int,
    initial_pwm: int,
    target_ma: float,
    csv_writer: csv.DictWriter,
    hardware_max_current_ma: int,
    on_sample_failure: Callable[[Exception, int], None],
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
        samples, metrics = collect_current_samples(
            args,
            meter,
            f"点位 {percent}% / 目标 {target_ma:.1f}mA / logicalPwm={candidate}",
            nonzero_output=True,
            maximum_ma=float(hardware_max_current_ma),
            on_sample_failure=lambda error: on_sample_failure(error, seq),
        )
        seq += 1
        status = require_result(
            station.request("readStatus", {"sessionId": session, "seq": seq})
        )
        if (int(status.get("outputEnabled", 0)) != 1 or
                int(status.get("logicalPwm", -1)) != candidate or
                int(status.get("protectCode", 0)) != 0):
            raise RuntimeError(f"device changed state during sampling at {percent}%: {status}")
        measured = metrics["average"]
        check_measured_power(args, measured, f"search {percent}%")
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


class CalibrationSafeStopError(RuntimeError):
    def __init__(self, errors: list[str], last_sequence: int):
        self.errors = tuple(errors)
        self.last_sequence = last_sequence
        super().__init__("calibration safety shutdown failed: " + "; ".join(errors))


def require_zero_output_state(result: dict[str, Any], action: str) -> None:
    try:
        enabled = exact_json_integer(result.get("outputEnabled"), "outputEnabled", 0, 1)
        logical_pwm = exact_json_integer(
            result.get("logicalPwm"), "logicalPwm", 0, PWM_LOGICAL_MAX_LIMIT
        )
    except ValueError as exc:
        raise RuntimeError(f"{action} did not return an exact output state: {result}") from exc
    if enabled != 0 or logical_pwm != 0:
        raise RuntimeError(f"{action} did not force zero output: {result}")


def calibration_safe_stop(
    station: MqttStation,
    session: str,
    sequence: int,
    *,
    temporary_curve_active: bool,
    abort_session: bool,
) -> int:
    """Best-effort, idempotent zero-output sequence; never skips abort after an error."""
    commands: list[tuple[str, dict[str, Any]]] = []
    if temporary_curve_active:
        commands.append(("setTestPercent", {"percent": 0}))
    else:
        commands.append(
            (
                "setPwm",
                {"pointIndex": 0, "targetPercent": 0, "logicalPwm": 0},
            )
        )
    if abort_session:
        commands.append(("abort", {"reason": 1}))

    errors: list[str] = []
    for action, fields in commands:
        sequence += 1
        try:
            result = require_result(
                station.request(action, {"sessionId": session, "seq": sequence, **fields})
            )
            require_zero_output_state(result, action)
        except Exception as exc:
            errors.append(f"{action}: {exc}")
    if errors:
        raise CalibrationSafeStopError(errors, sequence)
    return sequence


def run_calibrate(
    station: MqttStation,
    args: argparse.Namespace,
    meter: CurrentMeter | None,
    session: str,
    csv_writer: csv.DictWriter,
    report_path: Path,
    csv_path: Path,
) -> None:
    if not args.rated_current_ma:
        raise ValueError("--rated-current-ma is required for calibrate mode")
    info = run_info(station)
    calibration_context = validate_calibration_info(info, args)
    context_crc = int(calibration_context["contextCrc"])
    calibration_max_current_ma = int(calibration_context["calibrationMaxCurrentMa"])
    logical_max = int(calibration_context["pwmLogicalMax"])
    hw_max_current = int(calibration_context["hardwareMaxCurrentMa"])
    estimated_full_pwm = max(
        20, round(logical_max * calibration_max_current_ma / hw_max_current)
    )
    seq = 1
    committed = False
    entered = False
    session_maybe_active = False
    temporary_curve_active = False
    primary_error: BaseException | None = None
    curve = (
        load_resume_curve(args.resume_csv, calibration_max_current_ma, calibration_context)
        if args.resume_csv is not None else [0]
    )
    curve = apply_curve_overrides(curve, args.curve_override, logical_max)
    manifest_path = write_resume_manifest(csv_path, calibration_context)

    def emergency_output_off(sample_error: Exception, sequence_hint: int | None = None) -> None:
        nonlocal seq
        if not entered:
            return
        if sequence_hint is not None:
            seq = max(seq, sequence_hint)
        try:
            seq = calibration_safe_stop(
                station,
                session,
                seq,
                temporary_curve_active=temporary_curve_active,
                abort_session=False,
            )
        except CalibrationSafeStopError as exc:
            seq = exc.last_sequence
            raise RuntimeError(
                f"emergency output-off failed after sampling error {sample_error}: {exc}"
            ) from sample_error

    try:
        session_maybe_active = True
        require_result(
            station.request(
                "enter", {"sessionId": session, "seq": seq, "contextCrc": context_crc, "timeoutSec": 300}
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
        leakage_samples, leakage_metrics = collect_current_samples(
            args,
            meter,
            "0% 硬关断泄漏验证",
            on_sample_failure=emergency_output_off,
        )
        if abs(leakage_metrics["average"]) > args.leakage_max_ma:
            raise RuntimeError(f"0% leakage {leakage_metrics['average']:.2f}mA exceeds limit")
        csv_writer.writerow(
            {
                "ts": now_text(), "phase": "zero", "percent": 0, "target_ma": 0,
                "logical_pwm": 0, "measured_ma": leakage_metrics["average"],
                "error_ma": leakage_metrics["average"], "iteration": 1,
                "samples": json.dumps(leakage_samples),
            }
        )

        for index in range(len(curve), 21):
            target = (calibration_max_current_ma * index * 5 + 50) // 100
            if len(curve) >= 3:
                initial = curve[-1] + max(1, curve[-1] - curve[-2])
            else:
                initial = round(estimated_full_pwm * index * 5 / 100)
            point_pwm, seq = search_point(
                station, args, meter, session, seq, index, curve[-1], logical_max,
                initial, float(target), csv_writer, hw_max_current, emergency_output_off,
            )
            curve.append(point_pwm)

        seq += 1
        zero_response = require_result(
            station.request(
                "setPwm",
                {
                    "sessionId": session, "seq": seq, "pointIndex": 0,
                    "targetPercent": 0, "logicalPwm": 0,
                },
            )
        )
        if int(zero_response.get("outputEnabled", 1)) != 0:
            raise RuntimeError(f"failed to disable output before curve upload: {zero_response}")

        crc = curve_crc(curve, calibration_max_current_ma)
        for start in (0, 7, 14):
            seq += 1
            require_result(
                station.request(
                    "writeCurveChunk",
                    {
                        "sessionId": session, "seq": seq,
                        "curveVersion": CURVE_VERSION,
                        "contextCrc": context_crc, "curveCrc": crc,
                        "calibrationMaxCurrentMa": calibration_max_current_ma,
                        "startIndex": start,
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
        temporary_curve_active = True

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
            samples, metrics = collect_current_samples(
                args,
                meter,
                f"临时曲线复测 {percent}%",
                nonzero_output=percent > 0,
                maximum_ma=float(hw_max_current) if percent > 0 else None,
                on_sample_failure=emergency_output_off,
            )
            seq += 1
            status = require_result(
                station.request("readStatus", {"sessionId": session, "seq": seq})
            )
            if (int(status.get("outputEnabled", -1)) != expected_enabled or
                    int(status.get("logicalPwm", -1)) != expected_pwm or
                    int(status.get("protectCode", 0)) != 0):
                raise RuntimeError(f"device changed state during preview at {percent}%: {status}")
            target = (calibration_max_current_ma * percent + 50) // 100
            check_measured_power(args, metrics["average"], f"preview {percent}%")
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
                    "sessionId": session, "contextCrc": context_crc,
                    "profileCrc": context_crc, "curveCrc": crc,
                    "curveVersion": CURVE_VERSION,
                    "ratedCurrentMa": args.rated_current_ma,
                    "calibrationMaxCurrentMa": calibration_max_current_ma,
                    "logicalPwm": curve, "createdAt": now_text(),
                    **build_report_provenance(
                        csv_path, manifest_path, args.resume_csv, calibration_context
                    ),
                    "meterId": args.meter_id, "meterCalibrationDate": args.meter_cal_date,
                    "meterSource": "electronic-load-serial" if meter is not None else "manual",
                    "eloadPort": args.eload_port, "eloadBaud": args.eload_baud,
                    "eloadQuery": SCPI_CURRENT_QUERY,
                    "eloadResponseUnit": args.eload_response_unit,
                    "meterSamples": args.meter_samples,
                    "meterSampleIntervalMs": args.meter_sample_interval_ms,
                    "loadVoltageV": args.load_voltage_v,
                    "powerLimitW": args.power_limit_w,
                    "curveOverrides": args.curve_override,
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
                    "commit", {"sessionId": session, "seq": seq,
                               "contextCrc": context_crc, "curveCrc": crc}
                )
            )
            committed = True
            seq += 1
            require_result(station.request("exit", {"sessionId": session, "seq": seq}))
            entered = False
            session_maybe_active = False
            temporary_curve_active = False
        else:
            print("预览通过，但未指定 --commit；将 abort，不写 Flash。")
    except BaseException as exc:
        primary_error = exc
        raise
    finally:
        if entered or session_maybe_active:
            try:
                seq = calibration_safe_stop(
                    station,
                    session,
                    seq,
                    temporary_curve_active=temporary_curve_active,
                    abort_session=True,
                )
                entered = False
                session_maybe_active = False
            except CalibrationSafeStopError as shutdown_error:
                seq = shutdown_error.last_sequence
                if primary_error is not None and not entered:
                    print(
                        "warning: enter may not have completed; cleanup was rejected while "
                        f"preserving the original error: {shutdown_error}"
                    )
                    session_maybe_active = False
                    continue_original_error = True
                else:
                    continue_original_error = False
                if primary_error is not None:
                    if not continue_original_error:
                        raise RuntimeError(
                            f"calibration failed ({primary_error!r}) and safe shutdown also failed: "
                            f"{shutdown_error}"
                        ) from primary_error
                elif not continue_original_error:
                    raise
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
        choices=("eload-test", "info", "smoke", "protocol", "fuzz", "regression", "calibrate",
                 "jlink-read", "jlink-reset", "jlink-flash"),
        default="info",
    )
    parser.add_argument("--host", default="47.120.15.220")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--imei")
    parser.add_argument("--pub-topic")
    parser.add_argument("--sub-topic")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--rated-current-ma", type=int)
    parser.add_argument(
        "--calibration-max-current-ma",
        type=int,
        help="curve full-scale current; defaults to --rated-current-ma",
    )
    parser.add_argument("--settle-ms", type=int, default=2000)
    parser.add_argument("--max-iterations", type=int, default=16)
    parser.add_argument("--leakage-max-ma", type=float, default=10.0)
    parser.add_argument("--low-power-limit", type=int, default=25)
    parser.add_argument("--enable-output", action="store_true")
    parser.add_argument("--enable-high-power", action="store_true")
    parser.add_argument("--commit", action="store_true")
    parser.add_argument("--meter-id")
    parser.add_argument("--meter-cal-date")
    parser.add_argument("--environment-c", type=float)
    parser.add_argument("--load-voltage-v", type=float)
    parser.add_argument("--power-limit-w", type=float)
    parser.add_argument("--resume-csv", type=Path, action="append")
    parser.add_argument("--curve-override", action="append", default=[])
    parser.add_argument("--eload-port", help="electronic-load serial port, for example COM7")
    parser.add_argument("--eload-baud", type=int, default=9600)
    parser.add_argument("--eload-timeout", type=float, default=1.0)
    parser.add_argument("--eload-response-unit", choices=("A", "mA"), default="A")
    parser.add_argument("--meter-samples", type=int, default=24)
    parser.add_argument("--meter-sample-interval-ms", type=int, default=100)
    parser.add_argument("--meter-sample-timeout", type=float, default=25.0)
    parser.add_argument("--serial-port")
    parser.add_argument("--serial-baud", type=int, default=1_000_000)
    parser.add_argument("--jlink-exe", default=r"D:\Keil_v5\ARM\Segger\JLink.exe")
    parser.add_argument("--jlink-device", default="STM32F103RC")
    parser.add_argument("--jlink-speed", type=int, default=4000)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--allow-reset-output", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path("tools/current_calibration/logs"))
    args = parser.parse_args()

    try:
        validate_cli_numeric_args(args)
    except ValueError as exc:
        parser.error(str(exc))

    if args.enable_high_power and not args.enable_output:
        parser.error("--enable-high-power also requires --enable-output")
    if args.mode != "eload-test" and not args.imei:
        parser.error("--imei is required except in eload-test mode")
    if args.mode == "eload-test" and not args.eload_port:
        parser.error("--eload-port is required in eload-test mode")
    if args.resume_csv and any(not path.is_file() for path in args.resume_csv):
        parser.error("every --resume-csv must point to an existing calibration CSV")
    if (args.serial_port and args.eload_port and
            args.serial_port.casefold() == args.eload_port.casefold()):
        parser.error("--serial-port and --eload-port must be different physical ports")
    if args.mode == "calibrate" and args.commit and (not args.meter_id or not args.meter_cal_date):
        parser.error("committed calibration requires --meter-id and --meter-cal-date")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    run_stamp = stamp()
    jsonl_path = args.log_dir / f"calibration_{args.mode}_{run_stamp}.jsonl"
    csv_path = args.log_dir / f"calibration_{args.mode}_{run_stamp}.csv"
    report_path = args.log_dir / f"calibration_curve_{run_stamp}.json"
    session = f"CAL-{run_stamp}"
    serial_logger = start_serial_logger(args, args.log_dir)
    meter = None

    try:
        if args.mode in {"calibrate", "eload-test"} and args.eload_port:
            meter = ScpiCurrentMeter(
                args.eload_port,
                baud=args.eload_baud,
                timeout=args.eload_timeout,
                unit=args.eload_response_unit,
            )
        with jsonl_path.open("w", encoding="utf-8") as log_handle, csv_path.open(
            "w", encoding="utf-8-sig", newline=""
        ) as csv_handle:
            fieldnames = (
                "ts", "phase", "percent", "target_ma", "logical_pwm", "measured_ma",
                "error_ma", "iteration", "samples",
            )
            writer = csv.DictWriter(csv_handle, fieldnames=fieldnames)
            writer.writeheader()
            if args.mode == "eload-test":
                samples, metrics = collect_current_samples(args, meter, "电子负载串口测试")
                writer.writerow(
                    {
                        "ts": now_text(), "phase": "eload-test", "percent": "",
                        "target_ma": "", "logical_pwm": "",
                        "measured_ma": metrics["average"], "error_ma": "",
                        "iteration": 1, "samples": json.dumps(samples),
                    }
                )
                print(json.dumps({"measuredMa": metrics["average"], **metrics}, ensure_ascii=False))
            else:
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
                        run_calibrate(station, args, meter, session, writer, report_path, csv_path)
                    elif args.mode == "jlink-read":
                        run_jlink_read(station, args, args.log_dir)
                    elif args.mode == "jlink-reset":
                        run_jlink_reset(station, args, args.log_dir)
                    elif args.mode == "jlink-flash":
                        run_jlink_flash(station, args, args.log_dir)
                finally:
                    station.close()
    finally:
        if meter is not None:
            meter.close()
        if serial_logger is not None:
            serial_logger[0].set()
            serial_logger[1].join(timeout=2)
    print(json.dumps({"jsonl": str(jsonl_path), "csv": str(csv_path)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
