from __future__ import annotations

import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "build_provenance", ROOT / "tools/build_provenance.py"
)
assert SPEC and SPEC.loader
PROVENANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROVENANCE)


class BuildProvenanceTests(unittest.TestCase):
    def test_canonical_identity_is_order_independent(self) -> None:
        left = {"z": [3, 2, 1], "a": {"second": 2, "first": 1}}
        right = {"a": {"first": 1, "second": 2}, "z": [3, 2, 1]}
        self.assertEqual(
            PROVENANCE.sha256_bytes(PROVENANCE.canonical_bytes(left)),
            PROVENANCE.sha256_bytes(PROVENANCE.canonical_bytes(right)),
        )

    def test_shader_cache_abi_changes_only_with_explicit_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            contract = root / "config/shader-cache-abi.json"
            contract.parent.mkdir(parents=True)
            contract.write_text('{"schemaVersion":1,"globalAbi":"one"}\n', encoding="utf-8")
            unrelated = root / "src/ShaderCache.cpp"
            unrelated.parent.mkdir(parents=True)
            unrelated.write_text("first implementation\n", encoding="utf-8")

            first = PROVENANCE.shader_contract_identity(
                root, ["config/shader-cache-abi.json"], "VR"
            )
            unrelated.write_text("second implementation\n", encoding="utf-8")
            second = PROVENANCE.shader_contract_identity(
                root, ["config/shader-cache-abi.json"], "VR"
            )
            self.assertEqual(first, second)

            contract.write_text('{"schemaVersion":1,"globalAbi":"two"}\n', encoding="utf-8")
            third = PROVENANCE.shader_contract_identity(
                root, ["config/shader-cache-abi.json"], "VR"
            )
            self.assertNotEqual(first, third)

    def test_finalize_binds_exact_artifact_and_verify_rejects_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = root / "CommunityShaders.dll"
            artifact.write_bytes(b"first artifact")
            identity = {"shaderCache": {"contract": {"schemaVersion": 1}}}
            manifest = {
                "schema": PROVENANCE.SCHEMA,
                "schemaVersion": PROVENANCE.SCHEMA_VERSION,
                "buildId": PROVENANCE.sha256_bytes(
                    PROVENANCE.canonical_bytes(identity)
                ),
                "identity": identity,
            }
            manifest["identity"]["shaderCache"]["abiId"] = (
                PROVENANCE.sha256_bytes(
                    PROVENANCE.canonical_bytes(
                        manifest["identity"]["shaderCache"]["contract"]
                    )
                )
            )
            # Build ID covers the completed identity, including shader ABI.
            manifest["buildId"] = PROVENANCE.sha256_bytes(
                PROVENANCE.canonical_bytes(manifest["identity"])
            )
            base = root / "base.json"
            final = root / "CSX.BuildManifest.json"
            base.write_text(json.dumps(manifest), encoding="utf-8")

            args = type(
                "Args",
                (),
                {"base_manifest": base, "artifact": artifact, "output_manifest": final},
            )()
            self.assertEqual(PROVENANCE.finalize(args), 0)
            recorded = json.loads(final.read_text(encoding="utf-8"))
            self.assertEqual(recorded["artifact"]["sha256"], PROVENANCE.sha256_file(artifact))

            artifact.write_bytes(b"different artifact")
            verify_args = type(
                "Args", (), {"manifest": final, "artifact": artifact}
            )()
            with self.assertRaises(ValueError):
                PROVENANCE.verify(verify_args)

    def test_dirty_digest_changes_with_untracked_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(
                ["git", "-C", str(root), "config", "user.email", "test@example.invalid"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", str(root), "config", "user.name", "Test"], check=True
            )
            tracked = root / "tracked.txt"
            tracked.write_text("tracked", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "tracked.txt"], check=True)
            subprocess.run(["git", "-C", str(root), "commit", "-qm", "base"], check=True)
            self.assertFalse(PROVENANCE.working_tree_state(root)[0])

            extra = root / "extra.txt"
            extra.write_text("one", encoding="utf-8")
            first = PROVENANCE.working_tree_state(root)
            extra.write_text("two", encoding="utf-8")
            second = PROVENANCE.working_tree_state(root)
            self.assertTrue(first[0])
            self.assertNotEqual(first[1], second[1])

    def test_clean_rejection_identifies_untracked_source_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            extra = root / "runner-generated.txt"
            extra.write_text("generated", encoding="utf-8")

            diagnostic = PROVENANCE.explain_unclean_provenance(root, True, [])

            self.assertIn("source status:", diagnostic)
            self.assertIn("?? runner-generated.txt", diagnostic)


if __name__ == "__main__":
    unittest.main()
