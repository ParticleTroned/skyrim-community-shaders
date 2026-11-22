"""Exercise production category matching and scoped Visuals weather saves."""

import argparse
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json-include", required=True)
    args = parser.parse_args()
    compiler = shutil.which("cl")
    if not compiler:
        raise RuntimeError("Use tools/run-msvc-command.ps1 to initialize MSVC")
    store = (ROOT / "src/WeatherFeatureOverrides.cpp").read_text(encoding="utf-8-sig")
    menu = (ROOT / "src/Menu/VisualsWeather.cpp").read_text(encoding="utf-8-sig")
    editor = (ROOT / "src/CSEditor/EditorWindow.cpp").read_text(encoding="utf-8-sig")
    production = block(editor, "void EditorWindow::RecordWeatherFeatureSave(")
    production += store[store.index("namespace WeatherFeatureOverrides"): ]
    production += "\nnamespace MenuUI {\nusing json = nlohmann::json;\n"
    production += block(menu, "json ApplyVisualsWeather(") + "\n}\n"
    bridge = (ROOT / "src/MenuDevBenchBridge.cpp").read_text(encoding="utf-8-sig")
    production += "\njson Dispatch(const json& a_args) {\nconst auto action = a_args.value(\"action\", std::string{});\n"
    production += block(bridge, 'if (action == "apply_visuals_weather")')
    production += '\nreturn {{"ok", false}};\n}\n'
    descriptor = re.search(r'R"\((\{"description":"[^\r\n]*\})\)"', bridge).group()
    schema_start = bridge.index('document["inputSchema"]["properties"]["action"]["enum"].push_back("apply_visuals_weather")')
    schema_end = bridge.index('for (const auto* action : { "adaptive_balance_locations"', schema_start)
    production += "\njson WeatherSchema() { auto document = json::parse(" + descriptor + ");\n"
    production += bridge[schema_start:schema_end] + "\nreturn document;\n}\n"
    with tempfile.TemporaryDirectory(prefix="csx-visuals-weather-") as name:
        temp = Path(name)
        # Engine behavior is supplied by the fixture; only category flags are needed.
        (temp / "RE/T").mkdir(parents=True)
        (temp / "RE/T/TESWeather.h").write_text("#pragma once\n", encoding="utf-8")
        (temp / "visuals_weather_under_test.h").write_text(production, encoding="utf-8")
        exe = temp / "visuals_weather_test.exe"
        includes = [f"/I{Path(path).resolve()}" for path in args.json_include.split(";") if path]
        subprocess.run([compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                        f"/I{ROOT / 'src'}", f"/I{temp}", *includes,
                        str(ROOT / "tests/visuals_weather_test.cpp"), f"/Fe:{exe}"],
                       cwd=temp, check=True, timeout=90)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
