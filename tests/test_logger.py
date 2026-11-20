import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
_binary = Path(os.environ.get("LOGGER_TEST_BINARY", str(ROOT / "dist/out/dlogger")))
BINARY = _binary if _binary.is_absolute() else (ROOT / _binary).resolve()


def active_services():
    try:
        result = subprocess.run(
            ["systemctl", "list-units", "--type=service", "--state=running",
             "--no-legend", "--plain", "--no-pager"],
            capture_output=True, text=True, timeout=10, check=True,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    services = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields and fields[0].endswith(".service"):
            services.append(fields[0])
    return services


def active_service():
    services = active_services()
    return services[0] if services else None


class LoggerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.config = Path(self.tmp.name) / "config.json"
        self.workdir = Path(self.tmp.name) / "work"
        self.report_dir = self.workdir / "data/reports"
        self.report_dir.mkdir(parents=True)

    def write_config(self, services):
        self.config.write_text(json.dumps({"services": services, "procs": []}))

    def run_logger(self, *args, env=None):
        return subprocess.run(
            [str(BINARY), "-f", str(self.config), *args],
            cwd=self.workdir, capture_output=True, text=True, env=env, timeout=15,
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

    def test_empty_report(self):
        if active_service() is None:
            self.skipTest("No accessible systemd manager with an active service")
        self.write_config([])
        result = self.run_logger()
        self.assertEqual(result.returncode, 0, result.stderr)
        reports = list(self.report_dir.glob("report-*.txt"))
        self.assertEqual(len(reports), 1)
        self.assertEqual(reports[0].read_text(), "")

    def test_missing_report_directory_is_an_error(self):
        if active_service() is None:
            self.skipTest("No accessible systemd manager with an active service")
        self.write_config([])
        self.report_dir.rmdir()
        result = self.run_logger()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not create report", result.stderr)

    def test_report_contains_processes_for_multiple_services(self):
        services = active_services()[:2]
        if len(services) < 2:
            self.skipTest("Need two running systemd services")
        self.write_config(services)
        result = self.run_logger()
        self.assertEqual(result.returncode, 0, result.stderr)
        reports = list(self.report_dir.glob("report-*.txt"))
        self.assertEqual(len(reports), 1)
        report = reports[0].read_text()

        for service in services:
            self.assertIn(f"SERVICE: {service}\n", report)
            self.assertIn(f"Service: {service}\n", report)
        self.assertIn("\tPID:", report)
        self.assertIn(" Cgroup: ", report)

    def test_once_and_repeated_watch(self):
        service = active_service()
        if service is None:
            self.skipTest("No accessible systemd manager with an active service")
        self.write_config([service])

        result = self.run_logger()
        self.assertEqual(result.returncode, 0, result.stderr)
        initial_reports = list(self.report_dir.glob("report-*.txt"))
        self.assertEqual(len(initial_reports), 1)
        self.assertIn(f"SERVICE: {service}", initial_reports[0].read_text())

        output_path = Path(self.tmp.name) / "watch.log"
        watch_dir = Path(self.tmp.name) / "watch"
        watch_reports = watch_dir / "data/reports"
        watch_reports.mkdir(parents=True)
        with output_path.open("w") as output:
            process = subprocess.Popen(
                [str(BINARY), "-f", str(self.config), "-w", "1"],
                cwd=watch_dir, stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                snapshots = 0
                first_fds = None
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    time.sleep(0.1)
                    text = output_path.read_text()
                    snapshots = len(list(watch_reports.glob("report-*.txt")))
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
        compile_cmd = [
            "gcc", "-std=gnu11", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
            str(ROOT / "tests/test_allocation_failures.c"),
            str(ROOT / "src/systemd/systemd.c"), str(ROOT / "src/utils.c"),
            str(ROOT / "src/reporter/reporter.c"),
            str(ROOT / "third_party/cjson/cJSON.c"), "-lsystemd",
            "-Wl,--wrap=malloc", "-Wl,--wrap=realloc", "-Wl,--wrap=strdup",
            "-o", str(binary),
        ]
        try:
            subprocess.run(
                compile_cmd,
                cwd=ROOT, capture_output=True, text=True, timeout=30, check=True,
            )
        except subprocess.CalledProcessError as exc:
            combined = f"{exc.stdout}\n{exc.stderr}"
            if "systemd/sd-bus.h" in combined or "cannot find -lsystemd" in combined:
                self.skipTest("libsystemd development files are unavailable")
            raise
        result = subprocess.run(
            [str(binary), str(self.config)], cwd=ROOT,
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
