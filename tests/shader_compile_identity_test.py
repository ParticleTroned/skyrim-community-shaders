"""Exercise compiled-input cache contracts against the runtime implementation."""

from __future__ import annotations

import importlib.util
import json
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
from hlslkit.shader_digest import combine_hashes, hash_string, to_hex
import yaml

spec = importlib.util.spec_from_file_location("cache_builder", REPO / "tools/build-shader-cache.py")
BUILDER = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = BUILDER
spec.loader.exec_module(BUILDER)
RUNTIME_TEST = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None


class ShaderCompileIdentityTests(unittest.TestCase):
    def test_shipped_inventories_resolve_unique_macro_tasks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for runtime, name in (("SE", "shader-validation.yaml"), ("VR", "shader-validation-vr.yaml")):
                filtered = BUILDER.filter_profile_defines(
                    REPO / ".github/configs" / name,
                    Path(temporary) / f"{runtime}.yaml", yaml,
                    BUILDER.SHIPPED_CACHE_PROFILE,
                    additional_excluded_defines=frozenset({"HORIZON_FIX"}),
                )
                tasks = parse_shader_configs(str(filtered))
                self.assertGreater(len(tasks), 1000)
                self.assertEqual(len(compile_task_defines(tasks)), len(tasks))

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
