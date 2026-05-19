#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
SYS_DATA = ROOT / "Core/Src/sys_data.h"


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8", errors="ignore")


class ZkPropertyFlashPersistenceTests(unittest.TestCase):
    def test_property_record_uses_existing_parameter_partition_pages(self):
        source = read_source()
        sys_data = SYS_DATA.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#define ZK_PROPERTY_FLASH_MAIN_ADDR (DATAROM_STARTADDR + FLASH_PAGE_SIZE)", source)
        self.assertIn("#define ZK_PROPERTY_FLASH_BACKUP_ADDR (BAKDATAROM_STARTADDR + FLASH_PAGE_SIZE)", source)
        self.assertIn("#define   DATAROM_STARTADDR               (u32)0x8005000", sys_data)
        self.assertIn("#define   BAKDATAROM_STARTADDR            (u32)0x8006800", sys_data)
        self.assertIn("#define SYS_DATA_ST_EXPECTED_SIZE         408", sys_data)

    def test_property_write_saves_flash_before_ram_commit(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_property_write"):
            source.index("static boolean_en zk_json_pick_number_field")
        ]

        self.assertIn("candidate = zk_dev_cfg;", handler)
        self.assertIn("zk_property_flash_store_config(&candidate)", handler)
        self.assertIn("zk_dev_cfg = candidate;", handler)
        self.assertIn("zk_publish_simple_response(header, ZK_FLASH_SAVE_ERROR);", handler)
        self.assertLess(
            handler.index("zk_property_flash_store_config(&candidate)"),
            handler.index("zk_dev_cfg = candidate;"),
        )

    def test_rtc_write_is_not_part_of_persisted_property_record(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_property_write"):
            source.index("static boolean_en zk_json_pick_number_field")
        ]
        persist_line = "persist_needed = (gis != NULL || dim != NULL || sense != NULL || svr != NULL) ? 1 : 0;"

        self.assertIn(persist_line, handler)
        self.assertNotIn("rtc != NULL ||", persist_line)
        self.assertIn("zk_validate_rtc_config(rtc, &rtc_value)", handler)
        self.assertIn("zk_set_local_rtc(&rtc_value);", handler)
        self.assertIn("time_text = zk_json_get_rtc_time_text(rtc);", source)
        self.assertIn('time_node = cJSON_GetObjectItem(node, "time");', source)

    def test_restore_overwrites_property_flash_without_reset(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_control_message"):
            source.index("boolean_en zk_handle_request_message")
        ]
        restore_block = handler[
            handler.index("restore_type == 0 || restore_type == 1 || restore_type == 6"):
            handler.index("if (restore_type == 5)")
        ]

        self.assertIn("zk_device_config_set_defaults(&restore_config);", restore_block)
        self.assertIn("zk_property_flash_store_config(&restore_config)", restore_block)
        self.assertIn("zk_dev_cfg = restore_config;", restore_block)
        self.assertNotIn("NVIC_SystemReset", restore_block)


if __name__ == "__main__":
    unittest.main()
