from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "validate_changed_shaders", ROOT / "tools/validate_changed_shaders.py"
)
assert SPEC and SPEC.loader
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class ValidateChangedShadersTests(unittest.TestCase):
    def test_command_option_accepts_both_supported_forms(self) -> None:
        self.assertEqual(
            VALIDATOR._command_option(["compiler", "--output-dir", "cache"], "--output-dir"),
            "cache",
        )
        self.assertEqual(
            VALIDATOR._command_option(["compiler", "--output-dir=cache"], "--output-dir"),
            "cache",
        )

    def test_run_creates_output_directory_before_compiler_starts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_dir = Path(temporary) / "nested" / "cache"

            def completed(command: list[str]) -> subprocess.CompletedProcess[str]:
                self.assertTrue(output_dir.is_dir())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(VALIDATOR.subprocess, "run", side_effect=completed):
                result = VALIDATOR._run(
                    ["hlslkit-compile", "--output-dir", str(output_dir)],
                    dry_run=False,
                )

            self.assertEqual(result, 0)

    def test_dry_run_does_not_create_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_dir = Path(temporary) / "cache"
            result = VALIDATOR._run(
                ["hlslkit-compile", f"--output-dir={output_dir}"],
                dry_run=True,
            )
            self.assertEqual(result, 0)
            self.assertFalse(output_dir.exists())

    def test_output_directory_failure_prevents_compiler_start(self) -> None:
        with (
            mock.patch.object(VALIDATOR.os, "makedirs", side_effect=OSError("denied")),
            mock.patch.object(VALIDATOR.subprocess, "run") as run,
        ):
            result = VALIDATOR._run(
                ["hlslkit-compile", "--output-dir", "cache"],
                dry_run=False,
            )

        self.assertEqual(result, 1)
        run.assert_not_called()

    def test_compiler_launch_failure_returns_error(self) -> None:
        with mock.patch.object(
            VALIDATOR.subprocess, "run", side_effect=OSError("missing compiler")
        ):
            result = VALIDATOR._run(["missing-compiler"], dry_run=False)

        self.assertEqual(result, 1)


if __name__ == "__main__":
    unittest.main()
