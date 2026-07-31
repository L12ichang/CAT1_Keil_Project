import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Phase3CellularProductionCHarnessTests(unittest.TestCase):
    def test_production_cellular_host_harness(self):
        harness = ROOT / "tests" / "phase3_cellular_host_harness.c"
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "phase3_cellular_host_harness"
            compile_result = subprocess.run(
                [
                    "clang",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "Core" / "System"),
                    "-I",
                    str(ROOT / "Core" / "Config"),
                    "-I",
                    str(ROOT / "Core" / "Src"),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                compile_result.returncode,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(binary)],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                0,
                run_result.returncode,
                run_result.stdout + run_result.stderr,
            )
            self.assertIn("PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
