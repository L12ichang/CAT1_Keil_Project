"""Pure-Python public vectors for the Phase 4 meter calibration station."""
from __future__ import annotations

import struct
import unittest
from decimal import Decimal

from tools.current_calibration.calibration_station import Response
from tools.current_calibration.meter_calibration_station import (
    Coefficients, FitPolicy, MeterCalibrationClient, MeterContext, ScpiReferenceMeter, decode_coefficients,
    encode_coefficients, energy_gain_q24, execute_meter_command, fit_frequency,
    fit_linear, meter_request_json, plan_chunks, strict_meter_response,
)


class MockSerial:
    def __init__(self, replies: list[bytes]) -> None:
        self.replies = list(replies)
        self.writes: list[bytes] = []
        self.clears = 0
        self.flushed = 0
        self.timeout = 1.0

    def reset_input_buffer(self) -> None:
        self.clears += 1

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        self.flushed += 1

    def readline(self) -> bytes:
        return self.replies.pop(0) if self.replies else b""


class MockMeterClient:
    def __init__(self) -> None:
        self.calls: list[tuple[str, int | None]] = []

    def info(self, seq: int = 1) -> dict[str, int]:
        self.calls.append(("info", seq)); return {"result": 0}

    def begin(self, session_id: str, seq: int) -> dict[str, int]:
        self.calls.append(("begin", seq)); return {"result": 0}

    def upload(self, payload: bytes, session_id: str, seq: int) -> int:
        self.calls.append(("upload", seq)); return seq + 4

    def status(self, session_id: str, seq: int) -> dict[str, object]:
        self.calls.append(("status", seq)); return {"meterComplete": True, "meterValidated": True}

    def commit(self, payload: bytes, session_id: str, seq: int) -> dict[str, int]:
        self.calls.append(("commit", seq)); return {"result": 0}

    def readback(self, session_id: str, seq: int, payload: bytes) -> dict[str, int]:
        self.calls.append(("readback", seq)); return {"result": 0}

    def upload_commit_readback(self, payload: bytes, session_id: str, seq: int,
                               timeout_seconds: int = 300) -> int:
        self.begin(session_id, seq)
        next_seq = self.upload(payload, session_id, seq + 1)
        self.status(session_id, next_seq)
        self.commit(payload, session_id, next_seq + 1)
        self.readback(session_id, next_seq + 2, payload)
        return next_seq + 3


class MockTransport:
    def __init__(self) -> None:
        self.actions: list[str] = []

    def request(self, action: str, fields: dict[str, object]) -> Response:
        self.actions.append(action)
        node = {"readMeterInfo": "CalibrationMeterInfo", "readMeterSample": "CalibrationMeterSample",
                "readMeterStatus": "CalibrationMeterStatus"}.get(action, "CalibrationAck")
        body = {"action": action, "result": 0}
        return Response({"DT": {node: body}}, body)


class WorkflowTransport:
    def __init__(self, fail_action: str | None = None) -> None:
        self.actions: list[str] = []
        self.fail_action = fail_action
        self.crc = 0

    def request(self, action: str, fields: dict[str, object]) -> Response:
        self.actions.append(action)
        if action == self.fail_action:
            raise RuntimeError("primary failure")
        node = {"readMeterInfo": "CalibrationMeterInfo", "readMeterSample": "CalibrationMeterSample",
                "readMeterStatus": "CalibrationMeterStatus"}.get(action, "CalibrationAck")
        body: dict[str, object] = {"action": action, "result": 0}
        if action in {"writeMeterChunk", "commitMeter"}:
            self.crc = int(fields["meterDataCrc"])
        if action == "readMeterStatus":
            body.update({"meterComplete": 1, "meterValidated": 1, "meterDataCrc": self.crc})
        return Response({"DT": {node: body}}, body)


