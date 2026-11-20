import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
_binary = Path(os.environ.get("LOGGER_TEST_BINARY", str(ROOT / "dist/out/dlogger")))
BINARY = _binary if _binary.is_absolute() else (ROOT / _binary).resolve()


class SMTPTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.config = Path(self.tmp.name) / "config.json"
        self.workdir = Path(self.tmp.name) / "work"
        self.workdir.mkdir()
        self.env = os.environ.copy()
        self.env["DLOGGER_MODE"] = "dev"

    def run_logger(self, *args):
        return subprocess.run(
            [str(BINARY), "-f", str(self.config), *args],
            cwd=self.workdir, capture_output=True, text=True, env=self.env, timeout=15,
        )

    def test_add_mail_recipient_adds_new_entry_with_expiry(self):
        self.config.write_text(json.dumps({"service-accounts": []}))
        result = self.run_logger("-m", "first@example.com")
        self.assertEqual(result.returncode, 0, result.stderr)

        config = json.loads(self.config.read_text())
        self.assertEqual(len(config["service-accounts"]), 1)
        entry = config["service-accounts"][0]
        self.assertEqual(entry["to"], "first@example.com")
        self.assertRegex(entry["expires"], r"^\d{8}T\d{6}Z$")

    def test_add_mail_recipient_ignores_duplicates(self):
        self.config.write_text(json.dumps({"service-accounts": []}))
        first = self.run_logger("-m", "dupe@example.com")
        second = self.run_logger("-m", "dupe@example.com")
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)

        config = json.loads(self.config.read_text())
        self.assertEqual(
            [item["to"] for item in config["service-accounts"]],
            ["dupe@example.com"],
        )

    def test_add_mail_recipient_requires_service_accounts_array(self):
        self.config.write_text(json.dumps({"service-accounts": {}}))
        result = self.run_logger("-m", "missing@example.com")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("service accounts", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
