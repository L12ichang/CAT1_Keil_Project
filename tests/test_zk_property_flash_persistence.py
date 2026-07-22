#!/usr/bin/env python3

from pathlib import Path
import math
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROPERTY_SOURCE = ROOT / "Core/Src/LampProtocolLib/zk_property.c"
MQTT_SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
RUNTIME_SOURCE = ROOT / "Core/Src/LampProtocolLib/zk_runtime_stats.c"
STORAGE_SOURCE = ROOT / "Core/Src/current_cal_storage.c"
FLASH_SOURCE = ROOT / "Core/Src/hw_flash.c"
SYS_DATA = ROOT / "Core/Src/sys_data.h"


def read_property_source() -> str:
    return PROPERTY_SOURCE.read_text(encoding="utf-8", errors="ignore")


def read_mqtt_source() -> str:
    return MQTT_SOURCE.read_text(encoding="utf-8", errors="ignore")


FLASH_PAGE_SIZE = 0x800
CALIBRATION_SLOT_OFFSET = 0x400
CALIBRATION_SLOT_SIZE = 0x100


def checked_page_update(page: bytearray, offset: int, payload: bytes,
                        fail_program_offset: int | None = None) -> bool:
    """Model hw_flash_update_bytes_checked's full-page RMW and readback."""
    expected = bytearray(page)
    expected[offset:offset + len(payload)] = payload
    programmed = bytearray(b"\xff" * FLASH_PAGE_SIZE)
    for word_offset in range(0, FLASH_PAGE_SIZE, 4):
        if fail_program_offset == word_offset:
            page[:] = programmed
            return False
        programmed[word_offset:word_offset + 4] = expected[word_offset:word_offset + 4]
    page[:] = programmed
    return page == expected


def property_sequence_compare(lhs: int, rhs: int) -> int | None:
    difference = (lhs - rhs) & 0xFFFFFFFF
    if difference == 0:
        return 0
    if difference == 0x80000000:
        return None
    return 1 if difference < 0x80000000 else -1


def select_property_record(main: tuple[int, bytes],
                           backup: tuple[int, bytes]) -> tuple[int, bytes] | None:
    order = property_sequence_compare(main[0], backup[0])
    if order == 0:
        return main if main == backup else None
    if order == 1:
        return main
    if order == -1:
        return backup
    return None


def calibration_blob(sequence: int, seed: int) -> bytes:
    body = struct.pack("<I", sequence) + bytes(
        (index * 37 + seed) & 0xFF for index in range(CALIBRATION_SLOT_SIZE - 8)
    )
    checksum = sum(body) & 0xFFFFFFFF
    return body + struct.pack("<I", checksum)


def calibration_valid(blob: bytes) -> bool:
    return (len(blob) == CALIBRATION_SLOT_SIZE and
            struct.unpack_from("<I", blob, CALIBRATION_SLOT_SIZE - 4)[0] ==
            sum(blob[:-4]) & 0xFFFFFFFF)


def reboot_latest_calibration(pages: list[bytearray]) -> bytes | None:
    records = [bytes(page[CALIBRATION_SLOT_OFFSET:
                         CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE])
               for page in pages]
    valid = [record for record in records if calibration_valid(record)]
    if not valid:
        return None
    if len(valid) == 1 or valid[0] == valid[1]:
        return valid[0]
    order = property_sequence_compare(
        struct.unpack_from("<I", valid[0])[0],
        struct.unpack_from("<I", valid[1])[0],
    )
    if order == 1:
        return valid[0]
    if order == -1:
        return valid[1]
    return None


