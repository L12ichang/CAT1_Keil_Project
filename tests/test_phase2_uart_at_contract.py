import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "MDK-ARM-8008000" / "project.uvprojx"


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="ignore")


def function_body(source: str, function_name: str) -> str:
    match = re.search(
        rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{",
        source,
    )
    if match is None:
        raise AssertionError(f"function not found: {function_name}")
    depth = 1
    index = match.end()
    while index < len(source) and depth > 0:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth != 0:
        raise AssertionError(f"function has unbalanced braces: {function_name}")
    return source[match.end() : index - 1]


def normalized_path(path: str) -> str:
    return path.replace("\\", "/").lower()


class Phase2UartAtContractTests(unittest.TestCase):
    def setUp(self):
        project_root = ET.parse(PROJECT).getroot()
        self.targets = project_root.findall("./Targets/Target")

    def test_phase2_source_files_exist(self):
        for relative_path in (
            "Core/Src/hw_uart1.c",
            "Core/Src/hw_uart1.h",
            "Core/System/sys_at_engine.c",
            "Core/System/sys_at_engine.h",
            "Core/System/sys_resource.c",
            "Core/System/sys_resource.h",
            "Core/Src/LampProtocolLib/nb_at_legacy_adapter.c",
            "Core/Src/LampProtocolLib/nb_at_legacy_adapter.h",
        ):
            self.assertTrue((ROOT / relative_path).is_file(), relative_path)

    def test_uart1_rx_callback_uses_stable_byte_and_never_reads_dr(self):
        source = read_text("Core/Src/hw_uart1.c")
        callback = function_body(source, "HAL_UART_Rx1CpltCallback")

        self.assertIn("_rx_ring[_rx_head] = _rx_isr_byte;", callback)
        self.assertIn("hw_uart1_arm_rx();", callback)
        self.assertNotIn("->DR", callback)
        self.assertNotIn("saveUsartByte", callback)
        self.assertIn("HAL_UART_Receive_IT(&huart1, &_rx_isr_byte, 1U)", source)

    def test_uart1_has_error_recovery_and_diagnostics(self):
        header = read_text("Core/Src/hw_uart1.h")
        source = read_text("Core/Src/hw_uart1.c")
        error_callback = function_body(source, "HAL_UART_Error1Callback")
        uart2 = function_body(
            read_text("Core/Src/hw_uart2.c"), "HAL_UART_ErrorCallback"
        )

        for field in (
            "rx_overflow_count",
            "rx_rearm_error_count",
            "tx_busy_count",
            "tx_error_count",
            "ore_count",
            "fe_count",
            "ne_count",
            "pe_count",
        ):
            self.assertIn(field, header)
        for error in (
            "HAL_UART_ERROR_ORE",
            "HAL_UART_ERROR_FE",
            "HAL_UART_ERROR_NE",
            "HAL_UART_ERROR_PE",
        ):
            self.assertIn(error, error_callback)
        self.assertNotIn("HAL_UART_AbortReceive", error_callback)
        self.assertNotIn("hw_uart1_arm_rx", error_callback)
        self.assertIn("_rx_armed = BOOL_FALSE;", error_callback)
        self.assertIn("HAL_UART_Error1Callback(huart);", uart2)

    def test_at_engine_copies_requests_and_owns_single_transaction(self):
        header = read_text("Core/System/sys_at_engine.h")
        source = read_text("Core/System/sys_at_engine.c")

        self.assertIn("SYS_AT_STATE_WAIT_RESPONSE", header)
        self.assertIn("SYS_AT_STATE_RETRY_WAIT", header)
        self.assertIn("SYS_AT_RESULT_TIMEOUT", header)
        self.assertIn("SYS_AT_PARSE_RAW", header)
        self.assertIn("char command[SYS_AT_COMMAND_CAPACITY]", source)
        self.assertIn("char expected_token[SYS_AT_EXPECTED_CAPACITY]", source)
        self.assertIn("static sys_at_request_slot_st _active;", source)
        self.assertIn("sys_at_dequeue_highest", source)
        self.assertIn("sys_at_retry_or_finish", source)
        self.assertIn("sys_at_handle_prompt", source)
        self.assertIn("sys_at_route_urc", source)
        self.assertIn("sys_at_trim_line_end(slot->expected_token);", source)
        self.assertIn("sys_at_trim_line_end(slot->error_token);", source)

    def test_modem_resource_has_owner_generation_depth_and_lease(self):
        header = read_text("Core/System/sys_resource.h")
        source = read_text("Core/System/sys_resource.c")

        self.assertIn("SYS_RESOURCE_MODEM_EXCLUSIVE", header)
        self.assertIn("u16 owner_id;", header)
        self.assertIn("u16 generation;", header)
        self.assertIn("u16 depth;", header)
        self.assertIn("u32 lease_due_ms;", header)
        self.assertIn("slot->generation++", source)
        self.assertIn("slot->depth++", source)
        self.assertIn("sys_resource_slot_expired", source)
        self.assertIn("sys_resource_release", source)
        self.assertIn("sys_resource_renew", header)
        self.assertIn("boolean_en sys_resource_renew(", source)

    def test_raw_mode_requires_resource_token_and_generation(self):
        engine = read_text("Core/System/sys_at_engine.c")
        adapter = read_text(
            "Core/Src/LampProtocolLib/nb_at_legacy_adapter.c"
        )
        ota = read_text("Core/Src/LampProtocolLib/ota.c")

        self.assertIn("sys_resource_validate(token)", engine)
        self.assertIn("token->generation != _raw_token.generation", engine)
        self.assertIn("sys_at_engine_arm_raw_mode", engine)
        self.assertIn("nb_at_legacy_adapter_begin_exclusive", adapter)
        self.assertIn("nb_at_legacy_adapter_end_exclusive", adapter)
        self.assertIn('nb_at_legacy_adapter_arm_raw_mode("+QIRD:")', ota)
        self.assertIn('nb_at_legacy_adapter_arm_raw_mode("CONNECT")', ota)
        self.assertIn("nb_at_legacy_adapter_leave_raw_mode", ota)
        self.assertIn("sys_at_recover_from_invalid_raw_token", engine)
        self.assertIn("sys_at_parse_byte(byte);", engine)

        begin_exclusive = function_body(
            adapter,
            "nb_at_legacy_adapter_begin_exclusive",
        )
        self.assertLess(
            begin_exclusive.index("sys_resource_validate"),
            begin_exclusive.index("sys_resource_renew"),
        )

        qird_state = ota.index("case CONNECT_OTA_AT_RAW_QIRD:")
        qird_zero = ota.index("if (qird_len == 0U)", qird_state)
        qird_nonzero = ota.index("else", qird_zero)
        qird_zero_block = ota[qird_zero:qird_nonzero]
        self.assertLess(
            qird_zero_block.index("nb_at_legacy_adapter_leave_raw_mode"),
            qird_zero_block.index(
                "ota_connect_state=CONNECT_OTA_AT_RAW_QIRD_TRAILER"
            ),
        )
        self.assertIn("qird_zero_raw_release", qird_zero_block)

    def test_legacy_business_has_no_second_uart_queue_consumer(self):
        forbidden = (
            "usartRecvQueue",
            "flushQueue(",
            "saveUsartByte",
            "delayMs(",
        )
        for source_path in (ROOT / "Core").rglob("*"):
            if source_path.suffix.lower() not in {".c", ".h"}:
                continue
            if source_path.name in {"Queue.c", "Queue.h"}:
                continue
            text = source_path.read_text(encoding="utf-8", errors="ignore")
            for token in forbidden:
                self.assertNotIn(token, text, f"{source_path}: {token}")

    def test_legacy_will_and_publish_prompt_ack_use_at_adapter(self):
        source = read_text("Core/Src/LampProtocolLib/NbDriver.c")
        machine_start = source.index("void _4G_configModule_machine(void)")
        will_start = source.index(
            "case CONNECT_CONFIG_AT_TIMEOUT:", machine_start
        )
        will_end = source.index("case CONNECT_CONFIG_AT_IPPORT:", will_start)
        publish_start = source.index("void nbSendTcpData_sm(void)")
        publish_end = source.index(
            "void SET_NB_STAT_EPOWER_DOWN", publish_start
        )
        will_block = source[will_start:will_end]
        publish_block = source[publish_start:publish_end]

        self.assertIn('send_AT_Command_machine_star(', will_block)
        self.assertIn('">"', will_block)
        self.assertNotIn("nb_mqtt_publish_read_prompt", will_block)
        self.assertIn("nb_at_legacy_adapter_start(", publish_block)
        self.assertIn('"+QMTPUBEX:"', publish_block)
        self.assertIn('""', publish_block)
        self.assertNotIn("readLine(", publish_block)
        self.assertIn("if (atcommand == nb_will_payload)", source)
        self.assertIn("retry_max = 0U;", source)
        self.assertIn('nb_request_reconnect("WILL_RESULT")', will_block)

    def test_production_at_engine_host_harness(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        harness = ROOT / "tests" / "phase2_at_engine_host_harness.c"
        with tempfile.TemporaryDirectory(prefix="phase2_at_engine_") as temp:
            executable = Path(temp) / "phase2_at_engine_host"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "Core" / "Src"),
                    str(harness),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                built.returncode,
                msg=f"host harness compile failed:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                ran.returncode,
                msg=f"host harness failed:\n{ran.stdout}\n{ran.stderr}",
            )
            self.assertIn(
                "phase2 AT engine production-C harness: PASS",
                ran.stdout,
            )

    def test_production_resource_host_harness(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        harness = ROOT / "tests" / "phase2_resource_host_harness.c"
        with tempfile.TemporaryDirectory(prefix="phase2_resource_") as temp:
            executable = Path(temp) / "phase2_resource_host"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(harness),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                built.returncode,
                msg=f"resource harness compile failed:\n"
                f"{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                ran.returncode,
                msg=f"resource harness failed:\n{ran.stdout}\n{ran.stderr}",
            )
            self.assertIn(
                "phase2 resource production-C harness: PASS",
                ran.stdout,
            )

    def test_boot_and_scheduler_keep_required_order(self):
        boot = function_body(read_text("Core/App/app_boot.c"), "app_boot_init")
        scheduler = function_body(
            read_text("Core/App/app_scheduler.c"), "app_scheduler_process"
        )

        self.assertLess(boot.index("hw_uart1_init();"), boot.index("sys_at_engine_init();"))
        self.assertLess(boot.index("sys_resource_init();"), boot.index("sys_at_engine_init();"))
        self.assertLess(
            scheduler.index("hw_uart1_process"),
            scheduler.index("sys_at_engine_process"),
        )
        self.assertLess(
            scheduler.index("sys_at_engine_process"),
            scheduler.index("app_scheduler_legacy_network_process"),
        )

    def test_both_keil_targets_reference_phase2_sources_once(self):
        self.assertEqual(2, len(self.targets))
        expected_paths = (
            "../core/src/hw_uart1.c",
            "../core/system/sys_at_engine.c",
            "../core/system/sys_resource.c",
            "../core/src/lampprotocollib/nb_at_legacy_adapter.c",
        )
        for target in self.targets:
            target_name = target.findtext("TargetName")
            target_files = [
                normalized_path(path.text or "")
                for path in target.findall(".//FilePath")
            ]
            for expected_path in expected_paths:
                self.assertEqual(
                    1,
                    target_files.count(expected_path),
                    f"{target_name}: {expected_path}",
                )


if __name__ == "__main__":
    unittest.main()
