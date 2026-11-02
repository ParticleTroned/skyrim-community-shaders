"""Extract the production preset UI, instrumenting only real ImGui item observations."""

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = (args.source_dir / "src/Menu/AdaptiveBalancePresets.cpp").read_text(encoding="utf-8")
    source = source[source.index("namespace MenuUI::AdaptiveBalancePresets"):]
    source = source.replace("ImGui::Button(", "TrackedPresetButton(")
    source = source.replace("ImGui::InputTextWithHint(", "TrackedPresetNameInput(")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "adaptive_preset_folder_ui_under_test.h").write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
