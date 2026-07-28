#!/usr/bin/env python3
"""Offline-safe workstation support for the 96-byte meter calibration image.

The module deliberately has no side effects at import time.  Its MQTT and serial
adapters are dependency-injected, so the calculation and protocol planning can be
tested without a broker, a DUT, or a COM port.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import struct
import time
import zlib
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any, Iterable, Mapping, Protocol

try:  # supports both `python tools/.../meter_...py` and package-style imports
    from calibration_station import MqttStation, Response, exact_json_integer, require_result
except ImportError:
    from tools.current_calibration.calibration_station import (  # type: ignore
        MqttStation, Response, exact_json_integer, require_result,
    )


SERIALIZED_SIZE = 96
PREFIX_SIZE = 92
Q24 = 1 << 24
CHANNELS = (
    "input_voltage_mv", "input_current_ua", "input_active_power_mw",
    "input_frequency_millihz", "output_voltage_mv", "output_current_ua",
)
LINEAR_CHANNELS = tuple(channel for channel in CHANNELS if channel != "input_frequency_millihz")
MAX_CHUNK_BYTES = 24
MAX_JSON_BYTES = 2048

# Mirror of Core/Src/meter_calibration.c physical guards (coefficient v2).
LINEAR_RAW_MAX = (0x00FFFFFF, 0x00FFFFFF, 0x007FFFFF, 0, 0x00000FFF, 0x00000FFF)
ZERO_LIMITS = ((0, 1_000_000), (0, 1_000_000), (-262_144, 262_144),
               (0, 0), (0, 1_024), (0, 1_024))
FACTOR_LIMITS = ((16_777, 3_355_443), (167_772, 83_886_080),
                 (16_777, 33_554_432), (8_388_608_000_000_000, 25_165_824_000_000_000),
                 (16_777_216, 1_677_721_600), (16_777_216, 838_860_800_000))
RAW_SPAN_MINIMUMS = (16_000_000, 16_000_000, 8_300_000, 0, 3_500, 3_500)
VALIDATION_MINIMUMS = (10_000, 10_000, 1_000, 0, 1_000, 10_000)
ENGINEERING_MAXIMUMS = (1_000_000, 10_000_000, 2_000_000, 120_000, 200_000, 25_000_000)
FREQUENCY_RAW_MIN, FREQUENCY_RAW_MAX = 10_000, 40_000
ENERGY_GAIN_Q24_MIN, ENERGY_GAIN_Q24_MAX = 16_777, 1_677_721_600_000
FLAGS_ENERGY_CF24 = 3


@dataclass(frozen=True)
class MeterContext:
    """Only device-provided context is accepted; no workstation power profile exists."""
    imei: str
    context_crc: int
    rated_current_ma: int
    calibration_max_current_ma: int
    hardware_max_current_ma: int
    output_current_sensor_mohm: int

    @property
    def sampling_resistor_mohm(self) -> int:
        """Compatibility alias for pre-v2 workstation callers."""
        return self.output_current_sensor_mohm

    @classmethod
    def from_device(cls, imei: str, values: Mapping[str, Any]) -> "MeterContext":
        if not isinstance(imei, str) or not imei.strip():
            raise ValueError("imei is required")
        required = ("contextCrc", "ratedCurrentMa", "calibrationMaxCurrentMa",
                    "hardwareMaxCurrentMa")
        if any(name not in values for name in required):
            raise ValueError("device calibration context is incomplete")
        # Firmware v2 exposes the physical output-current sensor name.  Older
        # captured contexts remain readable, but cannot override the live name.
        sensor_name = ("outputCurrentSensorMohm" if "outputCurrentSensorMohm" in values
                       else "samplingResistorMohm")
        if sensor_name not in values:
            raise ValueError("device context has no output-current sensor resistance")
        context = cls(
            imei.strip(),
            exact_json_integer(values["contextCrc"], "contextCrc", 0, 0xFFFFFFFF),
            exact_json_integer(values["ratedCurrentMa"], "ratedCurrentMa", 1, 0xFFFFFFFF),
            exact_json_integer(values["calibrationMaxCurrentMa"], "calibrationMaxCurrentMa", 1, 0xFFFFFFFF),
            exact_json_integer(values["hardwareMaxCurrentMa"], "hardwareMaxCurrentMa", 1, 0xFFFFFFFF),
            exact_json_integer(values[sensor_name], sensor_name, 1, 0xFFFFFFFF),
        )
        if context.rated_current_ma > context.calibration_max_current_ma or context.calibration_max_current_ma > context.hardware_max_current_ma:
            raise ValueError("device current limits are inconsistent")
        return context


@dataclass(frozen=True)
class Coefficients:
    context_crc: int
    zero_raw: tuple[int, ...]
    factor_q24: tuple[int, ...]
    energy_gain_q24: int
    flags: int = 3
    version: int = 2
    channel_count: int = 6

    def __post_init__(self) -> None:
        if len(self.zero_raw) != 6 or len(self.factor_q24) != 6:
            raise ValueError("exactly six zero and factor fields are required")
        if any(not -(1 << 31) <= value < (1 << 31) for value in self.zero_raw):
            raise ValueError("zero_raw must be signed 32-bit")
        if any(not 0 <= value <= 0xFFFFFFFFFFFFFFFF for value in (*self.factor_q24, self.energy_gain_q24)):
            raise ValueError("Q24 fields must be unsigned 64-bit")


def coefficient_prefix(coefficients: Coefficients) -> bytes:
    """The firmware's explicit 92-byte LE layout, excluding its CRC."""
    return struct.pack("<HHI6i6QQI", coefficients.version, coefficients.channel_count,
                       coefficients.context_crc, *coefficients.zero_raw,
                       *coefficients.factor_q24, coefficients.energy_gain_q24,
                       coefficients.flags)


