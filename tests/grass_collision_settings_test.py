"""Execute production grass collision persistence and multiplier validation."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from skylighting_settings_test import block

ROOT = Path(__file__).resolve().parents[1]


class GrassCollisionSettingsTests(unittest.TestCase):
    def test_legacy_defaults_round_trip_and_invalid_values(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        header = (ROOT / "src/Features/GrassCollision.h").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/GrassCollision.cpp").read_text(encoding="utf-8")
        constants = "\n".join(re.findall(r"static constexpr float k\w+CollisionStrength = [^;]+;", header))
        serialization = source[source.index("NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT("):source.index("void GrassCollision::DrawSettings")]
        methods = "\n".join(block(source, signature) for signature in (
            "void GrassCollision::NormalizeSettings(",
            "void GrassCollision::Reset(",
            "GrassCollisionMode::Mode GrassCollision::GetRequestedMode(",
            "GrassCollisionMode::Mode GrassCollision::GetVertexMode(",
            "bool GrassCollision::NeedsVertexDeformation(",
            "float GrassCollision::GetCullingStrength(",
            "bool GrassCollision::Configure(",
            "float GrassCollision::GetCollisionStrength(",
            "bool GrassCollision::SetCollisionStrength(",
            "void GrassCollision::LoadSettings(",
            "void GrassCollision::SaveSettings(",
            "void GrassCollision::RestoreDefaultSettings(",
            "json GrassCollision::CapturePerformanceSettingsState(",
        ))
        driver = r'''
#include "Utils/Finite.h"
#include "Features/GrassCollisionMode.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <array>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <limits>
#include <source_location>
using json = nlohmann::json;
struct FrameState { std::atomic_uint32_t frameCountAtomic{0}; };
namespace globals { FrameState instance; FrameState* state = &instance; }
struct GrassCollision {
CONSTANTS
SETTINGS;
PARAMETER;
PARAMETERS
Settings settings;
bool perFrame = false, loaded = true;
std::atomic_bool resourceFailed{false}, currentDeformationActive{false};
std::atomic_uint32_t publishedFrame{UINT32_MAX};
std::atomic<GrassCollisionMode::Mode> failedMode{GrassCollisionMode::Mode::Disabled};
struct Published { float CollisionStrength = 0, PreviousCollisionStrength = 0; } published;
std::atomic<GrassCollisionMode::Mode> resourceMode{GrassCollisionMode::Mode::Disabled};
uint32_t lastUpdateFrame = 0;
unsigned releases = 0;
void ReleaseResources() { perFrame = false; resourceMode = GrassCollisionMode::Mode::Disabled; ++releases; }
void Reset();
GrassCollisionMode::Mode GetRequestedMode() const;
GrassCollisionMode::Mode GetVertexMode() const;
bool NeedsVertexDeformation() const;
float GetCullingStrength() const;
void NormalizeSettings();
bool Configure(const json&, std::string&);
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
    using GrassCollisionMode::Mode;
    using GrassCollisionMode::Select;
    for (Mode outgoing : { Mode::Basic, Mode::Advanced }) {
        for (Mode requested : { Mode::Disabled, Mode::Basic, Mode::Advanced }) {
            Check(Select(requested, Mode::Disabled, false, false) == requested);
            Check(Select(requested, outgoing, true, false) == outgoing);
            Check(Select(requested, outgoing, false, true) == outgoing);
            Check(Select(requested, outgoing, false, false) == requested);
            Check(Select(outgoing, outgoing, false, false) == outgoing);
        }
    }
    GrassCollision retiring;
    retiring.perFrame = true;
    retiring.resourceMode = Mode::Advanced;
    retiring.Reset();
    Check(retiring.perFrame && retiring.releases == 0);
    globals::state->frameCountAtomic = 1;
    retiring.Reset();
    Check(!retiring.perFrame && retiring.releases == 1);
    retiring.Reset();
    Check(retiring.releases == 1);
    retiring.perFrame = true;
    retiring.resourceMode = Mode::Basic;
    retiring.Reset();
    Check(retiring.perFrame && retiring.releases == 1);
    retiring.settings.EnableGrassCollision = false;
    retiring.Reset();
    Check(!retiring.perFrame && retiring.releases == 2);
    GrassCollision motion;
    auto* state = globals::state;
    globals::state = nullptr;
    Check(motion.GetVertexMode() == Mode::Basic);
    motion.loaded = false;
    Check(motion.GetVertexMode() == Mode::Disabled);
    motion.loaded = true;
    globals::state = state;
    motion.resourceMode = Mode::Advanced;
    motion.currentDeformationActive = true;
    Check(motion.GetVertexMode() == Mode::Advanced);
    motion.currentDeformationActive = false;
    motion.publishedFrame = globals::state->frameCountAtomic.load();
    Check(motion.GetVertexMode() == Mode::Advanced);
    ++globals::state->frameCountAtomic;
    Check(motion.GetVertexMode() == Mode::Basic);
    motion.resourceMode = Mode::Basic;
    motion.SetCollisionStrength(5);
    Check(motion.GetCullingStrength() == 5);
    motion.SetCollisionStrength(0);
    motion.published.PreviousCollisionStrength = 4;
    Check(motion.GetCullingStrength() == 4);
    motion.resourceMode = Mode::Advanced;
    motion.settings.EnableAdvancedGrassCollision = true;
    motion.SetCollisionStrength(5);
    Check(motion.GetCullingStrength() == 1);
    motion.settings.EnableGrassCollision = false;
    Check(motion.GetVertexMode() == Mode::Disabled && motion.GetCullingStrength() == 0);
    motion.settings.EnableGrassCollision = true;
    motion.resourceMode = Mode::Disabled;
    motion.failedMode = Mode::Advanced;
    motion.resourceFailed = true;
    Check(motion.GetVertexMode() == Mode::Disabled);
    motion.settings.EnableAdvancedGrassCollision = false;
    Check(motion.GetVertexMode() == Mode::Basic);
    GrassCollision feature;
    json legacy = { { "EnableGrassCollision", false }, { "TrackRagdolls", false } };
    feature.LoadSettings(legacy);
    Check(feature.GetCollisionStrength() == 1 && !feature.settings.EnableGrassCollision && !feature.settings.TrackRagdolls && !feature.settings.EnableAdvancedGrassCollision);
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
    for (const json invalidType : { json(nullptr), json("strong"), json::array(), json::object() }) {
        Check(feature.SetCollisionStrength(2));
        auto input = legacy;
        input["CollisionStrength"] = invalidType;
        bool rejected = false;
        try { feature.LoadSettings(input); }
        catch (const json::exception&) { rejected = true; }
        Check(rejected && feature.GetCollisionStrength() == 2);
    }
    feature.RestoreDefaultSettings();
    std::string error;
    const json original = feature.CapturePerformanceSettingsState();
    json presetBase = json::parse(R"CSX(BASE_DEFAULTS)CSX");
    Check(presetBase.size() == original.size());
    for (const auto& [key, value] : original.items())
        Check(presetBase.contains(key));
    feature.LoadSettings(presetBase);
    Check(feature.CapturePerformanceSettingsState() == original);
    for (const auto& parameter : GrassCollision::parameters) {
        for (float value : { parameter.minimum, parameter.maximum }) {
            Check(feature.Configure({ { parameter.name, value } }, error));
            Check(feature.settings.*parameter.member == value);
            json saved;
            feature.SaveSettings(saved);
            feature.RestoreDefaultSettings();
            feature.LoadSettings(saved);
            Check(feature.settings.*parameter.member == value);
        }
        const auto before = feature.CapturePerformanceSettingsState();
        Check(!feature.Configure({ { "EnableGrassCollision", false }, { parameter.name, parameter.maximum + 1 } }, error));
        Check(feature.CapturePerformanceSettingsState() == before);
        Check(!feature.Configure({ { parameter.name, std::numeric_limits<double>::infinity() } }, error));
    }
    Check(!feature.Configure({ { "Unknown", 1 } }, error));
    for (const auto* name : { "EnableGrassCollision", "EnableAdvancedGrassCollision", "TrackRagdolls" }) {
        const auto before = feature.CapturePerformanceSettingsState();
        Check(!feature.Configure({ { name, 1 } }, error));
        Check(feature.CapturePerformanceSettingsState() == before);
    }
    for (bool advanced : { true, false }) {
        Check(feature.Configure({ { "EnableAdvancedGrassCollision", advanced } }, error));
        json saved;
        feature.SaveSettings(saved);
        feature.RestoreDefaultSettings();
        Check(!feature.settings.EnableAdvancedGrassCollision);
        feature.LoadSettings(saved);
        Check(feature.settings.EnableAdvancedGrassCollision == advanced);
    }
    auto before = feature.CapturePerformanceSettingsState();
    Check(!feature.Configure({ { "EnableAdvancedGrassCollision", true }, { "SpringStrength", -1 } }, error));
    Check(feature.CapturePerformanceSettingsState() == before);
    Check(!feature.Configure(json::array(), error));
    Check(feature.Configure({ { "EnableGrassCollision", false }, { "TrackRagdolls", false } }, error));
    Check(!feature.settings.EnableGrassCollision && !feature.settings.TrackRagdolls);
    feature.RestoreDefaultSettings();
    Check(feature.CapturePerformanceSettingsState() == original);
    std::cout << "Grass collision defaults, bounds, invalid-input preservation and save/load passed\n";
}
'''
        base = json.loads((ROOT / "docs/development/unified-preset-templates/Base.SettingsUser.json").read_text(encoding="utf-8"))
        driver = driver.replace("BASE_DEFAULTS", json.dumps(base["Grass Collision"]))
        for marker, value in (("CONSTANTS", constants), ("SETTINGS", block(header, "struct Settings")), ("PARAMETERS", header[header.index("static constexpr std::array parameters"):header.index("/** @brief Apply a partial")]), ("PARAMETER", block(header, "struct Parameter")), ("SERIALIZATION", serialization), ("METHODS", methods)):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-grass-collision-settings-") as temporary:
            directory = Path(temporary)
            cpp = directory / "settings.cpp"
            exe = directory / "settings.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{ROOT / 'src'}", f"/I{os.environ['CSX_JSON_INCLUDE']}", str(cpp), f"/Fe:{exe}"]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
