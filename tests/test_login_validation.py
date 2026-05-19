#!/usr/bin/env python3

import json
import unittest

from tools.login_flow import (
    generate_password,
    get_upgrade_topics,
    get_will_topic,
    get_topics,
    make_heartbeat_packet,
    make_login_packet,
    make_will_packet,
    next_json_id,
    parse_login_response,
    validate_topic_permissions,
)


class LoginValidationTests(unittest.TestCase):
    def setUp(self):
        self.imei = "860608074646596"

    def test_generate_password(self):
        self.assertEqual(generate_password(self.imei), "FBE709622252")

    def test_make_login_packet_contains_expected_fields(self):
        payload = json.loads(make_login_packet(self.imei, firmware_version=101, hardware_version=202, tm="2026-04-02 12:00:00"))
        self.assertEqual(payload["SN"], self.imei)
        self.assertEqual(payload["ID"], "000001")
        self.assertEqual(payload["DT"]["DevInfo"]["sver"], 101)
        self.assertEqual(payload["DT"]["DevInfo"]["hver"], 202)
        self.assertIn("Gis", payload["DT"])
        self.assertIn("Dim", payload["DT"])
        self.assertIn("Sense", payload["DT"])

    def test_upgrade_and_will_topics(self):
        publish_topic, subscribe_topic = get_upgrade_topics(self.imei)
        self.assertEqual(publish_topic, f"MS/{self.imei}/dev2pcp")
        self.assertEqual(subscribe_topic, f"MS/{self.imei}/pcp2dev")
        self.assertEqual(get_will_topic(self.imei), f"MS/{self.imei}/offline")

    def test_make_will_packet_uses_standard_json_envelope(self):
        payload = json.loads(make_will_packet(self.imei, tm="2026-04-02 12:00:00"))
        self.assertEqual(payload["SN"], self.imei)
        self.assertEqual(payload["SV"], "rept")
        self.assertEqual(payload["ID"], "000000")
        self.assertEqual(payload["CT"], "H")
        self.assertNotIn("DT", payload)

    def test_make_heartbeat_packet_uses_report_envelope(self):
        payload = json.loads(make_heartbeat_packet(self.imei, message_id="12", tm="2026-04-02 12:00:00"))
        self.assertEqual(payload["SN"], self.imei)
        self.assertEqual(payload["SV"], "rept")
        self.assertEqual(payload["ID"], "000012")
        self.assertEqual(payload["CT"], "H")
        self.assertNotIn("DT", payload)

    def test_heartbeat_id_range_and_wrap(self):
        self.assertEqual(next_json_id(1), 2)
        self.assertEqual(next_json_id(2), 3)
        self.assertEqual(next_json_id(999998), 999999)
        self.assertEqual(next_json_id(999999), 2)
        payload = json.loads(make_heartbeat_packet(self.imei, message_id=str(next_json_id(1)), tm="2026-04-02 12:00:00"))
        self.assertEqual(payload["ID"], "000002")

    def test_make_heartbeat_packet_rejects_out_of_range_id(self):
        with self.assertRaises(ValueError):
            make_heartbeat_packet(self.imei, message_id="000001")
        with self.assertRaises(ValueError):
            make_heartbeat_packet(self.imei, message_id="1000000")

    def test_validate_topic_permissions_success(self):
        publish_topic, subscribe_topic = get_topics(self.imei)
        permission = validate_topic_permissions(self.imei, publish_topic, subscribe_topic)
        self.assertTrue(permission.valid)
        self.assertEqual(permission.reason, "权限合法")

    def test_validate_topic_permissions_reject_cross_device_publish(self):
        _, subscribe_topic = get_topics(self.imei)
        permission = validate_topic_permissions(self.imei, "MS/000000000000000/dev2plt", subscribe_topic)
        self.assertFalse(permission.valid)
        self.assertEqual(permission.reason, "发布主题不匹配")

    def test_parse_login_response_success(self):
        payload = json.dumps(
            {
                "SN": self.imei,
                "TM": "2026-04-02 12:01:00",
                "SV": "rept",
                "ID": "000001",
                "CT": "L",
            }
        )
        result = parse_login_response(payload)
        self.assertTrue(result["success"])
        self.assertEqual(result["data"]["SN"], self.imei)

    def test_parse_login_response_reject_malformed_json(self):
        result = parse_login_response("{bad-json}")
        self.assertFalse(result["success"])
        self.assertIn("JSON解析失败", result["reason"])

    def test_parse_login_response_reject_missing_field(self):
        result = parse_login_response(json.dumps({"SN": self.imei, "CT": "L"}))
        self.assertFalse(result["success"])
        self.assertIn("缺少字段", result["reason"])


if __name__ == "__main__":
    unittest.main()
