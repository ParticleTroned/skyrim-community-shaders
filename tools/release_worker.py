"""Resume both release cache runtimes using the canonical compiler and packer."""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import importlib.metadata
import inspect
import json
import os
import sys
import threading
import traceback
import uuid
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path


from release_worker_support import (
    AlreadyRunning, checked_path, exclusive_owner, file_inventory, own_process_job, validate_config,
)


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def canonical_modules(config: dict):
    tools = Path(config["repository"]) / "tools"
    from build_provenance import sha256_file
    for name in ("build_provenance", "shader_bytecode", "shader_cache_manifest", "standalone_shader_cache"):
        module = sys.modules.get(name)
        if module is not None and sha256_file(Path(module.__file__)) != sha256_file(tools / (name + ".py")):
            raise ValueError(f"Imported helper differs from the frozen release source: {name}")
    sys.path.insert(0, str(tools))
    return load_module("release_cache_builder", tools / "build-shader-cache.py")


def compile_mode(config: dict) -> int:
    config = validate_config(config)
    verify_compiler_pin(Path(config["repository"]))
    builder = canonical_modules(config)
    from shader_compile_checkpoints import Checkpoints
    from build_provenance import sha256_file, tree_digest
    from shader_cache_manifest import compile_task_defines
    adapter = load_module("canonical_shader_adapter", Path(config["repository"]) / "tools/shader_cache_compile.py")
    native = adapter.compiler
    signature = inspect.signature(adapter._compile_shader)
    original = adapter.compile_shader
    arguments = native.parse_arguments(4)
    shader_root = checked_path(Path(arguments.shader_dir))
    output_root = checked_path(Path(arguments.output_dir))
    if arguments.extra_includes:
        raise ValueError("Release builds require includes inside the frozen shader tree")
    workspace = Path(config["workspace"])
    if (not shader_root.is_relative_to(workspace) or not output_root.is_relative_to(workspace)
            or shader_root == workspace or output_root == workspace
            or shader_root.is_relative_to(output_root) or output_root.is_relative_to(shader_root)
            or not checked_path(Path(arguments.config)).is_relative_to(workspace)
            or arguments.jobs != config["jobs"]
            or checked_path(Path(arguments.fxc)) != Path(config["fxc"])):
        raise ValueError("Compiler arguments differ from the owning worker configuration")
    file_inventory(output_root)
    store = Checkpoints(Path(config["state"]) / "objects")
    file_inventory(shader_root)
    shared = {"schema": 1, "shaderRoot": str(shader_root), "shaderTree": tree_digest(shader_root),
              "compiler": sha256_file(Path(arguments.fxc)),
              "compilerModule": sha256_file(Path(native.__file__)),
              "adapter": sha256_file(Path(adapter.__file__))}
    counters = {"reused": 0, "compiled": 0}
    counter_lock = threading.Lock()
    from hlslkit.compile_shaders import parse_shader_configs
    expected = compile_task_defines(parse_shader_configs(arguments.config))
    existing = builder.cache_blob_paths(output_root)
    if len(existing) > 20000:
        raise RuntimeError("Unexpectedly large interrupted cache")
    displaced = Path(config["state"]) / "displaced" / uuid.uuid4().hex
    for blob in existing:
        if (blob.parent.name, blob.name) not in expected:
            if not blob.resolve().is_relative_to(output_root):
                raise RuntimeError("Cache contains an escaped path")
            destination = displaced / blob.relative_to(output_root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            blob.replace(destination)

    def checked_compile(*args, **kwargs):
        bound = signature.bind(*args, **kwargs)
        bound.apply_defaults()
        values = bound.arguments
        (source_key, blob_name), macros = next(iter(compile_task_defines([
            (values["shader_file"], values["shader_type"], values["entry"], values["defines"])
        ]).items()))
        target = checked_path(Path(values["output_dir"]) / source_key / blob_name)
        if not target.is_relative_to(output_root):
            raise ValueError("Compiler output escaped its cache directory")
        extension = target.suffix
        options = {name: values[name] for name in ("debug", "strip_debug_defines", "optimization_level",
                   "force_partial_precision", "debug_defines", "extra_includes")}
        if options["debug_defines"] is not None:
            options["debug_defines"] = sorted(options["debug_defines"])
        inputs = {**shared, "source": values["shader_file"], "stage": values["shader_type"],
                  "entry": values["entry"], "macros": macros, "options": options}
        restored = store.restore(inputs, target, extension)
        if restored is not None and not native.stop_event.is_set():
            with counter_lock:
                counters["reused"] += 1
            return {"file": values["shader_file"], "entry": values["entry"], "type": values["shader_type"],
                    "log": restored.get("diagnostics", ""), "success": True, "cmd": [], "duration_seconds": 0.0}
        result = original(*args, **kwargs)
        if result["success"]:
            store.save(inputs, target, extension, diagnostics=result["log"])
            with counter_lock:
                counters["compiled"] += 1
        return result

    native.validate_shader_inputs = adapter.validate_shader_inputs
    native.compile_shader = checked_compile
    result = native.main()
    print(f"Durable checkpoint results: {json.dumps(counters, sort_keys=True)}", flush=True)
    return result


def checkpoint_helpers(original, tasks, shader_root, cache_dir, fxc, jobs, state, implementation):
    """Checkpoint helpers individually while retaining the canonical FXC invocation."""
    from shader_compile_checkpoints import Checkpoints
    from build_provenance import sha256_file, tree_digest
    import standalone_shader_cache
    store = Checkpoints(state / "objects")
    shared = {"kind": "standalone", "shaderRoot": str(shader_root.resolve()),
              "shaderTree": tree_digest(shader_root), "compiler": sha256_file(Path(fxc)),
              "implementation": sha256_file(implementation)}

    def one(item):
        relative, request = item
        destination = cache_dir / relative
        if not destination.resolve().is_relative_to(cache_dir.resolve()):
            raise RuntimeError("Standalone checkpoint escaped the task cache")
        inputs = {**shared, "request": {key: value for key, value in request.items() if key != "compile_digest"}}
        extension = standalone_shader_cache.STAGES[request["profile"]][1]
        if store.restore(inputs, destination, extension) is None:
            original({relative: request}, shader_root, cache_dir, fxc, 1)
            store.save(inputs, destination, extension)

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(one, tasks.items()))


