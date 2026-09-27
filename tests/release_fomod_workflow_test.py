#!/usr/bin/env python3
"""Exercise release FOMOD runtime selection and its embedded archive assembly."""

from __future__ import annotations

import os
import fnmatch
import json
from pathlib import Path
import re
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

    def test_only_complete_aio_is_published_and_attested(self) -> None:
        publish = next(step for step in self.steps if step["name"] == "Create or Update Release")
        attest = next(step for step in self.steps if step["name"] == "Generate artifact attestations")
        self.assertEqual(attest["with"]["subject-path"], "dist/CSX_AIO-*.7z")
        self.assertEqual(publish["with"]["artifacts"], "${{ github.workspace }}/dist/CSX_AIO-*.7z")
        assets = ["CSX_AIO-3.19.2-VR.7z", "CSX-3.19.2-VR.7z", "Wetterness-3.19.2-VR.7z",
                  "ShaderCache-VR-csx3.19.2.7z", "ShaderCache-SE-csx3.19.2.7z"]
        selected = [name for name in assets if fnmatch.fnmatchcase("dist/" + name, attest["with"]["subject-path"])]
        self.assertEqual(selected, [assets[0]])
        self.assertIn("release_name", publish["with"]["name"])

    def test_release_requires_generated_csx_feature_notes(self) -> None:
        audit = self.workflow["jobs"]["feature-audit"]
        self.assertFalse(audit.get("continue-on-error", False))
        self.assertIn("needs.feature-audit.result == 'success'", self.workflow["jobs"]["release"]["if"])
        download = next(step for step in self.steps if step["name"] == "Download feature audit artifact")
        self.assertFalse(download.get("continue-on-error", False))
        notes = next(step for step in self.steps if step["name"] == "Generate combined release notes")
        self.assertIn("python tools/csx_release.py merge-audit", notes["run"])
        self.assertNotIn("if [ -f feature-version-audit-latest.md ]", notes["run"])

    def test_nexus_plan_rejects_missing_or_ambiguous_aio(self) -> None:
        nexus = yaml.safe_load((REPO / ".github/workflows/nexus-upload.yaml").read_text(encoding="utf-8"))
        events = nexus.get("on", nexus.get(True))
        for trigger in ("workflow_dispatch", "workflow_call"):
            inputs = events[trigger]["inputs"]
            self.assertEqual(inputs["artifact_pattern"]["default"], "CSX_AIO-*.7z")
            self.assertEqual(inputs["nexus_mod_id"]["default"], "")
            self.assertEqual(inputs["nexus_file_group_id"]["default"], "")
        generate = next(step for step in nexus["jobs"]["prepare-nexus-matrix"]["steps"] if step.get("id") == "generate")
        program = re.search(r'python -c "\n(.*?)\n"', generate["run"], re.DOTALL).group(1)
        env = {**os.environ, "PYTHONPATH": str(REPO / "tools")}
        for assets, valid in (([], False), (["Wetterness-3.19.2-VR.7z"], False),
                              (["CSX_AIO-one.7z", "CSX_AIO-two.7z"], False),
                              (["CSX_AIO-3.19.2-VR.7z", "CSX-3.19.2-VR.7z"], True)):
            for configured in (False, True):
                with self.subTest(assets=assets, configured=configured), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / "release.json").write_text(json.dumps({
                        "body": "Release notes.\n\n---\n\n# CSX Feature Audit\nTable.",
                        "assets": [{"name": name} for name in assets],
                    }))
                    (root / "nexus-matrix-raw.json").write_text(json.dumps([{
                        "name": "core", "artifact_pattern": "CSX_AIO-*.7z", "auto_upload": configured,
                    }]))
                    result = subprocess.run([sys.executable, "-c", program], cwd=root,
                                            env=env, text=True, capture_output=True)
                    self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
                    if valid:
                        matrix = json.loads((root / "nexus-matrix.json").read_text())
                        self.assertEqual(matrix["include"][0]["changelog"], "Release notes.")
                        state = json.loads((root / "nexus-upload-state.json").read_text())
                        self.assertIs(state["has_uploads"], configured)

    def test_nexus_workflow_requires_explicit_csx_target(self) -> None:
        nexus = yaml.safe_load((REPO / ".github/workflows/nexus-upload.yaml").read_text(encoding="utf-8"))
        generate = next(step for step in nexus["jobs"]["prepare-nexus-matrix"]["steps"] if step.get("id") == "generate")
        bash = str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe") if os.name == "nt" else "bash"
        for dry_run, mod_id, group, valid in (("true", "", "", True),
                                              ("false", "", "", False),
                                              ("false", "123456", "", False),
                                              ("false", "86492", "654321", False),
                                              ("false", "123456", "654321", True)):
            with self.subTest(dry_run=dry_run, mod_id=mod_id, group=group), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "tools").mkdir()
                for name in ("feature_version_audit.py", "csx_release.py"):
                    shutil.copyfile(REPO / "tools" / name, root / "tools" / name)
                (root / "release.json").write_text(json.dumps({
                    "body": "CSX release.", "assets": [{"name": "CSX_AIO-3.19.2-VR.7z"}],
                }))
                script = root / "generate.sh"
                script.write_text(generate["run"], encoding="utf-8", newline="\n")
                env = {**os.environ, "DRY_RUN": dry_run, "INPUT_NEXUS_MOD_ID": mod_id,
                       "INPUT_NEXUS_FILE_GROUP_ID": group, "INPUT_MOD_FILENAME": "CSX",
                       "INPUT_ARTIFACT_PATTERN": "CSX_AIO-*.7z", "RELEASE_TAG": "csx3.19.2",
                       "GITHUB_OUTPUT": (root / "output.txt").as_posix(),
                       "PATH": str(Path(sys.executable).parent) + os.pathsep + os.environ["PATH"]}
                result = subprocess.run([bash, str(script)], cwd=root, env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
                if valid:
                    self.assertIn("has_uploads=" + str(bool(mod_id)).lower(), (root / "output.txt").read_text())

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
