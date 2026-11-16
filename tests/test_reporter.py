import datetime
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReporterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir = tempfile.TemporaryDirectory()
        cls.driver = Path(cls.build_dir.name) / "reporter-test"
        subprocess.run(
            ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             str(ROOT / "tests/reporter_driver.c"),
             str(ROOT / "src/reporter/reporter.c"),
             str(ROOT / "src/utils.c"),
             str(ROOT / "third_party/cjson/cJSON.c"),
             "-Wl,--wrap=time", "-o", str(cls.driver)],
            capture_output=True, text=True, check=True, timeout=30,
        )

    @classmethod
    def tearDownClass(cls):
        cls.build_dir.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.config = self.directory / "config.json"
        self.reports = self.directory / "reports"
        self.reports.mkdir()

    def configure(self, value):
        self.config.write_text(json.dumps({
            "properties": {"report-properties": {"day-range": value}}
        }))

    def add_reports(self, *names):
        for name in names:
            (self.reports / name).touch()

    def run_reporter(self, now, report_dir=None):
        env = os.environ.copy()
        env["TZ"] = "Pacific/Honolulu"
        result = subprocess.run(
            [str(self.driver), str(self.config),
             str(report_dir if report_dir is not None else self.reports),
             str(int(now.timestamp()))],
            capture_output=True, text=True, env=env, timeout=10,
        )
        files = sorted(line.removeprefix("FILE:") for line in result.stdout.splitlines()
                       if line.startswith("FILE:"))
        return result, files

    def test_inclusive_configured_utc_range_and_filename_format(self):
        self.configure([3, 20])
        self.add_reports(
            "report-20261023T235959Z.txt",
            "report-20261024T000000Z.txt",
            "report-20261101T120000Z.txt",
            "report-20261110T235959Z.txt",
            "report-20261111T000000Z.txt",
            "report-20261024T000000Z.txt.bak",
            "report-20261024T000000X.txt",
            "report-20261101T12x000Z.txt",
            "other-20261101T120000Z.txt",
        )
        now = datetime.datetime(2026, 11, 13, 1, tzinfo=datetime.timezone.utc)
        result, files = self.run_reporter(now)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files, [
            "report-20261024T000000Z.txt",
            "report-20261101T120000Z.txt",
            "report-20261110T235959Z.txt",
        ])

    def test_same_day_and_year_boundary(self):
        self.configure([0, 0])
        self.add_reports("report-20251231T235959Z.txt", "report-20260101T000000Z.txt")
        now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
        result, files = self.run_reporter(now)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files, ["report-20260101T000000Z.txt"])

        self.configure([0, 1])
        result, files = self.run_reporter(now)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(files), 2)

    def test_leap_day_and_empty_results(self):
        self.configure([1, 1])
        now = datetime.datetime(2024, 3, 1, tzinfo=datetime.timezone.utc)
        result, files = self.run_reporter(now)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files, [])
        self.add_reports("report-20240229T120000Z.txt")
        result, files = self.run_reporter(now)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(files, ["report-20240229T120000Z.txt"])

    def test_invalid_ranges(self):
        now = datetime.datetime(2026, 11, 13, tzinfo=datetime.timezone.utc)
        for value in ([], [0], [0, 1, 2], [-1, 3], [3, 2],
                      [0.5, 3], ["0", 3], [0, None], [0, 2147483648]):
            with self.subTest(value=value):
                self.configure(value)
                result, files = self.run_reporter(now)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid properties.report-properties.day-range", result.stderr)
                self.assertEqual(files, [])

    def test_invalid_config_and_missing_directory(self):
        now = datetime.datetime(2026, 11, 13, tzinfo=datetime.timezone.utc)
        self.config.write_text('{"properties": {}}')
        result, _ = self.run_reporter(now)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid properties.report-properties.day-range", result.stderr)
        self.config.write_text("{invalid")
        result, _ = self.run_reporter(now)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not parse configuration", result.stderr)
        self.configure([0, 20])
        result, _ = self.run_reporter(now, self.directory / "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not open report directory", result.stderr)

    def test_unrepresentable_date_is_an_error(self):
        self.configure([0, 2147483647])
        now = datetime.datetime(2026, 11, 13, tzinfo=datetime.timezone.utc)
        result, files = self.run_reporter(now)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not calculate UTC report date", result.stderr)
        self.assertEqual(files, [])


if __name__ == "__main__":
    unittest.main()
