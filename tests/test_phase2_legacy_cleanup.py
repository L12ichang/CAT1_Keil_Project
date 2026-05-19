#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class Phase2LegacyCleanupTests(unittest.TestCase):
    def test_legacy_protocol_disabled_in_main_loop(self):
        """旧协议函数在main.c中已被注释禁用"""
        main_c = read_text("Core/Src/main.c")

        for symbol in [
            "app_activate_init(",
            "app_activate_process(",
            "http_congfig_fsm(",
            "http_post_fsm(",
            "appProcess(",
        ]:
            with self.subTest(symbol=symbol):
                # 每个出现都应是被注释的 (// 开头)
                for line in main_c.splitlines():
                    if symbol in line:
                        stripped = line.strip()
                        self.assertTrue(
                            stripped.startswith("//"),
                            f"{symbol} found in active code: '{stripped}'"
                        )

    def test_sys_tick_still_calls_legacy_timer(self):
        """sys_tick.c仍调用app_activate_timer()（已知遗留项，待后续清理）"""
        sys_tick_c = read_text("Core/Src/sys_tick.c")
        self.assertIn("app_activate_timer();", sys_tick_c)

    def test_tcp_client_uses_zk_protocol_only(self):
        tcp_client_c = read_text("Core/Src/LampProtocolLib/TcpClient.c")

        # 不再使用旧的二进制协议封包函数
        for symbol in [
            "makeLogin" "Pack(",
            "makePing" "Pack(",
        ]:
            with self.subTest(symbol=symbol):
                self.assertNotIn(symbol, tcp_client_c)

        # 使用中科协议函数
        self.assertIn("zk_mqtt_session_process()", tcp_client_c)

    def test_json_protocol_routes_to_zk_handlers_only(self):
        json_protocol_c = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")
        mqtt_zk_c = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        # 旧协议cmd处理已禁用
        self.assertIn("旧协议已禁用", json_protocol_c)
        self.assertIn("旧协议 cmd 处理已移除", json_protocol_c)

        # Json入口只调用中科统一分发，具体handler顺序收口到mqtt_zk_protocol.c
        self.assertIn("zk_dispatch_message(root, header)", json_protocol_c)
        self.assertIn("zk_handle_property_read", mqtt_zk_c)
        self.assertIn("zk_handle_property_write", mqtt_zk_c)
        self.assertIn("zk_handle_control_message", mqtt_zk_c)
        self.assertIn("zk_handle_request_message", mqtt_zk_c)
        self.assertIn("zk_handle_ota_message", mqtt_zk_c)

    def test_zk_protocol_only_flag_enabled(self):
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")
        self.assertIn("#define ZK_PROTOCOL_ONLY        1", header)

    def test_legacy_binary_json_commands_disabled(self):
        json_protocol_c = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")

        # 旧的set_level/get_realtime_data命令在注释块中
        self.assertIn("旧协议处理已禁用", json_protocol_c)
        self.assertIn("旧协议 cmd 处理已移除", json_protocol_c)


if __name__ == "__main__":
    unittest.main()
