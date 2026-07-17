#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/ota_test/jlink_verify.ps1"


class JLinkVerifyContractTests(unittest.TestCase):
    def test_verify_failure_cannot_return_success(self):
        source = SCRIPT.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("$jlinkExitCode = $LASTEXITCODE", source)
        self.assertIn("if ($jlinkExitCode -ne 0)", source)
        self.assertIn("^Verify successful\\.$", source)
        self.assertIn("J-Link verify did not succeed", source)


if __name__ == "__main__":
    unittest.main()
