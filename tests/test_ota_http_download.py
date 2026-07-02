from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class OtaHttpDownloadTests(unittest.TestCase):
    def test_ota_uses_custom_http10_get_header(self):
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        ota_h = (ROOT / "Core/Src/LampProtocolLib/ota.h").read_text(encoding="utf-8")

        self.assertIn('AT+QHTTPCFG=\\"requestheader\\",1', ota_c)
        self.assertIn("GET %s HTTP/1.0", ota_c)
        self.assertIn("AT+QHTTPGET=120,%u,30", ota_c)
        self.assertIn('"+QHTTPGET:"', ota_c)
        self.assertIn("ota_parse_qhttpget_result", ota_c)
        self.assertIn("server response: err=%d http=%d content_len=%u", ota_c)
        self.assertIn("code marker=ota_http_status_v2", ota_c)
        self.assertIn("CONNECT_OTA_AT_QHTTPGET_HEADER", ota_h)
        self.assertNotIn("QHTTPGETEX", ota_c)
        self.assertNotIn('"+QHTTPGET: 0,20"', ota_c)
        self.assertNotIn("firm_name_buffer1", ota_c)
        self.assertNotIn("firm_name_buffer2", ota_c)

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

    def test_ota_logging_is_macro_controlled_and_covers_flow(self):
        common_h = (ROOT / "Core/Src/common.h").read_text(encoding="utf-8")
        ota_c = (ROOT / "Core/Src/LampProtocolLib/ota.c").read_text(encoding="utf-8")
        mqtt_c = (ROOT / "Core/Src/LampProtocolLib/mqtt_zk_protocol.c").read_text(encoding="utf-8")

        self.assertIn("#define APP_OTA_LOG_ENABLE 1", common_h)
        self.assertIn("#define OTA_LOGI", common_h)
        self.assertIn("cmd received", mqtt_c)
        for text in (
            "http download start",
            "download success",
            "download failed",
            "server response",
            "save to module fs start",
            "module fs save command accepted",
            "module fs save line",
            "module fs diagnose start",
            "module fs diag line",
            "module fs file present",
            "move firmware to flash start",
            "verify firmware start",
            "verify firmware complete",
            "upgrade start",
        ):
            self.assertIn(text, ota_c)


if __name__ == "__main__":
    unittest.main()
