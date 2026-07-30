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


class Phase1ArchitectureContractTests(unittest.TestCase):
    def setUp(self):
        self.project_root = ET.parse(PROJECT).getroot()
        self.targets = self.project_root.findall("./Targets/Target")
        self.targets_by_name = {
            target.findtext("TargetName"): target for target in self.targets
        }

    def test_phase1_source_files_exist(self):
        for relative_path in (
            "Core/App/app_boot.c",
            "Core/App/app_boot.h",
            "Core/App/app_scheduler.c",
            "Core/App/app_scheduler.h",
            "Core/System/sys_event.c",
            "Core/System/sys_event.h",
            "Core/System/sys_time.c",
            "Core/System/sys_time.h",
        ):
            self.assertTrue((ROOT / relative_path).is_file(), relative_path)

    def test_main_is_limited_to_boot_and_scheduler(self):
        main_body = function_body(read_text("Core/Src/main.c"), "main")
        calls = [
            name
            for name in re.findall(r"\b([A-Za-z_]\w*)\s*\(", main_body)
            if name not in {"while", "if", "for", "switch"}
        ]
        self.assertEqual(["app_boot_init", "app_scheduler_process"], calls)
        self.assertIn("while (1)", main_body)

    def test_systick_only_advances_hal_and_monotonic_time(self):
        handler = function_body(read_text("Core/Src/stm32f1xx_it.c"), "SysTick_Handler")
        calls = re.findall(r"\b([A-Za-z_]\w*)\s*\(", handler)
        self.assertEqual(["HAL_IncTick", "sys_time_tick_isr"], calls)
        self.assertNotIn("sys_tick_process", handler)

    def test_both_targets_include_phase1_sources_exactly_once(self):
        self.assertEqual({"program", "Release-MinSize"}, set(self.targets_by_name))
        expected_paths = (
            "../core/app/app_boot.c",
            "../core/app/app_scheduler.c",
            "../core/system/sys_event.c",
            "../core/system/sys_time.c",
            "../core/src/sys_tick.c",
        )
        for target_name, target in self.targets_by_name.items():
            target_files = [
                normalized_path(file_path.text or "")
                for file_path in target.findall(".//FilePath")
            ]
            for expected_path in expected_paths:
                self.assertEqual(
                    1,
                    target_files.count(expected_path),
                    f"{target_name}: {expected_path}",
                )

    def test_targets_keep_layer_include_paths_and_memory_baseline(self):
        expected_cpu = (
            'IRAM(0x20000000,0x0000C000) IROM(0x08000000,0x00040000) '
            'CPUTYPE("Cortex-M3") CLOCK(12000000) ELITTLE'
        )
        for target_name, target in self.targets_by_name.items():
            self.assertEqual(expected_cpu, target.findtext("./TargetOption/TargetCommonOption/Cpu"))
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
                f"{target_name}: effective APP IROM",
            )
            self.assertEqual(
                ("0x20000000", "0xc000"),
                (
                    memories.findtext("./OCR_RVCT9/StartAddress"),
                    memories.findtext("./OCR_RVCT9/Size"),
                ),
                f"{target_name}: effective IRAM",
            )
            include_paths = [
                normalized_path(item.text or "")
                for item in target.findall(".//IncludePath")
            ]
            self.assertTrue(
                any("../core/app" in item and "../core/system" in item for item in include_paths),
                target_name,
            )
            defines = [
                item.text or "" for item in target.findall(".//Define") if item.text
            ]
            self.assertEqual(1, sum("APROM_OFFSET" in item for item in defines), target_name)

    def test_flash_calibration_and_plan_slots_remain_fixed(self):
        sys_data = read_text("Core/Src/sys_data.h")
        for definition in (
            "#define   DATAROM_STARTADDR               (u32)0x8005000",
            "#define   BAKDATAROM_STARTADDR            (u32)0x8006800",
            "#define   APROM_STARTADDR                 (u32)0x8008000",
            "#define   APROM_SAFE_ENDADDR              (u32)0x8024000",
            "#define   OTABAKROM_STARTADDR             (u32)0x8024000",
        ):
            self.assertIn(definition, sys_data)

        assignments = read_text("Core/Src/flash_address_assignment.h")
        for definition in (
            "#define CURRENT_CAL_FLASH_SLOT_OFFSET      0x400UL",
            "#define CURRENT_CAL_FLASH_SLOT_RESERVED    0x100UL",
            "CURRENT_CAL_STATIC_ASSERT(CURRENT_CAL_FLASH_SLOT_A_ADDR == 0x08005c00UL);",
            "CURRENT_CAL_STATIC_ASSERT(CURRENT_CAL_FLASH_SLOT_B_ADDR == 0x08007400UL);",
        ):
            self.assertIn(definition, assignments)

        plan_source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")
        self.assertIn("#define ZK_PLAN_FLASH_MAIN_ADDR     ((u32)0x08006000)", plan_source)
        self.assertIn("#define ZK_PLAN_FLASH_BACKUP_ADDR   ((u32)0x08007800)", plan_source)

    def test_legacy_10ms_business_runs_from_scheduler_not_isr(self):
        handler = function_body(read_text("Core/Src/stm32f1xx_it.c"), "SysTick_Handler")
        scheduler = function_body(
            read_text("Core/App/app_scheduler.c"), "app_scheduler_process"
        )
        self.assertNotIn("sys_tick_process", handler)
        self.assertIn("APP_SCHEDULER_TASK_LEGACY_10MS", scheduler)
        self.assertIn("sys_tick_process", scheduler)
        self.assertIn("app_scheduler_periodic_is_due", scheduler)

    def test_event_queue_coalesces_only_stateful_critical_events_by_type(self):
        source = read_text("Core/System/sys_event.c")
        coalesce = function_body(source, "sys_event_try_coalesce")
        policy = function_body(source, "sys_event_get_policy")
        coalescible = function_body(source, "sys_event_is_coalescible")

        self.assertIn("if (_queue[queue_index].type == event->type)", coalesce)
        self.assertIn("_queue[queue_index] = *event;", coalesce)
        for event_type in (
            "SYS_EVENT_MQTT_DISCONNECTED",
            "SYS_EVENT_PROTECTION_CHANGED",
        ):
            self.assertIn(event_type, policy)
            self.assertIn(event_type, coalescible)
        for event_type in (
            "SYS_EVENT_POWER_FAIL",
            "SYS_EVENT_STORAGE_FAILED",
            "SYS_EVENT_OTA_FINISHED",
        ):
            self.assertIn(event_type, policy)
            self.assertNotIn(event_type, coalescible)

    def test_noncritical_coalescible_eviction_explicitly_excludes_critical(self):
        source = read_text("Core/System/sys_event.c")
        finder = function_body(source, "sys_event_find_noncritical_coalescible")

        self.assertIn("SYS_EVENT_POLICY_CRITICAL", finder)
        self.assertRegex(
            finder,
            r"sys_event_get_policy\s*\([^)]*\)\s*!=\s*"
            r"SYS_EVENT_POLICY_CRITICAL",
        )
        self.assertIn("sys_event_is_coalescible", finder)

    def test_critical_admission_evicts_low_before_noncritical_coalescible(self):
        source = read_text("Core/System/sys_event.c")
        critical_start = source.index("if (policy == SYS_EVENT_POLICY_CRITICAL)")
        coalescible_start = source.index(
            "else if ((policy == SYS_EVENT_POLICY_COALESCIBLE)", critical_start
        )
        critical_branch = source[critical_start:coalescible_start]

        low_index = critical_branch.index("sys_event_find_policy(SYS_EVENT_POLICY_LOW")
        coalescible_index = critical_branch.index(
            "sys_event_find_noncritical_coalescible"
        )
        self.assertLess(low_index, coalescible_index)

    def test_normal_and_coalescible_admission_never_select_critical_for_eviction(self):
        source = read_text("Core/System/sys_event.c")
        queue_full_start = source.index("if (_queue_count >= SYS_EVENT_QUEUE_CAPACITY)")
        queue_full = source[queue_full_start:source.index("if (_queue_count >=", queue_full_start + 1)]

        self.assertIn("sys_event_find_noncritical_coalescible", queue_full)
        self.assertIn("sys_event_find_policy(SYS_EVENT_POLICY_LOW", queue_full)
        self.assertNotIn("sys_event_find_policy(SYS_EVENT_POLICY_CRITICAL", queue_full)

    def test_portable_time_delay_consumes_requested_ms_and_saturates(self):
        portable = function_body(
            read_text("Core/Src/LampProtocolLib/Portable.c"), "updateTimeTick"
        )

        self.assertIn("TickCount += ms;", portable)
        self.assertIn("if (timeDelay <= ms)", portable)
        self.assertIn("timeDelay = 0;", portable)
        self.assertIn("timeDelay -= ms;", portable)
        self.assertNotIn("timeDelay -= 10", portable)


if __name__ == "__main__":
    unittest.main()
