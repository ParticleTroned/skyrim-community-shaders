"""Extract cache metadata readiness and its real startup/frame-maintenance calls."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    source = (root / "src/ShaderCache.cpp").read_text(encoding="utf-8-sig")
    header = (root / "src/ShaderCache.h").read_text(encoding="utf-8-sig")
    startup = (root / "src/XSEPlugin.cpp").read_text(encoding="utf-8-sig")
    state = (root / "src/State.cpp").read_text(encoding="utf-8-sig")
    declarations = []
    for name in ("IsCompiling", "IsDiskCache", "SetDiskCache",
                 "TryCompleteStartupCompilationPhase", "WriteDiskCacheInfoWhenReady",
                 "WritePendingDiskCacheInfoIfNeeded", "RequestClear", "ProcessPendingClear"):
        match = re.search(r"^\s*(?:bool|void) " + name + r"\([^;{}]*\)[^;{}]*;", header, re.MULTILINE)
        if not match:
            raise ValueError("Missing production ShaderCache declaration: " + name)
        declarations.append(match.group().strip())
    for name in ("isDiskCache", "diskCacheHeld", "featureSetRevertPending",
                 "featureSetChanged", "diskCacheInfoWritePending", "pendingClear",
                 "saveLoadDiskPersistenceBlocked", "menuLoaded", "startupCompilationComplete"):
        match = re.search(r"^\s*(?:bool|std::atomic_bool|std::atomic<bool>) " + name + r"[^\n]*;", header, re.MULTILINE)
        if not match:
            raise ValueError("Missing production ShaderCache field: " + name)
        declarations.append(match.group().strip())
    declarations.append(function(header, "bool HasFeatureSetChanges() const"))
    declarations.append(function(header, "bool IsDiskCacheActive() const"))

    bodies = []
    for signature in ("bool ShaderCache::IsCompiling()", "bool ShaderCache::IsDiskCache() const",
                      "void ShaderCache::SetDiskCache(bool value)",
                      "void ShaderCache::TryCompleteStartupCompilationPhase()",
                      "void ShaderCache::WriteDiskCacheInfoWhenReady()",
                      "void ShaderCache::WritePendingDiskCacheInfoIfNeeded()",
                      "void ShaderCache::RequestClear()", "void ShaderCache::ProcessPendingClear()"):
        bodies.append(function(source, signature))

    # Native deletion/rollback I/O is outside this fixture. Verify those actual
    # mutation entry points still cancel a request belonging to the old cache.
    cancellation = "diskCacheInfoWritePending.store(false, std::memory_order_release);"
    for signature in ("void ShaderCache::DeleteDiskCache()", "bool ShaderCache::RestorePreviousDiskCache()"):
        if cancellation not in function(source, signature):
            raise ValueError("Cache mutation no longer cancels stale metadata: " + signature)

    data_loaded = startup[startup.index("case SKSE::MessagingInterface::kDataLoaded:"):]
    data_loaded = data_loaded[:data_loaded.index("case SKSE::MessagingInterface::kPreLoadGame:")]
    wait_end = function(data_loaded, "while (shaderCache->IsCompiling()")
    callbacks = data_loaded[data_loaded.index(wait_end.strip()) + len(wait_end.strip()):]
    publish = re.search(r"shaderCache->menuLoaded\.store\([^;]*;", callbacks)
    complete = re.search(r"shaderCache->TryCompleteStartupCompilationPhase\(\);", callbacks)
    request = re.search(r"shaderCache->WriteDiskCacheInfoWhenReady\(\);", callbacks)
    if not publish or not complete or not request or not publish.start() < complete.start() < request.start():
        raise ValueError("DataLoaded must publish readiness and complete scheduling before requesting metadata")
    startup_body = "\n".join(match.group() for match in (publish, complete, request))

    frame = function(state, "void State::Reset()")
    maintenance = re.search(
        r"globals::shaderCache->ProcessPendingClear\(\);\s*"
        r"globals::shaderCache->WritePendingDiskCacheInfoIfNeeded\(\);", frame)
    if not maintenance:
        raise ValueError("Frame maintenance must consume metadata after pending cache clears")

    output.mkdir(parents=True, exist_ok=True)
    (output / "shader_cache_readiness_members.h").write_text("\n".join(declarations), encoding="utf-8")
    (output / "shader_cache_readiness_under_test.h").write_text(
        "namespace SIE {\n" + "\n".join(bodies) + "}\n"
        + "void DataLoadedForTest(SIE::ShaderCache* shaderCache) {\n" + startup_body + "\n}\n"
        + "void FrameMaintenanceForTest() {\n" + maintenance.group() + "\n}\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
