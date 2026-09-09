from __future__ import annotations

import copy
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "tools" / "build-shader-cache.py"
CONTRACT_CASES = REPO / "tests" / "data" / "shader_cache_pack_contract_cases.json"


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


def json_pointer_parent(document: object, pointer: str) -> tuple[dict, str]:
    parts = [part.replace("~1", "/").replace("~0", "~") for part in pointer.split("/")[1:]]
    if not parts:
        raise AssertionError("contract case cannot replace the document root")
    current = document
    for part in parts[:-1]:
        if not isinstance(current, dict):
            raise AssertionError(f"invalid contract-case pointer: {pointer}")
        current = current[part]
    if not isinstance(current, dict):
        raise AssertionError(f"invalid contract-case pointer: {pointer}")
    return current, parts[-1]


def apply_contract_case(base_manifest: dict, base_stats: dict, case: dict) -> tuple[dict, dict]:
    manifest = copy.deepcopy(base_manifest)
    stats = copy.deepcopy(base_stats)
    for pointer, value in case.get("manifestOverrides", {}).items():
        parent, key = json_pointer_parent(manifest, pointer)
        parent[key] = value
    for pointer in case.get("manifestRemovals", []):
        parent, key = json_pointer_parent(manifest, pointer)
        parent.pop(key)
    for file_name, overrides in case.get("fileOverrides", {}).items():
        stats[file_name].update(overrides)
    return manifest, stats


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
    feature_registration = {
        **scoped_registration,
        "identity": "org.example.feature",
        "scopes": [{"kind": "feature", "value": "HorizonFix"}],
    }
    for unsupported in (scoped_registration, feature_registration):
        try:
            builder.canonical_compatibility_registration(unsupported)
            raise AssertionError("reserved compatibility scope was accepted")
        except SystemExit:
            pass
    family_registration = {
        **scoped_registration,
        "scopes": [{"kind": "shader-family", "value": "Water"}],
    }
    for field in (
        "contractMajor", "currentMinor", "minimumCompatibleMinor", "maximumCompatibleMinor"
    ):
        for value in (True, -1, 0x100000000):
            invalid = {**family_registration, field: value}
            try:
                builder.canonical_compatibility_registration(invalid)
                raise AssertionError(f"invalid uint32 {field} was accepted: {value!r}")
            except SystemExit:
                pass
    for control in ("\0", "\n", "\r", "\t", "\x7f"):
        for field in ("resourceFingerprint", "scopes"):
            invalid = copy.deepcopy(family_registration)
            if field == "scopes":
                invalid[field][0]["value"] += control + "scope=family:water"
            else:
                invalid[field] = control + "scope=family:water"
            try:
                builder.canonical_compatibility_registration(invalid)
                raise AssertionError("control character in canonical compatibility text was accepted")
            except SystemExit:
                pass
    unicode_registration = {**family_registration, "resourceFingerprint": "éclair"}
    for canonicalize in (
        builder.canonical_compatibility_requirement_set,
        builder.canonical_compatibility_domain_set,
    ):
        length, value = canonicalize([unicode_registration]).split(":", 1)
        assert int(length) == len(value[:-1].encode("utf-8"))
    upper_unicode_family = {
        **family_registration,
        "scopes": [{"kind": "shader-family", "value": "WÄTER"}],
    }
    assert "scope=family:wÄter" in builder.canonical_compatibility_registration(upper_unicode_family)
    for field in ("resourceFingerprint", "scopes"):
        invalid = copy.deepcopy(family_registration)
        if field == "scopes":
            invalid[field][0]["value"] = "é" * 257
        else:
            invalid[field] = "é" * 257
        try:
            builder.canonical_compatibility_registration(invalid)
            raise AssertionError("oversized UTF-8 canonical compatibility text was accepted")
        except SystemExit:
            pass
    with tempfile.TemporaryDirectory(prefix="csx-compatibility-input-test-") as temporary:
        source = Path(temporary)
        path = source / builder.COMPATIBILITY_VARIANTS_FILE
        path.parent.mkdir()
        document = json.loads((REPO / builder.COMPATIBILITY_VARIANTS_FILE).read_text(encoding="utf-8"))
        invalid = copy.deepcopy(family_registration)
        invalid["scopes"][0]["value"] = "unmatched\nfamily"
        document["variants"][0]["registrations"] = [invalid]
        path.write_text(json.dumps(document), encoding="utf-8")
        try:
            builder.compatibility_variant_manifest(source)
            raise AssertionError("invalid unmatched provider was silently dropped")
        except SystemExit:
            pass
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

        sequence_pack = root / "sequence-domain.csxpack"
        builder.write_shader_pack(
            sequence_pack, 1, 1,
            [{"logicalKey": "logical", "exactKey": "exact", "metadata": "{}", "bytecode": b"shader"}],
            optimized_a["packSetId"],
        )
        sequence_bytes = bytearray(sequence_pack.read_bytes())
        struct.pack_into("<Q", sequence_bytes, 96, 0xFFFFFFFFFFFFFFFE)
        sequence_pack.write_bytes(sequence_bytes)
        assert builder.validate_shader_pack(sequence_pack, 1)["recordCount"] == 1
        struct.pack_into("<Q", sequence_bytes, 96, 0xFFFFFFFFFFFFFFFF)
        sequence_pack.write_bytes(sequence_bytes)
        try:
            builder.validate_shader_pack(sequence_pack, 1)
            raise AssertionError("exhausted record sequence was accepted")
        except SystemExit:
            pass
        for first_sequence in (2, 3):
            sequence_bytes = bytearray((standard / "Optimized.A.csxpack").read_bytes())
            struct.pack_into("<Q", sequence_bytes, 96, first_sequence)
            sequence_pack.write_bytes(sequence_bytes)
            try:
                builder.validate_shader_pack(sequence_pack, 1)
                raise AssertionError("non-increasing pack record sequence was accepted")
            except SystemExit:
                pass
        manifest = json.loads(
            (standard / "PackManifest.json").read_text(encoding="utf-8")
        )
        assert not (standard / "Manifest.json").exists()
        assert manifest["compatibilityVariants"] == [
            "default",
            "legacy-horizon-fix",
        ]
        assert manifest["shaderCacheABI"] == "a" * 64
        assert all(
            stats["packSetId"] == manifest["packSetId"]
            for stats in (optimized_a, optimized_b, developer_a, developer_b)
        )
        builder.validate_pack_manifest_contract(
            manifest,
            "VR",
            {
                "Optimized.A.csxpack": optimized_a,
                "Optimized.B.csxpack": optimized_b,
                "Developer.A.csxpack": developer_a,
                "Developer.B.csxpack": developer_b,
            },
        )
        pack_stats = {
            "Optimized.A.csxpack": optimized_a,
            "Optimized.B.csxpack": optimized_b,
            "Developer.A.csxpack": developer_a,
            "Developer.B.csxpack": developer_b,
        }

        def rejected(candidate_manifest, candidate_stats=pack_stats) -> None:
            try:
                builder.validate_pack_manifest_contract(
                    candidate_manifest, "VR", candidate_stats
                )
                raise AssertionError("invalid managed pack contract was accepted")
            except SystemExit:
                pass

        for field, value in (
            ("schemaVersion", True),
            ("schemaVersion", 2.0),
            ("formatVersion", False),
            ("formatVersion", 1.0),
        ):
            invalid = copy.deepcopy(manifest)
            invalid[field] = value
            rejected(invalid)

        swapped = copy.deepcopy(manifest)
        swapped["files"]["Optimized.A.csxpack"], swapped["files"][
            "Optimized.B.csxpack"
        ] = (
            swapped["files"]["Optimized.B.csxpack"],
            swapped["files"]["Optimized.A.csxpack"],
        )
        rejected(swapped)

        # Manifest generation/count values are immutable installation baselines:
        # appends may grow the matching generation, and compaction/reset may
        # advance a generation while changing its record count.
        appended = copy.deepcopy(pack_stats)
        appended["Optimized.A.csxpack"]["recordCount"] = 4
        builder.validate_pack_manifest_contract(manifest, "VR", appended)
        compacted = copy.deepcopy(pack_stats)
        compacted["Optimized.B.csxpack"]["generation"] = 2
        compacted["Optimized.B.csxpack"]["recordCount"] = 2
        builder.validate_pack_manifest_contract(manifest, "VR", compacted)
        reset = copy.deepcopy(pack_stats)
        reset["Optimized.A.csxpack"]["generation"] = 5
        reset["Optimized.A.csxpack"]["recordCount"] = 0
        reset["Optimized.B.csxpack"]["generation"] = 3
        reset["Optimized.B.csxpack"]["recordCount"] = 0
        builder.validate_pack_manifest_contract(manifest, "VR", reset)

        regressed = copy.deepcopy(pack_stats)
        regressed["Optimized.A.csxpack"]["recordCount"] = 2
        rejected(manifest, regressed)
        equal_generations = copy.deepcopy(pack_stats)
        equal_generations["Optimized.B.csxpack"]["generation"] = 1
        rejected(manifest, equal_generations)
        wrong_identity = copy.deepcopy(pack_stats)
        wrong_identity["Developer.B.csxpack"]["packSetId"] = "f" * 32
        rejected(manifest, wrong_identity)
        duplicate_variant = copy.deepcopy(manifest)
        duplicate_variant["compatibilityVariants"].append("default")
        rejected(duplicate_variant)

        corpus = json.loads(CONTRACT_CASES.read_text(encoding="utf-8"))
        assert corpus["schemaVersion"] == 1
        for case in corpus["cases"]:
            candidate_manifest, candidate_stats = apply_contract_case(
                manifest, pack_stats, case
            )
            try:
                builder.validate_pack_manifest_contract(
                    candidate_manifest, "VR", candidate_stats
                )
                accepted = True
            except SystemExit:
                accepted = False
            assert accepted is case["accepted"], case["name"]
        accepted_cases = sum(case["accepted"] for case in corpus["cases"])
        print(
            "validated shared managed-pack corpus: "
            f"{len(corpus['cases'])} cases "
            f"({accepted_cases} accepted, {len(corpus['cases']) - accepted_cases} rejected)"
        )
        try:
            builder.write_shader_pack(
                root / "zero.csxpack",
                1,
                1,
                [],
                "0" * 32,
            )
            raise AssertionError("all-zero pack-set identity was accepted")
        except SystemExit:
            pass
        assert not list(standard.rglob("*.pso"))
        if len(sys.argv) == 2:
            default_requirement = builder.canonical_compatibility_requirement_set([])
            exact_key = (
                "Water/1.pso|compat-domain="
                f"{builder.sha256_hex(builder.canonical_compatibility_domain_set([]))}"
                f"|content={'1' * 32}|compat={builder.sha256_hex(default_requirement)}"
            )
            subprocess.run(
                [
                    sys.argv[1],
                    str(standard / "Optimized.A.csxpack"),
                    str(standard / "Optimized.B.csxpack"),
                    exact_key,
                    manifest["packSetId"],
                ],
                check=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
