"""Exercise production weather evaluation and scene queries without a game process."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


def extract(root):
    weather = (root / "src/WeatherManager.cpp").read_text(encoding="utf-8-sig")
    weather = weather[weather.index("namespace\n{"):]
    disk = block(weather, "void WeatherManager::LoadPerWeatherSettingsFromDisk()")
    weather = weather.replace(disk, """
void WeatherManager::LoadPerWeatherSettingsFromDisk() {
    if (!diskReadFails) PublishLoadedSettings(diskSettings);
}
""")
    # Count entry into real evaluation/query bodies; failure injection checks retry admission.
    for signature, probe in [
        ("void WeatherManager::EvaluateCurrentWeatherState(bool force)",
         "++evaluations; if (failEvaluation) throw EvaluationFailure{};"),
        ("WeatherManager::CurrentWeathers WeatherManager::GetCurrentWeathers()", "++queries;"),
    ]:
        body = block(weather, signature)
        weather = weather.replace(body, body.replace("{", "{\n" + probe, 1))
    game = (root / "src/Utils/Game.cpp").read_text(encoding="utf-8-sig")
    state = (root / "src/State.cpp").read_text(encoding="utf-8-sig")
    return (weather + "\nnamespace Util {\n" + block(game, "bool IsInterior()") + "\n}\n" +
            block(state, "bool State::HasDirectionalShadows() const"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json-include", required=True)
    args = parser.parse_args()
    compiler = os.environ.get("CXX") or shutil.which("cl")
    if not compiler:
        raise RuntimeError("MSVC environment required; use the repository tool environment")
    includes = [f"/I{Path(entry).resolve()}" for entry in args.json_include.split(";") if entry]
    with tempfile.TemporaryDirectory(prefix="csx-weather-draw-") as name:
        temp = Path(name)
        (temp / "weather_manager_under_test.h").write_text(extract(ROOT), encoding="utf-8")
        exe = temp / "weather_draw_updates.exe"
        subprocess.run([compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                        "/DNOMINMAX", f"/I{ROOT / 'src'}", f"/I{temp}", *includes,
                        str(ROOT / "tests/weather_draw_updates_test.cpp"), f"/Fe:{exe}"],
                       cwd=temp, check=True, timeout=90)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
