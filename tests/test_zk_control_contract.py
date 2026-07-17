#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8", errors="ignore")


class ZkControlContractTests(unittest.TestCase):
    def test_cnctrl_validates_all_items_before_brightness_output(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_control_message"):
            source.index("boolean_en zk_handle_request_message")
        ]

        self.assertIn("err = zk_validate_cnctrl_item(item, &target, &last_sec);", handler)
        self.assertIn("zk_apply_brightness(target);", handler)
        self.assertLess(
            handler.index("for (index = 0; index < item_count; ++index)"),
            handler.index("restore_brightness = zk_get_current_brightness();"),
        )
        self.assertLess(
            handler.index("restore_brightness = zk_get_current_brightness();"),
            handler.index("zk_apply_brightness(target);"),
        )

    def test_cnctrl_requires_cns_and_checks_brightness_last_ranges(self):
        source = read_source()
        validator = source[
            source.index("static int zk_validate_cnctrl_item"):
            source.index("static void zk_apply_brightness(int brightness)\n{")
        ]
        picker = source[
            source.index("static int zk_pick_control_target"):
            source.index("static int zk_validate_cnctrl_item")
        ]

        self.assertIn('cJSON_GetObjectItem(item, "cns")', validator)
        self.assertIn("cns != ZK_CNCTRL_SUPPORTED_CNS", validator)
        self.assertIn("return 6;", validator)
        self.assertIn("ZK_CNCTRL_LAST_MAX_SEC", validator)
        self.assertIn("brightness < 0 || brightness > 100", picker)
        self.assertIn("return 3;", picker)

    def test_last_duration_is_non_blocking(self):
        source = read_source()
        control_source = source[
            source.index("static void zk_schedule_control_restore"):
            source.index("boolean_en zk_handle_request_message")
        ]

        self.assertIn("zk_control_restore_pending", control_source)
        self.assertIn("Timer_PassedDelay(zk_control_restore_tick, zk_control_restore_delay_ms)", control_source)
        self.assertIn("ZK_CNCTRL_LAST_MAX_SEC  (24UL * 60UL * 60UL)", source)
        self.assertNotIn("HAL_Delay", control_source)
        self.assertNotIn("delayMs", control_source)

    def test_restore_routes_persistence_by_restore_type(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_control_message"):
            source.index("boolean_en zk_handle_request_message")
        ]

        self.assertIn("restore_type == 2 || restore_type == 3 || restore_type == 4", handler)
        defaults = handler[
            handler.index("restore_type == 0 || restore_type == 1 || restore_type == 6"):
            handler.index("restore_type == 5")
        ]
        self.assertIn("zk_device_config_restore_defaults()", defaults)
        self.assertNotIn("sys_data_store", defaults)
        energy_reset = handler[
            handler.index("restore_type == 5"):
            handler.index("zk_publish_simple_response(header, 0)", handler.index("restore_type == 5"))
        ]
        self.assertIn("sys_bl0942_energy_stats_clear()", energy_reset)
        self.assertIn("sys_data_store()", energy_reset)
        self.assertIn("zk_runtime_stats_clear()", energy_reset)
        self.assertNotIn("hw_flash", handler)
        self.assertNotIn("data_backup_store_data", handler)

    def test_patrol_uses_pending_report_instead_of_immediate_double_publish(self):
        source = read_source()
        handler = source[
            source.index("boolean_en zk_handle_control_message"):
            source.index("boolean_en zk_handle_request_message")
        ]
        patrol_block = handler[
            handler.index('strcmp(do_node->valuestring, "patrol") == 0'):
            handler.index('strcmp(do_node->valuestring, "reboot") == 0')
        ]
        session = source[
            source.index("void zk_mqtt_session_process"):
            source.index("static void zk_copy_json_string")
        ]

        self.assertIn("zk_patrol_report_pending = BOOL_TRUE;", patrol_block)
        self.assertNotIn("zk_publish_runtime_report", patrol_block)
        self.assertIn("zk_patrol_report_pending == BOOL_TRUE", session)
        self.assertIn("pubsend_state_idle() == BOOL_TRUE", session)
        self.assertIn("zk_publish_runtime_report(ZK_CT_CYCLIC)", session)


if __name__ == "__main__":
    unittest.main()
