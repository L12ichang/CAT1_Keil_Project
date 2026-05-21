#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c"
NB_SOURCE = ROOT / "Core/Src/LampProtocolLib/NbDriver.c"


def read_source() -> str:
    return SOURCE.read_text(encoding="utf-8", errors="ignore")


def block(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index:source.index(end, start_index + len(start))]


class ZkPublishBackpressureTests(unittest.TestCase):
    def test_zk_publish_uses_returning_driver_api(self):
        source = read_source()
        sender = block(source, "static int zk_send_payload", "static int zk_send_json_root")
        json_sender = block(source, "static int zk_send_json_root", "static void zk_schedule_simple_response")

        self.assertIn("result = nbSendTcpData((uint8 *)payload, length);", sender)
        self.assertIn("result = g4Send_MQTT_Data((char *)topic, (char *)payload);", sender)
        self.assertIn("zk_note_send_payload_result(result);", sender)
        self.assertNotIn("sendTcpData(", source)
        self.assertNotIn("send_MQTT_Data(", source)
        self.assertIn("return zk_send_payload(zk_tx_buf, (uint16)len, topic);", json_sender)

    def test_publish_functions_only_report_success_after_enqueue(self):
        source = read_source()
        login = block(source, "int zk_publish_login_packet", "int zk_publish_heartbeat_packet")
        heartbeat = block(source, "int zk_publish_heartbeat_packet", "int zk_publish_error_response")
        runtime = block(source, "static int zk_publish_runtime_report", "static int zk_publish_time_request")
        time_request = block(source, "static int zk_publish_time_request", "int zk_publish_alarm_report")
        ota_progress = block(source, "static int zk_publish_ota_progress_now", "int zk_publish_ota_progress")
        ota_error = block(source, "static int zk_publish_ota_error_now", "int zk_publish_ota_error")

        self.assertLess(login.index("zk_send_payload"), login.index("zk_login_state = ZK_LOGIN_STATE_WAIT_ACK;"))
        self.assertLess(heartbeat.index("zk_send_payload"), heartbeat.index("zk_heartbeat_tick = Timer_GetTickCount();"))
        for publish_block in (runtime, time_request, ota_progress, ota_error):
            self.assertIn("if (zk_send_json_root", publish_block)
            self.assertIn("return -1;", publish_block)

    def test_session_priority_and_single_attempt_per_round(self):
        source = read_source()
        session = block(source, "void zk_mqtt_session_process", "static void zk_copy_json_string")

        order = [
            "zk_alarm_process()",
            "zk_ota_error_pending == BOOL_TRUE",
            "zk_ota_progress_pending == BOOL_TRUE",
            "zk_response_pending == BOOL_TRUE",
            "Timer_PassedDelay(zk_change_report_tick",
            "zk_patrol_report_pending == BOOL_TRUE",
            "zk_publish_runtime_report(ZK_CT_CHANGE)",
            "Timer_PassedDelay(zk_report_tick, report_period_ms)",
            "Timer_PassedDelay(zk_time_request_tick, time_request_period_ms)",
            "Timer_PassedDelay(zk_heartbeat_tick, heartbeat_period_ms)",
        ]
        positions = [session.index(item) for item in order]
        self.assertEqual(positions, sorted(positions))

        self.assertGreaterEqual(session.count("return;"), len(order))
        self.assertLess(
            session.index("zk_publish_runtime_report(ZK_CT_CHANGE)"),
            session.index("zk_change_report_pending = BOOL_FALSE;"),
        )
        self.assertLess(
            session.index("ZK_CHANGE_REPORT_SETTLE_MS"),
            session.index("zk_publish_runtime_report(ZK_CT_CHANGE)"),
        )
        self.assertLess(
            session.index("zk_publish_runtime_report(ZK_CT_CYCLIC)"),
            session.index("zk_report_tick = now;"),
        )
        self.assertLess(
            session.index("zk_publish_time_request()"),
            session.index("zk_time_request_tick = now;"),
        )
        self.assertNotIn("zk_report_tick == 0", session)
        self.assertNotIn("zk_time_request_tick == 0", session)
        self.assertIn('zk_log_periodic_send_failure("cyclic report");', session)
        self.assertIn('zk_log_periodic_send_failure("time request");', session)

    def test_pending_slots_are_lightweight_and_reset_with_session(self):
        source = read_source()
        reset = block(source, "void zk_mqtt_reset_session", "const zk_mqtt_config_t *zk_mqtt_get_config")
        response = block(source, "static void zk_schedule_simple_response", "static int zk_publish_simple_response_now")
        ota_progress = block(source, "int zk_publish_ota_progress", "static int zk_publish_ota_error_now")
        ota_error = block(source, "int zk_publish_ota_error", "void zk_notify_state_changed")
        notify_change = block(source, "void zk_notify_state_changed", "static void zk_cancel_control_restore")

        self.assertIn("static zk_message_header_t zk_response_pending_header;", source)
        self.assertIn("#define ZK_CHANGE_REPORT_SETTLE_MS 3000UL", source)
        self.assertIn("memcpy(&zk_response_pending_header, request", response)
        self.assertIn("zk_response_pending = BOOL_TRUE;", response)
        self.assertIn("zk_ota_progress_value = progress;", ota_progress)
        self.assertIn("zk_ota_progress_pending = BOOL_TRUE;", ota_progress)
        self.assertIn("zk_ota_error_code = err_code;", ota_error)
        self.assertIn("zk_ota_error_pending = BOOL_TRUE;", ota_error)
        self.assertIn("zk_change_report_tick = Timer_GetTickCount();", notify_change)
        for flag in (
            "zk_response_pending = BOOL_FALSE;",
            "zk_ota_progress_pending = BOOL_FALSE;",
            "zk_ota_error_pending = BOOL_FALSE;",
            "zk_change_report_pending = BOOL_FALSE;",
        ):
            self.assertIn(flag, reset)
        self.assertIn("zk_change_report_tick = 0;", reset)

    def test_login_ack_starts_full_period_windows_before_periodic_work(self):
        source = read_source()
        login_ack = block(source, "boolean_en zk_mqtt_accept_login_ack", "boolean_en zk_mqtt_accept_heartbeat_ack")
        timer_sync = block(source, "static void zk_sync_online_period_timers", "static int zk_get_run_status_code")

        self.assertIn("uint32 now;", login_ack)
        self.assertIn("now = Timer_GetTickCount();", login_ack)
        self.assertIn("zk_sync_online_period_timers(now);", login_ack)
        self.assertNotIn("zk_report_tick = 0;", login_ack)
        self.assertNotIn("zk_time_request_tick = 0;", login_ack)

        for assignment in (
            "zk_report_tick = now;",
            "zk_time_request_tick = now;",
            "zk_heartbeat_tick = now;",
        ):
            self.assertIn(assignment, timer_sync)

    def test_busy_protection_clears_local_publish_slot_after_three_failures(self):
        source = read_source()
        nb_source = NB_SOURCE.read_text(encoding="utf-8", errors="ignore")
        note_result = block(source, "static void zk_note_send_payload_result", "static int zk_send_payload")
        reset_idle = block(nb_source, "void pubsend_state_set_idle", "void nbSendTcpData_sm")

        self.assertIn("#define ZK_SEND_BUSY_CLEAR_THRESHOLD 3U", source)
        self.assertIn("++zk_send_busy_fail_count;", note_result)
        self.assertIn("zk_send_busy_fail_count >= ZK_SEND_BUSY_CLEAR_THRESHOLD", note_result)
        self.assertIn("pubsend_state_set_idle();", note_result)
        self.assertIn("zk_send_busy_fail_count = 0;", note_result)
        self.assertIn("pub_en_flag=0;", reset_idle)
        self.assertIn("pubsend_state=PUBSEDN_STATE_IDLE;", reset_idle)


if __name__ == "__main__":
    unittest.main()
