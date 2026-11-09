"""Exercise compiled-input cache contracts against the runtime implementation."""

from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from shader_cache_manifest import compile_task_defines, prepare_fxc_defines, write_manifest
from hlslkit.compile_shaders import parse_shader_configs
from hlslkit.shader_digest import combine_hashes, compute_shader_content_digest, hash_string, to_hex
import yaml
import standalone_shader_cache as standalone
from dxbc_fixtures import make_dxbc

spec = importlib.util.spec_from_file_location("cache_builder", REPO / "tools/build-shader-cache.py")
BUILDER = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = BUILDER
spec.loader.exec_module(BUILDER)
RUNTIME_TEST = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None


class ShaderCompileIdentityTests(unittest.TestCase):
    def test_standalone_key_matches_runtime_fixture(self) -> None:
        request = {"source": "test.hlsl", "profile": "ps_5_0", "entry": "main",
                   "defines": [("EXTRA", "1"), ("PSHADER", ""), ("WINPC", ""), ("DX11", "")]}
        digest = to_hex(standalone.compile_digest(request, "test-abi"))
        self.assertEqual(digest, "86afdff32904f1c54f253703a78b0748")
        self.assertEqual(standalone.serialize_defines([("B", None), ("A", "1"), ("B", "")]), "1:A1:11:B0:")
        self.assertNotEqual(standalone.serialize_defines([("A", "0"), ("A", "1")]),
                            standalone.serialize_defines([("A", "1"), ("A", "0")]))
        for changes in ({"entry": "alternate"}, {"profile": "vs_5_0"}, {"source": "other.hlsl"}):
            self.assertNotEqual(digest, to_hex(standalone.compile_digest({**request, **changes}, "test-abi")))
        self.assertNotEqual(digest, to_hex(standalone.compile_digest(request, "VR;test-abi")))

    def test_standalone_inventory_sources_and_runtime_gates(self) -> None:
        roots = BUILDER.shader_source_roots(REPO)
        features = {name: True for name in BUILDER.packaged_feature_directories(REPO)}
        for runtime in ("SE", "VR"):
            tasks = standalone.resolve_tasks(runtime, features, runtime + ";test-abi")
            self.assertGreater(len(tasks), 30)
            self.assertLessEqual(len(tasks), 4096)
            for task in tasks.values():
                self.assertTrue(any((root / task["source"]).is_file() for root in roots), task["source"])
                self.assertEqual(any(name == "VR" for name, _ in task["defines"]), runtime == "VR")
        disabled = standalone.release_requests("SE", {})
        self.assertFalse(any(task["source"].startswith("DynamicCubemaps/") for task in disabled))
        self.assertFalse(any(task["source"].startswith("ScreenSpaceGI/") for task in disabled))

    def test_standalone_depth_conventions_have_disjoint_cache_keys(self) -> None:
        for runtime in ("SE", "VR"):
            standard = standalone.resolve_tasks(runtime, {"ReverseZ": True}, "test-abi")
            reversed_tasks = standalone.resolve_tasks(runtime, {"ReverseZ": True}, "test-abi", reverse_z_enabled=True)
            unloaded = standalone.resolve_tasks(runtime, {"ReverseZ": False}, "test-abi")
            self.assertEqual(unloaded, standalone.resolve_tasks(runtime, {"ReverseZ": False}, "test-abi", reverse_z_enabled=True))
            self.assertEqual({key: task for key, task in standard.items() if not task["source"].startswith("ReverseZ/")}, unloaded)
            self.assertTrue(standard.keys().isdisjoint(reversed_tasks))
            self.assertTrue(all(("REVERSE_Z", "1") in task["defines"] for task in reversed_tasks.values()))
            self.assertFalse(any(name == "REVERSE_Z" for task in standard.values() for name, _ in task["defines"]))
            adapters = {task["source"]: task["profile"] for task in reversed_tasks.values() if task["source"].startswith("ReverseZ/")}
            self.assertEqual(adapters, {
                "ReverseZ/OccluderDepthDownscalePS.hlsl": "ps_5_0",
                "ReverseZ/OccluderBoxVS.hlsl": "vs_5_0",
                "ReverseZ/HiddenAreaMeshVS.hlsl": "vs_5_0",
            } if runtime == "VR" else {})

    def test_engine_depth_macros_match_runtime_factories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for runtime, name in (("SE", "shader-validation.yaml"), ("VR", "shader-validation-vr.yaml")):
                for enabled in (True, False):
                    profile = BUILDER.SHIPPED_CACHE_PROFILE
                    filtered = BUILDER.filter_profile_defines(
                        REPO / ".github/configs" / name,
                        Path(temporary) / f"{runtime}-{enabled}.yaml", yaml, profile,
                        feature_states={"ReverseZ": True},
                        reverse_z_enabled=enabled,
                    )
                    for source, _, _, defines in parse_shader_configs(str(filtered)):
                        depth = [value for value in defines if value.partition("=")[0] == "REVERSE_Z"]
                        expected = ["REVERSE_Z=1" if source == "Utility.hlsl" else "REVERSE_Z="] if enabled else []
                        self.assertEqual(depth, expected, source)

    def test_standalone_pack_paths_fail_closed(self) -> None:
        path = "Standalone/" + "a" * 32 + ".cso"
        for source in ("../evil.hlsl", "/evil.hlsl", "C:/evil.hlsl", "folder/../evil.hlsl", "folder\\evil.hlsl", "./evil.hlsl"):
            with self.assertRaises(ValueError):
                standalone.validate_pack_inputs({path: {"source": source, "profile": "cs_5_0"}})
        for relative in ("Standalone/1.cso", "Standalone/" + "a" * 32 + ".pso"):
            with self.assertRaises(ValueError):
                standalone.validate_pack_inputs({relative: {"source": "test.hlsl", "profile": "cs_5_0"}})

    def test_standalone_source_compatibility_keeps_pack_scope_restrictions(self) -> None:
        registration = dict(BUILDER.compatibility_variant_manifest(REPO)["legacy-horizon-fix"]["registrations"][0])
        registration["scopes"] = [{"kind": "shader-source", "value": "DynamicCubemaps/UpdateCubemapCS.hlsl"}]
        relative = "Standalone/" + "a" * 32 + ".cso"
        content = "b" * 32
        self.assertTrue(BUILDER.compatibility_registration_applies(registration, "Standalone", "Shaders/DynamicCubemaps/UpdateCubemapCS.hlsl", set()))
        self.assertFalse(BUILDER.compatibility_registration_applies(registration, "Standalone", "Shaders/DynamicCubemaps/InferCubemapCS.hlsl", set()))
        with self.assertRaisesRegex(SystemExit, "unsupported compatibility scope"):
            BUILDER.shader_pack_record_identity(relative, content, [registration], "DynamicCubemaps/UpdateCubemapCS.hlsl")
        other = BUILDER.shader_pack_record_identity(relative, content, [registration], "DynamicCubemaps/InferCubemapCS.hlsl")
        self.assertNotIn(registration["identity"], other["metadata"])

    def test_standalone_compiler_flags_macros_and_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "helper.hlsl").write_text("float4 main() : SV_Target { return 1; }", encoding="utf-8")
            task = {"source": "helper.hlsl", "profile": "ps_5_0", "entry": "main", "defines": [("EMPTY", ""), ("BARE", None), ("VALUE", "2")]}
            relative = "Standalone/" + "a" * 32 + ".cso"

            def fake_compile(command, **kwargs):
                self.assertEqual(command[:9], ["fake-fxc", "/nologo", "/T", "ps_5_0", "/E", "main", "/O3", "/Ges", "/Vd"])
                self.assertIn("EMPTY= ", command)
                self.assertIn("BARE", command)
                self.assertIn("VALUE=2", command)
                Path(command[command.index("/Fo") + 1]).write_bytes(make_dxbc(stage=0))
                return subprocess.CompletedProcess(command, 0, "", "")

            with mock.patch.object(standalone.subprocess, "run", side_effect=fake_compile):
                standalone.compile_tasks({relative: task}, root, root, "fake-fxc", 1)
            failure = subprocess.CompletedProcess([], 1, "", "compile failed")
            with mock.patch.object(standalone.subprocess, "run", return_value=failure), self.assertRaisesRegex(RuntimeError, "compile failed"):
                standalone.compile_tasks({relative: task}, root, root, "fake-fxc", 1)

    def test_standalone_manifest_and_pack_preserve_sources_and_stages(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Shaders"
            source.mkdir()
            (source / "helper.hlsl").write_text("float4 main() : SV_Target { return 1; }", encoding="utf-8")
            cache = root / "ShaderCache"
            (cache / "Standalone").mkdir(parents=True)
            tasks = {}
            for profile, stage in (("cs_5_0", 5), ("ps_5_0", 0), ("vs_5_0", 1)):
                request = {"source": "helper.hlsl", "profile": profile, "entry": "main", "defines": [(standalone.STAGES[profile][0], "")]}
                digest = standalone.compile_digest(request, "test-abi")
                relative = f"Standalone/{to_hex(digest)}.cso"
                tasks[relative] = {**request, "compile_digest": digest}
                (cache / relative).write_bytes(make_dxbc(stage=stage))
            manifest_path = cache / "Manifest.json"
            self.assertEqual(write_manifest(cache, source, "test-abi", manifest_path, compile_tasks=[], standalone_tasks=tasks), 3)
            manifest = json.loads(manifest_path.read_text())
            self.assertEqual(len(manifest["standaloneInputs"]), 3)
            BUILDER.build_managed_shader_packs(REPO, cache, None, "SE", "test")
            packed = json.loads((cache / "PackManifest.json").read_text())
            self.assertEqual(packed["standaloneInputs"], manifest["standaloneInputs"])
            inventory = BUILDER.PackagedCompatibilityInventory(REPO, ["default"], packed["standaloneInputs"])
            stats = {name: BUILDER.validate_shader_pack(cache / name, lane, packed["packSetId"], inspect_record=inventory.inspector(name))
                     for name, lane in BUILDER.PACK_LANES.items()}
            inventory.validate(stats)
            BUILDER.validate_pack_manifest_contract(packed, "SE", stats)
            relative, task = next(iter(tasks.items()))
            identity = BUILDER.shader_pack_record_identity(relative, manifest["entries"][relative], [], task["source"])
            with self.assertRaises(SystemExit):
                BUILDER.PackagedCompatibilityInventory(REPO, ["default"]).identify(identity["logicalKey"], identity["exactKey"], identity["metadata"], None)
            bad = BUILDER.PackagedCompatibilityInventory(REPO, ["default"], packed["standaloneInputs"])
            bad.inspector("Optimized.A.csxpack")(identity["logicalKey"], identity["exactKey"], identity["metadata"], make_dxbc(stage=1))
            with self.assertRaises(SystemExit):
                bad.validate(stats)

    def test_standalone_manifest_requires_every_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            task = {"source": "missing.hlsl", "profile": "cs_5_0", "entry": "main", "defines": [], "compile_digest": 0}
            with self.assertRaisesRegex(ValueError, "no output blobs"):
                write_manifest(root, root, "test", root / "Manifest.json", compile_tasks=[], standalone_tasks={"Standalone/" + "0" * 32 + ".cso": task})

    def test_se_release_macros_preserve_requests_with_current_grass_features(self) -> None:
        fixture = json.loads((REPO / "tests/data/shader_cache_se_runtime_trace.json").read_text(encoding="utf-8"))
        distribution = BUILDER.derive_distribution_profile(REPO)
        stages = {"Pixel": "PSHADER", "Vertex": "VSHADER", "Compute": "CSHADER"}
        pattern = re.compile(r"Compiling Data/Shaders/(\S+) (\S+):(Pixel|Vertex|Compute):([0-9A-F]+) to (.*)")
        with tempfile.TemporaryDirectory() as temporary:
            config = BUILDER.filter_profile_defines(
                REPO / ".github/configs/shader-validation.yaml",
                Path(temporary) / "SE.yaml", yaml, BUILDER.SHIPPED_CACHE_PROFILE,
                feature_states={},
                additional_excluded_defines=distribution.excluded_defines | {distribution.horizon_fix_define},
                add_cross_modlist_variants=True,
            )
            generated = compile_task_defines(parse_shader_configs(str(config)))
            for line in fixture["compileLines"]:
                match = pattern.search(line)
                self.assertIsNotNone(match, line)
                source, family, stage, descriptor, macros = match.groups()
                # Runtime diagnostic bare names are empty D3D macros, not FXC's 1.
                defines = [
                    token if "=" in token else token + "="
                    for token in macros.split()
                    if token not in {"D3DCOMPILE_DEBUG", "D3DCOMPILE_SKIP_OPTIMIZATION"}
                ]
                if source == "RunGrass.hlsl":
                    # This SE trace predates grass features; fresh VR requests cover them.
                    defines.extend(
                        token if "=" in token else token + "="
                        for token in BUILDER.SHIPPED_CACHE_PROFILE.file_defines[source]
                    )
                # The preserved capture predates SnowCover's three shader families.
                if source in {"Lighting.hlsl", "RunGrass.hlsl", "DistantTree.hlsl"}:
                    defines.append("SNOW_COVER=")
                captured = compile_task_defines([(source, stages[stage], f"{family}:{stage}:{descriptor}", defines)])
                for key, expected in captured.items():
                    with self.subTest(source=source, stage=stage, descriptor=descriptor):
                        self.assertIn(key, generated)
                        self.assertEqual(generated[key], expected)

    def test_vr_grass_macros_match_completed_runtime_requests(self) -> None:
        fixture = json.loads((REPO / "tests/data/shader_cache_vr_grass_runtime_trace.json").read_text(encoding="utf-8"))
        self.assertTrue(fixture["grassHooksInstalled"])
        self.assertEqual(fixture["failedTasks"], 0)
        self.assertGreater(fixture["completedTasks"], 0)
        pattern = re.compile(r"Compiling Data/Shaders/(\S+) (\S+):(Pixel|Vertex):([0-9A-F]+) to (.*)")
        stages = {"Pixel": "PSHADER", "Vertex": "VSHADER"}
        with tempfile.TemporaryDirectory() as temporary:
            filtered = BUILDER.filter_profile_defines(
                REPO / ".github/configs/shader-validation-vr.yaml",
                Path(temporary) / "VR.yaml", yaml, BUILDER.SHIPPED_CACHE_PROFILE,
                feature_states={"ReverseZ": False},
                additional_excluded_defines=frozenset({"HORIZON_FIX"}),
            )
            generated = compile_task_defines(parse_shader_configs(str(filtered)))
            self.assertEqual(len(fixture["compileLines"]), 8)
            for line in fixture["compileLines"]:
                match = pattern.search(line)
                self.assertIsNotNone(match, line)
                source, family, stage, descriptor, macros = match.groups()
                defines = [
                    token if "=" in token else token + "="
                    for token in macros.split()
                    if token not in {"D3DCOMPILE_DEBUG", "D3DCOMPILE_SKIP_OPTIMIZATION"}
                ]
                # Keep the captured requests intact while applying the current feature scope.
                defines.append("SNOW_COVER=")
                captured = compile_task_defines([(source, stages[stage], f"{family}:{stage}:{descriptor}", defines)])
                for key, expected in captured.items():
                    with self.subTest(stage=stage, descriptor=descriptor):
                        self.assertEqual(generated[key], expected)

    def test_se_stage_excludes_non_shipped_includes_from_source_identity(self) -> None:
        distribution = BUILDER.derive_distribution_profile(REPO)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "SE"
            BUILDER.stage_merged_shaders(REPO, stage, distribution.excluded_packages)
            self.assertFalse((stage / "Features/WetnessEffects.ini").exists())
            self.assertFalse((stage / "WetnessEffects").exists())
            cache = root / "cache"
            cache.mkdir()
            BUILDER.write_info_ini(
                cache, stage, REPO, "CSX 3.19-VR", "SE",
                BUILDER.SHIPPED_CACHE_PROFILE, "test-abi",
                excluded_features=distribution.excluded_short_names,
            )
            self.assertNotIn("WetnessEffects", BUILDER.read_feature_states(cache))

            for name in ("Lighting.hlsl", "Water.hlsl"):
                before = compute_shader_content_digest(stage / name, stage, {})
                self.assertIsNotNone(before)
                # The legacy conditional include participates even when its macro is off.
                legacy = stage / "WetnessEffects/WetnessEffects.hlsli"
                legacy.parent.mkdir(exist_ok=True)
                legacy.write_text("// unshipped include\n", encoding="utf-8")
                self.assertNotEqual(before, compute_shader_content_digest(stage / name, stage, {}))
                legacy.unlink()

    def test_shipped_inventories_resolve_unique_macro_tasks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for runtime, name in (("SE", "shader-validation.yaml"), ("VR", "shader-validation-vr.yaml")):
                filtered = BUILDER.filter_profile_defines(
                    REPO / ".github/configs" / name,
                    Path(temporary) / f"{runtime}.yaml", yaml,
                    BUILDER.SHIPPED_CACHE_PROFILE,
                    additional_excluded_defines=frozenset({"HORIZON_FIX"}),
                    feature_states={"ReverseZ": runtime == "VR"},
                )
                tasks = parse_shader_configs(str(filtered))
                self.assertGreater(len(tasks), 1000)
                self.assertEqual(len(compile_task_defines(tasks)), len(tasks))

    def test_category_cache_covers_both_runtime_layouts(self) -> None:
        flags = BUILDER.character_category_flags(REPO)
        self.assertEqual(flags, {
            "Lighting": 1 << 5, "Grass": 1 << 9, "DistantTree": 1 << 9,
            "Sky": 1 << 9, "Effect": 1 << 9,
        })
        with tempfile.TemporaryDirectory() as temporary:
            for runtime, name in (("SE", "shader-validation.yaml"), ("VR", "shader-validation-vr.yaml")):
                filtered = BUILDER.filter_profile_defines(
                    REPO / ".github/configs" / name,
                    Path(temporary) / f"{runtime}.yaml", yaml,
                    BUILDER.SHIPPED_CACHE_PROFILE,
                    feature_states={"ReverseZ": runtime == "VR"},
                )
                tasks = parse_shader_configs(str(filtered))
                canonical = compile_task_defines(tasks)
                pairs = 0
                families = set()
                for source, stage, entry, defines in tasks:
                    names = {value.partition("=")[0] for value in defines}
                    if "CHARACTER_CATEGORY" not in names:
                        continue
                    self.assertEqual(stage, "PSHADER")
                    family, label, descriptor = entry.split(":")
                    if family != "Grass":
                        self.assertIn("DEFERRED", names)
                    flag = flags[family]
                    self.assertTrue(int(descriptor, 16) & flag)
                    base_descriptor = int(descriptor, 16) & ~flag
                    base_key = (Path(source).stem, f"{base_descriptor:X}.pso")
                    self.assertIn(base_key, canonical)
                    base = compile_task_defines([(
                        source, stage, f"{family}:{label}:{base_descriptor:X}",
                        [value for value in defines if value.partition("=")[0] != "CHARACTER_CATEGORY"],
                    )])
                    self.assertEqual(base[base_key], canonical[base_key])
                    families.add(family)
                    pairs += 1
                self.assertEqual(families, set(flags))
                self.assertGreater(pairs, 500)

    def test_category_overlay_is_idempotent_and_retains_captured_entries(self) -> None:
        config = {
            "captured_shader_variants": 2,
            "shaders": [{"file": "Lighting.hlsl", "configs": {
                "PSHADER": {"common_defines": [], "entries": [
                    {"entry": "Lighting:Pixel:1", "defines": ["DEFERRED", "VC"]},
                    {"entry": "Lighting:Pixel:2", "defines": ["VC"]},
                ]},
                "VSHADER": {"common_defines": [], "entries": [
                    {"entry": "Lighting:Vertex:1", "defines": ["DEFERRED"]},
                ]},
            }}],
        }
        self.assertEqual(BUILDER.append_character_category_variants(config, REPO), 1)
        entries = config["shaders"][0]["configs"]["PSHADER"]["entries"]
        self.assertEqual(entries[2], {
            "entry": "Lighting:Pixel:21",
            "defines": ["DEFERRED", "VC", "CHARACTER_CATEGORY"],
        })
        self.assertEqual(BUILDER.append_character_category_variants(config, REPO), 0)
        self.assertEqual(len(entries), 3)
        self.assertEqual(config["captured_shader_variants"], 2)
        self.assertEqual(len(config["shaders"][0]["configs"]["VSHADER"]["entries"]), 1)

    def test_category_overlay_rejects_conflicting_inputs(self) -> None:
        from copy import deepcopy
        config = {"shaders": [{"file": "Lighting.hlsl", "configs": {
            "PSHADER": {"common_defines": [], "entries": [
                {"entry": "Lighting:Pixel:1", "defines": ["DEFERRED"]},
                {"entry": "Lighting:Pixel:21", "defines": ["DEFERRED", "CHARACTER_CATEGORY", "VC"]},
            ]},
        }}]}
        with self.assertRaisesRegex(SystemExit, "conflicting category"):
            BUILDER.append_character_category_variants(deepcopy(config), REPO)
        pair = config["shaders"][0]["configs"]["PSHADER"]["entries"]
        pair[0]["defines"] = ["DEFERRED", "VALUE=1", "VALUE=2"]
        pair[1]["defines"] = ["DEFERRED", "CHARACTER_CATEGORY", "VALUE=2", "VALUE=1"]
        with self.assertRaisesRegex(SystemExit, "conflicting category"):
            BUILDER.append_character_category_variants(deepcopy(config), REPO)
        config["shaders"][0]["configs"]["PSHADER"]["entries"].pop()
        config["shaders"][0]["configs"]["PSHADER"]["entries"][0]["entry"] = "Lighting:Pixel:21"
        with self.assertRaisesRegex(SystemExit, "flag/macros disagree"):
            BUILDER.append_character_category_variants(config, REPO)

    def test_category_flags_fail_closed_on_unknown_runtime_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/ShaderCache.h").write_text(
                "static unsigned GetCharacterCategoryFlag(int type) {\n"
                "case RE::BSShader::Type::Lighting: return ChangedFlag;\n\t\t}",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(SystemExit, "unsupported runtime"):
                BUILDER.character_category_flags(root)

    def test_lighting_inventory_keys_match_runtime_flag_mask(self) -> None:
        flags = {"SPECULAR", "SOFT_LIGHTING", "RIM_LIGHTING", "BACK_LIGHTING"}
        mask = sum(1 << bit for bit in (9, 10, 11, 12))
        for name, expected in (("shader-validation.yaml", 246), ("shader-validation-vr.yaml", 284)):
            path = REPO / ".github/configs" / name
            config = yaml.safe_load(path.read_text(encoding="utf-8"))
            BUILDER.validate_captured_variant_count(config, path)
            lighting = next(shader for shader in config["shaders"] if shader["file"] == "Lighting.hlsl")
            entries = lighting["configs"]["PSHADER"]["entries"]
            self.assertEqual(len(entries), expected)
            keys = set()
            for entry in entries:
                descriptor = int(entry["entry"].rsplit(":", 1)[1], 16)
                self.assertNotIn(descriptor, keys)
                keys.add(descriptor)
                if (descriptor >> 24) & 0x3f not in (9, 18, 13, 15):
                    self.assertEqual(descriptor & mask, 0)
                    self.assertFalse(flags.intersection(entry["defines"]))

    def test_water_pixel_keys_share_wading_without_changing_vertices(self) -> None:
        config = {"captured_shader_variants": 3, "shaders": [{"file": "Water.hlsl", "configs": {
            "PSHADER": {"entries": [
                {"entry": "Water:Pixel:1", "defines": ["VC", "SPECULAR"]},
                {"entry": "Water:Pixel:41", "defines": ["VC", "SPECULAR", "WADING"]},
            ]},
            "VSHADER": {"entries": [{"entry": "Water:Vertex:41", "defines": ["WADING"]}]},
        }}]}
        BUILDER.normalize_water_pixel_variants(config)
        stages = config["shaders"][0]["configs"]
        self.assertEqual(stages["PSHADER"]["entries"], [
            {"entry": "Water:Pixel:1", "defines": ["VC", "SPECULAR"]},
        ])
        self.assertEqual(stages["VSHADER"]["entries"], [
            {"entry": "Water:Vertex:41", "defines": ["WADING"]},
        ])
        self.assertEqual(config["captured_shader_variants"], 3)
        stages["PSHADER"]["entries"] = [
            {"entry": "Water:Pixel:1", "defines": ["MODE=1", "MODE=2"]},
            {"entry": "Water:Pixel:41", "defines": ["MODE=2", "MODE=1", "WADING"]},
        ]
        with self.assertRaisesRegex(SystemExit, "conflicting water pixel"):
            BUILDER.normalize_water_pixel_variants(config)

    def test_normalized_inventory_retains_capture_count_and_fails_closed(self) -> None:
        from copy import deepcopy
        config = {
            "captured_shader_variants": 3,
            "normalized_shader_variants": 2,
            "shaders": [{"file": "Lighting.hlsl", "configs": {"PSHADER": {
                "entries": [{"entry": "Lighting:Pixel:0"}, {"entry": "Lighting:Pixel:1"}],
            }}}],
        }
        before = deepcopy(config)
        BUILDER.validate_captured_variant_count(config, Path("normalized.yaml"))
        self.assertEqual(config, before)
        for invalid in (None, True, 0, -1, 4, "2", 2.5, 1):
            with self.subTest(normalized=invalid):
                candidate = dict(config, normalized_shader_variants=invalid)
                with self.assertRaises(SystemExit):
                    BUILDER.validate_captured_variant_count(candidate, Path("invalid.yaml"))
        for missing in ("captured_shader_variants", "normalized_shader_variants"):
            candidate = dict(config)
            del candidate[missing]
            with self.assertRaises(SystemExit):
                BUILDER.validate_captured_variant_count(candidate, Path("incomplete.yaml"))

    def test_macro_encoding_preserves_boundaries_and_fxc_defaults(self) -> None:
        def encoded(defines):
            return compile_task_defines([("Test.hlsl", "PSHADER", "main:1", defines)])[("Test", "1.pso")]
        self.assertNotEqual(encoded(["A=1 B=2"]), encoded(["A=1", "B=2"]))
        self.assertNotEqual(encoded(["A"]), encoded(["A="]))
        self.assertEqual(encoded(["A"]), encoded(["A=1"]))
        self.assertEqual(encoded(["A=", "A="]), encoded(["A="]))
        self.assertNotEqual(encoded(["A=1", "A=2"]), encoded(["A=2", "A=1"]))

    def test_incomplete_outputs_and_replace_failure_preserve_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Grass").mkdir()
            (root / "Grass/7.pso").write_bytes(b"DXBC")
            (root / "Grass.hlsl").write_text("float4 main() : SV_Target { return 0; }")
            manifest = root / "Manifest.json"
            manifest.write_text("previous manifest")
            tasks = [("Grass.hlsl", "PSHADER", "main:7", ["PSHADER="])]
            missing_task = ("Grass.hlsl", "PSHADER", "main:8", ["PSHADER="])
            with self.assertRaisesRegex(ValueError, "no output blobs"):
                write_manifest(root, root, "", manifest, compile_tasks=[*tasks, missing_task])
            self.assertEqual(manifest.read_text(), "previous manifest")
            with mock.patch("pathlib.Path.replace", side_effect=OSError("injected replace failure")):
                with self.assertRaisesRegex(OSError, "injected replace"):
                    write_manifest(root, root, "", manifest, compile_tasks=tasks)
            self.assertEqual(manifest.read_text(), "previous manifest")
            self.assertEqual({path.name for path in root.iterdir()}, {"Grass", "Grass.hlsl", "Manifest.json"})

    @unittest.skipUnless(RUNTIME_TEST, "pass shader_compile_identity_test.exe for compiler parity")
    def test_fxc_empty_macros_match_runtime_bytecode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = {"shaders": [{"file": "Test.hlsl", "configs": {"PSHADER": {
                "common_defines": [["PSHADER", "A"]], "entries": [{"entry": "main:1", "defines": []}]
            }}}]}
            prepare_fxc_defines(config)
            config_path = root / "config.yaml"
            config_path.write_text(yaml.safe_dump(config), encoding="utf-8")
            tasks = parse_shader_configs(str(config_path))
            self.assertIn("A=", tasks[0][3])
            source = root / "Test.hlsl"
            source.write_text("float4 main() : SV_Target { return A + 1; }", encoding="utf-8")
            blob = root / "Test/1.pso"
            blob.parent.mkdir()
            command = [
                sys.executable, str(REPO / "tools/shader_cache_compile.py"),
                "--fxc", BUILDER.locate_fxc(None), "--config", str(config_path),
                "--shader-dir", str(root), "--output-dir", str(root),
                "--optimization-level", "3", "--jobs", "1",
            ]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            manifest = root / "Manifest.json"
            write_manifest(root, root, "", manifest, compile_tasks=tasks)
            legacy = root / "Legacy.json"
            legacy.write_text('{"schemaVersion":1,"entries":{}}')
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps({"global": "", "manifest": str(manifest), "legacyManifest": str(legacy), "entries": [{"source": str(source), "path": "Test/1.pso", "defines": tasks[0][3], "bytecode": str(blob)}]}), encoding="utf-8")
            subprocess.run([str(RUNTIME_TEST), str(fixture)], check=True)

    def test_unknown_duplicate_and_invalid_tasks_fail_closed(self) -> None:
        task = ("Grass.hlsl", "PSHADER", "main:7", ["PSHADER"])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            compile_task_defines([task, task])
        for stage, descriptor in (("INVALID", "7"), ("PSHADER", "../7")):
            with self.assertRaisesRegex(ValueError, "invalid"):
                compile_task_defines([("Grass.hlsl", stage, descriptor, [])])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Grass").mkdir()
            (root / "Grass/7.pso").write_bytes(b"DXBC")
            manifest = root / "Manifest.json"
            with self.assertRaisesRegex(ValueError, "no matching macro task"):
                write_manifest(root, root, "", manifest, compile_tasks=[])
            self.assertFalse(manifest.exists())
            with self.assertRaisesRegex(ValueError, "cannot verify"):
                write_manifest(root, root, "", manifest, compile_tasks=[task])
            self.assertFalse(manifest.exists())

    def test_macro_only_change_invalidates_loose_and_managed_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Grass").mkdir()
            (root / "Grass/7.pso").write_bytes(b"DXBC")
            source = "float4 main() : SV_Target { return 0; }"
            (root / "Grass.hlsl").write_text(source, encoding="utf-8")
            manifest = root / "Manifest.json"
            entries = []
            for defines in (["PSHADER"], ["PSHADER", "RENDER_DEPTH"], ["RENDER_DEPTH", "PSHADER", "PSHADER"]):
                write_manifest(root, root, "ABI=test;", manifest, compile_tasks=[("Grass.hlsl", "PSHADER", "main:7", defines)])
                entries.append(json.loads(manifest.read_text())["entries"]["Grass/7.pso"])
            self.assertNotEqual(entries[0], entries[1])
            self.assertEqual(entries[1], entries[2])
            self.assertNotEqual(entries[0], to_hex(combine_hashes(hash_string(source), hash_string("ABI=test;"))))
            original = BUILDER.shader_pack_record_identity("Grass/7.pso", entries[0], [])
            updated = BUILDER.shader_pack_record_identity("Grass/7.pso", entries[1], [])
            self.assertEqual(original["logicalKey"], updated["logicalKey"])
            self.assertNotEqual(original["exactKey"], updated["exactKey"])
            self.assertNotEqual(original["metadata"], updated["metadata"])

    @unittest.skipUnless(RUNTIME_TEST, "pass shader_compile_identity_test.exe for cross-language verification")
    def test_runtime_manifest_parity_for_stages_runtimes_and_imagespace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for runtime in ("SE", "VR"):
                cache = root / runtime
                cache.mkdir()
                source = root / "Shaders"
                source.mkdir(exist_ok=True)
                tasks = [
                    ("Grass.hlsl", "PSHADER", "main:7", ["PSHADER", "DO_ALPHA_TEST"]),
                    ("Grass.hlsl", "VSHADER", "main:8", ["VSHADER", "RENDER_DEPTH"]),
                    ("Water.hlsl", "PSHADER", "main:1", ["PSHADER", "HORIZON_FIX", "UNIFIED_WATER"]),
                    ("Utility.hlsl", "PSHADER", "main:2", ["PSHADER", "SHADOWFILTER=4", "SHADOWSPLITCOUNT=3", "FLAG1", "FLAG=1", "DUP=2", "DUP=1", "DUP=2"]),
                    ("ISReflectionsRayTracing.hlsl", "CSHADER", "main:A", ["CSHADER", "RAY_COUNT=3", "EMPTY=", "TEXT=café : B=2"]),
                ]
                remap = {"ReflectionsRayTracing": "ISReflectionsRayTracing"}
                fixture_entries = []
                for name, stage, entry, defines in tasks:
                    if runtime == "VR":
                        defines.append("VR")
                    defines[:] = [value if "=" in value else value + "=" for value in defines]
                    path = source / name
                    path.write_bytes(b"float4 main() : SV_Target { return 0; }\r\n")
                    family = "ReflectionsRayTracing" if name.startswith("IS") else path.stem
                    extension = {"PSHADER": ".pso", "VSHADER": ".vso", "CSHADER": ".cso"}[stage]
                    relative = f"{family}/{entry.rsplit(':', 1)[-1]}{extension}"
                    blob = cache / relative
                    blob.parent.mkdir(exist_ok=True)
                    blob.write_bytes(b"DXBC")
                    fixture_entries.append({"source": str(path), "path": relative, "defines": defines})
                global_state = ("VR;" if runtime == "VR" else "") + "ShaderCacheABI=test;"
                manifest = cache / "Manifest.json"
                self.assertEqual(write_manifest(cache, source, global_state, manifest, lambda name: remap.get(name, name), compile_tasks=tasks), len(tasks))
                legacy = cache / "Legacy.json"
                old = json.loads(manifest.read_text())
                old["schemaVersion"] = 1
                legacy.write_text(json.dumps(old), encoding="utf-8")
                fixture = cache / "fixture.json"
                fixture.write_text(json.dumps({"global": global_state, "manifest": str(manifest), "legacyManifest": str(legacy), "entries": fixture_entries}), encoding="utf-8")
                subprocess.run([str(RUNTIME_TEST), str(fixture)], check=True)


if __name__ == "__main__":
    unittest.main()
