import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "MDK-ARM-8008000" / "project.uvprojx"


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="ignore")


def normalized_path(path: str) -> str:
    return path.replace("\\", "/").lower()


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


def c_identifiers(source: str) -> set[str]:
    """Return identifiers while ignoring comments and string/character literals."""
    without_comments = re.sub(r"/\*.*?\*/", " ", source, flags=re.DOTALL)
    without_comments = re.sub(r"//[^\r\n]*", " ", without_comments)
    without_literals = re.sub(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        " ",
        without_comments,
    )
    return set(re.findall(r"\b[A-Za-z_]\w*\b", without_literals))


def defined_function_bodies(source: str) -> dict[str, str]:
    bodies = {}
    definitions = re.finditer(
        r"(?m)^[ \t]*(?:static[ \t]+)?"
        r"(?:void|boolean_en|u8|u16|u32|uint8|uint16|uint32|int|s8|s16|s32)"
        r"(?:[ \t]*\*)?[ \t]+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
        source,
    )
    for definition in definitions:
        name = definition.group(1)
        bodies[name] = function_body(source[definition.start() :], name)
    return bodies


class Phase3NetworkContractTests(unittest.TestCase):
    def setUp(self):
        project_root = ET.parse(PROJECT).getroot()
        self.targets = project_root.findall("./Targets/Target")
        self.targets_by_name = {
            target.findtext("TargetName"): target for target in self.targets
        }

    def test_phase3_sources_exist_and_are_in_both_keil_targets(self):
        expected_files = (
            "Core/Config/network_config.h",
            "Core/System/sys_cellular.c",
            "Core/System/sys_mqtt.c",
            "Core/System/sys_connectivity.c",
        )
        for expected_file in expected_files:
            self.assertTrue((ROOT / expected_file).is_file(), expected_file)

        self.assertEqual({"program", "Release-MinSize"}, set(self.targets_by_name))
        target_sources = (
            "../core/system/sys_cellular.c",
            "../core/system/sys_mqtt.c",
            "../core/system/sys_connectivity.c",
        )
        for target_name, target in self.targets_by_name.items():
            target_files = [
                normalized_path(file_path.text or "")
                for file_path in target.findall(".//FilePath")
            ]
            for expected_path in target_sources:
                self.assertEqual(
                    1,
                    target_files.count(expected_path),
                    f"{target_name}: {expected_path}",
                )
            include_paths = " ".join(
                normalized_path(item.text or "")
                for item in target.findall(".//IncludePath")
            )
            self.assertIn("../core/system", include_paths, target_name)
            self.assertIn("../core/config", include_paths, target_name)

    def test_network_failures_are_diagnostic_and_never_reset_the_mcu(self):
        network_sources = (
            "Core/System/sys_cellular.c",
            "Core/System/sys_mqtt.c",
            "Core/System/sys_connectivity.c",
            "Core/Config/network_config.h",
            "Core/App/app_scheduler.c",
        )
        reset_calls = re.compile(
            r"\b(?:NVIC_SystemReset|HAL_NVIC_SystemReset|soft_reset)\s*\("
        )
        for relative_path in network_sources:
            source = read_text(relative_path)
            self.assertIsNone(
                reset_calls.search(source),
                f"{relative_path}: network recovery must not reset the MCU",
            )

        watchdog = read_text("Core/Src/watchdog.c")
        diagnostics = function_body(watchdog, "mcu_runtime_diag_process")
        health = function_body(watchdog, "mcu_health_is_ok")

        for diagnostic in (
            "nb_mqtt_get_publish_timeout_count",
            "usart_queue_drop_count",
            "mqtt_pub_timeout_bad_count",
            "uart1_queue_drop_bad_count",
        ):
            self.assertIn(diagnostic, diagnostics)
        for diagnostic in (
            "mqtt_pub_timeout",
            "uart1_queue_drop",
            "usart_queue_drop",
        ):
            self.assertNotIn(
                diagnostic,
                health,
                f"{diagnostic} is diagnostic-only and must not refuse watchdog feed",
            )

    def test_sys_mqtt_has_no_business_or_legacy_session_dependency(self):
        source = read_text("Core/System/sys_mqtt.c")
        identifiers = {identifier.lower() for identifier in c_identifiers(source)}

        forbidden_exact = {
            "online",
            "gateway_state",
            "onnbevent",
            "nvic_systemreset",
            "hal_nvic_systemreset",
            "soft_reset",
        }
        self.assertTrue(
            forbidden_exact.isdisjoint(identifiers),
            f"forbidden sys_mqtt identifiers: "
            f"{sorted(forbidden_exact.intersection(identifiers))}",
        )
        forbidden_families = sorted(
            identifier
            for identifier in identifiers
            if identifier.startswith("zk")
            or "json" in identifier
            or "login" in identifier
        )
        self.assertEqual(
            [],
            forbidden_families,
            "sys_mqtt owns transport only, not ZK JSON or business login",
        )

    def test_scheduler_cuts_over_all_old_normal_network_state_machines(self):
        scheduler = read_text("Core/App/app_scheduler.c")
        function_bodies = defined_function_bodies(scheduler)
        executable_source = "\n".join(function_bodies.values())
        scheduler_ids = c_identifiers(executable_source)

        for legacy_state in ("connect_state", "NB_STATE", "pubsend_state"):
            self.assertNotIn(legacy_state, scheduler_ids)

        for legacy_call in (
            "_4G_configModule_machine",
            "nbSendTcpData_sm",
            "nbModuleProcess",
            "resetNbModule_machine",
            "hw_gateway_process",
        ):
            self.assertNotIn(
                legacy_call,
                scheduler_ids,
                f"normal scheduler still runs legacy network path: {legacy_call}",
            )

        old_at_functions = [
            (name, body)
            for name, body in function_bodies.items()
            if "send_AT_Command_machine" in c_identifiers(body)
        ]
        for function_name, body in old_at_functions:
            self.assertRegex(
                body,
                r"OTA_ENABLE_IS_SET|SYS_RESOURCE_MODEM_EXCLUSIVE|"
                r"nb_at_legacy_adapter_has_exclusive",
                f"{function_name}: legacy AT machine is permitted only behind "
                "OTA exclusive ownership",
            )

    def test_normal_urc_ownership_and_ota_legacy_adapter_boundary(self):
        adapter = read_text("Core/Src/LampProtocolLib/nb_at_legacy_adapter.c")
        adapter_init = function_body(adapter, "nb_at_legacy_adapter_init")
        adapter_urc = function_body(adapter, "nb_at_legacy_urc_handler")
        begin_exclusive = function_body(
            adapter, "nb_at_legacy_adapter_begin_exclusive"
        )
        end_exclusive = function_body(
            adapter, "nb_at_legacy_adapter_end_exclusive"
        )

        self.assertIn("_urc_enabled = BOOL_FALSE;", adapter_init)
        self.assertIn("_urc_enabled != BOOL_TRUE", adapter_urc)
        self.assertIn(
            "nb_at_legacy_adapter_set_urc_enabled(BOOL_TRUE)",
            begin_exclusive,
        )
        self.assertIn(
            "nb_at_legacy_adapter_set_urc_enabled(BOOL_FALSE)",
            end_exclusive,
        )

        for module, handler_hint in (
            ("Core/System/sys_cellular.c", "cellular"),
            ("Core/System/sys_mqtt.c", "mqtt"),
        ):
            source = read_text(module)
            registrations = re.findall(
                r"sys_at_engine_add_urc_handler\s*\(\s*"
                r"([A-Za-z_]\w*)\s*,",
                source,
            )
            self.assertEqual(
                1,
                len(registrations),
                f"{module}: register exactly one normal-path URC handler",
            )
            self.assertIn(handler_hint, registrations[0].lower())

        send_raw = function_body(adapter, "nb_at_legacy_adapter_send_raw")
        raw_handler = function_body(adapter, "nb_at_legacy_raw_handler")
        read_line = function_body(adapter, "nb_at_legacy_adapter_read_line")
        self.assertIn("hw_uart1_write", send_raw)
        self.assertIn("nb_at_legacy_enqueue_byte", raw_handler)
        self.assertIn("nb_at_legacy_adapter_read_byte", read_line)

    def test_qmtrecv_has_one_effective_owner_and_fixed_storage(self):
        mqtt_header = read_text("Core/System/sys_mqtt.h")
        mqtt_source = read_text("Core/System/sys_mqtt.c")
        cellular_source = read_text("Core/System/sys_cellular.c")
        connectivity_source = read_text("Core/System/sys_connectivity.c")
        adapter = read_text("Core/Src/LampProtocolLib/nb_at_legacy_adapter.c")

        self.assertIn("+QMTRECV:", mqtt_source)
        self.assertNotIn("+QMTRECV:", cellular_source)
        self.assertNotIn("+QMTRECV:", connectivity_source)
        self.assertRegex(mqtt_header, r"\bchar\s+topic\s*\[[^\]]+\]")
        self.assertRegex(mqtt_header, r"\bchar\s+payload\s*\[[^\]]+\]")
        self.assertRegex(
            mqtt_source,
            r"\b(?:memcpy|strncpy|snprintf)\s*\(",
            "QMTRECV data must be copied into MQTT-owned fixed storage",
        )

        legacy_urc = function_body(adapter, "nb_at_legacy_urc_handler")
        self.assertIn("_urc_enabled != BOOL_TRUE", legacy_urc)
        scheduler = read_text("Core/App/app_scheduler.c")
        for old_consumer in ("nbModuleProcess", "hw_gateway_process"):
            scheduler_bodies = "\n".join(
                defined_function_bodies(scheduler).values()
            )
            self.assertNotIn(old_consumer, c_identifiers(scheduler_bodies))

    def test_pdp_evidence_is_independent_from_registration(self):
        config = read_text("Core/Config/network_config.h")
        cellular = read_text("Core/System/sys_cellular.c")
        notify = function_body(cellular, "sys_cellular_notify_transport_opened")

        self.assertIn("QMTOPEN", config)
        self.assertNotIn("AT+QIACT", config)
        self.assertNotIn("AT+CGACT", config)
        self.assertIn("_snapshot.registered", notify)
        self.assertIn("_snapshot.pdp_active = BOOL_TRUE;", notify)
        self.assertEqual(
            1,
            cellular.count("_snapshot.pdp_active = BOOL_TRUE;"),
            "registration alone must never be promoted to PDP-active evidence",
        )

    def test_flash_boot_ota_calibration_and_plan_layout_is_unchanged(self):
        for target_name, target in self.targets_by_name.items():
            memories = target.find(
                "./TargetOption/TargetArmAds/ArmAdsMisc/OnChipMemories"
            )
            self.assertIsNotNone(memories, target_name)
            self.assertEqual(
                ("0x8008000", "0x1c000"),
                (
                    memories.findtext("./OCR_RVCT4/StartAddress"),
                    memories.findtext("./OCR_RVCT4/Size"),
                ),
                f"{target_name}: APP IROM",
            )
            self.assertEqual(
                ("0x20000000", "0xc000"),
                (
                    memories.findtext("./OCR_RVCT9/StartAddress"),
                    memories.findtext("./OCR_RVCT9/Size"),
                ),
                f"{target_name}: IRAM",
            )
            self.assertIn(
                "--bincombined_base=0x8008000",
                target.findtext(
                    "./TargetOption/TargetCommonOption/AfterMake/"
                    "UserProg1Name"
                )
                or "",
                f"{target_name}: post-build APP base",
            )

        sys_data = read_text("Core/Src/sys_data.h")
        for definition in (
            "#define   BOOTROM_STARTADDR               (u32)0x8000000",
            "#define   DATAROM_STARTADDR               (u32)0x8005000",
            "#define   BAKDATAROM_STARTADDR            (u32)0x8006800",
            "#define   APROM_STARTADDR                 (u32)0x8008000",
            "#define   APROM_SAFE_ENDADDR              (u32)0x8024000",
            "#define   OTABAKROM_STARTADDR             (u32)0x8024000",
            "#define   OTABAKROM_ENDADDR               (u32)0x803FFFF",
        ):
            self.assertIn(definition, sys_data)

        assignments = read_text("Core/Src/flash_address_assignment.h")
        for contract in (
            "CURRENT_CAL_FLASH_SLOT_A_ADDR == 0x08005c00UL",
            "CURRENT_CAL_FLASH_SLOT_B_ADDR == 0x08007400UL",
            "APROM_STARTADDR == 0x08008000UL",
            "APROM_SAFE_ENDADDR == 0x08024000UL",
            "OTABAKROM_STARTADDR == 0x08024000UL",
            "OTABAKROM_ENDADDR == 0x0803ffffUL",
        ):
            self.assertIn(contract, assignments)

        work_plan = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")
        self.assertIn(
            "#define ZK_PLAN_FLASH_MAIN_ADDR     ((u32)0x08006000)",
            work_plan,
        )
        self.assertIn(
            "#define ZK_PLAN_FLASH_BACKUP_ADDR   ((u32)0x08007800)",
            work_plan,
        )


if __name__ == "__main__":
    unittest.main()
