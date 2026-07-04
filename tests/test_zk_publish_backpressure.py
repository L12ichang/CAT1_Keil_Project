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
        sender = block(source, "static int zk_send_payload", "int zk_send_json_root")
        json_sender = block(source, "int zk_send_json_root", "void zk_schedule_simple_response")

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
        response = block(source, "void zk_schedule_simple_response", "int zk_publish_simple_response_now")
        ota_progress = block(source, "int zk_publish_ota_progress", "static int zk_publish_ota_error_now")
        ota_error = block(source, "int zk_publish_ota_error", "void zk_notify_state_changed")
        notify_change = block(source, "void zk_notify_state_changed", "static void zk_cancel_control_restore")

        self.assertIn("#define ZK_RESPONSE_QUEUE_SIZE 2U", source)
        self.assertIn("static zk_response_pending_item_t zk_response_queue[ZK_RESPONSE_QUEUE_SIZE];", source)
        self.assertIn("static u8 zk_response_queue_head = 0;", source)
        self.assertIn("static u8 zk_response_queue_count = 0;", source)
        self.assertIn("static u32 zk_response_queue_drop_count = 0;", source)
        self.assertIn("#define ZK_CHANGE_REPORT_SETTLE_MS 3000UL", source)
        self.assertIn("memcpy(&zk_response_queue[write_index].header, request", response)
        self.assertIn("zk_response_queue_count++;", response)
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
            "zk_login_time_sync_pending = BOOL_FALSE;",
            "zk_response_queue_head = 0;",
            "zk_response_queue_count = 0;",
            "zk_response_queue_drop_count = 0;",
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
        self.assertIn("zk_login_time_sync_pending = BOOL_TRUE;", login_ack)
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

    def test_login_wait_recovers_from_publish_timeout_and_missing_ack(self):
        source = read_source()
        login = block(source, "int zk_publish_login_packet", "int zk_publish_heartbeat_packet")
        session = block(source, "void zk_mqtt_session_process", "static void zk_copy_json_string")
        reconnect = block(source, "static void zk_mqtt_force_reconnect", "static int zk_send_payload")
        reset = block(source, "void zk_mqtt_reset_session", "const zk_mqtt_config_t *zk_mqtt_get_config")

        self.assertIn("#define ZK_LOGIN_ACK_RECONNECT_THRESHOLD 2U", source)
        self.assertIn("zk_login_wait_pub_timeout_count = nb_mqtt_get_publish_timeout_count();", login)
        self.assertIn("nb_mqtt_get_publish_timeout_count() != zk_login_wait_pub_timeout_count", session)
        self.assertIn('zk_mqtt_force_reconnect("login_publish_timeout");', session)
        self.assertIn("zk_login_ack_timeout_count++;", session)
        self.assertIn('zk_mqtt_force_reconnect("login_ack_timeout");', session)
        self.assertIn("pubsend_state_set_idle();", reconnect)
        self.assertIn("onNBEvent(NB_EVENT_LOST_CONNECTION, 0, 0);", reconnect)
        self.assertIn("_4G_configModule_machine_star();", reconnect)
        self.assertIn("zk_login_ack_timeout_count = 0;", reset)
        self.assertIn("zk_login_wait_pub_timeout_count = 0;", reset)

    def test_pubsend_idle_includes_pending_payload_and_at_windows(self):
        nb_source = NB_SOURCE.read_text(encoding="utf-8", errors="ignore")
        at_busy = block(nb_source, "static boolean_en nb_at_command_is_busy", "static boolean_en pubsend_is_busy")
        busy_check = block(nb_source, "static boolean_en pubsend_is_busy", "uint8 nbSendTcpData")
        idle_check = block(nb_source, "boolean_en pubsend_state_idle", "void pubsend_state_set_idle")
        sender = block(nb_source, "uint8 g4Send_MQTT_Data", "boolean_en pubsend_state_finish")
        prepare = block(nb_source, "static uint8 nb_mqtt_publish_prepare", "static boolean_en nb_mqtt_publish_read_prompt")
        uart_owner = block(nb_source, "static boolean_en nb_mqtt_publish_owns_uart", "/**")
        at_machine = block(nb_source, "void send_AT_Command_machine", "boolean_en  _4G_configModule_machine_finish")
        nb_process = block(nb_source, "void nbModuleProcess", "}//switch")

        self.assertIn("sendcommad_state == SEND_COMMAND_STATE_IDLE", at_busy)
        self.assertIn("sendcommad_state == SEND_COMMAND_STATE_RXING_COMPLETE", at_busy)
        self.assertIn("pub_en_flag", busy_check)
        self.assertIn("pubsend_state != PUBSEDN_STATE_IDLE", busy_check)
        self.assertIn("nb_at_command_is_busy() == BOOL_TRUE", busy_check)
        self.assertIn("return nb_mqtt_publish_prepare(pub_topic, (uint8 *)pData, length);", sender)
        self.assertIn("pubsend_state = PUBSEDN_STATE_SEND_HEADER;", prepare)
        for state in (
            "PUBSEDN_STATE_SEND_HEADER",
            "PUBSEDN_STATE_WAIT_PROMPT",
            "PUBSEDN_STATE_SEND_PAYLOAD",
            "PUBSEDN_STATE_WAIT_ACK",
        ):
            self.assertIn(state, uart_owner)
        self.assertIn("if (nb_mqtt_publish_owns_uart() == BOOL_TRUE)", at_machine)
        self.assertIn("if (nb_mqtt_publish_owns_uart() == BOOL_TRUE)", nb_process)
        self.assertIn("pubsend_is_busy() == BOOL_FALSE", idle_check)


if __name__ == "__main__":
    unittest.main()
