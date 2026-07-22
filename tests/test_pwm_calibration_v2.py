from __future__ import annotations

import struct
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CURVE_SOURCE = ROOT / "Core/Src/current_cal_curve.c"
STORAGE_SOURCE = ROOT / "Core/Src/current_cal_storage.c"
PWM_SOURCE = ROOT / "Core/Src/sys_pwm.c"
PROPERTY_SOURCE = ROOT / "Core/Src/LampProtocolLib/zk_calibration_property.c"
ZK_PROPERTY_SOURCE = ROOT / "Core/Src/LampProtocolLib/zk_property.c"
APP_SOURCE = ROOT / "Core/Src/LampProtocolLib/App.c"

POINTS = (0, 56, 80, 110, 138, 164, 195, 220, 248, 280, 306,
          335, 367, 392, 420, 448, 476, 504, 532, 560, 588)
CAL_MAX = 890
HW_MAX = 1680
V2_SIZE = 240
MARKER = 0x56414C43
SYS_DATA_SIZE = 408
SYS_DATA_FACTORY_OFFSET = 24
FACTORY_BUFFER_SIZE = 128


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def curve_crc(cal_max: int, points: tuple[int, ...] = POINTS) -> int:
    return crc32(struct.pack("<HHI21H", 2, 21, cal_max, *points))


