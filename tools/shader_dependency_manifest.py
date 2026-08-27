#!/usr/bin/env python3
"""Generate the static CSX shader dependency and invalidation manifest.

The deployed shader namespace is a merge of package/Shaders and every
features/*/Shaders directory.  This tool analyses that virtual namespace,
rather than treating the source directories as unrelated trees.

The result is deliberately evidence-based.  Static evidence is emitted with
its source location; anything that cannot be proved (dynamic paths, runtime
bindings, engine-owned formats) remains an explicit unresolved item.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


SCHEMA_VERSION = 2
GENERATOR = "tools/shader_dependency_manifest.py"
SHADER_SUFFIXES = {".hlsl", ".hlsli"}
STAGE_FROM_PROFILE = {
    "vs": "vertex",
    "ps": "pixel",
    "cs": "compute",
    "gs": "geometry",
    "hs": "hull",
    "ds": "domain",
}
RESOURCE_REGISTER_KIND = {"b": "constantBuffers", "t": "srvs", "u": "uavs", "s": "samplers"}
ENGINE_ROOT_EXCLUSIONS = {
    "CopyShadowDataCS.hlsl",
    "DeferredCompositeCS.hlsl",
}
SUPPORTED_ENGINE_SHADER_TYPES = {
    "BloodSplatter", "DistantTree", "Effect", "Grass", "ImageSpace",
    "Lighting", "Particle", "Sky", "Utility", "Water",
}
# Utility participates in the engine shader cache, but its define builder does
# not iterate Feature::GetFeatureList().  HasShaderDefine(true) therefore does
# not currently add feature macros to Utility permutations.
FEATURE_DEFINE_ENGINE_TYPES = SUPPORTED_ENGINE_SHADER_TYPES - {"Utility"}


def norm(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def slash(value: str) -> str:
    return re.sub(r"/+", "/", value.replace("\\", "/"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def unique_sorted(values: Iterable[str]) -> list[str]:
    return sorted(set(values), key=lambda item: (item.lower(), item))


def balanced_block(text: str, opening: int, open_char: str, close_char: str) -> tuple[str, int] | None:
    depth = 0
    quote = None
    escaped = False
    for index in range(opening, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
        elif char == open_char:
            depth += 1
        elif char == close_char:
            depth -= 1
            if depth == 0:
                return text[opening + 1:index], index
    return None


def route_flags(path: str, owner: str, defines: Iterable[str], source_text: str = "") -> dict[str, Any]:
    combined = " ".join([path, owner, *defines, source_text[:2000]]).upper()
    vr = any(token in combined for token in ("/VR/", " VR", "STEREO", "HMD", "OPENVR"))
    overlay = any(token in combined for token in ("INSCENEOVERLAY", "MENUCOMPOSITE", "MENULAYER", "BACKGROUNDBLUR"))
    vendor = any(token in combined for token in ("FIDELITYFX", "STREAMLINE", "DLSS", "FSR", "XESS"))
    compatibility = "HORIZON" in combined
    return {
        "flat": None if vr else True,
        "vr": vr,
        "stereo": vr or "STEREO" in combined,
        "overlay": overlay,
        "vendor": vendor,
        "compatibility": compatibility,
    }


@dataclass(frozen=True)
class SourceContribution:
    source_path: str
    virtual_path: str
    owner: str
    owner_kind: str
    physical: Path


def inventory_contributions(root: Path) -> list[SourceContribution]:
    result: list[SourceContribution] = []
    package = root / "package" / "Shaders"
    for path in sorted(package.rglob("*")):
        if path.is_file() and path.suffix.lower() in SHADER_SUFFIXES:
            result.append(SourceContribution(
                slash(str(path.relative_to(root))),
                slash(str(path.relative_to(package))),
                "core",
                "core",
                path,
            ))
    features = root / "features"
    for feature in sorted((item for item in features.iterdir() if item.is_dir()), key=lambda item: item.name.lower()):
        shaders = feature / "Shaders"
        if not shaders.is_dir():
            continue
        for path in sorted(shaders.rglob("*")):
            if path.is_file() and path.suffix.lower() in SHADER_SUFFIXES:
                result.append(SourceContribution(
                    slash(str(path.relative_to(root))),
                    slash(str(path.relative_to(shaders))),
                    feature.name,
                    "feature",
                    path,
                ))
    return result


def feature_inventory(root: Path) -> list[dict[str, Any]]:
    packages: dict[str, dict[str, Any]] = {}
    for ini in sorted((root / "features").glob("*/Shaders/Features/*.ini")):
        package = ini.parents[2].name
        packages[norm(package)] = {
            "id": ini.stem,
            "displayName": package,
            "featureDirectory": slash(str(ini.parents[2].relative_to(root))),
            "iniPaths": [slash(str(ini.relative_to(root)))],
            "sourceCandidates": [],
            "stateCapability": {"installed": True, "resident": None, "active": None},
            "shaderDefine": None,
            "shaderDefineScope": {
                "declaredShaderTypes": [],
                "declarationEvidence": [],
                "actualDirectSources": [],
                "actualEntryPoints": [],
                "potentialEngineShaderTypes": [],
                "currentEngineCacheScope": [],
                "semanticEngineShaderTypes": [],
                "candidateRemovableEngineTypes": [],
                "potentialIndependentPrograms": [],
                "precision": "no-define",
            },
            "classificationStatus": "static-classified",
            "notes": [],
        }

    source_files = sorted((root / "src" / "Features").rglob("*.h")) + sorted((root / "src" / "Features").rglob("*.cpp"))
    blobs: dict[str, list[tuple[Path, str]]] = defaultdict(list)
    for path in source_files:
        text = path.read_text(encoding="utf-8", errors="replace")
        short = re.search(r'GetShortName[^\{]*\{\s*return\s+"([^"]+)"', text, re.S)
        if not short:
            short = re.search(r'kFeatureShortName\s*=\s*"([^"]+)"', text)
        candidates = [norm(path.stem)]
        if short:
            candidates.insert(0, norm(short.group(1)))
        for candidate in candidates:
            blobs[candidate].append((path, text))

    for package_key, feature in packages.items():
        candidates = blobs.get(norm(feature["id"]), []) + blobs.get(package_key, [])
        dedup: dict[str, tuple[Path, str]] = {str(path): (path, text) for path, text in candidates}
        candidates = list(dedup.values())
        feature["sourceCandidates"] = unique_sorted(slash(str(path.relative_to(root))) for path, _ in candidates)
        define_names: set[str] = set()
        evidence: list[str] = []
        shader_types: set[str] = set()
        broad = False
        for path, text in candidates:
            for match in re.finditer(r'GetShaderDefineName[^\{]*\{\s*return\s+"([^"]+)"', text, re.S):
                define_names.add(match.group(1))
            for match in re.finditer(r'HasShaderDefine\s*\([^)]*\)[^\{;]*\{', text):
                start = match.start()
                brace = text.find("{", match.start(), match.end() + 1)
                parsed = balanced_block(text, brace, "{", "}") if brace >= 0 else None
                snippet = parsed[0] if parsed else text[start:match.end()]
                evidence.append(f"{slash(str(path.relative_to(root)))}:{line_number(text, start)}")
                shader_types.update(re.findall(r'RE::BSShader::Type::(\w+)', snippet))
                if re.search(r'\breturn\s+true\s*;', snippet):
                    broad = True
        # A switch/boolean expression containing explicit Type:: values is a
        # scoped predicate even though one of its branches returns true.
        if shader_types:
            broad = False
        if len(define_names) == 1:
            feature["shaderDefine"] = next(iter(define_names))
        elif len(define_names) > 1:
            feature["notes"].append("Conflicting shader define declarations: " + ", ".join(sorted(define_names)))
        feature["shaderDefineScope"]["declaredShaderTypes"] = ["*"] if broad else sorted(shader_types)
        feature["shaderDefineScope"]["declarationEvidence"] = unique_sorted(evidence)
        if feature["shaderDefine"]:
            feature["shaderDefineScope"]["precision"] = "pending-use-analysis"
    return sorted(packages.values(), key=lambda item: item["id"].lower())


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
CONDITIONAL_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def parse_includes(text: str, virtual_path: str, virtual_sources: dict[str, SourceContribution]) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    resolved: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    external: list[dict[str, Any]] = []
    conditions: list[str] = []
    parent = PurePosixPath(virtual_path).parent
    lower_index = {key.lower(): key for key in virtual_sources}
    for number, line in enumerate(text.splitlines(), 1):
        directive = CONDITIONAL_RE.match(line)
        if directive:
            kind, expression = directive.group(1), directive.group(2).strip()
            if kind in {"if", "ifdef", "ifndef"}:
                conditions.append(f"{kind} {expression}".strip())
            elif kind == "elif" and conditions:
                conditions[-1] = f"elif {expression}".strip()
            elif kind == "else" and conditions:
                conditions[-1] = "else"
            elif kind == "endif" and conditions:
                conditions.pop()
        include = INCLUDE_RE.match(line)
        if not include:
            continue
        token = slash(include.group(1))
        candidates = [slash(str(parent / token)), token]
        if token.lower().startswith("/shaders/"):
            candidates.append(token[len("/Shaders/"):])
        target = None
        for candidate in candidates:
            target = lower_index.get(candidate.lower())
            if target:
                break
        record = {"token": token, "line": number, "condition": " && ".join(conditions) or None}
        if target:
            record["resolvedVirtualPath"] = target
            resolved.append(record)
        elif token.lower() == "math.h" or token.lower().startswith("/test/"):
            record["externalKind"] = "compiler-sdk" if token.lower() == "math.h" else "shader-test-framework"
            external.append(record)
        else:
            unresolved.append(record)
    return resolved, unresolved, external


def parse_resources(text: str) -> dict[str, list[dict[str, Any]]]:
    resources: dict[str, list[dict[str, Any]]] = {name: [] for name in RESOURCE_REGISTER_KIND.values()}
    pattern = re.compile(
        r'(?m)^\s*(?P<type>(?:RW)?(?:Texture\w*|StructuredBuffer|ByteAddressBuffer|Buffer|AppendStructuredBuffer|ConsumeStructuredBuffer|SamplerState|SamplerComparisonState|cbuffer)(?:\s*<[^;\n]+?>)?)'
        r'\s+(?P<name>[A-Za-z_]\w*)[^;\n\{]*?:\s*register\s*\(\s*(?P<register>[btus]\d+)\s*\)'
    )
    for match in pattern.finditer(text):
        register = match.group("register")
        resources[RESOURCE_REGISTER_KIND[register[0]]].append({
            "name": match.group("name"),
            "type": " ".join(match.group("type").split()),
            "register": register,
            "line": line_number(text, match.start()),
        })
    for values in resources.values():
        values.sort(key=lambda item: (int(item["register"][1:]), item["name"].lower()))
    return resources


def stage_hint(path: str, text: str) -> str:
    name = PurePosixPath(path).name.lower()
    for suffix, stage in ((".vs.hlsl", "vertex"), ("vs.hlsl", "vertex"), (".ps.hlsl", "pixel"), ("ps.hlsl", "pixel"), (".cs.hlsl", "compute"), ("cs.hlsl", "compute")):
        if name.endswith(suffix):
            return stage
    if "[numthreads" in text:
        return "compute"
    if path.lower().endswith(".hlsli"):
        return "include"
    return "runtime-selected"


def macro_references(text: str) -> list[str]:
    result: set[str] = set()
    for line in text.splitlines():
        if not re.match(r'^\s*#\s*(if|ifdef|ifndef|elif)\b', line):
            continue
        result.update(re.findall(r'\b[A-Z][A-Z0-9_]{2,}\b', line))
    return sorted(result)


def entry_points(text: str) -> list[str]:
    result = set(re.findall(r'(?m)^\s*(?:\[[^\]]+\]\s*)*(?:[A-Za-z_]\w*(?:\s*<[^>]+>)?\s+)+([A-Za-z_]\w*)\s*\([^;]*\)\s*(?::[^\{]+)?\s*\{', text))
    preferred = {name for name in result if name.lower() == "main" or "main" in name.lower()}
    if "[numthreads" in text:
        preferred.update(result)
    return sorted(preferred)


def semantics(text: str) -> list[str]:
    return unique_sorted(re.findall(r':\s*((?:SV_)?[A-Z][A-Z0-9_]*\d*)\b', text))


def split_top_level(arguments: str) -> list[str]:
    result: list[str] = []
    start = 0
    depth = 0
    quote = None
    escaped = False
    for index, char in enumerate(arguments):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
        elif char in "({[<":
            depth += 1
        elif char in ")}]>":
            depth = max(0, depth - 1)
        elif char == "," and depth == 0:
            result.append(arguments[start:index].strip())
            start = index + 1
    result.append(arguments[start:].strip())
    return result


def balanced_call(text: str, open_paren: int) -> tuple[str, int] | None:
    return balanced_block(text, open_paren, "(", ")")


def shader_virtual_from_literal(value: str) -> str | None:
    match = re.search(r'L?"([^"]+)"', value)
    if not match:
        return None
    path = slash(match.group(1))
    marker = "data/shaders/"
    index = path.lower().find(marker)
    return path[index + len(marker):] if index >= 0 else None


def infer_owner(cpp_path: str, features: list[dict[str, Any]]) -> str:
    pure = PurePosixPath(cpp_path)
    if len(pure.parts) >= 3 and pure.parts[0:2] == ("src", "Features"):
        candidate = norm(pure.parts[2] if pure.parts[2] != "VR" else "VR")
        for feature in features:
            if candidate in {norm(feature["id"]), norm(feature["displayName"])}:
                return feature["id"]
    if pure.name == "TruePBR.cpp":
        return "TruePBR"
    if "Menu" in pure.parts:
        return "menu-infrastructure"
    return "core"


def compile_sites(root: Path, virtual_sources: dict[str, SourceContribution], features: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    units: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    lower_virtual = {key.lower(): key for key in virtual_sources}
    source_files = sorted((root / "src").rglob("*.cpp"))
    tokens = ("Util::CompileShader", "D3DCompileFromFile")
    serial = 0
    for path in source_files:
        text = path.read_text(encoding="utf-8", errors="replace")
        relative = slash(str(path.relative_to(root)))
        owner = infer_owner(relative, features)
        for token in tokens:
            position = 0
            while True:
                found = text.find(token, position)
                if found < 0:
                    break
                open_paren = text.find("(", found + len(token))
                call = balanced_call(text, open_paren) if open_paren >= 0 else None
                if not call:
                    unresolved.append({"kind": "unparsed-compile-call", "source": relative, "line": line_number(text, found), "expression": token})
                    break
                body, end = call
                args = split_top_level(body)
                virtual = shader_virtual_from_literal(args[0]) if args else None
                profiles = re.findall(r'"((?:vs|ps|cs|gs|hs|ds)_\d_\d)"', body, re.I)
                profile = profiles[-1].lower() if profiles else None
                stage = STAGE_FROM_PROFILE.get(profile[:2], "unknown") if profile else "unknown"
                quoted = re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', body)
                entry = "main"
                if profile:
                    profile_pos = body.lower().rfind(f'"{profile}"')
                    later = re.findall(r'"([A-Za-z_]\w*)"', body[profile_pos + len(profile) + 2:])
                    if later:
                        entry = later[0]
                define_arg = args[1] if token == "Util::CompileShader" and len(args) > 1 else ""
                direct_defines = unique_sorted(re.findall(r'"([A-Z][A-Z0-9_]{1,})"', define_arg))
                define_mode = "none" if define_arg.strip() in {"", "{}", "nullptr"} else ("inline" if direct_defines else "dynamic")
                before = text[max(0, found - 240):found]
                target_match = re.search(r'([A-Za-z_]\w*)(?:\.attach\s*\(|\s*=\s*(?:static_cast|reinterpret_cast|\([^)]*\))?\s*)[^;\n]*$', before, re.S)
                target = target_match.group(1) if target_match else None
                if virtual:
                    canonical = lower_virtual.get(virtual.lower())
                    status = "resolved" if canonical else "missing-source"
                    serial += 1
                    units.append({
                        "id": f"compile-{serial:04d}",
                        "kind": "independent-program" if token == "Util::CompileShader" else "direct-file-program",
                        "sourceVirtualPath": canonical or virtual,
                        "sourcePath": virtual_sources[canonical].source_path if canonical else None,
                        "inlineSource": False,
                        "owner": owner,
                        "stage": stage,
                        "profile": profile,
                        "entryPoint": entry,
                        "compileDefines": direct_defines,
                        "defineMode": define_mode,
                        "compileSite": f"{relative}:{line_number(text, found)}",
                        "shaderObject": target,
                        "invocationSites": [],
                        "routes": route_flags(canonical or virtual, owner, direct_defines),
                        "classificationStatus": status,
                    })
                else:
                    if relative == "src/ShaderCache.cpp":
                        # This is the engine-family compiler represented by the
                        # synthetic engine-shader-cache-family units below.
                        position = end + 1
                        continue
                    if relative == "src/Utils/D3D.cpp":
                        # Generic implementation detail, not a requesting pass.
                        position = end + 1
                        continue
                    if relative == "src/ShaderTools/ShaderCompiler.cpp":
                        serial += 1
                        units.append({
                            "id": f"compile-{serial:04d}",
                            "kind": "runtime-tooling-dynamic-program",
                            "sourceVirtualPath": None,
                            "sourcePath": None,
                            "inlineSource": False,
                            "owner": "shader-tools",
                            "stage": stage,
                            "profile": profile,
                            "entryPoint": entry,
                            "compileDefines": [],
                            "defineMode": "caller-supplied-path",
                            "compileSite": f"{relative}:{line_number(text, found)}",
                            "shaderObject": None,
                            "invocationSites": [],
                            "routes": route_flags(relative, "shader-tools", []),
                            "classificationStatus": "resolved-dynamic-tooling-entrypoint",
                        })
                        position = end + 1
                        continue
                    # Recognise table-driven path construction (ScreenSpaceGI and Skylighting).
                    context = text[max(0, found - 9000):found]
                    prefix_matches = re.findall(r'std::filesystem::path\s*\(\s*"([^"]*Data\\\\Shaders\\\\[^"]+)"\s*\)', context)
                    filenames = unique_sorted(re.findall(r'"([^"\n]+\.hlsl)"', context[-7000:], re.I))
                    expanded = 0
                    if prefix_matches and filenames:
                        prefix = slash(prefix_matches[-1])
                        prefix = prefix[prefix.lower().find("data/shaders/") + len("data/shaders/"):]
                        for filename in filenames:
                            candidate = slash(str(PurePosixPath(prefix) / filename))
                            canonical = lower_virtual.get(candidate.lower())
                            if not canonical:
                                continue
                            serial += 1
                            expanded += 1
                            units.append({
                                "id": f"compile-{serial:04d}",
                                "kind": "table-driven-independent-program",
                                "sourceVirtualPath": canonical,
                                "sourcePath": virtual_sources[canonical].source_path,
                                "inlineSource": False,
                                "owner": owner,
                                "stage": stage,
                                "profile": profile,
                                "entryPoint": entry,
                                "compileDefines": [],
                                "defineMode": "table-driven",
                                "compileSite": f"{relative}:{line_number(text, found)}",
                                "shaderObject": target,
                                "invocationSites": [],
                                "routes": route_flags(canonical, owner, []),
                                "classificationStatus": "resolved-with-dynamic-variants",
                            })
                    if not expanded:
                        unresolved.append({
                            "kind": "dynamic-compile-path",
                            "source": relative,
                            "line": line_number(text, found),
                            "expression": args[0] if args else token,
                        })
                position = end + 1

        # Inline D3DCompile calls represent programs without tracked HLSL files.
        position = 0
        while True:
            found = text.find("D3DCompile(", position)
            if found < 0:
                break
            open_paren = text.find("(", found)
            call = balanced_call(text, open_paren)
            if not call:
                break
            body, end = call
            profiles = re.findall(r'"((?:vs|ps|cs|gs|hs|ds)_\d_\d)"', body, re.I)
            profile = profiles[-1].lower() if profiles else None
            stage = STAGE_FROM_PROFILE.get(profile[:2], "unknown") if profile else "unknown"
            entry_matches = re.findall(r'"([A-Za-z_]\w*)"\s*,\s*"(?:vs|ps|cs|gs|hs|ds)_\d_\d"', body, re.I)
            serial += 1
            units.append({
                "id": f"compile-{serial:04d}",
                "kind": "inline-program",
                "sourceVirtualPath": None,
                "sourcePath": None,
                "inlineSource": True,
                "owner": owner,
                "stage": stage,
                "profile": profile,
                "entryPoint": entry_matches[-1] if entry_matches else "main",
                "compileDefines": [],
                "defineMode": "inline-source",
                "compileSite": f"{relative}:{line_number(text, found)}",
                "shaderObject": None,
                "invocationSites": [],
                "routes": route_flags(relative, owner, [], body),
                "classificationStatus": "resolved-inline",
            })
            position = end + 1

    return units, unresolved


def annotate_compile_units(root: Path, units: list[dict[str, Any]], source_by_path: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    cpp_cache: dict[str, str] = {}
    passes: list[dict[str, Any]] = []
    for unit in units:
        virtual = unit["sourceVirtualPath"]
        dependencies: list[str] = []
        inputs: list[dict[str, Any]] = []
        outputs: list[dict[str, Any]] = []
        if virtual and virtual in source_by_path:
            source = source_by_path[virtual]
            dependencies = [virtual, *source["includeClosure"]]
            for dependency in dependencies:
                record = source_by_path[dependency]
                for kind in ("constantBuffers", "srvs", "samplers"):
                    for resource in record["resources"][kind]:
                        inputs.append({"kind": kind, "source": dependency, **resource})
                for resource in record["resources"]["uavs"]:
                    outputs.append({"kind": "uavs", "source": dependency, **resource})
                for semantic in record["semantics"]:
                    if semantic.startswith("SV_TARGET") or semantic in {"SV_DEPTH", "SV_DEPTHGREATER", "SV_DEPTHLESS"}:
                        outputs.append({"kind": "semantic", "source": dependency, "name": semantic})
        unit["dependencyClosure"] = unique_sorted(dependencies)
        unit["resourceInterface"] = {
            "inputs": sorted(inputs, key=lambda item: (item["kind"], item["source"].lower(), item["name"].lower())),
            "outputs": sorted(outputs, key=lambda item: (item["kind"], item["source"].lower(), item["name"].lower())),
            "runtimeBindingsComplete": False,
        }

        compile_source = unit["compileSite"].rsplit(":", 1)[0]
        target = unit["shaderObject"]
        if target and compile_source.startswith("src/"):
            if compile_source not in cpp_cache:
                path = root / compile_source
                cpp_cache[compile_source] = path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""
            cpp = cpp_cache[compile_source]
            invocations: list[str] = []
            for match in re.finditer(r'(?:VS|PS|CS|GS|HS|DS)SetShader\s*\([^;\n]*\b' + re.escape(target) + r'\b[^;\n]*\)', cpp):
                invocations.append(f"{compile_source}:{line_number(cpp, match.start())}")
            unit["invocationSites"] = unique_sorted(invocations)

        passes.append({
            "id": "pass-" + unit["id"].removeprefix("compile-"),
            "owner": unit["owner"],
            "pipelineKind": unit["kind"],
            "compileUnit": unit["id"],
            "invocationSites": unit["invocationSites"],
            "inputs": unit["resourceInterface"]["inputs"],
            "outputs": unit["resourceInterface"]["outputs"],
            "orderingBefore": [],
            "orderingAfter": [],
            "routes": unit["routes"],
            "classificationStatus": "static-classified" if unit["invocationSites"] or unit["kind"] == "engine-shader-cache-family" else "compile-classified-invocation-pending",
        })
    return passes


def engine_compile_units(sources: list[dict[str, Any]], start: int) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    serial = start
    for source in sources:
        virtual = source["virtualPath"]
        path = PurePosixPath(virtual)
        if source["role"] != "production-entry" or source["owner"] != "core" or len(path.parts) != 1:
            continue
        if path.name in ENGINE_ROOT_EXCLUSIONS:
            continue
        serial += 1
        result.append({
            "id": f"compile-{serial:04d}",
            "kind": "engine-shader-cache-family",
            "sourceVirtualPath": virtual,
            "sourcePath": source["sourcePath"],
            "inlineSource": False,
            "owner": "core",
            "stage": "runtime-selected",
            "profile": None,
            "entryPoint": "main",
            "compileDefines": [],
            "defineMode": "engine-descriptor-plus-resident-feature-set",
            "compileSite": "src/ShaderCache.cpp:D3DCompileFromFile",
            "shaderObject": None,
            "invocationSites": ["engine draw pipeline"],
            "routes": source["routes"],
            "classificationStatus": "static-classified-engine-family",
        })
    return result


def engine_shader_type(virtual_path: str) -> str:
    name = PurePosixPath(virtual_path).name
    stem = PurePosixPath(name).stem
    if name == "RunGrass.hlsl":
        return "Grass"
    if name.startswith("IS") or name == "UnderwaterFogToDepthOfField.hlsl":
        return "ImageSpace"
    return stem


def build_manifest(root: Path) -> dict[str, Any]:
    annotation_path = root / "docs/development/shader-analysis/shader-classification.annotations.json"
    annotations = json.loads(annotation_path.read_text(encoding="utf-8")) if annotation_path.is_file() else {
        "acceptedUnresolvedIncludes": [],
        "manualPipelines": [],
    }
    contributions = inventory_contributions(root)
    grouped: dict[str, list[SourceContribution]] = defaultdict(list)
    for contribution in contributions:
        grouped[contribution.virtual_path].append(contribution)
    # CMake copies package first, then features in sorted directory order.  The final
    # contribution is the deployed winner; every contribution remains recorded.
    virtual_sources = {virtual: values[-1] for virtual, values in grouped.items()}
    collisions = [
        {"virtualPath": virtual, "contributors": [item.source_path for item in values], "winner": values[-1].source_path}
        for virtual, values in sorted(grouped.items()) if len(values) > 1
    ]

    features = feature_inventory(root)
    unresolved_includes: list[dict[str, Any]] = []
    sources: list[dict[str, Any]] = []
    include_graph: dict[str, list[str]] = {}
    for virtual, contribution in sorted(virtual_sources.items(), key=lambda item: item[0].lower()):
        text = contribution.physical.read_text(encoding="utf-8", errors="replace")
        includes, missing, external = parse_includes(text, virtual, virtual_sources)
        include_graph[virtual] = [item["resolvedVirtualPath"] for item in includes]
        for item in missing:
            unresolved_includes.append({"sourceVirtualPath": virtual, **item})
        is_test = virtual.lower().startswith("tests/")
        role = "include" if contribution.physical.suffix.lower() == ".hlsli" else ("test-entry" if is_test else "production-entry")
        source = {
            "id": virtual,
            "virtualPath": virtual,
            "sourcePath": contribution.source_path,
            "contributors": [item.source_path for item in grouped[virtual]],
            "owner": contribution.owner,
            "ownerKind": contribution.owner_kind,
            "sha256": sha256(contribution.physical),
            "role": role,
            "stageHint": stage_hint(virtual, text),
            "entryPoints": entry_points(text),
            "directIncludes": includes,
            "externalIncludes": external,
            "includeClosure": [],
            "dependedOnByEntryPoints": [],
            "macroReferences": macro_references(text),
            "resources": parse_resources(text),
            "semantics": semantics(text),
            "routes": route_flags(virtual, contribution.owner, macro_references(text), text),
            "classificationStatus": "static-classified",
        }
        sources.append(source)

    source_by_path = {item["virtualPath"]: item for item in sources}
    closure_cache: dict[str, set[str]] = {}

    def closure(start: str, stack: tuple[str, ...] = ()) -> set[str]:
        if start in closure_cache:
            return set(closure_cache[start])
        if start in stack:
            return set()
        result: set[str] = set()
        for target in include_graph.get(start, []):
            result.add(target)
            result.update(closure(target, stack + (start,)))
        closure_cache[start] = result
        return set(result)

    production_entries = [item["virtualPath"] for item in sources if item["role"] == "production-entry"]
    reverse_entries: dict[str, set[str]] = defaultdict(set)
    for entry in production_entries:
        closed = closure(entry)
        source_by_path[entry]["includeClosure"] = unique_sorted(closed)
        for dependency in closed:
            reverse_entries[dependency].add(entry)
    for path, entries in reverse_entries.items():
        source_by_path[path]["dependedOnByEntryPoints"] = unique_sorted(entries)

    units, unresolved_compile = compile_sites(root, virtual_sources, features)
    units.extend(engine_compile_units(sources, len(units)))
    units.sort(key=lambda item: item["id"])
    passes = annotate_compile_units(root, units, source_by_path)

    # Some legacy include fragments use .hlsl rather than .hlsli.  A file that
    # is included by a production entry but has no compile site is not itself a
    # missing pass.
    compile_paths = {unit["sourceVirtualPath"] for unit in units if unit["sourceVirtualPath"]}
    for source in sources:
        if source["role"] == "production-entry" and source["dependedOnByEntryPoints"] and source["virtualPath"] not in compile_paths:
            source["role"] = "include-fragment"
    production_entries = [item["virtualPath"] for item in sources if item["role"] == "production-entry"]

    # Complete feature define scopes from actual preprocessing dependencies.
    for feature in features:
        define = feature["shaderDefine"]
        if not define:
            continue
        direct = [item["virtualPath"] for item in sources if define in item["macroReferences"]]
        affected_entries: set[str] = set()
        for path in direct:
            if source_by_path[path]["role"] == "production-entry":
                affected_entries.add(path)
            affected_entries.update(reverse_entries.get(path, set()))
        engine_paths = {
            unit["sourceVirtualPath"] for unit in units
            if unit["kind"] == "engine-shader-cache-family" and unit["sourceVirtualPath"]
        }
        independent_paths = {
            unit["sourceVirtualPath"] for unit in units
            if unit["kind"] != "engine-shader-cache-family" and unit["sourceVirtualPath"]
        }
        engine_types = {engine_shader_type(path) for path in affected_entries if path in engine_paths}
        independent_programs = affected_entries.intersection(independent_paths)
        scope = feature["shaderDefineScope"]
        scope["actualDirectSources"] = unique_sorted(direct)
        scope["actualEntryPoints"] = unique_sorted(affected_entries)
        scope["potentialEngineShaderTypes"] = unique_sorted(engine_types)
        scope["potentialIndependentPrograms"] = unique_sorted(independent_programs)
        declared = scope["declaredShaderTypes"]
        current_cache_scope = FEATURE_DEFINE_ENGINE_TYPES if declared == ["*"] else set(declared).intersection(FEATURE_DEFINE_ENGINE_TYPES)
        scope["currentEngineCacheScope"] = unique_sorted(current_cache_scope)
        scope["semanticEngineShaderTypes"] = unique_sorted(engine_types.intersection(current_cache_scope))
        scope["candidateRemovableEngineTypes"] = unique_sorted(current_cache_scope - engine_types)
        if declared == ["*"] and engine_types.intersection(FEATURE_DEFINE_ENGINE_TYPES) != FEATURE_DEFINE_ENGINE_TYPES:
            scope["precision"] = "declared-global-potential-subset"
        elif direct:
            scope["precision"] = "statically-mapped"
        else:
            scope["precision"] = "declared-but-no-static-use"

    compatibility_variants = [{
        "id": "horizon-fix-water-cache",
        "selection": "HorizonFix.dll active versus inactive",
        "structuralDefine": "HORIZON_FIX",
        "affectedFeature": "HorizonFix",
        "affectedEntryPoints": ["Water.hlsl"],
        "cacheDirectories": ["ShaderCache", "ShaderCache-HorizonFix"],
        "evidence": ["tools/build-shader-cache.py", "src/Features/HorizonFix.h", "package/Shaders/Water.hlsl"],
        "classificationStatus": "static-classified",
    }]

    unresolved_entry_sources = unique_sorted(
        item["virtualPath"] for item in sources
        if item["role"] == "production-entry" and item["virtualPath"] not in compile_paths
    )
    family_summary: dict[str, dict[str, Any]] = {}
    for entry in production_entries:
        source = source_by_path[entry]
        family = PurePosixPath(entry).stem
        family_summary[entry] = {
            "id": family,
            "entryPoint": entry,
            "owner": source["owner"],
            "stageHint": source["stageHint"],
            "includeCount": len(source["includeClosure"]),
            "featureOwnersInClosure": unique_sorted(source_by_path[path]["owner"] for path in source["includeClosure"] if source_by_path[path]["ownerKind"] == "feature"),
            "macroReferences": unique_sorted(source["macroReferences"] + [macro for path in source["includeClosure"] for macro in source_by_path[path]["macroReferences"]]),
            "routes": source["routes"],
        }

    accepted_lookup = {
        (item["sourceVirtualPath"].lower(), item["token"].lower()): item
        for item in annotations.get("acceptedUnresolvedIncludes", [])
    }
    for item in unresolved_includes:
        accepted = accepted_lookup.get((item["sourceVirtualPath"].lower(), item["token"].lower()))
        if accepted:
            item["acceptedClassification"] = accepted["classification"]
            item["acceptedReason"] = accepted["reason"]
    unaccepted_includes = [item for item in unresolved_includes if "acceptedClassification" not in item]

    invalidation_by_source = []
    for source in sources:
        affected = set(source["dependedOnByEntryPoints"])
        if source["role"] == "production-entry":
            affected.add(source["virtualPath"])
        invalidation_by_source.append({
            "sourceVirtualPath": source["virtualPath"],
            "affectedEntryPoints": unique_sorted(affected),
            "compileUnitIds": unique_sorted(
                unit["id"] for unit in units
                if unit["sourceVirtualPath"] in affected
            ),
        })
    invalidation_by_define = [
        {
            "feature": feature["id"],
            "define": feature["shaderDefine"],
            "declaredShaderTypes": feature["shaderDefineScope"]["declaredShaderTypes"],
            "potentialEngineShaderTypes": feature["shaderDefineScope"]["potentialEngineShaderTypes"],
            "currentEngineCacheScope": feature["shaderDefineScope"]["currentEngineCacheScope"],
            "semanticEngineShaderTypes": feature["shaderDefineScope"]["semanticEngineShaderTypes"],
            "candidateRemovableEngineTypes": feature["shaderDefineScope"]["candidateRemovableEngineTypes"],
            "potentialIndependentPrograms": feature["shaderDefineScope"]["potentialIndependentPrograms"],
            "affectedEntryPoints": feature["shaderDefineScope"]["actualEntryPoints"],
        }
        for feature in features if feature["shaderDefine"]
    ]

    return {
        "schemaVersion": SCHEMA_VERSION,
        "status": "static-classified",
        "generatedBy": GENERATOR,
        "inventory": {
            "sourceContributionCount": len(contributions),
            "virtualSourceCount": len(sources),
            "productionEntryCount": sum(item["role"] == "production-entry" for item in sources),
            "includeSourceCount": sum(item["role"] in {"include", "include-fragment"} for item in sources),
            "testEntryCount": sum(item["role"] == "test-entry" for item in sources),
            "featureCount": len(features),
            "compileUnitCount": len(units),
            "passCount": len(passes),
            "includeEdgeCount": sum(len(value) for value in include_graph.values()),
            "virtualCollisionCount": len(collisions),
            "unresolvedIncludeCount": len(unresolved_includes),
            "unacceptedUnresolvedIncludeCount": len(unaccepted_includes),
            "unresolvedCompileSiteCount": len(unresolved_compile),
            "unclassifiedProductionEntryCount": len(unresolved_entry_sources),
        },
        "features": features,
        "sources": sources,
        "families": list(family_summary.values()),
        "compileUnits": units,
        "passes": passes,
        "invalidationIndex": {
            "bySource": invalidation_by_source,
            "byFeatureDefine": invalidation_by_define,
            "structuralVariants": compatibility_variants,
        },
        "compatibilityVariants": compatibility_variants,
        "manualPipelines": annotations.get("manualPipelines", []),
        "unresolved": {
            "virtualPathCollisions": collisions,
            "includes": unresolved_includes,
            "compileSites": unresolved_compile,
            "productionEntriesWithoutCompileEvidence": unresolved_entry_sources,
            "runtimeEvidenceStillRequired": [
                "dynamic and engine-owned resource bindings, formats, dimensions, and lifetimes",
                "actual pass ordering and conditional scheduling for every independent program",
                "preprocessed conditional include closure for each structural permutation",
                "runtime shader-object identity and compiled bytecode hashes",
                "safe independent-disable boundaries and active/resident state transitions",
            ],
        },
    }


def validate_manifest(manifest: dict[str, Any]) -> list[str]:
    """Validate graph closure and the invariants needed by incremental consumers."""
    errors: list[str] = []
    inventory = manifest["inventory"]
    sources = manifest["sources"]
    units = manifest["compileUnits"]
    passes = manifest["passes"]
    source_paths = {item["virtualPath"] for item in sources}
    unit_ids = {item["id"] for item in units}
    pass_ids = {item["id"] for item in passes}
    production_entries = {item["virtualPath"] for item in sources if item["role"] == "production-entry"}

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    require(manifest["schemaVersion"] == SCHEMA_VERSION, "unexpected schema version")
    require(manifest["status"] == "static-classified", "manifest status is not static-classified")
    require(len(source_paths) == len(sources), "source virtual paths are not unique")
    require(len(unit_ids) == len(units), "compile unit IDs are not unique")
    require(len(pass_ids) == len(passes), "pass IDs are not unique")
    require(inventory["virtualSourceCount"] == len(sources), "virtual source inventory count is inconsistent")
    require(inventory["compileUnitCount"] == len(units), "compile unit inventory count is inconsistent")
    require(inventory["passCount"] == len(passes), "pass inventory count is inconsistent")
    require(inventory["productionEntryCount"] == len(production_entries), "production entry inventory count is inconsistent")
    require(inventory["unacceptedUnresolvedIncludeCount"] == 0, "unaccepted unresolved include edges remain")
    require(inventory["unresolvedCompileSiteCount"] == 0, "unresolved compile sites remain")
    require(inventory["unclassifiedProductionEntryCount"] == 0, "production entries without compile evidence remain")

    hash_pattern = re.compile(r"^[A-F0-9]{64}$")
    for source in sources:
        path = source["virtualPath"]
        require(bool(hash_pattern.fullmatch(source["sha256"])), f"invalid SHA-256 for {path}")
        direct_targets = {item["resolvedVirtualPath"] for item in source["directIncludes"]}
        require(direct_targets.issubset(source_paths), f"resolved include target is absent for {path}")
        require(set(source["includeClosure"]).issubset(source_paths), f"include closure target is absent for {path}")

    compiled_entries = {item["sourceVirtualPath"] for item in units if item["sourceVirtualPath"]}
    require(production_entries.issubset(compiled_entries), "not every production entry has a compile unit")
    for unit in units:
        path = unit["sourceVirtualPath"]
        require(path is None or path in source_paths, f"compile unit {unit['id']} references an absent source")
        require(set(unit["dependencyClosure"]).issubset(source_paths), f"compile unit {unit['id']} has an absent dependency")
    for render_pass in passes:
        require(render_pass["compileUnit"] in unit_ids, f"pass {render_pass['id']} references an absent compile unit")

    source_index = manifest["invalidationIndex"]["bySource"]
    require(len(source_index) == len(sources), "source invalidation index is incomplete")
    for record in source_index:
        require(record["sourceVirtualPath"] in source_paths, "source invalidation record references an absent source")
        require(set(record["affectedEntryPoints"]).issubset(production_entries), f"invalid affected entry for {record['sourceVirtualPath']}")
        require(set(record["compileUnitIds"]).issubset(unit_ids), f"invalid compile unit for {record['sourceVirtualPath']}")
    return errors


def markdown_report(manifest: dict[str, Any]) -> str:
    inv = manifest["inventory"]
    scope_candidates = [feature for feature in manifest["features"] if feature["shaderDefineScope"]["candidateRemovableEngineTypes"]]
    compile_kinds = Counter(unit["kind"] for unit in manifest["compileUnits"])
    routes_by_source: dict[str, set[str]] = defaultdict(set)
    for unit in manifest["compileUnits"]:
        if unit["sourceVirtualPath"]:
            routes_by_source[unit["sourceVirtualPath"]].add(unit["kind"])
    dual_route_sources = {
        path: kinds for path, kinds in routes_by_source.items()
        if "engine-shader-cache-family" in kinds and len(kinds) > 1
    }
    lines = [
        "# Generated shader dependency classification",
        "",
        "> Generated by `tools/shader_dependency_manifest.py`. Do not edit by hand.",
        "",
        "## Coverage",
        "",
        "| Measure | Count |",
        "|---|---:|",
    ]
    for label, key in (
        ("Tracked source contributions", "sourceContributionCount"),
        ("Deployed virtual sources", "virtualSourceCount"),
        ("Production entry candidates", "productionEntryCount"),
        ("Include sources", "includeSourceCount"),
        ("Test entry sources", "testEntryCount"),
        ("Features", "featureCount"),
        ("Compile units", "compileUnitCount"),
        ("Resolved include edges", "includeEdgeCount"),
        ("Unresolved include edges", "unresolvedIncludeCount"),
        ("Unaccepted unresolved include edges", "unacceptedUnresolvedIncludeCount"),
        ("Unresolved compile sites", "unresolvedCompileSiteCount"),
        ("Production entries without compile evidence", "unclassifiedProductionEntryCount"),
    ):
        lines.append(f"| {label} | {inv[key]} |")
    lines.extend(["", "## Compile routes", "", "| Route | Compile units |", "|---|---:|"])
    for kind, count in sorted(compile_kinds.items()):
        lines.append(f"| {kind} | {count} |")
    lines.extend(["", "Sources may legitimately have more than one compile route. The following are both engine families and independent programs:", ""])
    if dual_route_sources:
        for path, kinds in sorted(dual_route_sources.items()):
            lines.append(f"- `{path}`: {', '.join(sorted(kinds))}")
    else:
        lines.append("- None.")
    lines.extend([
        "",
        "## Feature define impact",
        "",
        "Current cache scope is where `HasShaderDefine` supplies the macro today. Source-proven semantic scope is its intersection with static preprocessing reach; the difference is a candidate for narrower cache identity and compilation. Independent-program reach remains potential until dynamically assembled compile defines are exported or traced.",
        "",
        "| Feature | Define | Current engine cache scope | Source-proven semantic scope | Potential independent programs | Precision |",
        "|---|---|---|---|---|---|",
    ])
    for feature in manifest["features"]:
        define = feature["shaderDefine"]
        if not define:
            continue
        scope = feature["shaderDefineScope"]
        lines.append(
            f"| {feature['id']} | `{define}` | {', '.join(scope['currentEngineCacheScope']) or 'none'} | "
            f"{', '.join(scope['semanticEngineShaderTypes']) or 'none found'} | "
            f"{', '.join(scope['potentialIndependentPrograms']) or 'none found'} | {scope['precision']} |"
        )
    lines.extend(["", "## Engine cache-scope narrowing candidates", ""])
    lines.append("These removals are justified by static source reach, but must still pass preprocessed-source and bytecode-equivalence validation before changing `HasShaderDefine`.")
    lines.append("")
    if scope_candidates:
        for feature in scope_candidates:
            scope = feature["shaderDefineScope"]
            lines.append(
                f"- **{feature['id']}** (`{feature['shaderDefine']}`): remove from current engine cache scope candidate(s) "
                f"{', '.join(scope['candidateRemovableEngineTypes'])}. Source-proven semantic scope: "
                f"{', '.join(scope['semanticEngineShaderTypes']) or 'none'}"
                f"{'; potential independent programs: ' + ', '.join(scope['potentialIndependentPrograms']) if scope['potentialIndependentPrograms'] else ''}."
            )
    else:
        lines.append("- None detected by the current static rules.")
    lines.extend(["", "## Compatibility variants", ""])
    for variant in manifest["compatibilityVariants"]:
        lines.append(f"- **{variant['id']}**: `{variant['structuralDefine']}` changes {', '.join(variant['affectedEntryPoints'])}; the FOMOD selects between {', '.join(variant['cacheDirectories'])}.")
    lines.extend(["", "## Manually closed overlay pipelines", ""])
    for pipeline in manifest["manualPipelines"]:
        lines.append(f"- **{pipeline['id']}** ({pipeline['kind']}): {pipeline['ordering']}")
    lines.extend(["", "## Accepted dormant or stale edges", ""])
    accepted = [item for item in manifest["unresolved"]["includes"] if "acceptedClassification" in item]
    if accepted:
        for item in accepted:
            lines.append(
                f"- `{item['sourceVirtualPath']}` -> `{item['token']}` under `{item['condition'] or 'unconditional'}`: "
                f"**{item['acceptedClassification']}** — {item['acceptedReason']}"
            )
    else:
        lines.append("- None.")
    lines.extend(["", "## Unresolved static evidence", ""])
    for item in manifest["unresolved"]["runtimeEvidenceStillRequired"]:
        lines.append(f"- {item}")
    lines.extend(["", "The JSON manifest contains exact source paths, hashes, include closures, resource register declarations, macro impact, compile sites, and unresolved records.", ""])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    root = args.repository_root.resolve()
    output = args.output or root / "docs/development/shader-analysis/shader-manifest.generated.json"
    report = args.report or root / "docs/development/shader-analysis/shader-dependency-report.generated.md"
    manifest = build_manifest(root)
    validation_errors = validate_manifest(manifest)
    if validation_errors:
        for error in validation_errors:
            print(f"shader dependency manifest validation failed: {error}", file=sys.stderr)
        return 2
    json_text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    report_text = markdown_report(manifest)
    if args.check:
        failures = []
        if not output.is_file() or output.read_text(encoding="utf-8") != json_text:
            failures.append(str(output))
        if not report.is_file() or report.read_text(encoding="utf-8") != report_text:
            failures.append(str(report))
        if failures:
            print("stale generated shader dependency artifacts: " + ", ".join(failures), file=sys.stderr)
            return 1
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json_text, encoding="utf-8", newline="\n")
        report.write_text(report_text, encoding="utf-8", newline="\n")
    print(json.dumps({"ok": True, "validationErrorCount": 0, **manifest["inventory"], "output": str(output), "report": str(report)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
