#!/usr/bin/env python3

from pathlib import Path
import unittest

from tools.login_flow import generate_password


ROOT = Path(__file__).resolve().parents[1]
MQTT_SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"


class MqttPasswordContractTests(unittest.TestCase):
    def test_protocol_reference_vector_uses_modbus_password(self):
        # 当前代码使用CRC16-Modbus (poly=0xA001, init=0xFFFF)
        self.assertEqual(generate_password("864294053651521"), "BD9D0EE1D3E1")

    def test_firmware_password_generation_uses_modbus_crc(self):
        source = MQTT_SOURCE.read_text(encoding="utf-8")
        function_body = source.split("void zk_mqtt_generate_password", 1)[1].split("void zk_device_config_init", 1)[0]

        self.assertIn('#include "crc16_modbus.h"', source)
        self.assertIn("crc16_modbus_get", function_body)

    def test_password_length_is_12_hex_chars(self):
        password = generate_password("860608074646596")
        self.assertEqual(len(password), 12)
        self.assertTrue(all(c in "0123456789ABCDEF" for c in password))


if __name__ == "__main__":
    unittest.main()
