#!/usr/bin/env python3
"""Exercise release FOMOD runtime selection and its embedded archive assembly."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import yaml

import fomod_package_test as fixtures


REPO = Path(__file__).resolve().parent.parent
WORKFLOW = REPO / ".github/workflows/release-build.yaml"


class ReleaseFomodWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
        cls.steps = cls.workflow["jobs"]["release"]["steps"]
        cls.packaging = next(
            step for step in cls.steps if step["name"] == "Build managed-cache FOMOD AIO"
        )

    @staticmethod
    def _evaluate(expression: str, event_name: str, include_se_ae: bool | None):
        expression = expression.removeprefix("${{").removesuffix("}}")
        for original, replacement in (
            ("github.event_name", "event_name"),
            ("inputs.include-se-ae", "include_se_ae"),
            ("true", "True"),
            ("&&", " and "),
            ("||", " or "),
        ):
            expression = expression.replace(original, replacement)
        return eval(expression.strip(), {"__builtins__": {}}, {
            "event_name": event_name,
            "include_se_ae": include_se_ae,
        })

    def test_runtime_selection_matches_build_download_and_assembly(self) -> None:
        events = self.workflow.get("on", self.workflow.get(True))
        option = events["workflow_dispatch"]["inputs"]["include-se-ae"]
        self.assertEqual(option["type"], "boolean")
        self.assertIs(option["default"], True)
        runtime = self.workflow["jobs"]["shader-cache"]["with"]["runtime"]
        download = next(step for step in self.steps if step["name"] == "Download SE shader cache")
        assembly = self.packaging["env"]["INCLUDE_SE_AE"]
        for event_name, requested, expected in (
            ("workflow_dispatch", option["default"], True),
            ("workflow_dispatch", True, True),
            ("workflow_dispatch", False, False),
            ("push", None, True),
            ("release", None, True),
            ("push", False, True),
            ("release", False, True),
        ):
            with self.subTest(event_name=event_name, requested=requested):
                self.assertEqual(self._evaluate(runtime, event_name, requested), "both" if expected else "VR")
                self.assertIs(self._evaluate(download["if"], event_name, requested), expected)
                self.assertIs(self._evaluate(assembly, event_name, requested), expected)

    def test_embedded_powershell_syntax(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            parser = root / "parse.ps1"
            parser.write_text(
                "param([string]$ScriptPath)\n"
                "$tokens = $null\n$parseErrors = $null\n"
                "[System.Management.Automation.Language.Parser]::ParseFile("
                "$ScriptPath, [ref]$tokens, [ref]$parseErrors) | Out-Null\n"
                "if ($parseErrors.Count) { $parseErrors | Out-String | Write-Error; exit 1 }\n",
                encoding="utf-8",
            )
            for index, step in enumerate(self.steps):
                if step.get("shell") == "pwsh" and "run" in step:
                    with self.subTest(step=step["name"]):
                        script = root / f"step-{index}.ps1"
                        script.write_text(step["run"], encoding="utf-8")
                        self._run(["pwsh", "-NoProfile", "-File", str(parser), str(script)], root)

    def _run(self, command: list[str], cwd: Path, **kwargs) -> subprocess.CompletedProcess:
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False, **kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_archive_assembly_from_sparse_checkout_in_both_modes(self) -> None:
        checkout = next(step for step in self.steps if step["name"] == "Checkout release packaging tools")
        for include_se_ae in (True, False):
            with self.subTest(include_se_ae=include_se_ae), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = root / "checkout"
                source.mkdir()
                for directory in checkout["with"]["sparse-checkout"].splitlines():
                    shutil.copytree(REPO / directory, source / directory)
                inputs = root / "inputs"
                inputs.mkdir()
                core, se_cache, vr_cache = fixtures.FomodPackageTests()._inputs(inputs)
                dist = source / "dist"
                dist.mkdir()
                archives = [("CSX_AIO-test.7z", core), ("ShaderCache-VR-test.7z", vr_cache)]
                if include_se_ae:
                    archives.append(("ShaderCache-SE-test.7z", se_cache))
                for name, archive_root in archives:
                    self._run(["cmake", "-E", "tar", "cf", str(dist / name), "--format=7zip", "--", "."], archive_root)
                cache_archives = {
                    path.name: path.read_bytes() for path in dist.glob("ShaderCache-*.7z")
                }
                runner_temp = root / "runner-temp"
                runner_temp.mkdir()
                environment = {
                    **os.environ,
                    "INCLUDE_SE_AE": str(include_se_ae).lower(),
                    "RELEASE_TAG": "v3.18.0",
                    "RUNNER_TEMP": str(runner_temp),
                    "PATH": str(Path(sys.executable).parent) + os.pathsep + os.environ["PATH"],
                }
                script = root / "assemble.ps1"
                script.write_text(self.packaging["run"], encoding="utf-8")
                self._run(["pwsh", "-NoProfile", "-File", str(script)], source, env=environment)
                self.assertEqual(list(runner_temp.iterdir()), [])
                self.assertEqual({path.name for path in dist.iterdir()}, {name for name, _ in archives})
                for name, content in cache_archives.items():
                    self.assertEqual((dist / name).read_bytes(), content)
                extracted = root / "extracted"
                extracted.mkdir()
                self._run(["cmake", "-E", "tar", "xf", str(dist / "CSX_AIO-test.7z")], extracted)
                expected_roots = {"Core", "fomod", "ShaderCache-VR"}
                if include_se_ae:
                    expected_roots.add("ShaderCache-SE-AE")
                self.assertEqual({path.name for path in extracted.iterdir()}, expected_roots)
                fixtures.BUILDER.validate_staged_package(extracted, "v3.18.0", include_se_ae)
                self.assertEqual((extracted / "Core/core-file.txt").read_bytes(), b"core")


if __name__ == "__main__":
    unittest.main()