def encode_coefficients(coefficients: Coefficients) -> bytes:
    prefix = coefficient_prefix(coefficients)
    if len(prefix) != PREFIX_SIZE:
        raise AssertionError("unexpected coefficient prefix size")
    return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)


def decode_coefficients(payload: bytes, *, expected_context_crc: int | None = None) -> Coefficients:
    if len(payload) != SERIALIZED_SIZE:
        raise ValueError("coefficient payload must be exactly 96 bytes")
    prefix, crc_bytes = payload[:PREFIX_SIZE], payload[PREFIX_SIZE:]
    if zlib.crc32(prefix) & 0xFFFFFFFF != struct.unpack("<I", crc_bytes)[0]:
        raise ValueError("coefficient CRC mismatch")
    values = struct.unpack("<HHI6i6QQI", prefix)
    coefficients = Coefficients(values[2], tuple(values[3:9]), tuple(values[9:15]), values[15], values[16], values[0], values[1])
    if expected_context_crc is not None and coefficients.context_crc != expected_context_crc:
        raise ValueError("coefficient context CRC mismatch")
    return coefficients


def _linear_q24_half_up(delta_raw: int, factor_q24: int) -> int:
    """Exact mirror of meter_cal_apply_linear_q24 for validator anchors."""
    if delta_raw < 0 or factor_q24 <= 0:
        raise ValueError("invalid linear fixed-point input")
    maximum_product = (0xFFFFFFFF << 24) + (Q24 // 2 - 1)
    if delta_raw and factor_q24 > maximum_product // delta_raw:
        raise ValueError("linear fixed-point conversion would saturate")
    return (delta_raw * factor_q24 + Q24 // 2) >> 24


def _reciprocal_q24_half_up(period_raw: int, numerator_q24: int) -> int:
    """Exact mirror of meter_cal_apply_reciprocal_q24's ceil-half rule."""
    if period_raw <= 0 or numerator_q24 <= 0:
        raise ValueError("invalid reciprocal fixed-point input")
    denominator = period_raw * Q24
    quotient, remainder = divmod(numerator_q24, denominator)
    if remainder >= denominator // 2 + denominator % 2:
        quotient += 1
    if quotient > 0xFFFFFFFF:
        raise ValueError("reciprocal fixed-point conversion would saturate")
    return quotient


def validate_coefficients(coefficients: Coefficients, expected_context_crc: int) -> None:
    """Python-equivalent of `meter_calibration_coefficients_validate` (v2)."""
    expected_context_crc = exact_json_integer(expected_context_crc, "contextCrc", 0, 0xFFFFFFFF)
    if coefficients.version != 2:
        raise ValueError("meter coefficient version must be 2")
    if coefficients.channel_count != 6:
        raise ValueError("meter coefficient channel count must be 6")
    if coefficients.context_crc != expected_context_crc:
        raise ValueError("meter coefficient context CRC mismatch")
    for index, channel in enumerate(CHANNELS):
        zero = coefficients.zero_raw[index]
        factor = coefficients.factor_q24[index]
        zero_low, zero_high = ZERO_LIMITS[index]
        factor_low, factor_high = FACTOR_LIMITS[index]
        if not zero_low <= zero <= zero_high:
            raise ValueError(f"{channel}: zero_raw is outside firmware limits")
        if factor == 0:
            raise ValueError(f"{channel}: factor_q24 is zero")
        if not factor_low <= factor <= factor_high:
            raise ValueError(f"{channel}: factor_q24 is outside firmware limits")
    frequency_factor = coefficients.factor_q24[3]
    for percent in (0, 25, 50, 75, 100):
        period = FREQUENCY_RAW_MIN + ((FREQUENCY_RAW_MAX - FREQUENCY_RAW_MIN) * percent + 50) // 100
        value = _reciprocal_q24_half_up(period, frequency_factor)
        if not 20_000 <= value <= 120_000:
            raise ValueError("input_frequency_millihz: firmware anchor validation failed")
    for index in (0, 1, 2, 4, 5):
        span = LINEAR_RAW_MAX[index] - coefficients.zero_raw[index]
        if span < RAW_SPAN_MINIMUMS[index]:
            raise ValueError(f"{CHANNELS[index]}: raw span is below firmware minimum")
        for percent in (25, 50, 100):
            raw_delta = (span * percent + 50) // 100
            value = _linear_q24_half_up(raw_delta, coefficients.factor_q24[index])
            minimum = (VALIDATION_MINIMUMS[index] * percent + 99) // 100
            maximum = (ENGINEERING_MAXIMUMS[index] * percent + 99) // 100
            if not minimum <= value <= maximum:
                raise ValueError(f"{CHANNELS[index]}: firmware anchor validation failed")
    if coefficients.flags & ~FLAGS_ENERGY_CF24:
        raise ValueError("meter coefficient flags are invalid")
    if coefficients.flags == 0:
        if coefficients.energy_gain_q24 != 0:
            raise ValueError("disabled energy requires zero energy gain")
    elif coefficients.flags == FLAGS_ENERGY_CF24:
        if not ENERGY_GAIN_Q24_MIN <= coefficients.energy_gain_q24 <= ENERGY_GAIN_Q24_MAX:
            raise ValueError("energy gain is outside firmware limits")
    else:
        raise ValueError("partial energy flags are invalid")


def validate_coefficient_payload(payload: bytes, expected_context_crc: int) -> Coefficients:
    """Validate size/LE CRC, then every firmware physical guard before transfer."""
    coefficients = decode_coefficients(payload, expected_context_crc=expected_context_crc)
    validate_coefficients(coefficients, expected_context_crc)
    return coefficients


@dataclass(frozen=True)
class FitPolicy:
    minimum_points: int = 3
    min_raw_span: float = 1.0
    min_slope: float = 1e-12
    max_relative_residual: float = 0.03
    mad_z: float = 3.5


@dataclass(frozen=True)
class LinearFit:
    zero_raw: int
    factor_q24: int
    slope: Decimal
    intercept: Decimal
    kept_points: int
    rejected_points: int
    raw_span: Decimal
    max_relative_residual: Decimal


def _decimal(value: Any, field: str = "value") -> Decimal:
    if isinstance(value, bool):
        raise ValueError(f"{field} must be numeric")
    try:
        result = Decimal(str(value))
    except (InvalidOperation, ValueError) as exc:
        raise ValueError(f"{field} must be numeric") from exc
    if not result.is_finite():
        raise ValueError(f"{field} must be finite")
    return result


def _round_half_up(value: Decimal) -> int:
    return int(value.to_integral_value(rounding=ROUND_HALF_UP))


def _finite_pairs(points: Iterable[tuple[float, float]]) -> list[tuple[Decimal, Decimal]]:
    result = [(_decimal(raw, "raw"), _decimal(reference, "reference")) for raw, reference in points]
    if not result:
        raise ValueError("calibration points must be finite")
    return result


def _ordinary_least_squares(points: list[tuple[Decimal, Decimal]]) -> tuple[Decimal, Decimal]:
    count = Decimal(len(points))
    mean_x = sum((point[0] for point in points), Decimal(0)) / count
    mean_y = sum((point[1] for point in points), Decimal(0)) / count
    denominator = sum(((point[0] - mean_x) ** 2 for point in points), Decimal(0))
    if denominator <= Decimal(0):
        raise ValueError("raw calibration span is zero")
    slope = sum(((point[0] - mean_x) * (point[1] - mean_y) for point in points), Decimal(0)) / denominator
    return slope, mean_y - slope * mean_x


def _theil_sen_seed(points: list[tuple[Decimal, Decimal]]) -> tuple[Decimal, Decimal]:
    """Outlier-resistant seed used before MAD filtering (small station datasets)."""
    slopes = [(y2 - y1) / (x2 - x1)
              for index, (x1, y1) in enumerate(points)
              for x2, y2 in points[index + 1:] if x2 != x1]
    if not slopes:
        raise ValueError("raw calibration span is zero")
    slope = Decimal(statistics.median(slopes))
    return slope, Decimal(statistics.median(y - slope * x for x, y in points))


def fit_linear(points: Iterable[tuple[float, float]], policy: FitPolicy = FitPolicy()) -> LinearFit:
    """Fit reference = slope * (raw - zero), rejecting MAD residual outliers."""
    values = _finite_pairs(points)
    if len(values) < policy.minimum_points:
        raise ValueError(f"at least {policy.minimum_points} good points are required")
    if max(raw for raw, _ in values) - min(raw for raw, _ in values) < _decimal(policy.min_raw_span, "min_raw_span"):
        raise ValueError("raw span is below the calibration threshold")
    slope, intercept = _theil_sen_seed(values)
    residuals = [reference - (slope * raw + intercept) for raw, reference in values]
    median = statistics.median(residuals)
    mad = statistics.median(abs(value - median) for value in residuals)
    # A zero MAD is common with integer engineering-unit references.  Preserve
    # normal +/- one-unit rounding rather than throwing away a valid third point.
    scale = Decimal("1.4826") * mad
    kept = values if scale == 0 else [point for point, residual in zip(values, residuals)
                                      if abs(residual - median) <= _decimal(policy.mad_z, "mad_z") * scale]
    if scale == 0:
        kept = [point for point, residual in zip(values, residuals)
                if abs(residual - median) <= Decimal(1)]
    if len(kept) < policy.minimum_points:
        raise ValueError("MAD outlier rejection left too few points")
    slope, intercept = _ordinary_least_squares(kept)
    if slope <= _decimal(policy.min_slope, "min_slope"):
        raise ValueError("linear calibration slope is below the threshold")
    raw_span = max(raw for raw, _ in kept) - min(raw for raw, _ in kept)
    if raw_span < _decimal(policy.min_raw_span, "min_raw_span"):
        raise ValueError("accepted raw span is below the calibration threshold")
    residual = max(abs(reference - (slope * raw + intercept)) / max(Decimal(1), abs(reference))
                   for raw, reference in kept)
    if residual > _decimal(policy.max_relative_residual, "max_relative_residual"):
        raise ValueError("linear calibration residual exceeds the threshold")
    zero = _round_half_up(-intercept / slope)
    factor = _round_half_up(slope * Q24)
    if factor <= 0 or factor > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("linear Q24 factor is out of range")
    return LinearFit(zero, factor, slope, intercept, len(kept), len(values) - len(kept), raw_span, residual)


def fit_frequency(points: Iterable[tuple[float, float]], policy: FitPolicy = FitPolicy(min_raw_span=1.0)) -> int:
    """Return reciprocal numerator Q24 from robust median(ref_mHz * period_raw)."""
    values = _finite_pairs(points)
    if len(values) < policy.minimum_points:
        raise ValueError(f"at least {policy.minimum_points} frequency points are required")
    products = [reference_millihz * period_raw for period_raw, reference_millihz in values
                if period_raw > 0 and reference_millihz > 0]
    if len(products) < policy.minimum_points:
        raise ValueError("frequency points must be positive")
    median = statistics.median(products)
    deviations = [abs(value - median) for value in products]
    mad = statistics.median(deviations)
    kept = products if mad == 0 else [value for value in products
                                      if abs(value - median) <= _decimal(policy.mad_z, "mad_z") * Decimal("1.4826") * mad]
    if len(kept) < policy.minimum_points:
        raise ValueError("frequency outlier rejection left too few points")
    numerator = _round_half_up(Decimal(statistics.median(kept)) * Q24)
    if not 0 < numerator <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("frequency Q24 numerator is out of range")
    return numerator


def energy_gain_q24(reference_active_power_mw: float, elapsed_seconds: float, cf_delta: int) -> int:
    """CF gain: known reference active power and elapsed time, in uWh/count Q24."""
    power = _decimal(reference_active_power_mw, "reference active power")
    elapsed = _decimal(elapsed_seconds, "elapsed seconds")
    if power <= 0 or elapsed <= 0 or isinstance(cf_delta, bool) or not isinstance(cf_delta, int) or cf_delta <= 0:
        raise ValueError("positive reference power, elapsed time, and CF delta are required")
    gain = power * elapsed * Decimal(1000) / Decimal(3600) / Decimal(cf_delta)
    value = _round_half_up(gain * Q24)
    if not 0 < value <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("energy gain is out of range")
    return value


def read_csv_points(path: Path) -> dict[str, list[tuple[Decimal, Decimal]]]:
    """Read `channel,raw,reference` records; an optional `good` column filters bad samples."""
    output: dict[str, list[tuple[Decimal, Decimal]]] = {channel: [] for channel in CHANNELS}
    with path.open(newline="", encoding="utf-8") as handle:
        for line, row in enumerate(csv.DictReader(handle), start=2):
            if not row or row.get("channel") not in output:
                raise ValueError(f"{path}:{line}: unknown channel")
            if str(row.get("good", "1")).strip().lower() in {"0", "false", "no", "bad"}:
                continue
            try:
                output[row["channel"]].append((_decimal(row["raw"], "raw"),
                                                _decimal(row["reference"], "reference")))
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"{path}:{line}: raw and reference are required numbers") from exc
    return output


def plan_chunks(payload: bytes, maximum_chunk_bytes: int = MAX_CHUNK_BYTES) -> list[dict[str, Any]]:
    if len(payload) != SERIALIZED_SIZE:
        raise ValueError("only complete 96-byte coefficient images may be uploaded")
    if not 1 <= maximum_chunk_bytes <= 32:
        raise ValueError("chunk size must be in [1, 32]")
    chunks = []
    for offset in range(0, len(payload), maximum_chunk_bytes):
        data = payload[offset:offset + maximum_chunk_bytes]
        chunks.append({"offset": offset, "values": list(data)})
    return chunks


def meter_request_json(imei: str, action: str, fields: Mapping[str, Any], message_id: str = "000001") -> bytes:
    """Canonical, bounded MQTT body used for static size checks and mock transports."""
    packet = {"SN": imei, "ID": message_id,
              "CT": "R" if action in {"readMeterInfo", "readMeterSample", "readMeterStatus"} else "W",
              "DT": {"Calibration": {"action": action, **dict(fields)}}}
    encoded = json.dumps(packet, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) >= MAX_JSON_BYTES:
        raise ValueError("MQTT JSON exceeds the 2048-byte firmware limit")
    return encoded


def strict_meter_response(response: Response, expected_action: str) -> dict[str, Any]:
    """Reject malformed or wrong-action responses before a station advances state."""
    if not isinstance(response.payload, dict) or not isinstance(response.calibration, dict):
        raise ValueError("malformed calibration response")
    node = response.payload.get("DT", {})
    expected_node = {
        "readMeterInfo": "CalibrationMeterInfo",
        "readMeterSample": "CalibrationMeterSample",
        "readMeterStatus": "CalibrationMeterStatus",
        "enter": "CalibrationAck",
        "beginMeter": "CalibrationAck",
        "writeMeterChunk": "CalibrationAck",
        "commitMeter": "CalibrationAck",
        "setTestPercent": "CalibrationAck",
        "abort": "CalibrationAck",
        "exit": "CalibrationAck",
    }.get(expected_action)
    if expected_node is None or not isinstance(node, dict) or not isinstance(node.get(expected_node), dict):
        raise ValueError("missing meter calibration response node")
    action = response.calibration.get("action")
    if action != expected_action:
        raise ValueError(f"unexpected meter response action {action!r}")
    require_result(response)
    calibration = dict(response.calibration)
    for name in ("contextCrc", "meterDataCrc"):
        if name in calibration:
            exact_json_integer(calibration[name], name, 0, 0xFFFFFFFF)
    for name in ("meterReceivedCount", "meterMissingCount", "meterVersion"):
        if name in calibration:
            exact_json_integer(calibration[name], name, 0, 0xFFFFFFFF)
    for name in ("meterComplete", "meterValidated"):
        if name not in calibration:
            continue
        value = calibration[name]
        if isinstance(value, bool):
            continue
        # cJSON emits numeric 0/1 in several deployed firmware variants.
        calibration[name] = bool(exact_json_integer(value, name, 0, 1))
    return calibration


class MeterCalibrationClient:
    """Protocol sequence over the existing `MqttStation` transport."""
    def __init__(self, station: Any, context: MeterContext | None = None) -> None:
        self.station, self.context = station, context
        self._last_successful_sequence = 0

    def _context(self) -> MeterContext:
        if self.context is None:
            raise ValueError("meter context from readMeterInfo is required for this operation")
        return self.context

    def request(self, action: str, fields: Mapping[str, Any] | None = None) -> dict[str, Any]:
        request_fields = dict(fields or {})
        response = self.station.request(action, request_fields)
        parsed = strict_meter_response(response, action)
        if "seq" in request_fields:
            self._last_successful_sequence = exact_json_integer(
                request_fields["seq"], "seq", 1, 0xFFFFFFFF)
        return parsed

    @staticmethod
    def _session_fields(session_id: str, seq: int) -> dict[str, Any]:
        if not isinstance(session_id, str) or not session_id:
            raise ValueError("active sessionId is required")
        return {"sessionId": session_id, "seq": exact_json_integer(seq, "seq", 1, 0xFFFFFFFF)}

    def info(self, seq: int = 1) -> dict[str, Any]:
        return self.request("readMeterInfo", {
            "seq": exact_json_integer(seq, "seq", 1, 0xFFFFFFFF),
        })

    def enter(self, session_id: str, seq: int, timeout_seconds: int) -> dict[str, Any]:
        context = self._context()
        fields = {**self._session_fields(session_id, seq), "contextCrc": context.context_crc,
                  "timeoutSec": exact_json_integer(timeout_seconds, "timeoutSec", 1, 0xFFFFFFFF)}
        return self.request("enter", fields)

    def exit(self, session_id: str, seq: int) -> dict[str, Any]:
        return self.request("exit", self._session_fields(session_id, seq))

    def abort(self, session_id: str, seq: int, reason: int = 1) -> dict[str, Any]:
        fields = {**self._session_fields(session_id, seq),
                  "reason": exact_json_integer(reason, "reason", 0, 0xFF)}
        return self.request("abort", fields)

    def set_test_percent(self, session_id: str, seq: int, percent: int = 0) -> dict[str, Any]:
        fields = {**self._session_fields(session_id, seq),
                  "percent": exact_json_integer(percent, "percent", 0, 100)}
        return self.request("setTestPercent", fields)

    def status(self, session_id: str, seq: int) -> dict[str, Any]:
        return self.request("readMeterStatus", self._session_fields(session_id, seq))

    def sample(self, session_id: str, seq: int) -> dict[str, Any]:
        return self.request("readMeterSample", self._session_fields(session_id, seq))

    def begin(self, session_id: str, seq: int) -> dict[str, Any]:
        """Start meter staging after enter; the firmware keeps its active PWM curve."""
        return self.request("beginMeter", self._session_fields(session_id, seq))

    def upload(self, payload: bytes, session_id: str, seq: int,
               maximum_chunk_bytes: int = MAX_CHUNK_BYTES) -> int:
        context = self._context()
        validate_coefficient_payload(payload, context.context_crc)
        crc = struct.unpack("<I", payload[-4:])[0]
        current_seq = exact_json_integer(seq, "seq", 1, 0xFFFFFFFF)
        for chunk in plan_chunks(payload, maximum_chunk_bytes):
            fields = {**self._session_fields(session_id, current_seq),
                      "meterVersion": 2, "contextCrc": context.context_crc,
                      "meterDataCrc": crc, "startOffset": chunk["offset"],
                      "values": chunk["values"]}
            meter_request_json(context.imei, "writeMeterChunk", fields)
            self.request("writeMeterChunk", fields)
            current_seq += 1
        return current_seq

    def commit(self, payload: bytes, session_id: str, seq: int) -> dict[str, Any]:
        crc = struct.unpack("<I", payload[-4:])[0]
        context = self._context()
        validate_coefficient_payload(payload, context.context_crc)
        fields = {**self._session_fields(session_id, seq), "contextCrc": context.context_crc,
                  "meterDataCrc": crc}
        return self.request("commitMeter", fields)

    def readback(self, session_id: str, seq: int, expected_payload: bytes) -> dict[str, Any]:
        """Read back firmware's persisted-state summary (the protocol never returns raw bytes)."""
        context = self._context()
        validate_coefficient_payload(expected_payload, context.context_crc)
        expected_crc = struct.unpack("<I", expected_payload[-4:])[0]
        response = self.status(session_id, seq)
        if response.get("meterDataCrc") != expected_crc or response.get("meterValidated") is not True:
            raise ValueError("firmware status does not confirm the expected meter payload")
        return response

    def upload_commit_readback(self, payload: bytes, session_id: str, first_seq: int,
                               timeout_seconds: int = 300) -> int:
        """Complete idle-to-idle transaction; best-effort cleanup never masks the failure."""
        sequence = exact_json_integer(first_seq, "seq", 1, 0xFFFFFFFF)
        entered = False
        try:
            self.enter(session_id, sequence, timeout_seconds)
            entered = True
            sequence += 1
            self.begin(session_id, sequence)
            sequence += 1
            sequence = self.upload(payload, session_id, sequence)
            status = self.status(session_id, sequence)
            if status.get("meterComplete") is not True or status.get("meterValidated") is not True:
                raise ValueError("firmware did not accept the complete coefficient image")
            sequence += 1
            self.commit(payload, session_id, sequence)
            sequence += 1
            self.readback(session_id, sequence, payload)
            sequence += 1
            self.exit(session_id, sequence)
            return sequence + 1
        except Exception:
            # setTestPercent only applies after a successful enter.  Abort is
            # still attempted after an uncertain enter acknowledgement.
            cleanup_seq = min(max(sequence + 1, self._last_successful_sequence + 1), 0xFFFFFFFF)
            if entered:
                try:
                    self.set_test_percent(session_id, cleanup_seq, 0)
                except Exception:
                    pass
                cleanup_seq = min(cleanup_seq + 1, 0xFFFFFFFF)
            try:
                self.abort(session_id, cleanup_seq, reason=1)
            except Exception:
                pass
            raise


class SerialLike(Protocol):
    timeout: float
    def reset_input_buffer(self) -> None: ...
    def write(self, data: bytes) -> int: ...
    def flush(self) -> None: ...
    def readline(self) -> bytes: ...


class ScpiReferenceMeter:
    """One cleared-buffer SCPI request/one response for voltage/current/power."""
    QUERIES = {"voltage": "MEAS:VOLT?", "current": "MEAS:CURR?", "power": "MEAS:POW?"}
    def __init__(self, port: SerialLike, *, timeout: float = 1.0,
                 ranges: Mapping[str, tuple[float, float]] | None = None) -> None:
        if timeout <= 0:
            raise ValueError("SCPI timeout must be positive")
        self.port, self.timeout = port, timeout
        self.ranges = dict(ranges or {"voltage": (0.0, 1000.0), "current": (0.0, 100.0), "power": (0.0, 100000.0)})

    @staticmethod
    def parse(text: str) -> float:
        value = text.strip()
        if not value or any(char.isspace() for char in value):
            raise ValueError("invalid SCPI response")
        try:
            parsed = float(value)
        except ValueError as exc:
            raise ValueError("invalid SCPI response") from exc
        if not math.isfinite(parsed):
            raise ValueError("SCPI response is not finite")
        return parsed

    def read(self, measurement: str) -> float:
        if measurement not in self.QUERIES:
            raise ValueError("unsupported SCPI measurement")
        self.port.reset_input_buffer()
        self.port.write((self.QUERIES[measurement] + "\n").encode("ascii"))
        self.port.flush()
        started = time.monotonic()
        response = self.port.readline()
        if not response or time.monotonic() - started > self.timeout:
            raise TimeoutError("SCPI response timed out")
        value = self.parse(response.decode("ascii", errors="strict"))
        low, high = self.ranges[measurement]
        if not low <= value <= high:
            raise ValueError(f"SCPI {measurement} is outside configured range")
        return value

    def stable(self, measurement: str, samples: int = 6, relative_span: float = 0.005) -> float:
        if samples < 3 or relative_span < 0:
            raise ValueError("invalid stability settings")
        values = [self.read(measurement) for _ in range(samples)]
        median = statistics.median(values)
        if max(values) - min(values) > max(1e-12, abs(median) * relative_span):
            raise ValueError("SCPI readings are not stable")
        return median


def build_payload(context: MeterContext, csv_path: Path, reference_active_power_mw: Any,
                  elapsed_seconds: Any, cf_delta: int) -> bytes:
    """Build a validated image without opening a serial port or network connection."""
    points = read_csv_points(csv_path)
    fits = {channel: fit_linear(points[channel]) for channel in LINEAR_CHANNELS}
    zero = tuple(0 if channel == "input_frequency_millihz" else fits[channel].zero_raw for channel in CHANNELS)
    factor = tuple(fit_frequency(points[channel]) if channel == "input_frequency_millihz" else fits[channel].factor_q24 for channel in CHANNELS)
    coefficients = Coefficients(context.context_crc, zero, factor,
                                energy_gain_q24(reference_active_power_mw, elapsed_seconds, cf_delta))
    payload = encode_coefficients(coefficients)
    validate_coefficient_payload(payload, context.context_crc)
    return payload


def execute_meter_command(client: MeterCalibrationClient, command: str, *, session_id: str | None = None,
                          seq: int | None = None, payload: bytes | None = None,
                          timeout_seconds: int = 300) -> tuple[Any, int | None]:
    """Dispatch the manual CLI operations; isolated for injected-mock tests."""
    if command == "info":
        info_seq = 1 if seq is None else seq
        return client.info(info_seq), info_seq + 1
    if session_id is None or seq is None:
        raise ValueError(f"{command} requires --session-id and --seq")
    if command == "sample":
        return client.sample(session_id, seq), seq + 1
    if command == "status":
        return client.status(session_id, seq), seq + 1
    if command == "begin":
        return client.begin(session_id, seq), seq + 1
    if payload is None:
        raise ValueError(f"{command} requires --payload")
    if command == "upload":
        next_seq = client.upload(payload, session_id, seq)
        return {"nextSeq": next_seq}, next_seq
    if command == "commit":
        return client.commit(payload, session_id, seq), seq + 1
    if command == "readback":
        return client.readback(session_id, seq, payload), seq + 1
    if command == "full":
        next_seq = client.upload_commit_readback(payload, session_id, seq, timeout_seconds)
        return {"nextSeq": next_seq}, next_seq
    raise ValueError(f"unsupported meter command {command!r}")


def _require(args: argparse.Namespace, *names: str) -> None:
    missing = [f"--{name.replace('_', '-')}" for name in names if getattr(args, name) is None]
    if missing:
        raise ValueError("required option(s): " + ", ".join(missing))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="?", default="build",
                        choices=("build", "info", "sample", "status", "begin", "upload", "commit", "readback", "full"))
    parser.add_argument("--csv", type=Path, help="channel,raw,reference CSV")
    parser.add_argument("--context-json", type=Path, help="captured device-only context JSON")
    parser.add_argument("--imei")
    parser.add_argument("--energy-power-mw", help="known reference active power (mW)")
    parser.add_argument("--energy-seconds", help="energy measurement elapsed seconds")
    parser.add_argument("--energy-cf-delta", type=int, help="trusted CF count delta")
    parser.add_argument("--output", type=Path, help="build output payload")
    parser.add_argument("--payload", type=Path, help="existing 96-byte payload for MQTT operations")
    parser.add_argument("--session-id")
    parser.add_argument("--seq", type=int)
    parser.add_argument("--host", help="MQTT broker host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--timeout-sec", type=int, default=300,
                        help="firmware calibration-session timeout used by full")
    parser.add_argument("--pub-topic")
    parser.add_argument("--sub-topic")
    parser.add_argument("--log", type=Path, default=Path("meter_calibration_mqtt.jsonl"))
    args = parser.parse_args()
    _require(args, "imei")
    if args.command == "build":
        _require(args, "csv", "context_json", "energy_power_mw", "energy_seconds", "energy_cf_delta", "output")
        context = MeterContext.from_device(args.imei, json.loads(args.context_json.read_text(encoding="utf-8")))
        args.output.write_bytes(build_payload(context, args.csv, args.energy_power_mw,
                                              args.energy_seconds, args.energy_cf_delta))
        return 0
    _require(args, "host")
    if args.command == "info":
        context = None
    else:
        _require(args, "context_json")
        context = MeterContext.from_device(
            args.imei, json.loads(args.context_json.read_text(encoding="utf-8")))
    payload = args.payload.read_bytes() if args.payload is not None else None
    if payload is not None:
        validate_coefficient_payload(payload, context.context_crc)
    with args.log.open("a", encoding="utf-8") as log_handle:
        station = MqttStation(args, log_handle)
        station.connect()
        try:
            result, _ = execute_meter_command(MeterCalibrationClient(station, context), args.command,
                                              session_id=args.session_id, seq=args.seq, payload=payload,
                                              timeout_seconds=args.timeout_sec)
        finally:
            station.close()
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
