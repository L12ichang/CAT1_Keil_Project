#!/usr/bin/env python3
from __future__ import annotations

import json
import io
import struct
import sys
import unittest
import zlib
from argparse import Namespace
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from calibration_station import (
    CalibrationSafeStopError,
    Response,
    apply_curve_overrides,
    build_report_provenance,
    calibration_safe_stop,
    curve_crc,
    interpolate,
    load_resume_curve,
    parse_current_response,
    read_meter_samples,
    run_calibrate,
    stable_samples,
    validate_calibration_info,
    validate_cli_numeric_args,
    validate_eload_query,
    validate_current_sample,
)


FLASH_MAGIC = 0x43414C31
FLASH_VALID = 0x56414C43
FLASH_TYPE_CURVE = 1
FLASH_TYPE_TOMBSTONE = 2
FLASH_RECORD_SIZE = 80


def pack_flash_record(sequence: int, record_type: int = FLASH_TYPE_CURVE,
                      marker: int = FLASH_VALID, profile_crc: int = 0x10203040) -> bytes:
    curve = [index * 10 for index in range(21)] if record_type == FLASH_TYPE_CURVE else [0] * 21
    stored_curve_crc = curve_crc(curve) if record_type == FLASH_TYPE_CURVE else 0
    curve_version = 1 if record_type == FLASH_TYPE_CURVE else 0
    point_count = 21 if record_type == FLASH_TYPE_CURVE else 0
    prefix = struct.pack(
        "<IHHIIIIHH21HH",
        FLASH_MAGIC, 1, FLASH_RECORD_SIZE, sequence & 0xFFFFFFFF, record_type,
        profile_crc, stored_curve_crc, curve_version, point_count, *curve, 0,
    )
    record_crc = zlib.crc32(prefix) & 0xFFFFFFFF
    record = prefix + struct.pack("<II", record_crc, marker)
    assert len(record) == FLASH_RECORD_SIZE
    return record


def flash_record_valid(record: bytes) -> bool:
    if len(record) != FLASH_RECORD_SIZE:
        return False
    magic, version, size = struct.unpack_from("<IHH", record)
    record_type = struct.unpack_from("<I", record, 12)[0]
    stored_crc, marker = struct.unpack_from("<II", record, 72)
    if (magic, version, size, marker) != (FLASH_MAGIC, 1, FLASH_RECORD_SIZE, FLASH_VALID):
        return False
    if zlib.crc32(record[:72]) & 0xFFFFFFFF != stored_crc:
        return False
    return record_type in (FLASH_TYPE_CURVE, FLASH_TYPE_TOMBSTONE)


def sequence_newer(lhs: int, rhs: int) -> bool:
    delta = (lhs - rhs) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def select_flash_slot(slot_a: bytes, slot_b: bytes) -> tuple[str, int, int] | None:
    candidates: list[tuple[str, int, int]] = []
    for name, record in (("A", slot_a), ("B", slot_b)):
        if flash_record_valid(record):
            sequence = struct.unpack_from("<I", record, 8)[0]
            record_type = struct.unpack_from("<I", record, 12)[0]
            candidates.append((name, sequence, record_type))
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    return candidates[1] if sequence_newer(candidates[1][1], candidates[0][1]) else candidates[0]


