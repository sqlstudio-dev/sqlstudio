"""Run with: python3 -m unittest discover -s testing/python -p test_mysql_versions.py"""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


# utils only needs GRT's logger at import time; version checks need no GUI.
source = Path(__file__).resolve().parents[2] / "library/python/studio/utils.py"
spec = importlib.util.spec_from_file_location("studio_version_utils", source)
utils = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"grt": types.SimpleNamespace(log_error=lambda *args: None)}):
    spec.loader.exec_module(utils)
Version = utils.Version


class MySQLVersionTests(unittest.TestCase):
    def test_known_release_lines(self):
        for version in ("5.6.51", "5.7.44", "8.0.36", "8.2.0", "8.4.0",
                        "9.0.0", "9.7.0", "26.7.0", "26.7.1-commercial", "26.10.0", "9.7.2", "8.4.13", "8.0.46"):
            with self.subTest(version=version):
                self.assertTrue(Version.fromstr(version).is_supported_mysql_version())

    def test_unknown_and_non_mysql_release_lines(self):
        for version in ("5.5.62", "6.0.0", "8.5.0", "9.8.0", "10.11.0-MariaDB",
                        "11.4.0-MariaDB", "26.6.0", "26.11.0", "27.7.0"):
            with self.subTest(version=version):
                self.assertFalse(Version.fromstr(version).is_supported_mysql_version())

    def test_features_on_new_servers(self):
        for version in ("8.2.0", "8.4.0", "9.7.0", "26.7.0", "26.10.0"):
            with self.subTest(version=version):
                server = Version.fromstr(version)
                for feature in ((5, 6, 0), (5, 7, 8), (8, 0, 22)):
                    self.assertTrue(server.is_supported_mysql_version_at_least(*feature))
                self.assertTrue(server.is_supported_mysql_version_at_least(server))
        self.assertFalse(Version(26, 7, 0).is_supported_mysql_version_at_least(26, 7, 1))
        self.assertFalse(Version(8, 2, 0).is_supported_mysql_version_at_least(8, 4, 0))
        self.assertFalse(Version(10, 11, 0).is_supported_mysql_version_at_least(8, 0, 22))
        self.assertTrue(Version(5, 5, 5).is_supported_mysql_version_at_least(5, 5, 5))


if __name__ == "__main__":
    unittest.main()
