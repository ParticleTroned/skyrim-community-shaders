#!/usr/bin/env python3
"""Regression tests for version-derived shader-cache FOMOD metadata."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
BUILDER_PATH = REPO / "tools/build-shader-cache.py"
SPEC = importlib.util.spec_from_file_location("build_shader_cache", BUILDER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load shader-cache builder: {BUILDER_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class ShaderCachePackagingTests(unittest.TestCase):
    def test_both_caches_default_to_the_release_core_identity(self) -> None:
        presets = {
            "configurePresets": [
                {
                    "name": "ALL",
                    "cacheVariables": {"CSX_VERSION": "12.345-VR"},
                },
                {
                    "name": "AIO-Release",
                    "cacheVariables": {"CSX_VERSION": "4.0-SE"},
                },
            ]
        }
        with tempfile.TemporaryDirectory() as temporary:
            source_root = Path(temporary)
            (source_root / "CMakePresets.json").write_text(
                json.dumps(presets),
                encoding="utf-8",
            )
            self.assertEqual(
                BUILDER.default_plugin_version(source_root, "SE"),
                "CSX 12.345-VR",
            )
            self.assertEqual(
                BUILDER.default_plugin_version(source_root, "VR"),
                "CSX 12.345-VR",
            )

    def test_compatibility_tag_is_derived_for_future_versions(self) -> None:
        cases = {
            "CSX 3.18-VR": "CSX3.18-VR",
            "CSX 4.0-SE": "CSX4.0-SE",
            "CSX 12.345-VR": "CSX12.345-VR",
        }
        for plugin_version, expected in cases.items():
            with self.subTest(plugin_version=plugin_version):
                self.assertEqual(
                    BUILDER.csx_compatibility_tag(plugin_version),
                    expected,
                )

    def test_compatibility_tag_rejects_noncanonical_labels(self) -> None:
        invalid = (
            "3.18-VR",
            "CSX3.18-VR",
            "CSX 3.18.0-VR",
            "CSX 3.18-AE",
            "CSX 3.18-vr",
            "CSX 3.18-VR ",
        )
        for plugin_version in invalid:
            with self.subTest(plugin_version=plugin_version):
                with self.assertRaises(SystemExit):
                    BUILDER.csx_compatibility_tag(plugin_version)

    def test_fomod_has_exact_version_gate_and_cache_mapping(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runtime_root = Path(temporary)
            fomod_dir = BUILDER.write_fomod_installer(
                runtime_root,
                "VR",
                "future-test",
                "CSX 12.345-VR",
            )
            root = ET.parse(fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME).getroot()
            dependencies = root.findall("./moduleDependencies/fileDependency")
            self.assertEqual(
                {
                    (dependency.get("file"), dependency.get("state"))
                    for dependency in dependencies
                },
                {
                    ("SKSE/Plugins/CommunityShaders.dll", "Active"),
                    (
                        "SKSE/Plugins/CommunityShaders/CSX12.345-VR.marker",
                        "Active",
                    ),
                },
            )
            BUILDER.validate_fomod_installer(fomod_dir, "CSX12.345-VR")

    def test_fomod_validation_rejects_weakened_contracts(self) -> None:
        mutations = (
            ("operator", "Or"),
            ("marker_state", "Inactive"),
            ("marker_name", "CSX3.18-VR.marker"),
        )
        for mutation, value in mutations:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    runtime_root = Path(temporary)
                    fomod_dir = BUILDER.write_fomod_installer(
                        runtime_root,
                        "VR",
                        "future-test",
                        "CSX 12.345-VR",
                    )
                    config_path = fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME
                    tree = ET.parse(config_path)
                    dependencies = tree.getroot().find("./moduleDependencies")
                    self.assertIsNotNone(dependencies)
                    marker = dependencies.findall("./fileDependency")[1]
                    if mutation == "operator":
                        dependencies.set("operator", value)
                    elif mutation == "marker_state":
                        marker.set("state", value)
                    else:
                        marker.set(
                            "file",
                            f"SKSE/Plugins/CommunityShaders/{value}",
                        )
                    tree.write(config_path, encoding="utf-8", xml_declaration=True)
                    with self.assertRaises(SystemExit):
                        BUILDER.validate_fomod_installer(
                            fomod_dir,
                            "CSX12.345-VR",
                        )

    def test_fomod_validation_rejects_invalid_structure(self) -> None:
        mutations = ("wrong_root", "wrong_order", "invalid_info")
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    runtime_root = Path(temporary)
                    fomod_dir = BUILDER.write_fomod_installer(
                        runtime_root,
                        "VR",
                        "future-test",
                        "CSX 12.345-VR",
                    )
                    config_path = fomod_dir / BUILDER.FOMOD_CONFIG_FILE_NAME
                    if mutation == "invalid_info":
                        (fomod_dir / BUILDER.FOMOD_INFO_FILE_NAME).write_text(
                            "<not-fomod />",
                            encoding="utf-8",
                        )
                    else:
                        tree = ET.parse(config_path)
                        root = tree.getroot()
                        if mutation == "wrong_root":
                            root.tag = "not-config"
                        else:
                            root[:] = [root[0], root[2], root[1]]
                        tree.write(
                            config_path,
                            encoding="utf-8",
                            xml_declaration=True,
                        )
                    with self.assertRaises(SystemExit):
                        BUILDER.validate_fomod_installer(
                            fomod_dir,
                            "CSX12.345-VR",
                        )


if __name__ == "__main__":
    unittest.main()
