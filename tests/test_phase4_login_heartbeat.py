#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="ignore")


class Phase4LoginHeartbeatTests(unittest.TestCase):
    def test_login_and_heartbeat_runtime_hooks(self):
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        tcp_source = read_text("Core/Src/LampProtocolLib/TcpClient.c")
        json_source = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")
        nb_source = read_text("Core/Src/LampProtocolLib/NbDriver.c")

        # 头文件声明
        self.assertIn("int zk_make_heartbeat_packet(char *buf, int buf_size);", header)
        self.assertIn("int zk_publish_heartbeat_packet(void);", header)
        self.assertIn("void zk_mqtt_session_process(void);", header)
        self.assertIn("boolean_en zk_mqtt_init(void);", header)
        self.assertIn("void zk_cjson_prepare_parse(void);", header)
        self.assertIn("uint32 zk_mqtt_next_json_id(void);", header)
        self.assertIn("uint16 zk_mqtt_next_packet_id(void);", header)
        self.assertIn("boolean_en zk_mqtt_accept_login_ack(const zk_message_header_t *header);", header)
        self.assertIn("int zk_parse_message_header_from_root(cJSON *root, zk_message_header_t *header);", header)
        self.assertIn("boolean_en zk_message_header_matches_device(const zk_message_header_t *header);", header)

        # 关键常量
        self.assertIn('#define ZK_CT_WRITE             "W"', header)
        self.assertIn("#define ZK_JSON_RX_MAX", header)
        self.assertIn("#define ZK_CJSON_POOL_SIZE", header)
        self.assertIn("#define ZK_JSON_ID_FIRST_REPORT", header)
        self.assertIn("#define ZK_JSON_ID_MAX", header)
        self.assertIn("#define ZK_LOGIN_ACK_TIMEOUT_MS", header)
        self.assertIn("#define ZK_HEARTBEAT_INTERVAL_SEC", header)

        # 登录/心跳组包
        self.assertIn('zk_fill_message_header(&header, ZK_SV_REPT, ZK_LOGIN_REQUEST_ID, ZK_CT_LOGIN);', mqtt_source)
        self.assertIn('zk_fill_message_header(&header, ZK_SV_REPT, message_id, ZK_CT_HEARTBEAT);', mqtt_source)

        # cJSON静态池初始化
        self.assertIn("cJSON_InitHooks(&hooks);", mqtt_source)
        self.assertIn("zk_cjson_pool_offset = 0;", mqtt_source)

        # JSON解析入口
        self.assertIn("zk_cjson_prepare_parse();", json_source)
        self.assertIn("rx_len > ZK_JSON_RX_MAX", json_source)
        self.assertIn("zk_parse_message_header_from_root(root, header)", json_source)
        self.assertIn("zk_message_header_matches_device(header)", json_source)

        # 登录状态管理
        self.assertIn("zk_login_state = ZK_LOGIN_STATE_WAIT_ACK;", mqtt_source)
        self.assertIn("zk_login_state = ZK_LOGIN_STATE_ONLINE;", mqtt_source)
        # 登录包发送不应直接设置ONLINE (ONLINE由ACK回调设置)
        login_func = mqtt_source[
            mqtt_source.index("int zk_publish_login_packet"):
            mqtt_source.index("int zk_publish_heartbeat_packet")
        ]
        self.assertNotIn("zk_login_state = ZK_LOGIN_STATE_ONLINE;", login_func)

        # 会话处理
        self.assertIn("zk_mqtt_session_process();", tcp_source)

        # 登录ACK处理
        self.assertIn("zk_mqtt_accept_login_ack(header)", json_source)
        self.assertIn("zk_mqtt_accept_heartbeat_ack(header)", json_source)

        # 中科业务统一分发
        self.assertIn("zk_dispatch_message(root, header)", json_source)

        # NbDriver IMEI检查
        self.assertIn("zk_mqtt_init() == BOOL_FALSE", nb_source)
        self.assertIn("zk_mqtt_next_packet_id()", nb_source)

    def test_iccid_is_late_bounded_and_non_blocking(self):
        header = read_text("Core/Src/LampProtocolLib/NbDriver.h")
        nb_source = read_text("Core/Src/LampProtocolLib/NbDriver.c")
        mqtt_header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        self.assertIn('#define NB_ICCID_DEFAULT "00000000000000000000"', header)
        self.assertIn("#define NB_ICCID_MAX_ATTEMPTS 3", header)
        self.assertIn("void zk_device_config_refresh_iccid(void);", mqtt_header)

        imei_block = nb_source[
            nb_source.index("case   CONNECT_CONFIG_AT_IEMI"):
            nb_source.index("case   CONNECT_CONFIG_AT_QCCID")
        ]
        self.assertIn("AT+CGSN", imei_block)
        self.assertIn("connect_state=CONNECT_CONFIG_AT_qmtping;", imei_block)
        self.assertNotIn("AT+QCCID", imei_block)

        qmtconn_start = nb_source.index(
            "case CONNECT_CONFIG_AT_QMTCONN:\n"
            "             if(send_AT_Command_machine_finish()==TRUE)"
        )
        qmtsub_start = nb_source.index(
            "case CONNECT_CONFIG_AT_QMTSUB:\n"
            "             if(send_AT_Command_machine_finish()==TRUE)",
            qmtconn_start
        )
        qmtconn_block = nb_source[qmtconn_start:qmtsub_start]
        self.assertIn('send_AT_Command_machine_star("AT+QCCID\\r\\n"', qmtconn_block)
        self.assertIn('"+QCCID:"', qmtconn_block)
        self.assertIn("connect_state=CONNECT_CONFIG_AT_QCCID;", qmtconn_block)
        qccid_send = nb_source.index('send_AT_Command_machine_star("AT+QCCID\\r\\n"', qmtconn_start)
        self.assertLess(qmtconn_start, qccid_send)
        self.assertLess(
            qccid_send,
            qmtsub_start
        )

        iccid_start = nb_source.index(
            "case   CONNECT_CONFIG_AT_QCCID:\n"
            "           if(send_AT_Command_machine_finish()==TRUE)"
        )
        http_start = nb_source.index("case CONNECT_CONFIG_HTTP_ACTIVE:", iccid_start)
        iccid_block = nb_source[iccid_start:http_start]
        self.assertIn("zk_device_config_refresh_iccid();", iccid_block)
        self.assertIn("zk_build_qmt_sub_cmd", iccid_block)
        self.assertIn("connect_state=CONNECT_CONFIG_AT_QMTSUB;", iccid_block)

        self.assertIn("resend_counter >= NB_ICCID_MAX_ATTEMPTS", nb_source)
        self.assertIn("use default invalid iccid", nb_source)
        self.assertIn("zk_device_config_refresh_iccid();", mqtt_source)

    def test_heartbeat_json_id_management(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        self.assertIn(
            'snprintf(message_id, sizeof(message_id), "%06lu", (unsigned long)zk_mqtt_next_json_id());',
            mqtt_source
        )
        # ID范围在2-999999
        self.assertIn("zk_json_message_counter < ZK_JSON_ID_FIRST_REPORT", mqtt_source)
        self.assertIn("zk_json_message_counter >= ZK_JSON_ID_MAX", mqtt_source)

    def test_error_response_format(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        # 错误响应包含ER字段
        self.assertIn('cJSON_AddNumberToObject(root, "ER", er_code);', mqtt_source)
        self.assertIn("int zk_publish_error_response", mqtt_source)

    def test_ele_info_power_factor_reports_tenth_units(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")

        self.assertIn('zk_cjson_create_tx_array("EleInfo.f")', mqtt_source)
        self.assertIn("cJSON_AddItemToArray(f, cJSON_CreateNumber((double)((u32)ac_pf * 10U)))", mqtt_source)

    def test_session_process_handles_all_periodic_tasks(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        session = mqtt_source[
            mqtt_source.index("void zk_mqtt_session_process"):
            mqtt_source.index("static void zk_copy_json_string")
        ]

        self.assertIn("zk_publish_heartbeat_packet()", session)
        self.assertIn("zk_publish_runtime_report(ZK_CT_CYCLIC)", session)
        self.assertIn("zk_publish_runtime_report(ZK_CT_CHANGE)", session)
        self.assertIn("zk_publish_time_request()", session)
        self.assertIn("zk_alarm_process()", session)
        self.assertIn("zk_patrol_report_pending", session)

    def test_time_sync_sources_are_explicit(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        property_source = read_text("Core/Src/LampProtocolLib/zk_property.c")
        json_source = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")
        sys_rtc_source = read_text("Core/Src/sys_aip1302.c")

        login_ack_block = json_source[
            json_source.index("if (zk_mqtt_accept_login_ack(header))"):
            json_source.index("if (zk_mqtt_accept_heartbeat_ack(header))")
        ]
        self.assertNotIn("zk_apply_server_time_from_header(header)", login_ack_block)
        self.assertIn("zk_login_time_sync_pending = BOOL_TRUE;", mqtt_source)

        request_handler = mqtt_source[
            mqtt_source.index("boolean_en zk_handle_request_message"):
            mqtt_source.index("boolean_en zk_handle_ota_message")
        ]
        dispatch_handler = mqtt_source[
            mqtt_source.index("boolean_en zk_dispatch_message"):
            mqtt_source.index("static boolean_en zk_signal_query_process")
        ]
        self.assertIn("zk_apply_server_time_from_header(header);", dispatch_handler)
        self.assertLess(
            dispatch_handler.index("zk_apply_server_time_from_header(header);"),
            dispatch_handler.index("zk_handle_property_read(root, header)"),
        )
        self.assertIn('cJSON_GetObjectItem(dt, "TmCali")', request_handler)
        self.assertIn("zk_apply_server_time_text(zk_json_get_rtc_time_text(tm_cali))", request_handler)
        self.assertNotIn("zk_apply_server_time_from_header(header)", request_handler)
        self.assertIn("#define ZK_DEFAULT_TIMEZONE_HOURS 8", mqtt_source)
        self.assertIn("static long zk_local_timezone_offset_seconds(void)", mqtt_source)
        self.assertIn("cfg->zone >= -12 && cfg->zone <= 12", mqtt_source)
        self.assertIn("zk_utc_rtc_to_local_rtc(&server_utc_time, &server_local_time)", mqtt_source)
        self.assertIn("zk_set_local_rtc(&server_local_time);", mqtt_source)
        self.assertNotIn("zk_set_local_rtc(&server_time);", mqtt_source)

        rtc_validator = property_source[
            property_source.index("static int zk_validate_rtc_config"):
            property_source.index("boolean_en zk_handle_property_read")
        ]
        self.assertIn("time_text = zk_json_get_rtc_time_text(rtc);", rtc_validator)
        self.assertIn('cJSON_GetObjectItem(node, "time")', mqtt_source)

        self.assertIn("apprtc_RtcTime.week = _RtcWeekToDs1302Week", sys_rtc_source)
        self.assertIn("ds1302.week = _HexToBcd(apprtc_RtcTime.week);", sys_rtc_source)
        self.assertIn("memset(&rtc, 0, sizeof(rtc));", sys_rtc_source)
        self.assertIn("rtc.ready = BOOL_TRUE;", sys_rtc_source)

    def test_login_ack_starts_period_timers_without_immediate_first_run(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        login_ack = mqtt_source[
            mqtt_source.index("boolean_en zk_mqtt_accept_login_ack"):
            mqtt_source.index("boolean_en zk_mqtt_accept_heartbeat_ack")
        ]
        session = mqtt_source[
            mqtt_source.index("void zk_mqtt_session_process"):
            mqtt_source.index("static void zk_copy_json_string")
        ]

        self.assertIn("static void zk_sync_online_period_timers(uint32 now)", mqtt_source)
        self.assertIn("zk_sync_online_period_timers(now);", login_ack)
        self.assertIn("zk_login_time_sync_pending = BOOL_TRUE;", login_ack)
        self.assertIn("zk_login_time_sync_pending == BOOL_TRUE", session)
        self.assertIn("zk_login_time_sync_pending = BOOL_FALSE;", session)
        self.assertNotIn("zk_report_tick = 0;", login_ack)
        self.assertNotIn("zk_time_request_tick = 0;", login_ack)
        self.assertIn("Timer_PassedDelay(zk_report_tick, report_period_ms)", session)
        self.assertIn("Timer_PassedDelay(zk_time_request_tick, time_request_period_ms)", session)
        self.assertNotIn("zk_report_tick == 0", session)
        self.assertNotIn("zk_time_request_tick == 0", session)

    def test_patrol_uses_pending_flag_not_immediate_publish(self):
        mqtt_source = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.c")
        ctrl_handler = mqtt_source[
            mqtt_source.index("boolean_en zk_handle_control_message"):
            mqtt_source.index("boolean_en zk_handle_request_message")
        ]

        self.assertIn("zk_patrol_report_pending = BOOL_TRUE;", ctrl_handler)
        # patrol不应在handler中直接调用上报
        self.assertNotIn("zk_publish_runtime_report", ctrl_handler)


if __name__ == "__main__":
    unittest.main()
