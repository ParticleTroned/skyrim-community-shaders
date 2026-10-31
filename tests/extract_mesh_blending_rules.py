"""Extract the production rule parser and save transaction for host tests."""

import argparse
import re
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root: Path, output: Path) -> None:
    header = (root / "src/Features/MeshBlending.h").read_text(encoding="utf-8-sig")
    source = (root / "src/Features/MeshBlending.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    members = (
        between(header, "\tenum class DetectionMode", "\t/** Stable model/node selector")
        + between(header, "\tstruct OverrideRule", "\t/** User-facing settings")
        + between(header, "\tstruct Settings", "\tSettings settings;")
        + between(header, "\tstruct DiscoverySaveResult", "\tstatic constexpr std::size_t kCacheSetCount")
        + "\n".join(re.findall(
            r"^\tstatic constexpr std::size_t kMaximum(?:Rules|RuleLength) = .+;$",
            header, re.MULTILINE))
    )
    (output / "mesh_blending_rule_members.h").write_text(members, encoding="utf-8")
    constants = "\n".join(re.findall(
        r"^\tconstexpr std::(?:uint32_t|uintmax_t) k(?:RuleFileSchemaVersion|OldestSupportedRuleFileSchemaVersion|MaximumRuleFileBytes) = .+;$",
        source, re.MULTILINE))
    serializer = between(
        source,
        "NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(\n\tMeshBlending::OverrideRule,",
        "namespace\n{",
    )
    helpers = [
        "\tbool IsFinite(",
        "\tbool IsValidUtf8(",
        "\tbool IsSafeRuleString(",
    ]
    methods = [
        "MeshBlending::Settings MeshBlending::Sanitize(",
        "MeshBlending::PerFrame MeshBlending::GetCommonBufferData(",
        "bool MeshBlending::IsOverlayPolicyEnabled(",
        "bool MeshBlending::IsRuntimeEnabled(",
        "MeshBlending::CompiledRules MeshBlending::CompileRules(",
        "bool MeshBlending::IsDiscoveryCaptureSaturated(",
        "void MeshBlending::SetDiscoveryCaptureEnabled(",
        "bool MeshBlending::LoadRuleFile(",
        "bool MeshBlending::ReadRuleFileState(",
        "void MeshBlending::ApplyRuleFileState(",
        "bool MeshBlending::WriteRuleFile(",
        "MeshBlending::DiscoverySaveResult MeshBlending::SaveDiscoveredRules(",
        "bool MeshBlending::ClearSavedRules(",
        "void MeshBlending::ClearDiscoveredRules(",
    ]
    bodies = constants + "\n" + serializer + "\n".join(function(source, name) for name in helpers + methods)
    (output / "mesh_blending_rules_under_test.h").write_text(bodies, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    extract(arguments.source_dir, arguments.output_dir)