def interpolate_setpoint(percent: int, set_current: int, cal_max: int = CAL_MAX) -> int:
    if percent <= 0 or set_current <= 0:
        return 0
    numerator = percent * set_current
    denominator = 100 * cal_max
    if numerator >= denominator:
        return POINTS[-1]
    position_numerator = numerator * 20
    index, remainder = divmod(position_numerator, denominator)
    delta = POINTS[index + 1] - POINTS[index]
    return POINTS[index] + (remainder * delta + denominator // 2) // denominator


def context_crc(set_current: int) -> int:
    # SET is accepted only to prove that it is not serialized.
    del set_current
    fields = (2, 1, 7, 3, HW_MAX, 100, 412, 71, 999, 2, 1, 588, 2)
    return crc32(struct.pack("<13I", *fields))


def legacy_profile_crc(set_current: int) -> int:
    fields = (1, 1, 7, 3, set_current, HW_MAX, 100, 412,
              71, 999, 2, 1, 588)
    return crc32(struct.pack("<13I", *fields))


def sequence_newer(lhs: int, rhs: int) -> bool:
    difference = (lhs - rhs) & 0xFFFFFFFF
    signed = difference if difference < 0x80000000 else difference - 0x100000000
    return signed > 0


def select_equal_sequence(record_a: bytes, record_b: bytes) -> bytes | None:
    return record_a if record_a == record_b else None


class LegacySetterModel:
    def __init__(self, set_current: int = CAL_MAX, hwmax: int = HW_MAX) -> None:
        self.buffer = bytearray(128)
        struct.pack_into(">H", self.buffer, 0x10, set_current)
        struct.pack_into(">H", self.buffer, 0x12, hwmax)
        self.persisted = bytes(self.buffer)
        self.legacy_active = True
        self.v2_active = False
        self.tombstoned = False
        self.output_events: list[str] = []

    def set_current(self, value: int, migrate_ok: bool) -> bool:
        self.output_events.append("force_off")
        if self.legacy_active:
            if not migrate_ok:
                return False
            self.legacy_active = False
            self.v2_active = True
        struct.pack_into(">H", self.buffer, 0x10, value)
        self.persisted = bytes(self.buffer)
        self.output_events.append("reload")
        return True

    def set_hwmax(self, value: int, invalidate_ok: bool) -> bool:
        self.output_events.append("force_off")
        if not invalidate_ok:
            return False
        self.tombstoned = True
        self.legacy_active = False
        struct.pack_into(">H", self.buffer, 0x12, value)
        self.persisted = bytes(self.buffer)
        self.output_events.append("reload")
        return True

    def reboot_values(self) -> tuple[int, int]:
        return struct.unpack_from(">HH", self.persisted, 0x10)


class FactoryTransactionModel:
    """Fault model using the firmware's 408-byte sys_data/128-byte factory layout."""

    def __init__(self) -> None:
        self.old = (890, 1680)
        self.old_factory = self._factory(890, 1680)
        self.ram = self._sys_data(self.old_factory)
        self.main = bytes(self.ram)
        self.backup = bytes(self.ram)
        self.curve_active = True
        self.curve_v2 = False
        self.output_on = True
        self.curve_restore_attempted = False
        self.curve_restore_failed = False

    @staticmethod
    def _factory(set_current: int, hwmax: int) -> bytes:
        factory = bytearray(FACTORY_BUFFER_SIZE)
        factory[0x04:0x07] = bytes((1, 7, 3))
        struct.pack_into(">HHHH", factory, 0x10, set_current, hwmax, 100, 412)
        return bytes(factory)

    @staticmethod
    def _sys_data(factory: bytes) -> bytearray:
        assert len(factory) == FACTORY_BUFFER_SIZE
        image = bytearray(SYS_DATA_SIZE)
        struct.pack_into("<6I", image, 0, 0x12345678, 0, 0, 0, 0x80, 0)
        image[SYS_DATA_FACTORY_OFFSET:
              SYS_DATA_FACTORY_OFFSET + FACTORY_BUFFER_SIZE] = factory
        checksum = (0xA5 + sum(image[:-2])) & 0xFFFF
        struct.pack_into(">H", image, SYS_DATA_SIZE - 2, checksum)
        return image

    @staticmethod
    def _valid_sys_data(image: bytes) -> bool:
        if len(image) != SYS_DATA_SIZE:
            return False
        expected = (0xA5 + sum(image[:-2])) & 0xFFFF
        return struct.unpack_from(">H", image, SYS_DATA_SIZE - 2)[0] == expected

    @staticmethod
    def _values(image: bytes) -> tuple[int, int]:
        return struct.unpack_from(">HH", image, SYS_DATA_FACTORY_OFFSET + 0x10)

    @staticmethod
    def _corrupt(image: bytes) -> bytes:
        corrupted = bytearray(image)
        corrupted[-1] ^= 0x5A
        return bytes(corrupted)

    def _checked_store(self, factory: bytes, phase: str,
                       failures: set[str]) -> bool:
        image = bytes(self._sys_data(factory))
        if f"{phase}_main" in failures:
            self.main = self._corrupt(image)
            return False
        self.main = image
        if f"{phase}_backup" in failures:
            self.backup = self._corrupt(image)
            return False
        self.backup = image
        return True

    def _rollback_factory(self, failures: set[str]) -> bool:
        self.ram = self._sys_data(self.old_factory)
        return self._checked_store(self.old_factory, "rollback", failures)

    def ram_values(self) -> tuple[int, int]:
        return self._values(self.ram)

    def reboot_values(self) -> tuple[int, int] | None:
        if self._valid_sys_data(self.main):
            return self._values(self.main)
        if self._valid_sys_data(self.backup):
            return self._values(self.backup)
        return None

    def change_context(self, new_hw: int, failures: set[str]) -> bool:
        self.output_on = False
        if "invalidate" in failures:
            return False
        self.curve_active = False
        candidate = self._factory(self.old[0], new_hw)
        self.ram = self._sys_data(candidate)
        if self._checked_store(candidate, "save", failures):
            self.output_on = True
            return True
        rollback_ok = self._rollback_factory(failures)
        if rollback_ok:
            self.curve_restore_attempted = True
            if "restore_curve" in failures:
                self.curve_restore_failed = True
            else:
                self.curve_active = True
                self.curve_v2 = True
        return False

    def change_set(self, new_set: int, failures: set[str]) -> bool:
        self.output_on = False
        if "migrate" in failures:
            return False
        self.curve_v2 = True
        candidate = self._factory(new_set, self.old[1])
        self.ram = self._sys_data(candidate)
        if not self._checked_store(candidate, "save", failures):
            self._rollback_factory(failures)
            return False
        self.output_on = True
        return True

    def change_context_with_dependent_failure(self, new_hw: int,
                                              failures: set[str]) -> bool:
        """Modern Factory + property packet: later partition rejects commit."""
        self.output_on = False
        self.curve_active = False
        candidate = self._factory(self.old[0], new_hw)
        self.ram = self._sys_data(candidate)
        if not self._checked_store(candidate, "save", failures):
            self._rollback_factory(failures)
            return False
        rollback_ok = self._rollback_factory(failures)
        if rollback_ok:
            self.curve_restore_attempted = True
            if "restore_curve" in failures:
                self.curve_restore_failed = True
            else:
                self.curve_active = True
                self.curve_v2 = True
        return False


def encode_meter(payload: bytes, context: int = 0xAABBCCDD) -> bytes:
    meter = bytearray(148)
    struct.pack_into("<IIIHH", meter, 0, 1, context,
                     crc32(struct.pack("<HH", 1, len(payload)) + payload),
                     1, len(payload))
    meter[16:16 + len(payload)] = payload
    return bytes(meter)


def encode_v2(sequence: int, set_current: int = CAL_MAX,
              meter: bytes | None = None, marker: int = MARKER) -> bytes:
    del set_current
    record = bytearray(V2_SIZE)
    struct.pack_into("<IHHII", record, 0, 0x43414C32, 2, V2_SIZE, sequence, 0)
    struct.pack_into("<IIIHHHHI", record, 16, 1, context_crc(CAL_MAX),
                     curve_crc(CAL_MAX), 1, 2, 21, 0, CAL_MAX)
    struct.pack_into("<21H", record, 40, *POINTS)
    if meter is not None:
        assert len(meter) == 148
        record[84:232] = meter
    struct.pack_into("<I", record, 232, crc32(record[:232]))
    struct.pack_into("<I", record, 236, marker)
    return bytes(record)


def valid_v2(record: bytes) -> bool:
    if len(record) != V2_SIZE:
        return False
    magic, version, size = struct.unpack_from("<IHH", record, 0)
    stored_crc, marker = struct.unpack_from("<II", record, 232)
    return (magic, version, size, marker) == (0x43414C32, 2, V2_SIZE, MARKER) \
        and stored_crc == crc32(record[:232])


def encode_v1(sequence: int, set_current: int = CAL_MAX) -> bytes:
    record = bytearray(80)
    legacy_curve_crc = crc32(struct.pack("<HH21H", 1, 21, *POINTS))
    struct.pack_into("<IHHIIIIHH21HH", record, 0, 0x43414C31, 1, 80,
                     sequence, 1, legacy_profile_crc(set_current), legacy_curve_crc,
                     1, 21, *POINTS, 0)
    struct.pack_into("<I", record, 72, crc32(record[:72]))
    struct.pack_into("<I", record, 76, MARKER)
    return bytes(record)


def normalize_v1(record: bytes, startup_set: int) -> dict[str, object] | None:
    if len(record) != 80 or struct.unpack_from("<I", record, 76)[0] != MARKER:
        return None
    if struct.unpack_from("<I", record, 72)[0] != crc32(record[:72]):
        return None
    if struct.unpack_from("<I", record, 16)[0] != legacy_profile_crc(startup_set):
        return None
    points = struct.unpack_from("<21H", record, 28)
    return {
        "curve_version": 2,
        "calibration_max_current_ma": startup_set,
        "context_crc": context_crc(startup_set),
        "curve_crc": curve_crc(startup_set, points),
        "points": points,
    }


class PwmCalibrationV2Tests(unittest.TestCase):
    def test_full_rated_current_uses_last_point(self) -> None:
        self.assertEqual(interpolate_setpoint(100, 890), 588)
        self.assertEqual(interpolate_setpoint(0, 890), 0)

    def test_derated_536ma_uses_continuous_60_22_percent_position(self) -> None:
        self.assertAlmostEqual(536 / 890 * 100, 60.224719, places=5)
        self.assertEqual(interpolate_setpoint(100, 536), 368)
        # This differs from prematurely rounding to a 60% curve lookup.
        self.assertEqual(POINTS[12], 367)

    def test_current_range_boundaries(self) -> None:
        def valid(set_current: int, cal_max: int, hw_max: int) -> bool:
            return 0 < set_current <= cal_max <= hw_max

        self.assertTrue(valid(1, 1, 1))
        self.assertTrue(valid(536, CAL_MAX, HW_MAX))
        self.assertFalse(valid(0, CAL_MAX, HW_MAX))
        self.assertFalse(valid(891, CAL_MAX, HW_MAX))
        self.assertFalse(valid(890, 0, HW_MAX))
        self.assertFalse(valid(890, 1700, HW_MAX))

    def test_set_change_does_not_change_context_but_changes_legacy_profile(self) -> None:
        self.assertEqual(context_crc(890), context_crc(536))
        self.assertNotEqual(legacy_profile_crc(890), legacy_profile_crc(536))

    def test_curve_crc_covers_calibration_max_current(self) -> None:
        self.assertNotEqual(curve_crc(890), curve_crc(889))
        self.assertEqual(curve_crc(890), 0x16B912D1)

    def test_v1_migration_is_ram_only_and_binds_startup_set_as_cal_max(self) -> None:
        flash = encode_v1(7, 890)
        before = bytes(flash)
        normalized = normalize_v1(flash, 890)
        self.assertIsNotNone(normalized)
        self.assertEqual(normalized["curve_version"], 2)
        self.assertEqual(normalized["calibration_max_current_ma"], 890)
        self.assertEqual(flash, before)
        self.assertIsNone(normalize_v1(flash, 536))

    def test_pwm_update_copy_forwards_meter_section_exactly(self) -> None:
        meter = encode_meter(b"meter-v1-coefficients")
        previous = encode_v2(10, meter=meter)
        updated = bytearray(encode_v2(11, meter=previous[84:232]))
        self.assertTrue(valid_v2(updated))
        self.assertEqual(updated[84:232], meter)

    def test_torn_write_marker_or_payload_is_rejected(self) -> None:
        staged = encode_v2(2, marker=0xFFFFFFFF)
        self.assertFalse(valid_v2(staged))
        corrupt = bytearray(encode_v2(2))
        corrupt[48] ^= 1
        self.assertFalse(valid_v2(corrupt))

    def test_sequence_wrap_selects_one_over_ffffffff_and_skips_zero_on_write(self) -> None:
        self.assertTrue(sequence_newer(1, 0xFFFFFFFF))
        self.assertFalse(sequence_newer(0xFFFFFFFF, 1))
        next_sequence = (0xFFFFFFFF + 1) & 0xFFFFFFFF
        if next_sequence == 0:
            next_sequence = 1
        self.assertEqual(next_sequence, 1)

    def test_equal_sequence_requires_byte_identical_records(self) -> None:
        curve = encode_v2(9)
        self.assertEqual(select_equal_sequence(curve, bytes(curve)), curve)
        tombstone = bytearray(curve)
        struct.pack_into("<I", tombstone, 16, 2)
        struct.pack_into("<I", tombstone, 232, crc32(tombstone[:232]))
        self.assertIsNone(select_equal_sequence(curve, bytes(tombstone)))

    def test_legacy_setter_migrates_before_persist_and_survives_reboot(self) -> None:
        device = LegacySetterModel()
        self.assertTrue(device.set_current(536, migrate_ok=True))
        self.assertTrue(device.v2_active)
        self.assertEqual(device.reboot_values(), (536, HW_MAX))
        self.assertEqual(device.output_events, ["force_off", "reload"])

    def test_legacy_migration_failure_keeps_set_and_persistent_buffer_unchanged(self) -> None:
        device = LegacySetterModel()
        before = device.persisted
        self.assertFalse(device.set_current(536, migrate_ok=False))
        self.assertEqual(device.persisted, before)
        self.assertEqual(device.reboot_values(), (CAL_MAX, HW_MAX))
        self.assertTrue(device.legacy_active)
        self.assertEqual(device.output_events, ["force_off"])

    def test_hwmax_failure_does_not_persist_value_or_tombstone(self) -> None:
        device = LegacySetterModel()
        before = device.persisted
        self.assertFalse(device.set_hwmax(1700, invalidate_ok=False))
        self.assertEqual(device.persisted, before)
        self.assertFalse(device.tombstoned)
        self.assertEqual(device.output_events, ["force_off"])

    def test_factory_context_transaction_fault_matrix_is_fail_safe(self) -> None:
        for failure in (
            "invalidate", "save_main", "save_backup", "rollback_main",
            "rollback_backup", "restore_curve",
        ):
            with self.subTest(failure=failure):
                failures = {failure}
                if failure.startswith("rollback_") or failure == "restore_curve":
                    failures.add("save_backup")
                device = FactoryTransactionModel()
                self.assertFalse(device.change_context(1700, failures))
                self.assertFalse(device.output_on)
                self.assertEqual(device.ram_values(), device.old)
                if failure == "invalidate":
                    self.assertEqual(device.reboot_values(), device.old)
                    self.assertTrue(device.curve_active)
                    self.assertFalse(device.curve_restore_attempted)
                elif failure in {"save_main", "save_backup"}:
                    self.assertEqual(device.reboot_values(), device.old)
                    self.assertTrue(device._valid_sys_data(device.main))
                    self.assertTrue(device._valid_sys_data(device.backup))
                    self.assertTrue(device.curve_active)
                    self.assertTrue(device.curve_v2)
                    self.assertTrue(device.curve_restore_attempted)
                    self.assertFalse(device.curve_restore_failed)
                elif failure == "rollback_main":
                    self.assertIsNone(device.reboot_values())
                    self.assertFalse(device.curve_active)
                    self.assertFalse(device.curve_restore_attempted)
                elif failure == "rollback_backup":
                    self.assertEqual(device.reboot_values(), device.old)
                    self.assertFalse(device.curve_active)
                    self.assertFalse(device.curve_restore_attempted)
                else:
                    self.assertFalse(device.curve_active)
                    self.assertEqual(device.reboot_values(), device.old)
                    self.assertTrue(device.curve_restore_attempted)
                    self.assertTrue(device.curve_restore_failed)

    def test_set_only_save_failure_keeps_old_set_and_migrated_v2_curve(self) -> None:
        for failure in ("migrate", "save_main", "save_backup",
                        "rollback_main", "rollback_backup"):
            with self.subTest(failure=failure):
                failures = {failure}
                if failure.startswith("rollback_"):
                    failures.add("save_backup")
                device = FactoryTransactionModel()
                self.assertFalse(device.change_set(536, failures))
                self.assertEqual(device.ram_values(), device.old)
                self.assertTrue(device.curve_active)
                self.assertEqual(device.curve_v2, failure != "migrate")
                self.assertFalse(device.output_on)
                if failure == "rollback_main":
                    self.assertIsNone(device.reboot_values())
                else:
                    self.assertEqual(device.reboot_values(), device.old)

    def test_checked_factory_success_persists_both_copies_and_reloads(self) -> None:
        device = FactoryTransactionModel()
        self.assertTrue(device.change_set(536, set()))
        self.assertEqual(device.ram_values(), (536, HW_MAX))
        self.assertEqual(device.reboot_values(), (536, HW_MAX))
        self.assertTrue(device._valid_sys_data(device.main))
        self.assertTrue(device._valid_sys_data(device.backup))
        self.assertTrue(device.output_on)

    def test_mixed_factory_then_property_failure_rolls_back_and_stays_off(self) -> None:
        device = FactoryTransactionModel()
        self.assertFalse(device.change_context_with_dependent_failure(1700, set()))
        self.assertFalse(device.output_on)
        self.assertEqual(device.ram_values(), device.old)
        self.assertEqual(device.reboot_values(), device.old)
        self.assertTrue(device.curve_restore_attempted)
        self.assertTrue(device.curve_active)
        self.assertTrue(device.curve_v2)

        for failure in ("rollback_main", "rollback_backup", "restore_curve"):
            with self.subTest(failure=failure):
                broken = FactoryTransactionModel()
                self.assertFalse(broken.change_context_with_dependent_failure(
                    1700, {failure}
                ))
                self.assertFalse(broken.output_on)
                self.assertEqual(broken.ram_values(), broken.old)
                self.assertFalse(broken.curve_active)

    def test_fixed_layout_and_protocol_contract_are_present_in_firmware(self) -> None:
        curve = CURVE_SOURCE.read_text(encoding="utf-8")
        storage = STORAGE_SOURCE.read_text(encoding="utf-8")
        pwm = PWM_SOURCE.read_text(encoding="utf-8")
        prop = PROPERTY_SOURCE.read_text(encoding="utf-8")
        zk_prop = ZK_PROPERTY_SOURCE.read_text(encoding="utf-8")
        app = APP_SOURCE.read_text(encoding="utf-8")

        self.assertIn("CURRENT_CAL_V2_RECORD_SIZE       240U", storage)
        self.assertIn("V2_OFF_RECORD_CRC                232U", storage)
        self.assertIn("V2_OFF_VALID_MARKER               236U", storage)
        self.assertIn("Boot is read-only", storage)
        self.assertIn("current_cal_legacy_profile_crc", curve)
        self.assertNotIn("current_cal_put_u32_le(out, SET_OUTCUR); out += 4;\n    current_cal_put_u32_le(out, HWMAX_OUTCUR)",
                         curve.split("u32 current_cal_context_crc(void)", 1)[1])
        self.assertIn("current_cal_curve_interpolate_setpoint", pwm)
        self.assertIn('"calibrationMaxCurrentMa"', prop)
        self.assertIn('"contextCrc"', prop)
        self.assertIn('"legacyProfileCrc"', prop)
        self.assertIn('"requiredCurveVersion"', prop)

        set_setter = app.split("u8 onSet_setcur_value", 1)[1].split(
            "u8 onSet_fa_test_value", 1
        )[0]
        self.assertIn("app_factory_buf_set_u16be(candidate, 0x10U", set_setter)
        self.assertIn("current_cal_storage_ensure_v2()", set_setter)
        self.assertLess(set_setter.index("sys_pwm_force_off()"),
                        set_setter.index("current_cal_storage_ensure_v2()"))
        self.assertLess(set_setter.index("current_cal_storage_ensure_v2()"),
                        set_setter.index(
                            "app_factory_commit_buffer(candidate, old_factory_buf"
                        ))
        self.assertIn("sys_pwm_release_and_reload()", set_setter)

        hw_setter = app.split("u8 onSethwmax_outcur_value", 1)[1].split(
            "u8 onSet_setcur_value", 1
        )[0]
        self.assertIn("app_factory_buf_set_u16be(candidate, 0x12U", hw_setter)
        self.assertLess(hw_setter.index("current_cal_storage_invalidate()"),
                        hw_setter.index(
                            "app_factory_commit_buffer(candidate, old_factory_buf"
                        ))
        self.assertIn("sys_pwm_release_and_reload()", hw_setter)
        self.assertIn("sequence_conflict = BOOL_TRUE", storage)

        force_off = pwm.split("void sys_pwm_force_off(void)", 1)[1].split(
            "void sys_pwm_get_status", 1
        )[0]
        self.assertLess(force_off.index("force_off_latched = BOOL_TRUE"),
                        force_off.index("reload = 0U"))
        self.assertLess(force_off.index("reload = 0U"),
                        force_off.index("hw_tim1_pwm2_set_PWM_OUT(0U)"))
        self.assertIn("pwm_fade_active = BOOL_FALSE", force_off)
        self.assertIn("pwm_fade_timer = TIMEOUT_MAX", force_off)
        self.assertIn("power_new = 0U", force_off)
        self.assertIn("force_off_latched == BOOL_TRUE", pwm.split(
            "void sys_pwm_process(void)", 1
        )[1].split("void sys_pwm_calibration_lock", 1)[0])
        internal_pwm = pwm.split("void pwm_output(u8 percent)", 1)[1].split(
            "void sys_pwm_timer", 1
        )[0]
        internal_fade = pwm.split("void sys_pwm_fade_output", 1)[1].split(
            "static void sys_pwm_output_from", 1
        )[0]
        external_output = pwm.split("static void sys_pwm_output_from", 1)[1].split(
            "void sys_pwm_output(u8 percent)", 1
        )[0]
        self.assertIn("force_off_latched == BOOL_TRUE", internal_pwm)
        self.assertNotIn("force_off_latched = BOOL_FALSE", internal_pwm)
        self.assertIn("force_off_latched == BOOL_TRUE", internal_fade)
        self.assertNotIn("force_off_latched = BOOL_FALSE", internal_fade)
        self.assertIn("force_off_latched = BOOL_FALSE", external_output)

        picker = zk_prop.split("static boolean_en zk_json_pick_config_number", 1)[1].split(
            "static boolean_en zk_json_pick_config_string", 1
        )[0]
        self.assertNotIn("*value = node->valueint", picker)
        self.assertIn("node->valuedouble != node->valuedouble", picker)
        self.assertIn("(double)*value != node->valuedouble", picker)
        writer = zk_prop.split("static boolean_en zk_property_flash_write_record", 1)[1].split(
            "static boolean_en zk_property_flash_load", 1
        )[0]
        self.assertIn("hw_flash_update_bytes_checked", writer)
        self.assertNotIn("hw_flash_write_bytes", writer)

        modern = zk_prop.split("boolean_en zk_handle_property_write", 1)[1]
        self.assertLess(modern.index("zk_property_flash_store_config(&candidate)"),
                        modern.index("sys_pwm_release_and_reload()"))
        property_failure = modern.split(
            "if (zk_property_flash_store_config(&candidate) == BOOL_FALSE)", 1
        )[1].split("zk_dev_cfg = candidate", 1)[0]
        self.assertIn("zk_factory_restore_checked(old_factory_buf)", property_failure)
        self.assertNotIn("sys_pwm_release_and_reload()", property_failure)


if __name__ == "__main__":
    unittest.main(verbosity=2)
