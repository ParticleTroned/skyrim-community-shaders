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
        assert optimized_a == {"generation": 1, "recordCount": 3}
        assert optimized_b == {"generation": 0, "recordCount": 0}
        assert developer_a == {"generation": 1, "recordCount": 0}
        assert developer_b == {"generation": 0, "recordCount": 0}
        manifest = json.loads(
            (standard / "PackManifest.json").read_text(encoding="utf-8")
        )
        assert manifest["compatibilityVariants"] == [
            "default",
            "legacy-horizon-fix",
        ]
        assert manifest["shaderCacheABI"] == "a" * 64
        assert not list(standard.rglob("*.pso"))
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
