"""Compile persistent navigation metadata and its production API without ImGui."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import function


def extract(root, output):
    source = (root / "src/Menu/SettingsPage.cpp").read_text(encoding="utf-8-sig")
    header = (root / "src/Menu/SettingsPage.h").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    parts = [function(header, "struct Section") + ";"]
    if "struct NavigationSection" in source:
        parts.append(function(source, "struct NavigationSection") + ";")
    parts.append(function(source, "struct Navigation\n") + ";")
    parts.extend([
        "std::unordered_map<std::string, Navigation> navigation;",
        "std::recursive_mutex navigationMutex;",
    ])
    constructor = function(source, "SettingsPage::SettingsPage(")
    publication = next(line for line in constructor.splitlines()
                       if "state.sections = sections;" in line or "state.SetSections(sections);" in line)
    parts.append("void Publish(const char* page, const std::vector<Section>& sections) {\n"
                 "auto& state = navigation[page];\n" + publication + "\n}")
    for marker in ("bool SettingsPage::Select(", "bool SettingsPage::Navigate(",
                   "nlohmann::json SettingsPage::Describe("):
        parts.append(function(source, marker))
    (output / "settings_navigation_under_test.h").write_text(
        "namespace MenuUI {\n" + "\n".join(parts) + "\n}\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
