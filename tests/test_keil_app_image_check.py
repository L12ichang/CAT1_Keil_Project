import unittest

from tools.check_keil_app_image import APP_BASE, APP_SAFE_END, ROOT, run_checks


class KeilAppImageCheckTest(unittest.TestCase):
    def test_8008000_project_matches_app_partition_when_freshness_is_skipped(self):
        report = run_checks(
            ROOT / "MDK-ARM-8008000" / "project.uvprojx",
            app_base=APP_BASE,
            safe_end=APP_SAFE_END,
            require_fresh=False,
        )
        self.assertEqual([], report.errors)
        self.assertEqual("0x08005000", report.details["source_flash_contract"]["DATAROM_STARTADDR"])
        self.assertEqual("0x08006800", report.details["source_flash_contract"]["BAKDATAROM_STARTADDR"])
        self.assertEqual("0x08024000", report.details["source_flash_contract"]["APROM_SAFE_ENDADDR"])
        self.assertEqual(408, report.details["sys_data_expected_size"])
        self.assertGreaterEqual(report.details["zk_json_rx_max"], 2048)
        self.assertGreaterEqual(report.details["zk_json_tx_size"], 2048)
        if report.details.get("keil_outputs_present"):
            self.assertEqual("0x08008000", report.details["sct_base"])
            self.assertEqual("0x08008000", report.details["map_values"]["vectors"])
        else:
            self.assertIn("missing_keil_outputs", report.details)
            self.assertTrue(any("rebuild in Keil" in warning for warning in report.warnings))

    def test_non_app_project_is_rejected_for_app_burning(self):
        report = run_checks(
            ROOT / "MDK-ARM-LEGACY" / "project.uvprojx",
            app_base=APP_BASE,
            safe_end=APP_SAFE_END,
            require_fresh=False,
        )
        self.assertTrue(report.errors)
        self.assertTrue(
            any("Project does not exist" in error for error in report.errors)
            or any("Wrong Keil project" in error for error in report.errors)
        )


if __name__ == "__main__":
    unittest.main()
