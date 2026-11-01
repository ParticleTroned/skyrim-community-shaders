"""Compile the production cache owners and teardown order with narrow I/O stubs."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    source = (root / "src/ShaderCache.cpp").read_text(encoding="utf-8-sig")
    header = (root / "src/ShaderCache.h").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    (output / "shader_cache_lifetime_members.h").write_text(
        function(header, "static ShaderCache& Instance()"), encoding="utf-8")
    owners = ""
    if "ShaderCacheManifestStorage()" in source:
        owners += function(source, "Util::ShaderCacheManifest::Manifest& ShaderCacheManifestStorage()")
    owners += function(source, "Util::ShaderCacheManifest::Manifest& GetShaderCacheManifest()")
    owners += function(source, "struct ManagedPackSet") + ";\n"
    owners += function(source, "ManagedPackSet& ManagedPacks()")
    lifecycle = function(source, "ShaderCache::ShaderCache()")
    lifecycle += function(source, "ShaderCache::~ShaderCache()")
    (output / "shader_cache_lifetime_under_test.h").write_text(
        "namespace SIE {\nnamespace {\n" + owners + "}\n" + lifecycle + "}\n",
        encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
