#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROPERTY_SOURCE = ROOT / "Core/Src/LampProtocolLib/zk_property.c"
MQTT_SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
SYS_DATA = ROOT / "Core/Src/sys_data.h"


def read_property_source() -> str:
    return PROPERTY_SOURCE.read_text(encoding="utf-8", errors="ignore")


def read_mqtt_source() -> str:
    return MQTT_SOURCE.read_text(encoding="utf-8", errors="ignore")


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

    def test_factory_write_reloads_pwm_after_flash_store(self):
        source = read_property_source()
        handler = source[
            source.index("boolean_en zk_handle_property_write"):
            len(source)
        ]
        factory_commit = handler[
            handler.index("if (factory_changed != 0)"):
            handler.index("zk_publish_simple_response(header, 0);")
        ]

        self.assertIn('#include "sys_pwm.h"', source)
        self.assertIn("memcpy(sys_data.fa_Parambuf, factory_buf, sizeof(factory_buf));", factory_commit)
        self.assertIn("factory_user_load_data();", factory_commit)
        self.assertIn("sys_data_store();", factory_commit)
        self.assertIn("sys_pwm_reload();", factory_commit)
        self.assertLess(factory_commit.index("sys_data_store();"), factory_commit.index("sys_pwm_reload();"))

    def test_factory_current_range_keeps_pwm_math_u32_safe(self):
        source = read_property_source()
        factory_header = (ROOT / "Core/Src/factory_user_data.h").read_text(encoding="utf-8", errors="ignore")
        factory_source = (ROOT / "Core/Src/factory_user_data.c").read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#define FACTORY_OUTCUR_MAX_MA 10000U", factory_header)
        self.assertIn("value > FACTORY_OUTCUR_MAX_MA", source)
        self.assertIn("SET_OUTCUR>FACTORY_OUTCUR_MAX_MA", factory_source)
        self.assertIn("HWMAX_OUTCUR>FACTORY_OUTCUR_MAX_MA", factory_source)

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
