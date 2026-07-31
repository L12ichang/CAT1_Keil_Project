import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Phase3MqttProductionCHarnessTests(unittest.TestCase):
    def test_production_mqtt_host_harness(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        harness = ROOT / "tests" / "phase3_mqtt_host_harness.c"
        with tempfile.TemporaryDirectory(prefix="phase3_mqtt_") as temp:
            executable = Path(temp) / "phase3_mqtt_host"
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
                msg=f"MQTT host harness compile failed:\n"
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
                msg=f"MQTT host harness failed:\n"
                f"{ran.stdout}\n{ran.stderr}",
            )
            self.assertIn(
                "phase3 MQTT production-C harness: PASS",
                ran.stdout,
            )


if __name__ == "__main__":
    unittest.main()
