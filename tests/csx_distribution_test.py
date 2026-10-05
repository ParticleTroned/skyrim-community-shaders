"""Verify the shipped CSX inventory and single-AIO release policy."""

import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import feature_version_audit as audit
import csx_release as release


class CsxDistributionTests(unittest.TestCase):
    def test_inventory_matches_shipped_packages(self):
        builder = importlib.import_module("build-shader-cache")
        profile = builder.derive_distribution_profile(ROOT)
        packages = builder.packaged_feature_directories(ROOT)
        metadata = audit.extract_feature_metadata(ROOT / "src/Features")
        expected = {short for short, package in packages.items()
                    if package not in profile.excluded_packages}
        self.assertEqual({row["short_name"] for row in metadata}, expected)
        self.assertEqual(len(metadata), len(expected))
        by_short_name = {row["short_name"]: row for row in metadata}
        for name in ("TruePBR", "Screenshot", "Wetterness", "UnifiedWater", "ImageBasedLighting", "PerformanceTuning"):
            self.assertIn(name, by_short_name)
        self.assertNotIn("WetnessEffects", by_short_name)
        self.assertEqual(by_short_name["AdaptiveBrightness"]["display_name"], "Adaptive Balance")
        for name in ("TruePBR", "Screenshot", "ScreenSpaceGI"):
            self.assertTrue(by_short_name[name]["description"])
        self.assertIn("Roughness, metallic, and displacement map support", by_short_name["TruePBR"]["key_features"])
        for row in metadata:
            self.assertTrue(row["is_core"])
            self.assertIsNone(row["mod_id"])
            self.assertEqual(row["mod_link"], "")
        lines, issues = audit.format_metadata_summary(metadata)
        text = "\n".join(lines)
        self.assertFalse(issues)
        for name in ("Adaptive Balance", "Performance Tuning", "Performance Overlay"):
            self.assertIn(name, text)
        self.assertNotIn("nexusmods.com", text)
        self.assertNotIn("Is Core", text)
        self.assertEqual(text.count('| Performance Tuning |'), 1)
        self.assertIn('without DevBench', by_short_name['PerformanceTuning']['description'])

    def test_csx_baseline_ignores_upstream_and_pr_tags(self):
        for tags, expected in (
            (b"v99.0.0\ncsx3.18\ncsx3.19.9\ncsx3.19.10\ncsx3.19-PR99\ncsx3.20.0-rc.1", "csx3.19.10"),
            (b"v99.0.0\ncsx3.18\nCSX3.17.2\n", "csx3.18"),
            (b"v99.0.0\ncsx3.19-PR99\n", None),
        ):
            with self.subTest(tags=tags), patch.object(audit.subprocess, "check_output", return_value=tags):
                self.assertEqual(audit.get_latest_release_tag("origin/main-VR"), expected)
        self.assertEqual(audit.DEFAULT_PR_BASE_REF, "origin/main-VR")

    def test_feature_table_is_alphabetical_and_lists_performance_tuning(self):
        metadata = [
            {"name": "zzz", "display_name": "Adaptive Balance", "description": "Profiles", "key_features": []},
            {"name": "aaa", "display_name": "Performance Overlay", "description": "Timings", "key_features": []},
            {"name": "bbb", "display_name": "RenderDoc", "description": "Capture", "key_features": []},
            {"name": "Performance Tuning", "description": "Feature cost controls", "key_features": []},
        ]
        lines, _ = audit.format_metadata_summary(list(reversed(metadata)))
        rows = [line.split('|')[1].strip() for line in lines if line.startswith('| ')][2:]
        self.assertEqual(rows, ["Adaptive Balance", "Performance Overlay", "Performance Tuning",
                                "RenderDoc", "Shader-cache management"])
        self.assertEqual(len(metadata), 4)
        report = audit.generate_audit_report('csx3.19.2', None, None, 'now', [], [], [], {},
                                             lambda _: None, audit.normalize_feature_key, metadata, [])
        self.assertLess(report.index('| Performance Tuning |'), report.index('## Component version audit'))

    def test_audit_appendix_replacement_preserves_prose(self):
        prose = "## CSX release\n\nUser notes.\n\n---\n\n## Install\n\nUse the AIO."
        for heading in ("# Feature Version Audit", "# CSX Feature Audit",
                        "## CSX bundled-feature audit: 3.18 to 3.19.2"):
            for separator in ("\n\n---\n\n", "\n\n"):
                for newline in ("\n", "\r\n"):
                    with self.subTest(heading=heading, separator=separator, newline=newline):
                        body = (prose + separator + heading + "\n\nStale tables.\n").replace("\n", newline)
                        stripped = release.strip_feature_audit(body)
                        self.assertEqual(stripped, prose)
                        self.assertEqual(release.strip_feature_audit(stripped), prose)

    def test_performance_tuning_version_reaches_both_header_registries(self):
        self.assert_feature_header_registries('Performance Tuning', 'PerformanceTuning')

    def test_grass_optimizations_version_reaches_both_header_registries(self):
        self.assert_feature_header_registries('Grass Optimizations', 'GrassOptimizations')

    def assert_feature_header_registries(self, folder, short_name):
        ini = ROOT / 'features' / folder / 'Shaders/Features' / f'{short_name}.ini'
        version = audit.get_version_from_ini(ini)
        self.assertIsNotNone(version)
        self.assertTrue(audit.get_feature_ini_metadata(ini)['audit_version'])
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        start = cmake.index('file(', cmake.index('# # Feature version detection'))
        end = cmake.index('target_sources(', start)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            build = Path(directory) / 'build'
            source.mkdir()
            shutil.copytree(ROOT / 'features' / folder, source / 'features' / folder)
            (source / 'cmake').mkdir()
            shutil.copyfile(ROOT / 'cmake/FeatureVersions.h.in', source / 'cmake/FeatureVersions.h.in')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.24)\nproject(FeatureRegistry NONE)\n'
                + cmake[start:end],
                encoding='utf-8',
            )
            command = ['pwsh', str(ROOT / 'tools/cmake.ps1')] if os.name == 'nt' else ['cmake']
            result = subprocess.run([*command, '-S', str(source), '-B', str(build), '-G', 'Ninja'],
                                    cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for header in (build / 'cmake/FeatureVersions.h', ROOT / 'include/FeatureVersions.h'):
                with self.subTest(header=header):
                    text = header.read_text(encoding='utf-8')
                    registered = re.search(r'\{"' + re.escape(short_name) + r'"sv,\s*\{(\d+),\s*(\d+),\s*(\d+)\}\}', text)
                    self.assertIsNotNone(registered)
                    self.assertEqual(tuple(map(int, registered.groups())), version)
                    self.assertIn(f'"{short_name}"sv', text.split('FEATURE_CORE_NAMES', 1)[1])

    def test_nexus_matrix_is_one_aio_without_inherited_targets(self):
        legacy = [{"name": "Wetterness", "is_core": False, "mod_id": "123", "auto_upload": True}]
        matrix = audit.build_nexus_upload_matrix(legacy, "", "CSX", "CSX_AIO-*.7z")
        self.assertEqual(len(matrix), 1)
        self.assertEqual(matrix[0]["nexus_mod_id"], "")
        self.assertFalse(matrix[0]["auto_upload"])
        matrix = audit.build_nexus_upload_matrix(legacy, "123456", "CSX", "CSX_AIO-*.7z", core_file_group_id="654321")
        self.assertEqual(matrix[0]["file_group_id"], "654321")
        self.assertTrue(matrix[0]["auto_upload"])
        for mod_id, group, pattern in (("86492", "", "CSX_AIO-*.7z"),
                                       ("abc", "", "CSX_AIO-*.7z"),
                                       ("123", "000000", "CSX_AIO-*.7z"),
                                       ("123", "456", "CSX-*.7z"),
                                       ("123", "456", "*.7z")):
            with self.subTest(mod_id=mod_id, group=group, pattern=pattern), self.assertRaises(ValueError):
                audit.build_nexus_upload_matrix([], mod_id, "CSX", pattern, core_file_group_id=group)

    def test_feature_section_reruns_preserve_following_history(self):
        history = "## Complete changes since 3.18\n\n<details>\n<summary>Commits</summary>\n\n- Existing commit\n\n</details>"
        original = "Intro.\n\n" + history
        audit_text = "# CSX Feature Audit\n\n## CSX core features\n\n| Performance Tuning | Yes |"
        updated = release.merge_feature_audit(original, audit_text)
        self.assertLess(updated.index('| Performance Tuning |'), updated.index(history))
        self.assertEqual(release.strip_feature_audit(updated), original)
        self.assertEqual(release.merge_feature_audit(updated, audit_text), updated)
        legacy = "Intro.\n\n---\n\n## CSX bundled-feature audit: 3.18 to 3.19.2\n\n### Features\n\nOld rows.\n\n---\n\n" + history
        self.assertEqual(release.strip_feature_audit(legacy), original)
        self.assertEqual(release.merge_feature_audit(legacy, audit_text), updated)
        with self.assertRaises(ValueError):
            release.strip_feature_audit(updated.replace(release.AUDIT_END, ''))
        with self.assertRaises(ValueError):
            release.merge_feature_audit(original, '')

    def test_matrix_cli_needs_no_git_baseline_or_feature_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "matrix.json"
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/feature_version_audit.py"),
                 "--export-nexus-matrix", "--matrix-output", str(output)],
                cwd=directory, text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(len(json.loads(output.read_text())), 1)
            self.assertIn("CSX AIO", result.stdout)


if __name__ == "__main__":
    unittest.main()
