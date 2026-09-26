"""Exercise release allocation refusals and the CMake version handshake."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import csx_release as release


class ReleaseTests(unittest.TestCase):
    def planned(self, expected="3.19.2", tags=None, dirty=False):
        existing = tags or ["csx3.19.1"]

        def git(*args):
            if args[0] == "status":
                return " M changed" if dirty else ""
            if args[0] == "tag":
                return "\n".join(existing)
            if args == ("rev-parse", "HEAD"):
                return "a" * 40
            self.fail(f"Unexpected git request: {args}")

        with patch.object(release, "git", side_effect=git), patch.object(release, "core_line", return_value=(3, 19)):
            return release.plan(expected)

    def test_next_patch(self):
        self.assertEqual(self.planned(), {"version": "3.19.2", "tag": "csx3.19.2",
                                        "base": "csx3.19.1", "source": "a" * 40})

    def test_refuse_dirty_missing_baseline_or_existing_release(self):
        for options in ({"dirty": True}, {"tags": ["csx3.18.9"]},
                        {"tags": ["csx3.19.1", "csx3.19.2"]},
                        {"tags": ["csx3.19.1", "csx3.19.3"]}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.planned(**options)

    def test_refuse_version_injection_skipped_patch_and_line_change(self):
        for value in ("", "3.19.02", "3.19.2\ntag=bad", "3.19.2-rc.1",
                      "3.19.4", "3.20.0", "3.19.65536"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.planned(value)

    def test_numeric_baseline_order(self):
        result = self.planned("3.19.11", tags=["csx3.19.9", "csx3.19.10", "csx3.19-PR16"])
        self.assertEqual(result["base"], "csx3.19.10")

    def test_cmake_versions_and_refusals(self):
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "version.cmake"
            script.write_text(
                f'include("{(ROOT / "cmake/ReleaseVersion.cmake").as_posix()}")\n'
                'csx_resolve_release_version("${RELEASE}" "${CORE}" version label)\n'
                'if(NOT version STREQUAL RELEASE OR NOT label STREQUAL EXPECTED_LABEL)\n'
                '  message(FATAL_ERROR "Version or display label mismatch")\nendif()\n',
                encoding="utf-8",
            )
            for value, core, valid in (
                ("3.19.2", "3.19-VR", True), ("3.15.7", "3.15-SE", True),
                ("3.19.2", "3.15-SE", False), ("03.19.2", "3.19-VR", False),
                ("3.19.2-rc.1", "3.19-VR", False), ("3.19.65536", "3.19-VR", False),
            ):
                with self.subTest(value=value, core=core):
                    result = subprocess.run(
                        ["cmake", f"-DRELEASE={value}", f"-DCORE={core}",
                         f"-DEXPECTED_LABEL={value}-{core.rsplit('-', 1)[-1]}", "-P", str(script)],
                        text=True, capture_output=True,
                    )
                    self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
