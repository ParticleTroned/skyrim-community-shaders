"""Execute production grass collision persistence and multiplier validation."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

def block(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


ROOT = Path(__file__).resolve().parents[1]


class GrassCollisionSettingsTests(unittest.TestCase):
    def test_legacy_defaults_round_trip_and_invalid_values(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        header = (ROOT / "src/Features/GrassCollision.h").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/GrassCollision.cpp").read_text(encoding="utf-8")
        constants = "\n".join(re.findall(r"static constexpr float k\w+CollisionStrength = [^;]+;", header))
        serialization = source[source.index("NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT("):source.index("void GrassCollision::DrawSettings")]
        methods = "\n".join(block(source, signature) for signature in (
            "float GrassCollision::GetCollisionStrength(",
            "bool GrassCollision::SetCollisionStrength(",
            "void GrassCollision::LoadSettings(",
            "void GrassCollision::SaveSettings(",
            "void GrassCollision::RestoreDefaultSettings(",
            "json GrassCollision::CapturePerformanceSettingsState(",
        ))
        driver = r'''
#include "Utils/Finite.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
using json = nlohmann::json;
struct GrassCollision {
CONSTANTS
SETTINGS;
Settings settings;
float GetCollisionStrength() const;
bool SetCollisionStrength(float);
void LoadSettings(json&);
void SaveSettings(json&);
void RestoreDefaultSettings();
json CapturePerformanceSettingsState() const;
};
SERIALIZATION
METHODS
void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) {
        std::cerr << "Grass collision settings failed at driver line " << where.line() << '\n';
        std::exit(1);
    }
}
int main() {
    GrassCollision feature;
    json legacy = { { "EnableGrassCollision", false }, { "TrackRagdolls", false } };
    feature.LoadSettings(legacy);
    Check(feature.GetCollisionStrength() == 1 && !feature.settings.EnableGrassCollision && !feature.settings.TrackRagdolls);
    for (float strength : { 0.0f, .5f, 1.0f, 2.0f, 3.0f, 5.0f }) {
        Check(feature.SetCollisionStrength(strength));
        json saved;
        feature.SaveSettings(saved);
        Check(saved.at("CollisionStrength") == strength && !saved.at("EnableGrassCollision").get<bool>());
        feature.RestoreDefaultSettings();
        Check(feature.GetCollisionStrength() == 1);
        feature.LoadSettings(saved);
        Check(feature.GetCollisionStrength() == strength && !feature.settings.EnableGrassCollision && !feature.settings.TrackRagdolls);
        Check(feature.CapturePerformanceSettingsState().at("CollisionStrength") == strength);
    }
    const float invalid[] = { -.01f, 5.01f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    for (float strength : invalid) {
        Check(feature.SetCollisionStrength(3));
        Check(!feature.SetCollisionStrength(strength) && feature.GetCollisionStrength() == 3);
        feature.settings.CollisionStrength = strength;
        const float bounded = Util::ClampFinite(strength, 0.0f, 5.0f, 1.0f);
        Check(feature.GetCollisionStrength() == bounded);
        json saved;
        feature.SaveSettings(saved);
        Check(saved.at("CollisionStrength") == bounded && !feature.settings.EnableGrassCollision);
        feature.LoadSettings(saved);
        Check(feature.GetCollisionStrength() == bounded);
    }
    for (double strength : { -1.0, 6.0, 1e300, -1e300 }) {
        auto input = legacy;
        input["CollisionStrength"] = strength;
        feature.LoadSettings(input);
        Check(feature.GetCollisionStrength() == (strength == -1 ? 0 : strength == 6 ? 5 : 1));
        Check(input.at("CollisionStrength") == strength);
    }
    for (const json& invalidType : { json(nullptr), json("strong"), json::array(), json::object() }) {
        Check(feature.SetCollisionStrength(2));
        auto input = legacy;
        input["CollisionStrength"] = invalidType;
        bool rejected = false;
        try { feature.LoadSettings(input); }
        catch (const json::exception&) { rejected = true; }
        Check(rejected && feature.GetCollisionStrength() == 2);
    }
    std::cout << "Grass collision defaults, bounds, invalid-input preservation and save/load passed\n";
}
'''
        for marker, value in (("CONSTANTS", constants), ("SETTINGS", block(header, "struct Settings")), ("SERIALIZATION", serialization), ("METHODS", methods)):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-grass-collision-settings-") as temporary:
            directory = Path(temporary)
            cpp = directory / "settings.cpp"
            exe = directory / "settings.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{ROOT / 'src'}", f"/I{os.environ['CSX_JSON_INCLUDE']}", str(cpp), f"/Fe:{exe}"]
            if Path(compiler).name not in ("cl", "cl.exe"):
                command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'src'}", f"-I{os.environ['CSX_JSON_INCLUDE']}", str(cpp), "-o", str(exe)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
