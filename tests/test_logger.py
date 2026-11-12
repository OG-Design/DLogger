import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("LOGGER_TEST_BINARY", ROOT / "dist/out/main"))


def active_service():
    try:
        result = subprocess.run(
            ["systemctl", "list-units", "--type=service", "--state=active",
             "--no-legend", "--plain", "--no-pager"],
            capture_output=True, text=True, timeout=10, check=True,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields and fields[0].endswith(".service"):
            return fields[0]
    return None


class LoggerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.config = Path(self.tmp.name) / "config.json"

    def write_config(self, services):
        self.config.write_text(json.dumps({"services": services, "procs": []}))

    def run_logger(self, *args, env=None):
        return subprocess.run(
            [str(BINARY), "-f", str(self.config), *args],
            cwd=ROOT, capture_output=True, text=True, env=env, timeout=15,
        )

    def test_invalid_service_entry_is_rejected(self):
        self.write_config([123])
        result = self.run_logger()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-string service", result.stderr)

    def test_invalid_json_is_rejected(self):
        self.config.write_text("{invalid")
        result = self.run_logger()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not parse", result.stderr)

    def test_unseekable_config_is_rejected(self):
        result = subprocess.run(
            [str(BINARY), "-f", "/dev/stdin"], input="{}", cwd=ROOT,
            capture_output=True, text=True, timeout=15,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed to seek config", result.stderr)

    def test_unavailable_bus_is_an_error(self):
        self.write_config([])
        env = os.environ.copy()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=/nonexistent/logger-test-bus"
        result = self.run_logger(env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed to connect to system bus", result.stderr)

    def test_add_and_duplicate_service(self):
        self.write_config([])
        for _ in range(2):
            result = self.run_logger("-s", "example.service")
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.config.read_text())["services"], ["example.service"])

    def test_once_and_repeated_watch(self):
        service = active_service()
        if service is None:
            self.skipTest("No accessible systemd manager with an active service")
        self.write_config([service])

        result = self.run_logger()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("Getting configured services."), 1)
        self.assertIn(f"SERVICE: {service}", result.stdout)
        self.assertIn(f"Service: {service}", result.stdout)

        output_path = Path(self.tmp.name) / "watch.log"
        with output_path.open("w") as output:
            process = subprocess.Popen(
                [str(BINARY), "-f", str(self.config), "-w", "1"],
                cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                snapshots = 0
                first_fds = None
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    time.sleep(0.1)
                    text = output_path.read_text()
                    snapshots = text.count("Getting configured services.")
                    self.assertIsNone(process.poll(), text)
                    if snapshots >= 2 and first_fds is None:
                        first_fds = len(os.listdir(f"/proc/{process.pid}/fd"))
                    if snapshots >= 6:
                        self.assertLessEqual(
                            len(os.listdir(f"/proc/{process.pid}/fd")), first_fds + 1
                        )
                        break
                self.assertGreaterEqual(snapshots, 6, text)
            finally:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=5)

    def test_allocation_failures_release_resources(self):
        if active_service() is None:
            self.skipTest("No accessible systemd manager with an active service")
        self.write_config([])
        binary = Path(self.tmp.name) / "allocation-test"
        subprocess.run(
            ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
             str(ROOT / "tests/test_allocation_failures.c"),
             str(ROOT / "src/systemd/systemd.c"), str(ROOT / "src/utils.c"),
             str(ROOT / "third_party/cjson/cJSON.c"), "-lsystemd",
             "-Wl,--wrap=malloc", "-Wl,--wrap=realloc", "-Wl,--wrap=strdup",
             "-o", str(binary)],
            cwd=ROOT, capture_output=True, text=True, timeout=30, check=True,
        )
        result = subprocess.run(
            [str(binary), str(self.config)], cwd=ROOT,
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
