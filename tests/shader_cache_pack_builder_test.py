from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "tools" / "build-shader-cache.py"


def load_builder():
    spec = importlib.util.spec_from_file_location("csx_build_shader_cache", SCRIPT)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load shader-cache builder")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_loose_cache(root: Path, entries: dict[str, tuple[str, bytes]]) -> None:
    manifest_entries: dict[str, str] = {}
    for relative, (contract, bytecode) in entries.items():
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(bytecode)
        manifest_entries[relative] = contract
    (root / "Manifest.json").write_text(
        json.dumps({"schemaVersion": 1, "entries": manifest_entries}),
        encoding="utf-8",
    )


def main() -> int:
    builder = load_builder()
    scoped_registration = {
        "identity": "org.example.scope",
        "owner": "test",
        "displayVersion": "1",
        "contractMajor": 1,
        "currentMinor": 0,
        "minimumCompatibleMinor": 0,
        "maximumCompatibleMinor": 0,
        "resourceFingerprint": "",
        "scopes": [{"kind": "shader-source", "value": "Data\\Shaders\\Water.hlsl"}],
    }
    assert builder.compatibility_registration_applies(
        scoped_registration, "water", "Shaders/Water.hlsl", set()
    )
    assert "scope=source:shaders/water.hlsl" in builder.canonical_compatibility_registration(
        scoped_registration
    )
    feature_registration = {
        **scoped_registration,
        "identity": "org.example.feature",
        "scopes": [{"kind": "feature", "value": "HorizonFix"}],
    }
    assert builder.compatibility_registration_applies(
        feature_registration, "water", "Shaders/Water.hlsl", {"horizonfix"}
    )
    assert not builder.compatibility_registration_applies(
        feature_registration, "water", "Shaders/Water.hlsl", set()
    )
    with tempfile.TemporaryDirectory(prefix="csx-pack-builder-test-") as temporary:
        root = Path(temporary)
        standard = root / "ShaderCache"
        horizon = root / "ShaderCache-HorizonFix"
        standard.mkdir()
        horizon.mkdir()
        write_loose_cache(
            standard,
            {
                "Water/1.pso": ("1" * 32, b"standard-water"),
                "Lighting/2.pso": ("2" * 32, b"standard-lighting"),
            },
        )
        write_loose_cache(
            horizon,
            {
                "Water/1.pso": ("3" * 32, b"horizon-water"),
            },
        )

        counts = builder.build_managed_shader_packs(
            REPO, standard, horizon, "VR", "a" * 64
        )
        assert counts == {"standard": 2, "horizon-fix": 1}
        assert not horizon.exists()
        optimized_a = builder.validate_shader_pack(
            standard / "Optimized.A.csxpack", 1
        )
        optimized_b = builder.validate_shader_pack(
            standard / "Optimized.B.csxpack", 1
        )
        developer_a = builder.validate_shader_pack(
            standard / "Developer.A.csxpack", 2
        )
        developer_b = builder.validate_shader_pack(
            standard / "Developer.B.csxpack", 2
        )
        assert optimized_a["generation"] == 1 and optimized_a["recordCount"] == 3
        assert optimized_b["generation"] == 0 and optimized_b["recordCount"] == 0
        assert developer_a["generation"] == 1 and developer_a["recordCount"] == 0
        assert developer_b["generation"] == 0 and developer_b["recordCount"] == 0
        manifest = json.loads(
            (standard / "PackManifest.json").read_text(encoding="utf-8")
        )
        assert manifest["compatibilityVariants"] == [
            "default",
            "legacy-horizon-fix",
        ]
        assert manifest["shaderCacheABI"] == "a" * 64
        assert all(
            stats["packSetId"] == manifest["packSetId"]
            for stats in (optimized_a, optimized_b, developer_a, developer_b)
        )
        assert not list(standard.rglob("*.pso"))
        (standard / "Info.ini").write_text(
            "[Cache]\nPluginVersion=CSX 3.19-VR\nShaderCacheABI="
            + "a" * 64
            + "\n",
            encoding="utf-8",
        )
        fomod = builder.write_fomod_installer(
            root,
            "VR",
            "test",
            "CSX 3.19-VR",
            source_root=REPO,
            horizon_variants=False,
        )
        builder.validate_fomod_installer(
            fomod,
            builder.csx_compatibility_tag("CSX 3.19-VR"),
            horizon_variants=False,
        )
        module_config = (fomod / "ModuleConfig.xml").read_text(encoding="utf-8")
        assert "ShaderCache-HorizonFix" not in module_config
        assert "HorizonFix.dll" not in module_config
        if len(sys.argv) == 2:
            default_requirement = builder.canonical_compatibility_requirement_set([])
            exact_key = (
                "Water/1.pso|compat="
                f"{builder.sha256_hex(default_requirement)}|content={'1' * 32}"
            )
            subprocess.run(
                [
                    sys.argv[1],
                    str(standard / "Optimized.A.csxpack"),
                    str(standard / "Optimized.B.csxpack"),
                    exact_key,
                ],
                check=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
