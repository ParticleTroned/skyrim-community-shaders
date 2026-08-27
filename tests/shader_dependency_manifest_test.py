#!/usr/bin/env python3
"""Regression tests for the shader dependency and invalidation graph."""

from __future__ import annotations

import importlib.util
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


MANIFEST_TOOL = load_module("shader_dependency_manifest", REPO / "tools/shader_dependency_manifest.py")
CACHE_BUILDER = load_module("build_shader_cache_for_dependency_test", REPO / "tools/build-shader-cache.py")


class ShaderDependencyManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = MANIFEST_TOOL.build_manifest(REPO)

    def test_graph_invariants_close(self) -> None:
        self.assertEqual(MANIFEST_TOOL.validate_manifest(self.manifest), [])
        inventory = self.manifest["inventory"]
        self.assertEqual(inventory["unacceptedUnresolvedIncludeCount"], 0)
        self.assertEqual(inventory["unresolvedCompileSiteCount"], 0)
        self.assertEqual(inventory["unclassifiedProductionEntryCount"], 0)

    def test_virtual_namespace_matches_real_cache_staging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / "Shaders"
            CACHE_BUILDER.stage_merged_shaders(REPO, stage, excluded_packages=frozenset())
            staged = {
                path.relative_to(stage).as_posix()
                for path in stage.rglob("*")
                if path.is_file() and path.suffix.lower() in MANIFEST_TOOL.SHADER_SUFFIXES
            }
        classified = {
            source["virtualPath"]
            for source in self.manifest["sources"]
            if not source["virtualPath"].lower().startswith("tests/")
        }
        self.assertEqual(classified, staged)

    def test_checked_in_artifact_is_fresh(self) -> None:
        expected = json.dumps(self.manifest, indent=2, ensure_ascii=False) + "\n"
        actual = (REPO / "docs/development/shader-analysis/shader-manifest.generated.json").read_text(encoding="utf-8")
        self.assertEqual(expected, actual)

    def test_invalidation_index_reaches_every_production_entry(self) -> None:
        production = {
            source["virtualPath"]
            for source in self.manifest["sources"]
            if source["role"] == "production-entry"
        }
        indexed = {
            entry
            for record in self.manifest["invalidationIndex"]["bySource"]
            for entry in record["affectedEntryPoints"]
        }
        self.assertEqual(production, indexed)

    def test_horizon_fomod_variant_matches_cache_builder_contract(self) -> None:
        variants = self.manifest["compatibilityVariants"]
        self.assertEqual(len(variants), 1)
        variant = variants[0]
        self.assertEqual(
            variant["cacheDirectories"],
            [CACHE_BUILDER.CACHE_DIRECTORY, CACHE_BUILDER.HORIZON_FIX_CACHE_DIRECTORY],
        )
        self.assertEqual(variant["affectedEntryPoints"], ["Water.hlsl"])
        macro_sources = {
            source["virtualPath"]
            for source in self.manifest["sources"]
            if "HORIZON_FIX" in source["macroReferences"]
        }
        self.assertEqual(macro_sources, {"Water.hlsl"})

    def test_captured_cache_configs_reference_classified_engine_entries(self) -> None:
        engine_entries = {
            unit["sourceVirtualPath"]
            for unit in self.manifest["compileUnits"]
            if unit["kind"] == "engine-shader-cache-family"
        }
        for config in CACHE_BUILDER.configs_for(REPO).values():
            captured = set(
                re.findall(r"(?m)^\s*-\s+file:\s+([^\s#]+)", config.read_text(encoding="utf-8"))
            )
            self.assertTrue(captured, config)
            self.assertEqual(captured - engine_entries, set(), config)

    def test_feature_cache_scope_matches_utility_define_builder_exception(self) -> None:
        broad_features = [
            feature for feature in self.manifest["features"]
            if feature["shaderDefineScope"]["declaredShaderTypes"] == ["*"]
        ]
        self.assertTrue(broad_features)
        for feature in broad_features:
            self.assertNotIn("Utility", feature["shaderDefineScope"]["currentEngineCacheScope"])
        self.assertIn("Utility", MANIFEST_TOOL.SUPPORTED_ENGINE_SHADER_TYPES)
        self.assertNotIn("Utility", MANIFEST_TOOL.FEATURE_DEFINE_ENGINE_TYPES)
        shader_cache = (REPO / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        for shader_type in MANIFEST_TOOL.FEATURE_DEFINE_ENGINE_TYPES:
            self.assertIn(f"HasShaderDefine(RE::BSShader::Type::{shader_type})", shader_cache)
        self.assertNotIn("HasShaderDefine(RE::BSShader::Type::Utility)", shader_cache)

    def test_switch_based_feature_scopes_are_not_promoted_to_global(self) -> None:
        by_id = {feature["id"]: feature for feature in self.manifest["features"]}
        self.assertEqual(
            by_id["ExtendedMaterials"]["shaderDefineScope"]["declaredShaderTypes"],
            ["Lighting"],
        )
        self.assertEqual(
            by_id["GrassCollision"]["shaderDefineScope"]["declaredShaderTypes"],
            ["Grass"],
        )


if __name__ == "__main__":
    unittest.main()
