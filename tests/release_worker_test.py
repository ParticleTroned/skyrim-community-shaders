"""Exercise interruption recovery and fail-closed bytecode checkpoint reuse."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from shader_compile_checkpoints import Checkpoints, atomic_json
from release_worker import exclusive_owner
from release_worker import checkpoint_helpers
from release_worker import verify_compiler_pin
from release_worker_support import validate_config, checked_path, file_inventory
from release_worker_package import completed_package, finish_package
from build_provenance import sha256_file


def bytecode(stage=0):
    return b"DXBC" + bytes(16) + struct.pack("<III", 1, 52, 1) + struct.pack("<I", 36) + b"SHDR" + struct.pack("<III", 8, (stage << 16) | 0x50, 2)


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.store = Checkpoints(self.root / "state")
        self.source = self.root / "first.pso"
        self.source.write_bytes(bytecode())
        self.inputs = {"source": "abc", "macros": "def", "compiler": "ghi"}

    def tearDown(self):
        self.temporary.cleanup()

    def test_restarts_restore_completed_bytes_into_another_variant(self):
        self.store.save(self.inputs, self.source, ".pso", diagnostics="retained warning")
        restarted = Checkpoints(self.root / "state")
        destination = self.root / "horizon" / "same.pso"
        record = restarted.restore(self.inputs, destination, ".pso")
        self.assertEqual(destination.read_bytes(), bytecode())
        self.assertEqual(record["diagnostics"], "retained warning")

    def test_changed_inputs_never_reuse_a_checkpoint(self):
        self.store.save(self.inputs, self.source, ".pso")
        for field in self.inputs:
            changed = {**self.inputs, field: "different"}
            self.assertIsNone(self.store.restore(changed, self.root / "out.pso", ".pso"))

    def test_corruption_and_truncation_force_recompilation(self):
        self.store.save(self.inputs, self.source, ".pso")
        record, blob = self.store.paths(self.inputs)
        for damaged in (b"", bytecode()[:-1], b"different"):
            blob.write_bytes(damaged)
            self.assertIsNone(self.store.restore(self.inputs, self.root / "out.pso", ".pso"))
        record.write_text("{interrupted")
        self.assertIsNone(self.store.restore(self.inputs, self.root / "out.pso", ".pso"))

    def test_missing_record_never_blesses_an_orphaned_object(self):
        self.store.save(self.inputs, self.source, ".pso")
        record, _ = self.store.paths(self.inputs)
        record.unlink()
        self.assertIsNone(self.store.restore(self.inputs, self.root / "out.pso", ".pso"))

    def test_stage_mismatch_is_rejected(self):
        with self.assertRaises(ValueError):
            self.store.save(self.inputs, self.source, ".vso")

    def test_legacy_adopted_output_is_rejected_despite_valid_bytecode(self):
        self.store.save(self.inputs, self.source, ".pso")
        record, _ = self.store.paths(self.inputs)
        metadata = json.loads(record.read_bytes())
        metadata["origin"] = "verified-interrupted-output"
        atomic_json(record, metadata)
        target = self.root / "out.pso"
        self.assertIsNone(self.store.restore(self.inputs, target, ".pso"))
        self.assertFalse(target.exists())

    def test_partial_target_is_replaced_from_verified_object(self):
        self.store.save(self.inputs, self.source, ".pso")
        target = self.root / "out.pso"
        target.write_bytes(b"DXBC")
        self.assertIsNotNone(self.store.restore(self.inputs, target, ".pso"))
        self.assertEqual(target.read_bytes(), bytecode())

    def test_atomic_status_update_is_readable(self):
        status = self.root / "status.json"
        atomic_json(status, {"phase": "SE"})
        atomic_json(status, {"phase": "VR"})
        self.assertEqual(json.loads(status.read_bytes()), {"phase": "VR"})

    def test_helper_checkpoint_reuses_identical_inputs_across_variant_names(self):
        shader_root = self.root / "Shaders"
        shader_root.mkdir()
        (shader_root / "Test.hlsl").write_text("shader source")
        compiler = self.root / "fxc.exe"
        compiler.write_bytes(b"test compiler identity")
        implementation = self.root / "implementation.py"
        implementation.write_text("implementation identity")
        cache = self.root / "helpers"
        cache.mkdir()
        calls = []

        def compile_one(tasks, stage, destination, fxc, jobs):
            calls.append(tasks)
            for relative in tasks:
                path = destination / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(bytecode())

        request = {"source": "Test.hlsl", "profile": "ps_5_0", "entry": "main",
                   "defines": [("MODE", "1")], "compile_digest": 1}
        checkpoint_helpers(compile_one, {"Standalone/a.cso": request}, shader_root, cache,
                           str(compiler), 1, self.root / "helper-state", implementation)
        checkpoint_helpers(compile_one, {"Standalone/b.cso": {**request, "compile_digest": 2}}, shader_root, cache,
                           str(compiler), 1, self.root / "helper-state", implementation)
        self.assertEqual(len(calls), 1)
        self.assertEqual((cache / "Standalone/b.cso").read_bytes(), bytecode())
        checkpoint_helpers(compile_one, {"Standalone/c.cso": {**request, "defines": [("MODE", "2")]}}, shader_root, cache,
                           str(compiler), 1, self.root / "helper-state", implementation)
        self.assertEqual(len(calls), 2)

    @unittest.skipUnless(sys.platform == "win32", "Windows ownership contract")
    def test_duplicate_owner_is_refused(self):
        with exclusive_owner(self.root / "owner.lock"):
            with self.assertRaises(OSError):
                with exclusive_owner(self.root / "owner.lock"):
                    self.fail("Second owner entered")

    @unittest.skipUnless(sys.platform == "win32", "Windows process job contract")
    def test_worker_death_terminates_only_its_child(self):
        import psutil
        child_file = self.root / "child.json"
        code = (
            "from release_worker import own_process_job; import subprocess,sys,time,json; "
            "job=own_process_job(); child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); "
            f"open({str(child_file)!r},'w').write(json.dumps({{'pid':child.pid}})); time.sleep(60)"
        )
        owner = subprocess.Popen([sys.executable, "-c", code], cwd=ROOT / "tools")
        child = None
        try:
            deadline = time.monotonic() + 10
            while not child_file.exists() and time.monotonic() < deadline:
                if owner.poll() is not None:
                    self.fail("Job owner exited before starting its child")
                time.sleep(0.05)
            self.assertTrue(child_file.exists())
            child = psutil.Process(json.loads(child_file.read_text())["pid"])
            owner.kill()
            owner.wait(timeout=5)
            child.wait(timeout=5)
            self.assertFalse(child.is_running())
        finally:
            if owner.poll() is None:
                owner.kill()
                owner.wait(timeout=5)
            if child is not None and child.is_running():
                child.kill()


class WorkerBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for name in ("source", "core"):
            (self.root / name).mkdir()
        compiler = self.root / "fxc.exe"
        compiler.write_bytes(b"compiler")
        self.config = {"repository": str(self.root / "source"), "taskRoot": str(self.root),
                       "core": str(self.root / "core"), "workspace": str(self.root / "workspace"),
                       "state": str(self.root / "state"), "output": str(self.root / "output"),
                       "fxc": str(compiler), "jobs": 2, "sourceCommit": "a" * 40,
                       "pluginVersion": "CSX 1.2.3-VR"}

    def tearDown(self):
        self.temporary.cleanup()

    def test_invalid_bounds_and_unknown_mutations_do_not_create_state(self):
        validate_config(self.config)
        for change in ({"jobs": True}, {"jobs": 0}, {"jobs": 65}, {"jobs": "2"},
                       {"workspace": self.config["core"]}, {"output": self.config["state"]},
                       {"state": str(self.root.parent)}, {"finishRelease": True},
                       {"sourceCommit": "bad"}, {"taskName": "unrelated-task"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_config({**self.config, **change})
        self.assertFalse((self.root / "state").exists())
        with self.assertRaises(ValueError):
            validate_config({**self.config, "workspace": "relative/workspace"})

    def test_compiler_revision_must_match_the_frozen_requirements(self):
        source = self.root / "source"
        (source / "tools").mkdir()
        (source / "tools/shader-cache-requirements.txt").write_text("git+https://example.test/hlslkit.git@" + "a" * 40 + "\n")
        for revision in ("a" * 40, "b" * 40, None):
            distribution = SimpleNamespace(read_text=lambda _: json.dumps({"vcs_info": {"commit_id": revision}}))
            with patch("release_worker.importlib.metadata.distribution", return_value=distribution):
                if revision == "a" * 40:
                    verify_compiler_pin(source)
                else:
                    with self.assertRaises(ValueError):
                        verify_compiler_pin(source)

    def test_linked_ancestor_is_rejected_without_writing_the_target(self):
        external = self.root / "outside"
        external.mkdir()
        link = self.root / "link"
        if sys.platform == "win32":
            subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(external)], check=True, capture_output=True)
        else:
            link.symlink_to(external, target_is_directory=True)
        try:
            with self.assertRaises(ValueError):
                checked_path(link / "new-state" / "objects")
            self.assertEqual(list(external.iterdir()), [])
        finally:
            if sys.platform == "win32":
                os.rmdir(link)
            else:
                link.unlink()

    def test_inventory_detects_binary_changes_and_ignores_python_cache(self):
        tree = self.root / "source"
        (tree / "blob").write_bytes(b"a\r\nb")
        original = file_inventory(tree)
        (tree / "blob").write_bytes(b"a\nb")
        self.assertNotEqual(file_inventory(tree), original)
        (tree / "__pycache__").mkdir()
        (tree / "__pycache__/unused.pyc").write_bytes(b"ignored")
        self.assertEqual(set(file_inventory(tree)), {"blob"})

    def test_stale_archive_cannot_reuse_receipt_for_changed_inputs(self):
        archive = self.root / "aio.7z"
        archive.write_bytes(b"old archive")
        marker = self.root / "package.json"
        inputs = {"core": "old", "VR": "old", "SE": "old", "settings": {"version": "old"}}
        atomic_json(marker, {"inputs": inputs, "size": archive.stat().st_size, "sha256": sha256_file(archive)})
        self.assertTrue(completed_package(marker, archive, inputs))
        for key in inputs:
            self.assertFalse(completed_package(marker, archive, {**inputs, key: "changed"}))
        archive.write_bytes(b"new archive")
        self.assertFalse(completed_package(marker, archive, inputs))
        marker.write_bytes(b"{partial")
        self.assertFalse(completed_package(marker, archive, inputs))

    def test_package_is_extracted_compared_and_reused_after_restart(self):
        config = self.config
        source = Path(config["repository"])
        (source / "tools").mkdir()
        (source / "tools/build-fomod-package.py").write_text("canonical")
        (Path(config["core"]) / "dll").write_bytes(b"production")
        for runtime in ("SE", "VR"):
            cache = Path(config["output"]) / runtime / "ShaderCache"
            cache.mkdir(parents=True)
            (cache / "pack").write_bytes(runtime.encode())
        state = Path(config["state"])
        state.mkdir()
        archiver = self.root / "7z.exe"
        archiver.write_bytes(b"archiver")
        config["package"] = {"version": "csx1.2.3", "archiveName": "CSX_AIO-1.2.3-Production-ShaderCache-FOMOD.7z",
                             "sevenZip": str(archiver), "pwsh": "pwsh"}
        import shutil
        stages = []
        validations = []
        corrupt_extraction = [False]

        def stage(core, se, vr, destination, version, **kwargs):
            shutil.copytree(core, destination / "Core")
            shutil.copytree(se, destination / "ShaderCache-SE-AE")
            shutil.copytree(vr, destination / "ShaderCache-VR")
            (destination / "fomod").mkdir()
            (destination / "fomod/info.xml").write_text(version)
            stages.append(destination)

        def run(arguments, **kwargs):
            if arguments[1] == "a":
                Path(arguments[6]).write_bytes(b"archive")
            elif "xf" in arguments:
                shutil.copytree(stages[-1], kwargs["cwd"], dirs_exist_ok=True)
                if corrupt_extraction[0]:
                    (Path(kwargs["cwd"]) / "Core/dll").write_bytes(b"wrong extraction")
            return SimpleNamespace(returncode=0)

        fomod = SimpleNamespace(stage_package=stage, validate_staged_package=lambda *args: validations.append(args))
        with patch("release_worker_package.subprocess.run", side_effect=run):
            archive = finish_package(config, fomod)
            self.assertTrue(archive.is_file())
            self.assertEqual(len(validations), 1)
            finish_package(config, fomod)
            self.assertEqual(len(stages), 1)
            (Path(config["core"]) / "dll").write_bytes(b"changed production")
            finish_package(config, fomod)
            self.assertEqual(len(stages), 2)
            self.assertEqual(len(list((state / "displaced").glob("*.7z"))), 1)
            receipt = (state / "package-complete-v2.json").read_bytes()
            (Path(config["core"]) / "dll").write_bytes(b"third production")
            corrupt_extraction[0] = True
            with self.assertRaises(RuntimeError):
                finish_package(config, fomod)
            self.assertEqual((state / "package-complete-v2.json").read_bytes(), receipt)



if __name__ == "__main__":
    unittest.main()
