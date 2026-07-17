#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import mcu_workflow
from tools.mcu_workflow import (
    APP_VERSION_PATTERN,
    FLASH_LIMIT,
    infer_level,
    infer_module,
    load_config,
    parse_nm_symbols,
    parse_log_entry,
    parse_jlink_pc,
    parse_jlink_word,
    parse_readelf_sections,
    parse_readelf_symbols,
    parse_mem32_values,
    parse_size_sections,
    parse_size_summary,
    sha256_of_file,
    flash_firmware,
)


class McuWorkflowTests(unittest.TestCase):
    def test_load_config(self):
        data = load_config(Path("config/mcu_workflow.json"))
        self.assertEqual(data["jlink"]["device"], "STM32F103RC")
        self.assertEqual(data["serial"]["baudrate"], 1000000)
        self.assertEqual(data["build"]["flash_base"], "0x08008000")

    def test_flash_limit_stops_before_ota_partition(self):
        self.assertEqual(FLASH_LIMIT, 0x08024000)

    def test_parse_mem32_values(self):
        output = "0xE0042000 = 0x0414C3F1\n0x1FFFF7E8 = 0x12345678\n"
        values = parse_mem32_values(output)
        self.assertEqual(values["0xE0042000"], "0x0414C3F1")
        self.assertEqual(values["0x1FFFF7E8"], "0x12345678")

    def test_parse_jlink_pc_and_vtor_without_0x_prefix(self):
        output = "PC = 0800E51E\nE000ED08 = 08008000\n"
        self.assertEqual(parse_jlink_pc(output), 0x0800E51E)
        self.assertEqual(parse_jlink_word(output, 0xE000ED08), 0x08008000)

    def test_parse_jlink_pc_uses_last_status_after_release(self):
        output = "PC = 080028E4\nPC = 0800F940\nE000ED08 = 08008000\n"
        self.assertEqual(parse_jlink_pc(output), 0x0800F940)

    def test_parse_log_entry(self):
        entry = parse_log_entry("[MQTT] login failed by timeout")
        self.assertEqual(entry["module"], "MQTT")
        self.assertEqual(entry["level"], "ERROR")

    def test_infer_level(self):
        self.assertEqual(infer_level("warning: retry again"), "WARN")
        self.assertEqual(infer_level("debug trace enabled"), "DEBUG")
        self.assertEqual(infer_level("boot complete"), "INFO")

    def test_infer_module(self):
        self.assertEqual(infer_module("[FLASH] verify done"), "FLASH")
        self.assertEqual(infer_module("mqtt connected"), "MQTT")
        self.assertEqual(infer_module("system ready"), "CORE")

    def test_sha256_of_file(self):
        with tempfile.NamedTemporaryFile("wb", delete=False) as handle:
            handle.write(b"abc")
            path = Path(handle.name)
        try:
            self.assertEqual(
                sha256_of_file(path),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            )
        finally:
            path.unlink(missing_ok=True)

    def test_common_header_version_pattern(self):
        text = "#define APP_VERSION         (u16)1"
        match = APP_VERSION_PATTERN.search(text)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "1")

    def test_parse_nm_symbols(self):
        output = "08008000 R g_pfnVectors\n2000C000 R _estack\n"
        values = parse_nm_symbols(output)
        self.assertEqual(values["g_pfnVectors"], 0x08008000)
        self.assertEqual(values["_estack"], 0x2000C000)

    def test_parse_readelf_sections(self):
        output = "  [ 1] .isr_vector       PROGBITS        08008000 001000 000130 00   A  0   0  1\n"
        values = parse_readelf_sections(output)
        self.assertEqual(values[".isr_vector"]["addr"], 0x08008000)
        self.assertEqual(values[".isr_vector"]["size"], 0x130)

    def test_parse_readelf_symbols(self):
        output = "  3619: 08008200     4 OBJECT  GLOBAL DEFAULT    1 prog_checksum\n"
        values = parse_readelf_symbols(output)
        self.assertEqual(values["prog_checksum"], 0x08008200)

    def test_parse_size_summary(self):
        output = "   text\t   data\t    bss\t    dec\t    hex\tfilename\n  59652\t   1152\t  25984\t  86788\t  15304\tbuild/pro.elf\n"
        values = parse_size_summary(output)
        self.assertEqual(values["text"], 59652)
        self.assertEqual(values["data"], 1152)
        self.assertEqual(values["bss"], 25984)

    def test_parse_size_sections(self):
        output = "build/pro.elf  :\nsection                size        addr\n.isr_vector             304   134250496\n.data                  1144   536870912\nTotal               1298160\n"
        values = parse_size_sections(output)
        self.assertEqual(values[".isr_vector"]["size"], 304)
        self.assertEqual(values[".data"]["addr"], 536870912)

    def test_flash_firmware_uses_explicit_app_base_for_elf(self):
        with tempfile.NamedTemporaryFile("wb", suffix=".bin", delete=False) as bin_handle:
            bin_handle.write(b"\x01\x02\x03\x04")
            normalized_bin = Path(bin_handle.name)
        with tempfile.NamedTemporaryFile("wb", suffix=".elf", delete=False) as elf_handle:
            elf_path = Path(elf_handle.name)

        commands_seen = []
        release_checks = 0

        def fake_jlink(commands, config, timeout=30):
            nonlocal release_checks
            commands_seen.append(commands)
            for command in commands:
                if command.startswith("savebin "):
                    target = Path(command.split(" ", 1)[1].split(",", 1)[0])
                    target.write_bytes(normalized_bin.read_bytes())
            if "regs" in commands:
                release_checks += 1
                if release_checks == 1:
                    return "PC = 08002776\n0xE000ED08 = 0x08000000\n"
                return "PC = 0800E51E\n0xE000ED08 = 0x08008000\n"
            return ""

        config = {
            "build": {"flash_base": "0x08008000"},
            "jlink": {"device": "STM32F103RC", "interface": "SWD", "speed_khz": 4000},
        }
        try:
            with mock.patch.object(mcu_workflow, "normalize_firmware_to_bin", return_value=(normalized_bin, False)):
                with mock.patch.object(mcu_workflow, "jlink_commander", side_effect=fake_jlink):
                    result = flash_firmware(config, elf_path)
        finally:
            normalized_bin.unlink(missing_ok=True)
            elf_path.unlink(missing_ok=True)

        flat_commands = "\n".join(command for group in commands_seen for command in group)
        self.assertIn(f"loadbin {normalized_bin},0x08008000", flat_commands)
        self.assertIn(f"savebin", flat_commands)
        self.assertIn("0x4", flat_commands)
        self.assertIn("sleep 500", flat_commands)
        self.assertIn("mem32 0xE000ED08,1", flat_commands)
        self.assertIn("w1 0x20000000 0x01", flat_commands)
        self.assertNotIn("loadfile", flat_commands)
        self.assertEqual(result["base_address"], "0x08008000")
        self.assertEqual(result["release"]["pc"], "0x0800E51E")


if __name__ == "__main__":
    unittest.main()
