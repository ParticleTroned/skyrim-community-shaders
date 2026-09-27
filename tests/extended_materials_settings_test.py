"""Execute the production parallax settings serialization and bounds checks."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class ExtendedMaterialsSettingsTests(unittest.TestCase):
    def test_saved_strength_defaults_bounds_and_round_trip(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        include = os.environ["CSX_JSON_INCLUDE"]
        header = (ROOT / "src/Features/ExtendedMaterials.h").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/ExtendedMaterials.cpp").read_text(encoding="utf-8")
        constants = "\n".join(re.findall(r"static constexpr float k\w+ParallaxStrength = [^;]+;", header))
        serialization = source[source.index("NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT("):source.index("void ExtendedMaterials::SanitizeSettings")]
        methods = "\n".join(block(source, signature) for signature in (
            "void ExtendedMaterials::SanitizeSettings(",
            "void ExtendedMaterials::LoadSettings(",
            "void ExtendedMaterials::SaveSettings(",
            "void ExtendedMaterials::RestoreDefaultSettings(",
            "json ExtendedMaterials::CapturePerformanceCostMeasurementState(",
            "void ExtendedMaterials::RestorePerformanceCostMeasurementState(",
        ))
        driver = r'''
#include "Utils/Finite.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
using json = nlohmann::json;
using uint = unsigned int;
struct ExtendedMaterials {
CONSTANTS
SETTINGS;
Settings settings;
static void SanitizeSettings(Settings&);
void LoadSettings(json&);
void SaveSettings(json&);
void RestoreDefaultSettings();
json CapturePerformanceCostMeasurementState() const;
void RestorePerformanceCostMeasurementState(const json&);
void DataLoaded() {}
};
SERIALIZATION
METHODS
void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) {
        std::cerr << "Parallax settings contract failed at driver line " << where.line() << '\n';
        std::exit(1);
    }
}
int main() {
    static_assert(sizeof(ExtendedMaterials::Settings) == 32);
    static_assert(offsetof(ExtendedMaterials::Settings, ParallaxStrength) == 24);
    ExtendedMaterials feature;
    json legacy = { { "EnableTerrain", 1 }, { "EnableShadows", 0 } };
    feature.LoadSettings(legacy);
    Check(feature.settings.ParallaxStrength == 1.0f && feature.settings.EnableTerrain == 1 && feature.settings.EnableShadows == 0);
    const float inputs[] = { 0, 0.5f, 1, 2, -1, 3,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    const float expected[] = { 0, 0.5f, 1, 2, 0, 2, 1, 1, 1 };
    for (unsigned i = 0; i < std::size(inputs); ++i) {
        auto input = legacy;
        input["ParallaxStrength"] = inputs[i];
        feature.LoadSettings(input);
        Check(feature.settings.ParallaxStrength == expected[i]);
        json saved;
        feature.SaveSettings(saved);
        Check(saved.at("ParallaxStrength") == expected[i]);
        feature.RestoreDefaultSettings();
        Check(feature.settings.ParallaxStrength == 1.0f);
        feature.LoadSettings(saved);
        Check(feature.settings.ParallaxStrength == expected[i]);
        Check(feature.settings.EnableTerrain == 1 && feature.settings.EnableShadows == 0);
        const auto captured = feature.CapturePerformanceCostMeasurementState();
        feature.settings.ParallaxStrength = 1.0f;
        feature.RestorePerformanceCostMeasurementState(captured);
        Check(feature.settings.ParallaxStrength == expected[i]);
    }
    feature.settings.ParallaxStrength = std::numeric_limits<float>::quiet_NaN();
    json saved;
    feature.SaveSettings(saved);
    Check(saved.at("ParallaxStrength") == 1.0f);
    for (const json invalid : { json(nullptr), json("deep"), json::array() }) {
        feature.settings.ParallaxStrength = 0.5f;
        auto input = legacy;
        input["ParallaxStrength"] = invalid;
        bool rejected = false;
        try { feature.LoadSettings(input); }
        catch (const json::exception&) { rejected = true; }
        Check(rejected && feature.settings.ParallaxStrength == 0.5f);
    }
    for (const bool value : { false, true }) {
        auto input = legacy;
        input["ParallaxStrength"] = value;
        feature.LoadSettings(input);
        Check(feature.settings.ParallaxStrength == (value ? 1.0f : 0.0f));
    }
    std::cout << "Legacy defaults, bounds, invalid types, save/load and capture/restore passed\n";
}
'''
        for marker, value in (("CONSTANTS", constants), ("SETTINGS", block(header, "struct alignas(16) Settings")), ("SERIALIZATION", serialization), ("METHODS", methods)):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-parallax-settings-") as temporary:
            directory = Path(temporary)
            cpp = directory / "settings.cpp"
            exe = directory / "settings.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{ROOT / 'src'}", f"/I{include}", str(cpp), f"/Fe:{exe}"]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
