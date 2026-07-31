from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source_block(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    end_index = source.index(end, start_index)
    return source[start_index:end_index]


class OtaHttpDownloadTests(unittest.TestCase):
    def test_ota_uses_raw_tcp_production_flow(self):
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        ota_h = (ROOT / "Core/Src/LampProtocolLib/ota.h").read_text(encoding="utf-8")
        ota_config = (ROOT / "Core/Src/LampProtocolLib/ota_config.h").read_text(encoding="utf-8")

        self.assertIn("#include \"ota_config.h\"", ota_c)
        self.assertIn("#define OTA_DEBUG_DOWNLOAD_ONLY             0U", ota_config)
        self.assertIn("#define OTA_STREAM_TO_BACKUP_DEBUG          0U", ota_config)
        self.assertIn("#define OTA_STREAM_ALLOW_RAW_BIN_TEST       0U", ota_config)
        self.assertIn("#define OTA_DEBUG_CLEAR_ALL_UFS             0U", ota_config)
        self.assertIn("#define OTA_USE_QHTTPREADFILE_UFS           0U", ota_config)
        self.assertIn("#define OTA_USE_RAW_TCP_STREAM              1U", ota_config)
        self.assertIn("#define OTA_RAW_TCP_STREAM_DEBUG            OTA_USE_RAW_TCP_STREAM", ota_config)
        self.assertIn("#define OTA_RAW_TCP_QIRD_LEN                512U", ota_config)
        self.assertIn("#define OTA_STREAM_BACKUP_CAPACITY", ota_config)
        self.assertIn('"GET %s HTTP/1.1\\r\\n"', ota_c)
        self.assertIn('"Accept-Encoding: identity\\r\\n"', ota_c)
        self.assertIn('"Connection: close\\r\\n"', ota_c)
        self.assertIn("code marker=ota_raw_tcp_v1", ota_c)
        self.assertIn('OTA_USE_RAW_TCP_STREAM ? "RAW_TCP_STREAM" : "MODULE_HTTP"', ota_c)
        self.assertIn("CONNECT_OTA_AT_RAW_QIRD", ota_h)
        self.assertIn("raw tcp mode: bypass module HTTP stack", ota_c)
        self.assertIn("Transfer-Encoding:", ota_c)
        self.assertIn("CONNECT_OTA_AT_STREAM_VERIFY", ota_h)
        self.assertIn("#if OTA_USE_QHTTPREADFILE_UFS", ota_c)
        self.assertIn("STREAM DOWNLOAD VERIFY SUCCESS", ota_c)
        self.assertIn("stream verify ok: mark upgrade and jump boot", ota_c)
        self.assertIn("sys_data.sn = 0xaa5555aa;", ota_c)
        self.assertIn("sys_data_store();", ota_c)
        self.assertIn("iap_jump2boot();", ota_c)
        self.assertNotIn("debug stream: skip boot/reset", ota_c)
        self.assertNotIn("skip mcu flash write in debug version", source_block(ota_c, "case CONNECT_OTA_AT_STREAM_VERIFY:", "#if OTA_USE_QHTTPREADFILE_UFS"))
        self.assertNotIn("QHTTPGETEX", ota_c)
        self.assertNotIn('"+QHTTPGET: 0,20"', ota_c)
        self.assertNotIn("firm_name_buffer1", ota_c)
        self.assertNotIn("firm_name_buffer2", ota_c)

    def test_ota_header_and_checksum_reject_debug_placeholders(self):
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        ota_config = (ROOT / "Core/Src/LampProtocolLib/ota_config.h").read_text(encoding="utf-8")

        self.assertIn("#define OTA_EXPECTED_DEVICE_TYPE            0x0003U", ota_config)
        self.assertIn("ota_stream_header_device_type == (u16)OTA_EXPECTED_DEVICE_TYPE", ota_c)
        self.assertIn("device type mismatch", ota_c)
        self.assertIn("sum == (u32)0x12345678", ota_c)
        self.assertIn("raw_size == (u32)0x89ABCDEF", ota_c)
        self.assertIn("ota_stream_size_in_range(size) != BOOL_TRUE", ota_c)
        self.assertIn("user_frash_checksum(size/4U)", ota_c)
        self.assertNotIn("58*2048", ota_c)
        self.assertNotIn("size<(u32)58", ota_c)

    def test_raw_qird_stream_flash_write_contract(self):
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")

        self.assertIn("ota_stream_erase_backup_area", ota_c)
        self.assertIn("stream backup erase start", ota_c)
        self.assertIn("FLASH_TYPEPROGRAM_HALFWORD", ota_c)
        self.assertIn("Flash program requested in QIRD_DATA", ota_c)
        self.assertNotIn("sendCommand(ota_log_line_buffer", ota_c)
        self.assertIn("static char ota_raw_qisend_cmd[32];", ota_c)
        self.assertNotIn("char qisend_cmd[32];", ota_c)
        self.assertIn("ota_stream_set_expected_size(ota_raw_http_content_length", ota_c)

        flush_page = source_block(
            ota_c,
            "static boolean_en ota_stream_flush_page",
            "static boolean_en ota_stream_write_byte",
        )
        self.assertIn("ota_stream_stage_current_page", flush_page)
        self.assertNotIn("flash_store", flush_page)
        self.assertNotIn("hw_flash_write_bytes", flush_page)
        self.assertNotIn("HAL_FLASHEx_Erase", flush_page)

        qird_data = source_block(
            ota_c,
            "case CONNECT_OTA_AT_RAW_QIRD_DATA:",
            "case CONNECT_OTA_AT_RAW_QIRD_TRAILER:",
        )
        for forbidden in (
            "HAL_FLASH",
            "flash_store",
            "hw_flash",
            "user_flash",
            "ota_stream_program",
            "ota_stream_flush",
            "ota_stream_stage",
        ):
            self.assertNotIn(forbidden, qird_data)

    def test_raw_qisend_prompt_waiter_and_modem_lock(self):
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        ota_h = (ROOT / "Core/Src/LampProtocolLib/ota.h").read_text(encoding="utf-8")
        ota_config = (ROOT / "Core/Src/LampProtocolLib/ota_config.h").read_text(encoding="utf-8")
        nb_c = (ROOT / "Core/Src/LampProtocolLib/NbDriver.c").read_text(encoding="utf-8")
        nb_h = (ROOT / "Core/Src/LampProtocolLib/NbDriver.h").read_text(encoding="utf-8")

        self.assertIn("static ota_raw_prompt_result_en ota_raw_wait_prompt(u32 timeout_ms)", ota_c)
        self.assertIn("raw tcp qisend prompt ok", ota_c)
        self.assertIn("raw tcp http request payload sent", ota_c)
        self.assertIn("raw tcp send ok", ota_c)
        self.assertIn("AT+QISTATE=1,0", ota_c)
        self.assertIn("raw tcp socket status result", ota_c)
        self.assertIn("raw http header done status=%d", ota_c)
        self.assertIn("#define OTA_RAW_QISEND_WITH_LEN             0U", ota_config)
        self.assertIn("CONNECT_OTA_AT_RAW_QISEND_QUERY", ota_h)
        self.assertIn('"AT+QISEND=?\\r\\n"', ota_c)
        self.assertIn("raw tcp qisend query rx=%s", ota_c)
        self.assertIn("raw tcp qisend mode=%s len=%u", ota_c)
        self.assertIn('"no_len_ctrl_z"', ota_c)
        self.assertIn('"AT+QISEND=%u,%u\\r\\n"', ota_c)
        self.assertIn('"AT+QISEND=%u\\r\\n"', ota_c)
        self.assertIn("http_req_len = (u16)strlen(common_send_buff);", ota_c)
        self.assertIn("ota_raw_qisend_payload[http_req_len] = 0x1AU;", ota_c)
        self.assertIn("nb_modem_send_command_ota(ota_raw_qisend_payload, send_len);", ota_c)
        self.assertIn("raw tcp qisend uart ret=%u", ota_c)
        self.assertIn("raw tcp http payload uart ret=%u", ota_c)
        self.assertNotIn("sendCommand(", ota_c)

        prompt_waiter = source_block(
            ota_c,
            "static ota_raw_prompt_result_en ota_raw_wait_prompt",
            "static void ota_raw_query_socket_state",
        )
        self.assertIn(
            "nb_at_legacy_adapter_read_byte(&dat) == BOOL_TRUE",
            prompt_waiter,
        )
        self.assertNotIn("usartRecvQueue", prompt_waiter)
        self.assertIn("dat == '>'", prompt_waiter)
        self.assertIn("+CME ERROR:", prompt_waiter)
        self.assertIn('"ERROR"', prompt_waiter)
        self.assertIn("ota_feed_watchdog_if_enabled();", prompt_waiter)
        self.assertNotIn("readLine", prompt_waiter)

        qird_data = source_block(
            ota_c,
            "case CONNECT_OTA_AT_RAW_QIRD_DATA:",
            "case CONNECT_OTA_AT_RAW_QIRD_TRAILER:",
        )
        self.assertIn("qird_feed_counter", qird_data)
        self.assertIn("ota_feed_watchdog_if_enabled();", qird_data)

        self.assertIn("static u8 nb_modem_ota_lock=0;", nb_c)
        self.assertIn("void nb_modem_lock_for_ota(void)", nb_c)
        self.assertIn("void nb_modem_unlock_for_ota(void)", nb_c)
        self.assertIn("boolean_en nb_modem_locked_by_ota(void)", nb_c)
        self.assertIn("nb_at_command_allowed_during_ota", nb_c)
        self.assertIn("modem ota lock blocked at command", nb_c)
        self.assertIn("modem ota lock blocked direct send", nb_c)
        self.assertIn("uint8 nb_modem_send_command_ota(void *command,uint16 length)", nb_c)
        self.assertIn("static u8 nb_modem_ota_tx_buffer[NB_MODEM_OTA_TX_BUFFER_SIZE];", nb_c)
        self.assertIn("memcpy(nb_modem_ota_tx_buffer, command, length);", nb_c)
        self.assertIn("nb_modem_send_command_raw_result(nb_modem_ota_tx_buffer, length);", nb_c)
        self.assertIn("modem ota uart write ret=%u", nb_c)
        self.assertIn("if (nb_modem_ota_lock)\n    {\n        OTA_LOGW(\"modem ota lock blocked direct send", nb_c)
        self.assertIn("sendcommad_state= SEND_COMMAND_STATE_RXING_COMPLETE;", nb_c)
        self.assertIn("nb_modem_lock_for_ota();", nb_c)
        self.assertIn("nb_modem_unlock_for_ota();", nb_c)
        self.assertIn("pub_en_flag = 0U;", nb_c)
        self.assertIn("if (nb_modem_ota_lock)\n    {\n        return;\n    }\n    nb_trace_state_change();", nb_c)
        self.assertIn("if (nb_modem_ota_lock)\n    {\n        return NB_ERROR_SEND_FAIL;\n    }", nb_c)
        self.assertIn("void nb_modem_lock_for_ota(void);", nb_h)
        self.assertIn("void nb_modem_unlock_for_ota(void);", nb_h)
        self.assertIn("boolean_en nb_modem_locked_by_ota(void);", nb_h)
        self.assertIn("uint8 nb_modem_send_command_ota(void *command,uint16 length);", nb_h)

    def test_non_ota_http_config_restores_default_request_header(self):
        http_active_c = (ROOT / "Core/Src/LampProtocolLib/http_active.c").read_text(encoding="utf-8")
        http_active_h = (ROOT / "Core/Src/LampProtocolLib/http_active.h").read_text(encoding="utf-8")

        self.assertIn('AT+QHTTPCFG=\\"requestheader\\",0', http_active_c)
        self.assertIn("HTTP_CONFIG_AT_QHTTPCFG_REQUESTHEADER", http_active_h)

    def test_mqtt_ota_uses_fixed_local_file_name_not_url_uid(self):
        mqtt_c = (ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c").read_text(encoding="utf-8")
        ota_h = (ROOT / "Core/Src/LampProtocolLib/ota.h").read_text(encoding="utf-8")

        self.assertIn('#define OTA_LOCAL_FIRMWARE_NAME "cat1.bin"', ota_h)
        self.assertIn("strncpy(firm_name_buffer, OTA_LOCAL_FIRMWARE_NAME, 255);", mqtt_c)
        self.assertNotIn("zk_ota_extract_filename", mqtt_c)

    def test_mqtt_ota_ack_is_published_before_download_start(self):
        mqtt_c = (ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c").read_text(encoding="utf-8")
        handler = source_block(
            mqtt_c,
            "boolean_en zk_handle_ota_message",
            "boolean_en zk_handle_alam_message"
        )
        ack_process = source_block(
            mqtt_c,
            "static boolean_en zk_ota_ack_process",
            "void zk_notify_state_changed"
        )
        session = source_block(
            mqtt_c,
            "void zk_mqtt_session_process",
            "static void zk_copy_json_string"
        )
        reset = source_block(
            mqtt_c,
            "void zk_mqtt_reset_session",
            "const zk_mqtt_config_t *zk_mqtt_get_config"
        )

        self.assertIn("ZK_OTA_ACK_STATE_SEND", mqtt_c)
        self.assertIn("ZK_OTA_ACK_STATE_WAIT_PUBLISH", mqtt_c)
        self.assertIn("#define ZK_OTA_ACK_PUBLISH_TIMEOUT_MS (45UL * 1000UL)", mqtt_c)
        self.assertIn("zk_ota_ack_defer_start(header);", handler)
        self.assertNotIn("set_OTA_ENABLE();", handler)
        self.assertIn("nb_mqtt_get_publish_success_count() != zk_ota_ack_pub_success_count", ack_process)
        self.assertLess(
            ack_process.index("nb_mqtt_get_publish_success_count() != zk_ota_ack_pub_success_count"),
            ack_process.index("set_OTA_ENABLE();")
        )
        self.assertIn("zk_ota_ack_process(now) == BOOL_TRUE", session)
        self.assertIn("zk_ota_ack_clear();", reset)

    def test_ota_logging_is_macro_controlled_and_covers_flow(self):
        common_h = (ROOT / "Core/Src/common.h").read_text(encoding="utf-8")
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        mqtt_c = (ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c").read_text(encoding="utf-8")

        self.assertIn("#define APP_OTA_LOG_ENABLE 1", common_h)
        self.assertIn("#define OTA_LOGI", common_h)
        self.assertIn("cmd received", mqtt_c)
        for text in (
            "mode=%s qhttpreadfile=%u download_only=%u raw_bin_test=%u",
            "raw tcp mode: bypass module HTTP stack",
            "raw tcp open ok",
            "raw tcp send ok",
            "raw http header done status=%d",
            "stream progress",
            "stream waiting",
            "stream body complete",
            "STREAM DOWNLOAD VERIFY SUCCESS",
            "stream verify ok: mark upgrade and jump boot",
            "download failed",
        ):
            self.assertIn(text, ota_c)
        self.assertNotIn("debug stream: skip boot/reset", ota_c)


if __name__ == "__main__":
    unittest.main()