class MeterCalibrationStationTests(unittest.TestCase):
    def test_public_little_endian_96_byte_vector_and_u64_fields(self) -> None:
        coefficients = Coefficients(0x12345678, (0, 1, 2, 3, 4, 5),
                                    (1, 2, 3, 4, 5, 6), 7)
        payload = encode_coefficients(coefficients)
        self.assertEqual(len(payload), 96)
        self.assertEqual(payload.hex(),
            "0200060078563412000000000100000002000000030000000400000005000000"
            "0100000000000000020000000000000003000000000000000400000000000000"
            "050000000000000006000000000000000700000000000000030000008f694ea1")
        self.assertEqual(struct.unpack_from("<Q", payload, 32)[0], 1)
        self.assertEqual(decode_coefficients(payload, expected_context_crc=0x12345678), coefficients)
        with self.assertRaises(ValueError):
            Coefficients(0, (0,) * 6, (1 << 64,) + (1,) * 5, 1)

    def test_robust_linear_fit_rejects_mad_outlier(self) -> None:
        fit = fit_linear([(0, 0), (10, 20), (20, 40), (30, 60), (40, 1000)],
                         FitPolicy(min_raw_span=10, max_relative_residual=0.01))
        self.assertEqual(fit.zero_raw, 0)
        self.assertEqual(fit.factor_q24, 2 << 24)
        self.assertEqual(fit.rejected_points, 1)
        self.assertEqual(fit.kept_points, 4)

    def test_fit_rejects_insufficient_and_bad_span(self) -> None:
        with self.assertRaises(ValueError):
            fit_linear([(0, 0), (1, 1)])
        with self.assertRaises(ValueError):
            fit_linear([(10, 1), (10, 2), (10, 3)])

    def test_frequency_median_and_energy_gain(self) -> None:
        value = fit_frequency([(20_000, 50_000), (20_000, 50_000), (2_000, 50_000),
                               (20_000, 50_000)])
        self.assertEqual(value, 1_000_000_000 * (1 << 24))
        self.assertEqual(energy_gain_q24(3_600, 10, 10), 1_000 * (1 << 24))

    def test_decimal_half_up_and_large_u64_factor(self) -> None:
        q24 = Decimal(1 << 24)
        half_slope = Decimal("1.5") / q24
        half_fit = fit_linear([(0, 0), (1, half_slope), (2, half_slope * 2)])
        self.assertEqual(half_fit.factor_q24, 2)  # Decimal ROUND_HALF_UP, never banker's round.
        large_factor = (1 << 53) + 1
        slope = Decimal(large_factor) / q24
        large_fit = fit_linear([(0, 0), (1, slope), (2, slope * 2)])
        self.assertEqual(large_fit.factor_q24, large_factor)
        self.assertEqual(decode_coefficients(encode_coefficients(
            Coefficients(99, (0,) * 6, (large_factor,) * 6, large_factor))).factor_q24[0], large_factor)
        half_power_mw = Decimal("1.8") / q24
        self.assertEqual(energy_gain_q24(half_power_mw, 1, 1), 1)

    def test_context_prefers_firmware_sensor_field_and_legacy_is_supported(self) -> None:
        common = {"contextCrc": 1, "ratedCurrentMa": 1, "calibrationMaxCurrentMa": 2,
                  "hardwareMaxCurrentMa": 3}
        preferred = MeterContext.from_device("imei", {**common, "outputCurrentSensorMohm": 2,
                                                        "samplingResistorMohm": 1})
        self.assertEqual(preferred.output_current_sensor_mohm, 2)
        legacy = MeterContext.from_device("imei", {**common, "samplingResistorMohm": 1})
        self.assertEqual(legacy.output_current_sensor_mohm, 1)

    def test_scpi_mock_is_single_query_clear_and_stable(self) -> None:
        serial = MockSerial([b"230.0\n", b"230.1\n", b"230.0\n", b"230.0\n", b"230.1\n", b"230.0\n"])
        meter = ScpiReferenceMeter(serial, ranges={"voltage": (200, 250), "current": (0, 10), "power": (0, 1000)})
        self.assertEqual(meter.stable("voltage"), 230.0)
        self.assertEqual(serial.clears, 6)
        self.assertEqual(serial.flushed, 6)
        self.assertEqual(serial.writes, [b"MEAS:VOLT?\n"] * 6)
        with self.assertRaises(TimeoutError):
            ScpiReferenceMeter(MockSerial([])).read("voltage")
        with self.assertRaises(ValueError):
            ScpiReferenceMeter(MockSerial([b"nan\n"])).read("voltage")

    def test_chunk_plan_and_json_limit(self) -> None:
        payload = encode_coefficients(Coefficients(1, (0,) * 6, (1,) * 6, 1))
        chunks = plan_chunks(payload, 24)
        self.assertEqual([item["offset"] for item in chunks], [0, 24, 48, 72])
        self.assertTrue(all(len(payload[item["offset"]:item["offset"] + 24]) <= 24 for item in chunks))
        self.assertEqual(len(plan_chunks(payload, 32)), 3)
        body = meter_request_json("123", "writeMeterChunk", {
            "sessionId": "s", "seq": 1, "meterVersion": 2, "contextCrc": 1,
            "meterDataCrc": 2, "startOffset": 0, "values": list(payload[:24]),
        })
        self.assertLess(len(body), 2048)

    def test_strict_response_node_and_action(self) -> None:
        response = Response({"DT": {"CalibrationMeterStatus": {"action": "readMeterStatus", "result": 0}}},
                            {"action": "readMeterStatus", "result": 0})
        self.assertEqual(strict_meter_response(response, "readMeterStatus")["result"], 0)
        with self.assertRaises(ValueError):
            strict_meter_response(response, "readMeterSample")

    def test_firmware_cjson_numeric_status_booleans_are_normalized(self) -> None:
        actual = {"action": "readMeterStatus", "result": 0, "meterComplete": 1,
                  "meterValidated": 0, "meterReceivedCount": 96, "meterMissingCount": 0,
                  "meterVersion": 2, "meterDataCrc": 0x1234, "contextCrc": 0x99}
        response = Response({"DT": {"CalibrationMeterStatus": actual}}, actual)
        parsed = strict_meter_response(response, "readMeterStatus")
        self.assertIs(parsed["meterComplete"], True)
        self.assertIs(parsed["meterValidated"], False)

    def test_full_cli_dispatch_begins_before_upload(self) -> None:
        fake = MockMeterClient()
        result, next_seq = execute_meter_command(fake, "full", session_id="s", seq=10, payload=b"x" * 96)
        self.assertEqual(result, {"nextSeq": 18})
        self.assertEqual(next_seq, 18)
        self.assertEqual(fake.calls, [("begin", 10), ("upload", 11), ("status", 15),
                                      ("commit", 16), ("readback", 17)])

    def test_info_cli_supplies_required_nonzero_sequence(self) -> None:
        fake = MockMeterClient()
        result, next_seq = execute_meter_command(fake, "info", seq=7)
        self.assertEqual(result, {"result": 0})
        self.assertEqual(next_seq, 8)
        self.assertEqual(fake.calls, [("info", 7)])

    def test_client_upload_with_existing_session_sends_chunks(self) -> None:
        context = MeterContext.from_device("imei", {"contextCrc": 1, "ratedCurrentMa": 1,
            "calibrationMaxCurrentMa": 2, "hardwareMaxCurrentMa": 3, "outputCurrentSensorMohm": 1})
        payload = encode_coefficients(Coefficients(1, (0,) * 6, (1,) * 6, 1))
        transport = MockTransport()
        client = MeterCalibrationClient(transport, context)
        self.assertEqual(client.upload(payload, "s", 2), 6)
        self.assertEqual(transport.actions, ["writeMeterChunk"] * 4)

    def test_info_is_usable_before_a_context_file_exists(self) -> None:
        transport = MockTransport()
        self.assertEqual(MeterCalibrationClient(transport).info(), {"action": "readMeterInfo", "result": 0})
        self.assertEqual(transport.actions, ["readMeterInfo"])

    def test_full_lifecycle_enters_and_exits_in_order(self) -> None:
        context = MeterContext.from_device("imei", {"contextCrc": 1, "ratedCurrentMa": 1,
            "calibrationMaxCurrentMa": 2, "hardwareMaxCurrentMa": 3, "outputCurrentSensorMohm": 1})
        payload = encode_coefficients(Coefficients(1, (0,) * 6, (1,) * 6, 1))
        transport = WorkflowTransport()
        self.assertEqual(MeterCalibrationClient(transport, context).upload_commit_readback(payload, "s", 10, 90), 20)
        self.assertEqual(transport.actions, ["enter", "beginMeter"] + ["writeMeterChunk"] * 4 +
                         ["readMeterStatus", "commitMeter", "readMeterStatus", "exit"])

    def test_full_failure_cleans_up_without_masking_primary_error(self) -> None:
        context = MeterContext.from_device("imei", {"contextCrc": 1, "ratedCurrentMa": 1,
            "calibrationMaxCurrentMa": 2, "hardwareMaxCurrentMa": 3, "outputCurrentSensorMohm": 1})
        payload = encode_coefficients(Coefficients(1, (0,) * 6, (1,) * 6, 1))
        transport = WorkflowTransport(fail_action="commitMeter")
        with self.assertRaisesRegex(RuntimeError, "primary failure"):
            MeterCalibrationClient(transport, context).upload_commit_readback(payload, "s", 10)
        self.assertEqual(transport.actions[-2:], ["setTestPercent", "abort"])


if __name__ == "__main__":
    unittest.main()