def worker_mode(config_path: Path, config: dict) -> int:
    from shader_compile_checkpoints import atomic_json
    from build_provenance import sha256_file
    config = validate_config(config)
    builder = canonical_modules(config)
    state = Path(config["state"])
    workspace = Path(config["workspace"])
    output = Path(config["output"])
    source = Path(config["repository"])
    core = Path(config["core"])
    with contextlib.ExitStack() as owners:
        # Lock each destination, so different state directories cannot share writers.
        for directory in sorted((state, workspace, output)):
            owners.enter_context(exclusive_owner(directory / "owner.lock"))
        job_handle = own_process_job()
        status_path = state / "status.json"

        def status(phase, condition="running", **details):
            atomic_json(status_path, {"state": condition, "phase": phase, "pid": os.getpid(),
                                      "updatedUtc": datetime.now(timezone.utc).isoformat(), **details})

        try:
            status("validate-inputs")
            core_manifest = json.loads((core / "SKSE/Plugins/CSX.BuildManifest.json").read_bytes())
            from build_provenance import validate_manifest_identity
            validate_manifest_identity(core_manifest)
            build = core_manifest["identity"]["build"]
            expected_options = {"DEVBENCH_BRIDGE": "OFF", "TRACY_SUPPORT": "OFF", "NVPERF_SUPPORT": "OFF",
                                "ENABLE_SKYRIM_SE": "ON", "ENABLE_SKYRIM_AE": "ON", "ENABLE_SKYRIM_VR": "ON"}
            if (build["configuration"] != "Release" or build["pluginVersion"] != config["pluginVersion"]
                    or any(build["options"].get(name) != value for name, value in expected_options.items())
                    or core_manifest["identity"]["source"]["dirty"]):
                raise ValueError("Release worker requires a clean universal production core")
            abi = core_manifest["identity"]["shaderCache"]["abiId"]
            if core_manifest["identity"]["source"]["commit"] != config["sourceCommit"]:
                raise RuntimeError("Release core source identity changed")
            dll = core / "SKSE/Plugins/CommunityShaders.dll"
            if (sha256_file(dll) != core_manifest["artifact"]["sha256"]
                    or dll.stat().st_size != core_manifest["artifact"]["sizeBytes"]):
                raise RuntimeError("Release production DLL differs from its producer manifest")
            import hlslkit.compile_shaders
            verify_compiler_pin(source)
            # Include imported hlslkit helpers, not only its CLI module.
            compiler_package = Path(hlslkit.compile_shaders.__file__).parent
            input_identity = {
                "schema": 2, "sourceCommit": config["sourceCommit"],
                "python": sys.version,
                "packages": {item.metadata["Name"]: item.version for item in importlib.metadata.distributions()},
                "compiler": sha256_file(Path(config["fxc"])),
                "compilerPackage": {name: value for name, value in file_inventory(compiler_package).items()
                                    if name.endswith(".py")},
                "source": source_inventory(source), "core": file_inventory(core),
                "worker": {name: sha256_file(Path(__file__).with_name(name)) for name in
                           ("release_worker.py", "release_worker_support.py", "shader_compile_checkpoints.py",
                            "release_worker_package.py", "build_provenance.py", "shader_bytecode.py")},
                "version": config["pluginVersion"], "abi": abi,
            }
            identity_path = state / "inputs-v2.json"
            if identity_path.exists() and json.loads(identity_path.read_bytes()) != input_identity:
                raise RuntimeError("Build inputs changed; use a new task root for different inputs")
            atomic_json(identity_path, input_identity)
            distribution = builder.derive_distribution_profile(source)
            snapshots = {}
            for runtime in ("SE", "VR"):
                snapshot = state / f"source-{runtime}"
                exclusions = distribution.excluded_packages if runtime == "SE" else frozenset(builder.NON_SHIPPED_PACKAGES)
                # Always derive and verify a fresh candidate, even after interrupted snapshot creation.
                candidate = state / f".source-{runtime}-{uuid.uuid4().hex}"
                builder.stage_merged_shaders(source, candidate, exclusions)
                for relative in file_inventory(candidate):
                    shipped = core / "Shaders" / relative
                    if (not shipped.is_file() or (candidate / relative).read_bytes().replace(b"\r\n", b"\n")
                            != shipped.read_bytes().replace(b"\r\n", b"\n")):
                        raise RuntimeError(f"Release core shader mismatch: {relative}")
                if snapshot.exists():
                    if file_inventory(snapshot) != file_inventory(candidate):
                        raise RuntimeError(f"Preserved {runtime} source differs from release inputs")
                    builder.shutil.rmtree(candidate)
                else:
                    candidate.replace(snapshot)
                snapshots[runtime] = snapshot
            os.environ["CSX_RELEASE_WORKER_CONFIG"] = str(config_path.resolve())
            compiler, yaml, write_manifest = builder.require_compile_tools()
            compiler = (sys.executable, str(Path(__file__).resolve()), "compile")
            fomod = load_module("release_fomod_validation", source / "tools/build-fomod-package.py")
            original_helpers = builder.standalone_shader_cache.compile_tasks
            builder.standalone_shader_cache.compile_tasks = lambda tasks, stage, cache, fxc, jobs: checkpoint_helpers(
                original_helpers, tasks, stage, cache, fxc, jobs, state, source / "tools/standalone_shader_cache.py")
            for runtime in ("SE", "VR"):
                status(runtime)
                if source_inventory(source) != input_identity["source"] or file_inventory(core) != input_identity["core"]:
                    raise ValueError("Release inputs changed during compilation")
                snapshot = snapshots[runtime]
                stage = workspace / f"staged-shaders-{runtime}"
                if stage.exists():
                    expected = file_inventory(snapshot)
                    actual = file_inventory(stage)
                    if any(expected.get(name) != item for name, item in actual.items()):
                        raise RuntimeError(f"{runtime} preserved shader tree differs from release source")
                builder.shutil.copytree(snapshot, stage, dirs_exist_ok=True)
                marker = state / f"complete-{runtime}-v2.json"
                published = output / runtime
                if marker.exists() and published.exists():
                    expected = json.loads(marker.read_bytes())
                    if (expected["inputs"] != input_identity
                            or managed_fingerprint(published, builder) != expected["files"]):
                        raise RuntimeError(f"Published {runtime} cache or build inputs changed")
                    fomod.validate_cache_source(published / "ShaderCache", "SE-AE" if runtime == "SE" else "VR", abi, True)
                    print(f"{runtime}: preserved completed runtime verified", flush=True)
                    continue
                candidate, counts, sections = builder.build_runtime(
                    source_root=source, stage=stage, workspace=workspace, runtime=runtime,
                    config_path=builder.configs_for(source)[runtime], plugin_version=config["pluginVersion"],
                    jobs=config["jobs"], fxc=config["fxc"], compiler=compiler, yaml=yaml,
                    write_manifest=write_manifest, profile=builder.SHIPPED_CACHE_PROFILE,
                    distribution_profile=distribution, include_horizon_fix=True, reverse_z_enabled=False)
                if (file_inventory(stage) != file_inventory(snapshot)
                        or source_inventory(source) != input_identity["source"]
                        or file_inventory(core) != input_identity["core"]
                        or sha256_file(Path(config["fxc"])) != input_identity["compiler"]):
                    raise ValueError("Build inputs changed before cache publication")
                fomod.validate_cache_source(candidate / "ShaderCache", "SE-AE" if runtime == "SE" else "VR", abi, True)
                builder.publish_runtime_cache(candidate, output, runtime)
                atomic_json(marker, {"inputs": input_identity, "files": managed_fingerprint(published, builder),
                                     "blobCounts": counts, "featureSections": sections})
                print(f"{runtime}: complete and checkpointed: {counts}", flush=True)
            if "package" in config:
                status("package")
                from release_worker_package import finish_package
                finish_package(config, fomod)
            status("finished", "complete")
            return 0
        except BaseException as error:
            status("stopped", "failed", error=str(error))
            traceback.print_exc()
            return 1