class CalibrationLogicTests(unittest.TestCase):
    def test_electronic_load_response_is_converted_to_milliamps(self) -> None:
        self.assertEqual(parse_current_response("2.700\r\n"), 2700.0)
        self.assertEqual(parse_current_response("1.25e-1\n"), 125.0)
        self.assertEqual(parse_current_response("2700", "mA"), 2700.0)
        with self.assertRaises(ValueError):
            parse_current_response("2.700 A")
        with self.assertRaises(ValueError):
            parse_current_response("nan")

    def test_automatic_meter_collects_two_stable_windows(self) -> None:
        class FakeMeter:
            def read_current_ma(self) -> float:
                return 1350.0

        samples, metrics = read_meter_samples(
            "test", meter=FakeMeter(), sample_interval_s=0.0, sample_timeout_s=1.0
        )
        self.assertEqual(len(samples), 24)
        self.assertEqual(metrics["average"], 1350.0)

    def test_unsafe_sample_requests_output_off_before_failure(self) -> None:
        class FakeMeter:
            def read_current_ma(self) -> float:
                return 900.0

        args = Namespace(load_voltage_v=56.0, power_limit_w=70.0, leakage_max_ma=10.0)
        callbacks: list[str] = []
        with self.assertRaisesRegex(RuntimeError, "hardware limit"):
            read_meter_samples(
                "test",
                meter=FakeMeter(),
                sample_interval_s=0.0,
                sample_timeout_s=1.0,
                sample_validator=lambda value: validate_current_sample(
                    args, value, "test", nonzero_output=True, maximum_ma=890.0
                ),
                on_sample_failure=lambda error: callbacks.append(str(error)),
            )
        self.assertEqual(len(callbacks), 1)

    def test_consecutive_meter_errors_fail_fast_and_request_output_off(self) -> None:
        class BrokenMeter:
            def read_current_ma(self) -> float:
                raise OSError("serial disconnected")

        callbacks: list[str] = []
        with self.assertRaisesRegex(RuntimeError, "3 consecutive"):
            read_meter_samples(
                "test",
                meter=BrokenMeter(),
                sample_interval_s=0.0,
                sample_timeout_s=1.0,
                on_sample_failure=lambda error: callbacks.append(str(error)),
            )
        self.assertEqual(len(callbacks), 1)

    def test_zero_output_allows_small_signed_offset_only(self) -> None:
        args = Namespace(load_voltage_v=None, power_limit_w=None, leakage_max_ma=10.0)
        validate_current_sample(args, -9.9, "zero", nonzero_output=False, maximum_ma=None)
        with self.assertRaisesRegex(RuntimeError, "zero-output"):
            validate_current_sample(args, -10.1, "zero", nonzero_output=False, maximum_ma=None)
        with self.assertRaisesRegex(RuntimeError, "non-positive"):
            validate_current_sample(args, 0.0, "nonzero", nonzero_output=True, maximum_ma=890.0)

    def test_eload_query_is_fixed_to_safe_read_only_command(self) -> None:
        self.assertEqual(validate_eload_query("MEAS:CURR?"), "MEAS:CURR?")
        for value in ("MEAS:VOLT?", "MEAS:CURR?\r\n", "OUTP ON"):
            with self.assertRaises(ValueError):
                validate_eload_query(value)

    def test_read_info_requires_consistent_current_and_power_limits(self) -> None:
        args = Namespace(imei="864512081541939", rated_current_ma=890,
                         load_voltage_v=56.0, power_limit_w=70.0)
        context = validate_calibration_info(
            {"profileCrc": 1, "ratedCurrentMa": 890,
             "hardwareMaxCurrentMa": 1680, "pwmLogicalMax": 999}, args
        )
        self.assertEqual(context["hardwareMaxCurrentMa"], 1680)
        with self.assertRaises(ValueError):
            validate_calibration_info(
                {"profileCrc": 1, "ratedCurrentMa": 1700,
                 "hardwareMaxCurrentMa": 1680, "pwmLogicalMax": 999}, args
            )
        limited = Namespace(imei="864512081541939", rated_current_ma=890,
                            load_voltage_v=56.0, power_limit_w=49.0)
        with self.assertRaisesRegex(ValueError, "rated output power"):
            validate_calibration_info(
                {"profileCrc": 1, "ratedCurrentMa": 890,
                 "hardwareMaxCurrentMa": 1680, "pwmLogicalMax": 999}, limited
            )

    def test_resume_requires_matching_manifest_and_recomputes_error(self) -> None:
        context = {
            "imei": "864512081541939", "profileCrc": 123,
            "ratedCurrentMa": 1000, "hardwareMaxCurrentMa": 1500, "pwmLogicalMax": 999,
        }
        class Manifest:
            def is_file(self) -> bool:
                return True

            def read_text(self, encoding: str) -> str:
                return json.dumps({"format": 1, "csvFile": "resume.csv", **context})

            def __str__(self) -> str:
                return "resume.manifest.json"

        class ResumeCsv:
            name = "resume.csv"

            def with_suffix(self, suffix: str) -> Manifest:
                if suffix != ".manifest.json":
                    raise AssertionError(suffix)
                return Manifest()

            def open(self, mode: str, *, encoding: str, newline: str) -> io.StringIO:
                if mode != "r":
                    raise AssertionError(mode)
                return io.StringIO(
                    "phase,percent,target_ma,logical_pwm,measured_ma,error_ma\n"
                    "search,5,50,20,150,0\n"
                )

            def __str__(self) -> str:
                return "resume.csv"

        path = ResumeCsv()
        self.assertEqual(load_resume_curve([path], 1000, context), [0])
        bad_context = {**context, "profileCrc": 124}
        with self.assertRaisesRegex(ValueError, "does not match"):
            load_resume_curve([path], 1000, bad_context)

    def test_report_provenance_pairs_zero_one_and_multiple_resume_inputs(self) -> None:
        context = {
            "imei": "864512081541939", "profileCrc": 123,
            "ratedCurrentMa": 890, "hardwareMaxCurrentMa": 1680,
            "pwmLogicalMax": 1000,
        }
        run_csv = Path("logs/current.csv")
        run_manifest = Path("logs/current.manifest.json")

        empty = build_report_provenance(run_csv, run_manifest, None, context)
        self.assertEqual(empty["runCsv"], str(run_csv))
        self.assertEqual(empty["runManifest"], str(run_manifest))
        self.assertEqual(empty["resumeInputs"], [])
        self.assertIsNone(empty["calibrationContext"]["contextCrc"])
        self.assertIsNone(empty["calibrationContext"]["calibrationMaxCurrentMa"])

        first = Path("logs/first.csv")
        one = build_report_provenance(
            run_csv, run_manifest, [first], context, manifest_exists=lambda path: True
        )
        self.assertEqual(one["resumeInputs"], [{
            "csv": str(first), "manifest": str(first.with_suffix(".manifest.json")),
            "manifestStatus": "present",
        }])
        self.assertNotEqual(one["resumeInputs"][0]["manifest"], one["runManifest"])

        second = Path("logs/second.csv")
        multiple = build_report_provenance(
            run_csv,
            run_manifest,
            [first, second],
            context,
            manifest_exists=lambda path: path.name.startswith("first"),
        )
        self.assertEqual(len(multiple["resumeInputs"]), 2)
        self.assertEqual(multiple["resumeInputs"][0]["manifestStatus"], "present")
        self.assertIsNone(multiple["resumeInputs"][1]["manifest"])
        self.assertEqual(multiple["resumeInputs"][1]["manifestStatus"], "missing")

    def test_curve_override_rejects_pwm_above_device_limit(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside"):
            apply_curve_overrides([0, 50], ["5=1001"], 1000)
        self.assertEqual(apply_curve_overrides([0, 50], ["5=60"], 1000), [0, 60])

    def test_read_info_rejects_fractional_nonfinite_and_out_of_range_numbers(self) -> None:
        args = Namespace(imei="864512081541939", rated_current_ma=890,
                         load_voltage_v=56.0, power_limit_w=70.0)
        base = {"profileCrc": 1, "ratedCurrentMa": 890,
                "hardwareMaxCurrentMa": 1680, "pwmLogicalMax": 1000}
        for field, value in (
            ("ratedCurrentMa", 890.9), ("ratedCurrentMa", float("nan")),
            ("hardwareMaxCurrentMa", float("inf")), ("hardwareMaxCurrentMa", 10001),
            ("pwmLogicalMax", 65536), ("profileCrc", 0x1_0000_0000),
        ):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_calibration_info({**base, field: value}, args)

    def test_cli_numeric_values_must_be_finite_and_reasonable(self) -> None:
        def valid_args() -> Namespace:
            return Namespace(
                rated_current_ma=890, meter_samples=24, meter_sample_interval_ms=100,
                settle_ms=2000, max_iterations=16, low_power_limit=25,
                timeout=30.0, eload_timeout=1.0, meter_sample_timeout=25.0,
                leakage_max_ma=10.0, environment_c=25.0,
                load_voltage_v=56.0, power_limit_w=70.0,
            )

        validate_cli_numeric_args(valid_args())
        for field, value in (("load_voltage_v", float("nan")),
                             ("power_limit_w", float("inf")),
                             ("timeout", float("nan")),
                             ("environment_c", float("inf"))):
            args = valid_args()
            setattr(args, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_cli_numeric_args(args)

    def test_safe_stop_handles_temporary_curve_and_verifies_abort(self) -> None:
        class FakeStation:
            def __init__(self, state: str, reject_abort: bool = False):
                self.actions: list[str] = []
                self.reject_abort = reject_abort
                self.state = state

            def request(self, action: str, fields: dict[str, object]) -> Response:
                self.actions.append(action)
                result = 0
                if action == "setTestPercent" and self.state != "TEMP_APPLIED":
                    result = 9
                elif action == "setPwm" and self.state == "TEMP_APPLIED":
                    result = 9
                elif action == "abort" and self.reject_abort:
                    result = 9
                if result == 0 and action in {"setTestPercent", "setPwm"}:
                    self.state = "READY"
                if result == 0 and action == "abort":
                    self.state = "IDLE"
                return Response({}, {"action": action, "result": result,
                                     "outputEnabled": 0, "logicalPwm": 0})

        station = FakeStation("TEMP_APPLIED")
        self.assertEqual(
            calibration_safe_stop(station, "CAL", 10,
                                  temporary_curve_active=True, abort_session=True),
            12,
        )
        self.assertEqual(station.actions, ["setTestPercent", "abort"])
        self.assertEqual(station.state, "IDLE")

        direct = FakeStation("READY")
        calibration_safe_stop(direct, "CAL", 30,
                              temporary_curve_active=False, abort_session=True)
        self.assertEqual(direct.actions, ["setPwm", "abort"])
        self.assertEqual(direct.state, "IDLE")

        rejecting = FakeStation("TEMP_APPLIED", reject_abort=True)
        with self.assertRaises(CalibrationSafeStopError):
            calibration_safe_stop(rejecting, "CAL", 20,
                                  temporary_curve_active=True, abort_session=True)
        self.assertEqual(rejecting.actions, ["setTestPercent", "abort"])

    def test_enter_ack_timeout_still_attempts_cleanup_without_masking_timeout(self) -> None:
        class LostAckStation:
            def __init__(self):
                self.actions: list[str] = []

            def request(self, action: str, fields: dict[str, object]) -> Response:
                self.actions.append(action)
                if action == "readInfo":
                    return Response({}, {
                        "action": action, "result": 0, "profileCrc": 1,
                        "ratedCurrentMa": 890, "hardwareMaxCurrentMa": 1680,
                        "pwmLogicalMax": 1000,
                    })
                if action == "enter":
                    raise TimeoutError("ACK lost")
                return Response({}, {"action": action, "result": 4,
                                     "outputEnabled": 0, "logicalPwm": 0})

        args = Namespace(
            imei="864512081541939", rated_current_ma=890,
            load_voltage_v=56.0, power_limit_w=70.0,
            resume_csv=None, curve_override=[],
        )
        station = LostAckStation()
        with patch("calibration_station.write_resume_manifest", return_value=Path("manifest.json")):
            with self.assertRaisesRegex(TimeoutError, "ACK lost"):
                run_calibrate(
                    station, args, None, "CAL", object(),
                    Path("report.json"), Path("run.csv"),
                )
        self.assertEqual(station.actions, ["readInfo", "enter", "setPwm", "abort"])

    def test_crc32_standard_vector(self) -> None:
        import zlib

        self.assertEqual(zlib.crc32(b"123456789") & 0xFFFFFFFF, 0xCBF43926)

    def test_curve_crc_vectors(self) -> None:
        self.assertEqual(curve_crc([i * 10 for i in range(21)]), 0x35206DBC)
        self.assertEqual(curve_crc(list(range(21))), 0x1F87FDE0)
        self.assertEqual(curve_crc([i * i + 3 * i for i in range(21)]), 0x4525FA2C)

    def test_profile_crc_public_vector(self) -> None:
        fields = (1, 1, 4, 2, 2700, 4700, 30, 0, 71, 999, 2, 1, 1000)
        self.assertEqual(zlib.crc32(struct.pack("<13I", *fields)) & 0xFFFFFFFF, 0x62FE9B2D)
        source = (ROOT / "Core/Src/current_cal_curve.c").read_text(encoding="utf-8")
        self.assertIn("u8 serialized[52]", source)
        self.assertIn("current_cal_pwm_logical_max()", source)

    def test_interpolation_boundaries(self) -> None:
        curve = [i * 10 for i in range(21)]
        expected = {0: 0, 1: 2, 4: 8, 5: 10, 6: 12, 52: 104, 99: 198, 100: 200}
        self.assertEqual({point: interpolate(curve, point) for point in expected}, expected)

    def test_stability_requires_two_windows(self) -> None:
        stable, metrics = stable_samples([100.0] * 24)
        self.assertTrue(stable)
        self.assertEqual(metrics["window_mean_drift_ma"], 0.0)
        self.assertEqual(metrics["window_mean_drift_limit_ma"], 5.0)

        shifted, metrics = stable_samples([100.0] * 12 + [110.0] * 12)
        self.assertFalse(shifted)
        self.assertEqual(metrics["window_mean_drift_ma"], 10.0)
        self.assertEqual(metrics["window_mean_drift_limit_ma"], 5.0)

        boundary, metrics = stable_samples([100.0] * 12 + [105.0] * 12)
        self.assertTrue(boundary)
        self.assertEqual(metrics["window_mean_drift_ma"], metrics["window_mean_drift_limit_ma"])

        beyond_boundary, _ = stable_samples([100.0] * 12 + [105.01] * 12)
        self.assertFalse(beyond_boundary)

        unstable, _ = stable_samples([100.0] * 12 + list(range(100, 124)))
        self.assertFalse(unstable)

    def test_mqtt_calibration_status_omits_internal_measurements(self) -> None:
        builder = (ROOT / "Core/Src/LampProtocolLib/zk_calibration_property.c").read_text(
            encoding="utf-8"
        )
        forbidden = (
            "measurementValid", "adcVoltageRaw", "adcCurrentRaw", "adcVoltageMv",
            "adcCurrentMv", "voltage01V", "currentMa", "power01W",
            "temperature01C", "sampleAgeMs",
        )
        for field in forbidden:
            self.assertNotIn(f'"{field}"', builder)

        station = (ROOT / "tools/current_calibration/calibration_station.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("measurementValid", station)
        self.assertIn('status.get("outputEnabled"', station)
        self.assertIn('status.get("logicalPwm"', station)
        self.assertIn('status.get("protectCode"', station)

    def test_curve_chunk_length_is_checked_before_u8_conversion(self) -> None:
        source = (ROOT / "Core/Src/LampProtocolLib/zk_calibration_property.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("int value_count;", source)
        self.assertNotIn("(u8)cJSON_GetArraySize", source)
        count_position = source.index("cJSON_GetArraySize(values_node)")
        validation_position = source.index("value_count < 1 || value_count > 7", count_position)
        conversion_position = source.index("values, (u8)value_count", validation_position)
        self.assertLess(count_position, validation_position)
        self.assertLess(validation_position, conversion_position)

        station = (ROOT / "tools/current_calibration/calibration_station.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("((3, 0), (4, 8), (5, 256), (6, 257))", station)
        self.assertIn('"seq": 10, "startIndex": 0, "values": [0]', station)
        self.assertIn('"seq": 11, "startIndex": 1, "values": list(range(1, 8))', station)

    def test_calibration_lock_guards_normal_control_paths(self) -> None:
        mqtt = (ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c").read_text(
            encoding="utf-8"
        )
        handler = mqtt[mqtt.index("boolean_en zk_handle_control_message"):]
        patrol_position = handler.index('strcmp(do_node->valuestring, "patrol")')
        guard_position = handler.index("current_calibration_is_active() == BOOL_TRUE")
        reboot_position = handler.index('strcmp(do_node->valuestring, "reboot")')
        self.assertLess(patrol_position, guard_position)
        self.assertLess(guard_position, reboot_position)
        self.assertIn("zk_publish_simple_response(header, 12);", handler[guard_position:reboot_position])

        plan = (ROOT / "Core/Src/LampProtocolLib/zk_work_plan.c").read_text(encoding="utf-8")
        process = plan[plan.index("void zk_work_plan_process(void)"):]
        self.assertLess(process.index("current_calibration_is_active()"),
                        process.index("zk_plan_ensure_loaded()"))
        active_branch = process[process.index("current_calibration_is_active()"):
                                process.index("zk_plan_ensure_loaded()")]
        self.assertNotIn("zk_last_exec_", active_branch)
        self.assertIn("zk_plan_calibration_was_active = BOOL_TRUE", active_branch)
        self.assertIn("zk_plan_calibration_skip_active = BOOL_TRUE", process)

        offline = (ROOT / "Core/Src/offline_Time_controlled_dimming.c").read_text(
            encoding="utf-8"
        )
        process = offline[offline.index("void Work_offline_dimming_process(void)"):]
        self.assertLess(process.index("current_calibration_is_active()"),
                        process.index("DAY_LOOP_EN"))
        self.assertIn("offline_calibration_skip_minute = BOOL_TRUE", process)

        pwm = (ROOT / "Core/Src/sys_pwm.c").read_text(encoding="utf-8")
        apply_normal = pwm[pwm.index("static void sys_pwm_apply_normal"):pwm.index("void pwm_output")]
        self.assertLess(apply_normal.index("calibration_lock_active"),
                        apply_normal.index("set_percent ="))
        direct_output = pwm[pwm.index("void pwm_output"):pwm.index("void sys_pwm_timer")]
        self.assertLess(direct_output.index("calibration_lock_active"),
                        direct_output.index("power_old ="))
        fade_output = pwm[pwm.index("void sys_pwm_fade_output"):pwm.index("static void sys_pwm_output_from")]
        self.assertLess(fade_output.index("calibration_lock_active"),
                        fade_output.index("power_current ="))
        output_from = pwm[pwm.index("static void sys_pwm_output_from"):pwm.index("void sys_pwm_output(")]
        self.assertLess(output_from.index("calibration_lock_active"),
                        output_from.index("normal_source ="))

    def test_flash_layout_is_256k_safe(self) -> None:
        text = (ROOT / "Core/Src/flash_address_assignment.h").read_text(encoding="utf-8")
        self.assertIn("CURRENT_CAL_FLASH_SLOT_A_ADDR", text)
        self.assertIn("CURRENT_CAL_FLASH_SLOT_B_ADDR", text)
        self.assertIn("0x08008000UL", text)
        self.assertNotIn("0x08040000", text)

    def test_net_dim_has_no_hardware_pwm_bypass(self) -> None:
        text = (ROOT / "Core/Src/gateway/net_dim.c").read_text(encoding="utf-8")
        self.assertNotIn("oco_on(", text)
        self.assertNotIn("oco_off(", text)
        self.assertNotIn("hw_tim1_pwm2_set_PWM_OUT", text)
        self.assertIn("sys_pwm_output_network", text)

    def test_chunk_payload_fits_json_limit(self) -> None:
        payload = {
            "SN": "864512081541939", "TM": "2026-07-14 12:00:00", "SV": "prop",
            "ID": "123456", "CT": "W", "DT": {"Calibration": {
                "action": "writeCurveChunk", "sessionId": "CAL-20260714-0001", "seq": 50,
                "curveVersion": 1, "profileCrc": 0xFFFFFFFF, "curveCrc": 0xFFFFFFFF,
                "startIndex": 14, "values": [65535] * 7,
            }},
        }
        self.assertLess(len(json.dumps(payload, separators=(",", ":")).encode()), 2048)

    def test_flash_blank_single_and_latest_selection(self) -> None:
        blank = b"\xff" * FLASH_RECORD_SIZE
        self.assertIsNone(select_flash_slot(blank, blank))
        self.assertEqual(select_flash_slot(pack_flash_record(1), blank), ("A", 1, FLASH_TYPE_CURVE))
        self.assertEqual(select_flash_slot(pack_flash_record(1), pack_flash_record(2)),
                         ("B", 2, FLASH_TYPE_CURVE))

    def test_flash_torn_or_corrupt_new_slot_keeps_old_slot(self) -> None:
        old = pack_flash_record(7)
        staged_without_marker = pack_flash_record(8, marker=0xFFFFFFFF)
        self.assertEqual(select_flash_slot(old, staged_without_marker),
                         ("A", 7, FLASH_TYPE_CURVE))
        corrupt = bytearray(pack_flash_record(8))
        corrupt[36] ^= 0x01
        self.assertEqual(select_flash_slot(old, bytes(corrupt)),
                         ("A", 7, FLASH_TYPE_CURVE))

    def test_flash_tombstone_wins_and_disables_curve(self) -> None:
        selected = select_flash_slot(pack_flash_record(40),
                                     pack_flash_record(41, FLASH_TYPE_TOMBSTONE))
        self.assertEqual(selected, ("B", 41, FLASH_TYPE_TOMBSTONE))

    def test_flash_fifty_alternating_commits(self) -> None:
        blank = b"\xff" * FLASH_RECORD_SIZE
        slots = {"A": blank, "B": blank}
        active = "B"
        for sequence in range(1, 51):
            active = "A" if active == "B" else "B"
            slots[active] = pack_flash_record(sequence)
            selected = select_flash_slot(slots["A"], slots["B"])
            self.assertEqual(selected, (active, sequence, FLASH_TYPE_CURVE))

    def test_flash_sequence_wrap_selects_one_as_newer(self) -> None:
        selected = select_flash_slot(pack_flash_record(0xFFFFFFFF), pack_flash_record(1))
        self.assertEqual(selected, ("B", 1, FLASH_TYPE_CURVE))

    def test_storage_source_commits_valid_marker_last(self) -> None:
        text = (ROOT / "Core/Src/current_cal_storage.c").read_text(encoding="utf-8")
        if "staged_record.valid_marker = 0xffffffffUL" in text:
            stage_pos = text.index("staged_record.valid_marker = 0xffffffffUL")
        else:
            stage_pos = text.index(
                "current_cal_put_u32_le(staged + V2_OFF_VALID_MARKER, 0xffffffffUL)"
            )
        page_write_pos = text.index("hw_flash_update_bytes_checked", stage_pos)
        marker_write_pos = text.index("hw_flash_program_bytes_checked", page_write_pos)
        self.assertLess(stage_pos, page_write_pos)
        self.assertLess(page_write_pos, marker_write_pos)


if __name__ == "__main__":
    unittest.main(verbosity=2)