def coordinated_shared_store(pages: list[bytearray], active_page: int,
                             offset: int, payload: bytes,
                             fail_page: int | None = None,
                             restore_original: bool = False) -> bool:
    """Model mirror-before-erase plus stop-on-first-failure."""
    active = bytes(pages[active_page][CALIBRATION_SLOT_OFFSET:
                                      CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE])
    assert calibration_valid(active)
    mirror_page = 1 - active_page
    pages[mirror_page][CALIBRATION_SLOT_OFFSET:
                       CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = active
    for page_index in range(2):
        before = bytes(pages[page_index])
        expected = bytearray(before)
        expected[offset:offset + len(payload)] = payload
        if page_index == fail_page:
            # A late program fault has reached and damaged the calibration
            # half of the page. Recovery, when possible, uses pre-erase bytes.
            pages[page_index][:] = b"\xff" * FLASH_PAGE_SIZE
            pages[page_index][:CALIBRATION_SLOT_OFFSET + 0x80] = \
                expected[:CALIBRATION_SLOT_OFFSET + 0x80]
            if restore_original:
                pages[page_index][:] = before
            return False
        pages[page_index][:] = expected
    return True


class ZkPropertyFlashPersistenceTests(unittest.TestCase):
    def test_property_record_uses_existing_parameter_partition_pages(self):
        source = read_property_source()
        sys_data = SYS_DATA.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#define ZK_PROPERTY_FLASH_MAIN_ADDR (DATAROM_STARTADDR + FLASH_PAGE_SIZE)", source)
        self.assertIn("#define ZK_PROPERTY_FLASH_BACKUP_ADDR (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE)", source)
        self.assertIn("#define   DATAROM_STARTADDR               (u32)0x8005000", sys_data)
        self.assertIn("#define   BAKDATAROM_STARTADDR            (u32)0x8006800", sys_data)
        self.assertIn("#define SYS_DATA_ST_EXPECTED_SIZE         408", sys_data)

    def test_property_write_saves_flash_before_ram_commit(self):
        source = read_property_source()
        handler = source[
            source.index("boolean_en zk_handle_property_write"):
            len(source)
        ]

        self.assertIn("candidate = zk_dev_cfg;", handler)
        self.assertIn("zk_property_flash_store_config(&candidate)", handler)
        self.assertIn("zk_dev_cfg = candidate;", handler)
        self.assertIn("zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);", handler)
        self.assertLess(
            handler.index("zk_property_flash_store_config(&candidate)"),
            handler.index("zk_dev_cfg = candidate;"),
        )

    def test_property_write_uses_checked_full_page_rmw_and_requires_both_copies(self):
        source = read_property_source()
        writer = source[
            source.index("static boolean_en zk_property_flash_write_record"):
            source.index("static boolean_en zk_property_flash_load")
        ]
        store = source[
            source.index("static boolean_en zk_property_flash_store_config"):
            source.index("static void zk_device_config_refresh_iccid_field")
        ]

        self.assertIn("hw_flash_update_bytes_checked", writer)
        self.assertNotIn("hw_flash_write_bytes", writer)
        self.assertIn("main_ok == BOOL_TRUE && backup_ok == BOOL_TRUE", store)
        self.assertIn("zk_property_record_from_config(&record, &zk_dev_cfg, rollback_seq)", store)
        self.assertIn("return BOOL_FALSE", store)

    def test_property_sequence_selection_is_wrap_safe_and_rejects_conflicts(self):
        old_config = b"old-config"
        failed_candidate = b"failed-candidate"

        # Starting at FFFFFFFE, candidate FFFFFFFF reached only main; rollback
        # wraps to 1 and reaches backup. The rollback is the newer record.
        self.assertEqual(
            select_property_record(
                (0xFFFFFFFF, failed_candidate), (1, old_config)
            ),
            (1, old_config),
        )
        self.assertEqual(
            select_property_record((1, b"new"), (0xFFFFFFFF, b"old")),
            (1, b"new"),
        )
        self.assertEqual(
            select_property_record((0xFFFFFFFF, b"old"), (1, b"new")),
            (1, b"new"),
        )
        self.assertEqual(
            select_property_record((7, b"same"), (7, b"same")),
            (7, b"same"),
        )
        self.assertIsNone(select_property_record((7, b"a"), (7, b"b")))
        self.assertIsNone(select_property_record((0, b"a"), (0x80000000, b"b")))

        source = read_property_source()
        loader = source[
            source.index("static int zk_property_seq_compare"):
            source.index("static boolean_en zk_property_flash_store_config")
        ]
        self.assertIn("difference == 0x80000000UL", loader)
        self.assertIn("difference < 0x80000000UL", loader)
        self.assertIn("memcmp(&main_record, &backup_record", loader)
        self.assertNotIn("main_record.seq >= backup_record.seq", loader)

    def test_property_rmw_preserves_calibration_slot_byte_for_byte(self):
        page = bytearray((index * 29 + 7) & 0xFF for index in range(FLASH_PAGE_SIZE))
        before_slot = bytes(page[
            CALIBRATION_SLOT_OFFSET:
            CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE
        ])
        property_record = bytes((index * 11 + 3) & 0xFF for index in range(0x180))

        self.assertTrue(checked_page_update(page, 0, property_record))
        self.assertEqual(page[:len(property_record)], property_record)
        self.assertEqual(
            bytes(page[CALIBRATION_SLOT_OFFSET:
                       CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE]),
            before_slot,
        )

    def test_every_non_calibration_shared_writer_preserves_latest_slot(self):
        writers = {
            "property": (0x000, bytes(range(64))),
            "runtime": (0x200, bytes((index + 3) & 0xFF for index in range(28))),
            "ota": (0x300, bytes((index + 9) & 0xFF for index in range(52))),
        }
        latest = calibration_blob(0x12345678, 11)
        for active_page in (0, 1):
            for name, (offset, payload) in writers.items():
                with self.subTest(active_page=active_page, writer=name):
                    pages = [bytearray(b"\xa5" * FLASH_PAGE_SIZE),
                             bytearray(b"\x5a" * FLASH_PAGE_SIZE)]
                    pages[active_page][CALIBRATION_SLOT_OFFSET:
                                       CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = latest
                    self.assertTrue(coordinated_shared_store(
                        pages, active_page, offset, payload
                    ))
                    for page in pages:
                        self.assertEqual(
                            bytes(page[CALIBRATION_SLOT_OFFSET:
                                       CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE]),
                            latest,
                        )

    def test_shared_writer_late_fault_or_power_loss_reboots_exact_latest_cal(self):
        writers = {
            "property": (0x000, b"P" * 384),
            "runtime": (0x200, b"R" * 28),
            "ota": (0x300, b"O" * 52),
        }
        latest = calibration_blob(0xFFFFFFFE, 23)
        for active_page in (0, 1):
            for name, (offset, payload) in writers.items():
                for fail_page in (0, 1):
                    for restored in (False, True):
                        with self.subTest(active_page=active_page, writer=name,
                                          fail_page=fail_page, restored=restored):
                            pages = [bytearray(b"\x3c" * FLASH_PAGE_SIZE),
                                     bytearray(b"\xc3" * FLASH_PAGE_SIZE)]
                            pages[active_page][CALIBRATION_SLOT_OFFSET:
                                               CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = latest
                            self.assertFalse(coordinated_shared_store(
                                pages, active_page, offset, payload,
                                fail_page=fail_page,
                                restore_original=restored,
                            ))
                            rebooted = reboot_latest_calibration(pages)
                            self.assertEqual(rebooted, latest)
                            self.assertEqual(rebooted[:4], latest[:4])
                            self.assertEqual(rebooted[-4:], latest[-4:])

    def test_calibration_writer_fault_keeps_active_slot_and_success_advances(self):
        previous = calibration_blob(41, 7)
        committed = calibration_blob(42, 13)
        for active_page in (0, 1):
            with self.subTest(active_page=active_page, outcome="late-fault"):
                pages = [bytearray(b"\x96" * FLASH_PAGE_SIZE),
                         bytearray(b"\x69" * FLASH_PAGE_SIZE)]
                pages[active_page][CALIBRATION_SLOT_OFFSET:
                                   CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = previous
                target = 1 - active_page
                pages[target][CALIBRATION_SLOT_OFFSET:
                              CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = \
                    b"\xff" * CALIBRATION_SLOT_SIZE
                pages[target][CALIBRATION_SLOT_OFFSET:
                              CALIBRATION_SLOT_OFFSET + 0x80] = committed[:0x80]
                self.assertEqual(reboot_latest_calibration(pages), previous)

            with self.subTest(active_page=active_page, outcome="success"):
                pages = [bytearray(b"\x96" * FLASH_PAGE_SIZE),
                         bytearray(b"\x69" * FLASH_PAGE_SIZE)]
                pages[active_page][CALIBRATION_SLOT_OFFSET:
                                   CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = previous
                target = 1 - active_page
                pages[target][CALIBRATION_SLOT_OFFSET:
                              CALIBRATION_SLOT_OFFSET + CALIBRATION_SLOT_SIZE] = committed
                self.assertEqual(reboot_latest_calibration(pages), committed)

    def test_all_four_shared_writer_sources_use_checked_coordination(self):
        property_source = read_property_source()
        runtime_source = RUNTIME_SOURCE.read_text(encoding="utf-8", errors="ignore")
        ota_source = read_mqtt_source()
        storage_source = STORAGE_SOURCE.read_text(encoding="utf-8", errors="ignore")
        flash_source = FLASH_SOURCE.read_text(encoding="utf-8", errors="ignore")

        blocks = {
            "property": property_source[
                property_source.index("static boolean_en zk_property_flash_write_record"):
                property_source.index("static void zk_device_config_refresh_iccid_field")
            ],
            "runtime": runtime_source[
                runtime_source.index("static boolean_en zk_runtime_flash_write_record"):
                runtime_source.index("static void zk_runtime_save_process")
            ],
            "ota": ota_source[
                ota_source.index("static boolean_en zk_ota_report_write_record"):
                ota_source.index("void zk_ota_report_mark_pending")
            ],
        }
        for name, block in blocks.items():
            with self.subTest(writer=name):
                self.assertIn("hw_flash_update_bytes_checked", block)
                self.assertNotIn("hw_flash_write_bytes", block)
                self.assertIn("current_cal_storage_prepare_shared_page_update", block)
                self.assertIn("sys_pwm_force_off", block)

        cal_writer = storage_source[
            storage_source.index("static boolean_en current_cal_write_v2"):
            storage_source.index("static boolean_en current_cal_storage_store")
        ]
        coordinator = storage_source[
            storage_source.index("boolean_en current_cal_storage_prepare_shared_page_update"):
            storage_source.index("boolean_en current_cal_storage_has_active_curve")
        ]
        self.assertIn("hw_flash_update_bytes_checked", cal_writer)
        self.assertIn("hw_flash_program_bytes_checked", cal_writer)
        self.assertIn("memcmp(mirror_record.bytes, active_record.bytes", coordinator)
        self.assertIn("active_record.sequence", coordinator)
        self.assertIn("hw_flash_original_page", flash_source)
        self.assertIn("hw_flash_program_full_page_checked", flash_source)
        self.assertIn("hw_flash_checked_fault = BOOL_TRUE", flash_source)
        self.assertIn("sector_addr, hw_flash_original_page", flash_source)

    def test_late_page_program_failure_is_reported_and_mixed_factory_stays_off(self):
        page = bytearray((index * 17 + 5) & 0xFF for index in range(FLASH_PAGE_SIZE))
        property_record = struct.pack("<IHH", 0x5A4B5052, 1, 0x180) + bytes(0x178)
        output_force_off_latched = True

        success = checked_page_update(
            page, 0, property_record,
            fail_program_offset=CALIBRATION_SLOT_OFFSET + 0x40,
        )
        if success:
            output_force_off_latched = False

        self.assertFalse(success)
        self.assertTrue(output_force_off_latched)
        handler = read_property_source().split(
            "boolean_en zk_handle_property_write", 1
        )[1]
        failure = handler.split(
            "if (zk_property_flash_store_config(&candidate) == BOOL_FALSE)", 1
        )[1].split("zk_dev_cfg = candidate", 1)[0]
        self.assertIn("zk_factory_restore_checked(old_factory_buf)", failure)
        self.assertNotIn("sys_pwm_release_and_reload", failure)

    def test_factory_json_numbers_must_be_finite_exact_integers(self):
        def strict_int(value: object) -> bool:
            return (isinstance(value, (int, float)) and not isinstance(value, bool) and
                    math.isfinite(float(value)) and -2147483648 <= float(value) <= 2147483647 and
                    int(value) == float(value))

        self.assertTrue(strict_int(890))
        for value in (890.9, -0.5, float("nan"), float("inf"),
                      float("-inf"), 2147483648, True):
            with self.subTest(value=value):
                self.assertFalse(strict_int(value))

        source = read_property_source()
        picker = source[
            source.index("static boolean_en zk_json_pick_config_number"):
            source.index("static boolean_en zk_json_pick_config_string")
        ]
        self.assertNotIn("*value = node->valueint", picker)
        self.assertIn("node->valuedouble != node->valuedouble", picker)
        self.assertIn("(double)*value != node->valuedouble", picker)

    def test_rtc_write_is_not_part_of_persisted_property_record(self):
        source = read_property_source()
        handler = source[
            source.index("boolean_en zk_handle_property_write"):
            len(source)
        ]
        persist_line = "persist_needed = (gis != NULL || dim != NULL || sense != NULL || svr != NULL) ? 1 : 0;"

        self.assertIn(persist_line, handler)
        self.assertNotIn("rtc != NULL ||", persist_line)
        self.assertIn("zk_validate_rtc_config(rtc, &rtc_value)", handler)
        self.assertIn("zk_set_local_rtc(&rtc_value);", handler)
        self.assertIn("time_text = zk_json_get_rtc_time_text(rtc);", source)
        self.assertIn('time_node = cJSON_GetObjectItem(node, "time");', read_mqtt_source())

    def test_restore_overwrites_property_flash_without_reset(self):
        source = read_mqtt_source()
        handler = source[
            source.index("boolean_en zk_handle_control_message"):
            source.index("boolean_en zk_handle_request_message")
        ]
        restore_block = handler[
            handler.index("restore_type == 0 || restore_type == 1 || restore_type == 6"):
            handler.index("if (restore_type == 5)")
        ]

        property_source = read_property_source()
        self.assertIn("zk_device_config_restore_defaults() == BOOL_FALSE", restore_block)
        self.assertIn("zk_device_config_set_defaults(&restore_config);", property_source)
        self.assertIn("zk_property_flash_store_config(&restore_config)", property_source)
        self.assertIn("zk_dev_cfg = restore_config;", property_source)
        self.assertNotIn("NVIC_SystemReset", restore_block)


if __name__ == "__main__":
    unittest.main()
