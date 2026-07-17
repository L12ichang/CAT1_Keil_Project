#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class Phase3JsonSkeletonTests(unittest.TestCase):
    def test_mqtt_protocol_uses_preallocated_json_rendering(self):
        source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")

        self.assertIn("cJSON_" "PrintPreallocated", source)
        self.assertNotIn("cJSON_" "PrintUnformatted", source)
        self.assertIn("cJSON_InitHooks", source)
        self.assertIn("#define ZK_CJSON_POOL_SIZE      8192", header)
        self.assertIn("#define ZK_CJSON_RX_POOL_SIZE   4096", header)
        self.assertIn("#define ZK_CJSON_TX_POOL_SIZE   4096", header)

    def test_cjson_uses_static_pool_with_alignment(self):
        source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        self.assertIn("static uint8 static_rx_pool[ZK_CJSON_RX_POOL_SIZE] __attribute__((aligned(8)));", source)
        self.assertIn("static uint8 static_tx_pool[ZK_CJSON_TX_POOL_SIZE] __attribute__((aligned(8)));", source)
        self.assertIn("hooks.malloc_fn = ZK_Cjson_Malloc;", source)
        self.assertIn("hooks.free_fn = ZK_Cjson_Free;", source)

    def test_cjson_pool_management_functions_exist(self):
        source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")

        self.assertIn("static void zk_cjson_init_rx(void)", source)
        self.assertIn("static void zk_cjson_init_tx(void)", source)
        self.assertIn("void zk_cjson_prepare_parse(void);", header)
        self.assertIn("static void zk_cjson_prepare_tx(void)", source)

    def test_nbdriver_has_publish_state_check(self):
        header = read_text("Core/Src/LampProtocolLib/NbDriver.h")
        source = read_text("Core/Src/LampProtocolLib/NbDriver.c")

        # pubsend_state_idle在NbDriver中定义
        self.assertIn("boolean_en pubsend_state_idle(void);", header)
        self.assertIn("boolean_en pubsend_state_idle()", source)

    def test_json_protocol_routes_to_zk_handlers(self):
        json_source = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        self.assertIn("zk_dispatch_message(root, header)", json_source)

        for handler in [
            "zk_handle_property_read",
            "zk_handle_property_write",
            "zk_handle_control_message",
            "zk_handle_request_message",
            "zk_handle_ota_message",
        ]:
            with self.subTest(handler=handler):
                self.assertIn(handler, mqtt_source)

        self.assertIn("zk_parse_message_header_from_root(root, header)", json_source)
        self.assertIn("zk_message_header_matches_device(header)", json_source)
        login_ack_handler = mqtt_source[
            mqtt_source.index("boolean_en zk_mqtt_accept_login_ack"):
            mqtt_source.index("boolean_en zk_mqtt_accept_heartbeat_ack")
        ]
        dispatch = mqtt_source[
            mqtt_source.index("boolean_en zk_dispatch_message"):
            mqtt_source.index("static boolean_en zk_signal_query_process")
        ]
        self.assertIn("zk_login_time_sync_pending", login_ack_handler)
        self.assertIn("zk_apply_server_time_from_header(header)", dispatch)
        self.assertIn("zk_publish_error_response", mqtt_source)

    def test_tcp_client_uses_zk_publish_interfaces(self):
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")
        tcp_source = read_text("Core/Src/LampProtocolLib/TcpClient.c")

        self.assertIn("int zk_publish_login_packet(void);", header)
        self.assertIn("int zk_publish_heartbeat_packet(void);", header)
        self.assertIn("void zk_mqtt_session_process(void);", header)
        self.assertIn("zk_mqtt_session_process()", tcp_source)


if __name__ == "__main__":
    unittest.main()
