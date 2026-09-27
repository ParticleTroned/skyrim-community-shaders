"""Extract weather refresh, lock routing and clock edits for controller tests."""

import argparse
from pathlib import Path

from extract_adaptive_balance_toggle import between, function


def extract(root, output):
    game = (root / "src/Utils/Game.cpp").read_text(encoding="utf-8-sig")
    editor = (root / "src/CSEditor/EditorWindow.cpp").read_text(encoding="utf-8-sig")
    output.mkdir(parents=True, exist_ok=True)
    hooks = between(editor, "\tstd::atomic<RE::TESWeather*> g_lockedWeather", "\tvoid ReapplyWeatherLock(")
    (output / "forced_weather_hooks.h").write_text(hooks, encoding="utf-8")
    bodies = "namespace {\n" + function(game, "\tvoid ResetModelHandle(") + "}\n"
    bodies += "namespace Util {\n" + function(game, "\tvoid RefreshForcedWeatherSky(") + "}\n"
    bodies += function(editor, "void EditorWindow::ForceWeather(")
    bodies += "namespace {\n"
    for signature in ["\tvoid ReapplyWeatherLock(", "\tvoid SetWeatherHook::thunk(", "\tvoid ForceWeatherHook::thunk("]:
        bodies += function(editor, signature)
    bodies += "}\n"
    bodies += function(editor, "bool EditorWindow::DrawGameHourSlider(")
    (output / "forced_weather_under_test.h").write_text(bodies, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source_dir, args.output_dir)