def source_inventory(source: Path) -> dict:
    """Freeze the canonical builder, distribution contracts and package sources."""
    result = {}
    for name in ("tools", "config", "package", "features", "src", ".github/configs"):
        result[name] = file_inventory(source / name)
    for name in ("CMakePresets.json", "CMakeLists.txt"):
        from build_provenance import sha256_file
        result[name] = sha256_file(source / name)
    return result


def verify_compiler_pin(source: Path) -> None:
    """Refuse a different hlslkit revision before accepting compiler checkpoints."""
    import re
    requirement = (source / "tools/shader-cache-requirements.txt").read_text(encoding="utf-8")
    expected = re.search(r"hlslkit\.git@([0-9a-f]{40})(?:\s|$)", requirement)
    installed = json.loads(importlib.metadata.distribution("hlslkit").read_text("direct_url.json") or "{}")
    if expected is None or installed.get("vcs_info", {}).get("commit_id") != expected[1]:
        raise ValueError("Install the frozen source's pinned shader-cache requirements before resuming")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "compile":
        del sys.argv[1]
        config = json.loads(Path(os.environ["CSX_RELEASE_WORKER_CONFIG"]).read_text(encoding="utf-8-sig"))
        return compile_mode(config)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--validate-config", action="store_true")
    arguments = parser.parse_args()
    config = json.loads(arguments.config.read_text(encoding="utf-8-sig"))
    if arguments.validate_config:
        print(json.dumps(validate_config(config)))
        return 0
    try:
        return worker_mode(arguments.config, config)
    except AlreadyRunning:
        print("Another release worker is already active; no changes were made.", flush=True)
        return 75


def managed_fingerprint(runtime_root: Path, builder) -> dict:
    """Bind completed runtimes to all six exact managed-cache files."""
    cache = runtime_root / "ShaderCache"
    files = file_inventory(cache)
    names = ["Info.ini", "PackManifest.json", *builder.PACK_FILE_NAMES]
    if set(path.name for path in cache.iterdir()) != set(names):
        raise RuntimeError("Completed runtime contains unexpected cache contents")
    return files


if __name__ == "__main__":
    raise SystemExit(main())
