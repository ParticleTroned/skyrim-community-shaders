"""Exercise True PBR through the production feature save/load dispatch."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class TruePBRSettingsTests(unittest.TestCase):
    def test_save_restore_legacy_and_invalid_settings(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        include = os.environ["CSX_JSON_INCLUDE"]
        header = (ROOT / "src/TruePBR.h").read_text(encoding="utf-8")
        source = (ROOT / "src/TruePBR.cpp").read_text(encoding="utf-8")
        base_header = (ROOT / "src/Feature.h").read_text(encoding="utf-8")
        base_source = (ROOT / "src/Feature.cpp").read_text(encoding="utf-8")
        state_header = (ROOT / "src/State.h").read_text(encoding="utf-8")
        state_defaults = "\n".join(re.findall(
            r"static constexpr float kDefaultPbrMetal\w+ = [^;]+;", state_header))
        self.assertEqual(len(state_defaults.splitlines()), 2)
        settings_marker = "NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(\n\tTruePBR::Settings,"
        settings_start = source.index(settings_marker)
        serialization = source[settings_start:source.index(");", settings_start) + 2]
        base_methods = block(base_header, "virtual bool HasFeatureSettings()")
        derived_methods = block(header, "virtual std::string GetName()")
        for signature in ("virtual bool HasFeatureSettings()", "virtual void RestoreDefaultSettingsForLoad()"):
            if signature in header:
                derived_methods += "\n" + block(header, signature)
        if "virtual void RestoreDefaultSettingsForLoad()" in base_header:
            base_methods += "\n" + block(base_header, "virtual void RestoreDefaultSettingsForLoad()")
        methods = "\n".join(block(source, signature) for signature in (
            "void TruePBR::SaveSettings(",
            "void TruePBR::LoadSettings(",
            "void TruePBR::RestoreDefaultSettings(",
        ))
        driver = r'''
#include <nlohmann/json.hpp>
#include "Utils/Finite.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
#include <string>
using json = nlohmann::json;
using uint = unsigned int;
namespace logger {
template<class... Args> void info(Args&&...) {}
template<class... Args> void warn(Args&&...) {}
}
struct State {
STATE_DEFAULTS
    float pbrMetalReflectionScale = 0.5f;
    float pbrMetalHighlightScale = 1.5f;
};
namespace globals { State instance; State* state = &instance; }
struct Feature {
    virtual ~Feature() = default;
    virtual std::string GetName() = 0;
    virtual void SaveSettings(json&) = 0;
    virtual void LoadSettings(json&) = 0;
    virtual void RestoreDefaultSettings() = 0;
BASE_METHODS
    void Save(json&);
    void Load(json& o_json) {
LOAD_DISPATCH
    }
};
struct TruePBR : Feature {
DERIVED_METHODS
SETTINGS;
    Settings settings;
    bool enableVerboseJsonLogging = false;
    void SaveSettings(json&) override;
    void LoadSettings(json&) override;
    void RestoreDefaultSettings() override;
};
SERIALIZATION
METHODS
SAVE_DISPATCH
void Check(bool result, std::source_location where = std::source_location::current()) {
    if (!result) {
        std::cerr << "True PBR persistence failed at driver line " << where.line() << '\n';
        std::exit(1);
    }
}
int main() {
    TruePBR feature;
    Feature& dispatch = feature;
    for (const float strength : { 0.0f, 0.25f, 1.0f }) {
        feature.settings.VertexAOStrength = strength;
        feature.settings.Enabled = 0;
        feature.settings.GrassEnabled = 1;
        json saved = json::object();
        dispatch.Save(saved);
        Check(saved.contains("True PBR"));
        Check(saved.at("True PBR").at("VertexAOStrength") == strength);
        saved = json::parse(saved.dump());
        feature.settings = {};
        dispatch.Load(saved);
        Check(feature.settings.VertexAOStrength == strength && feature.settings.Enabled == 0);
        Check(feature.settings.GrassEnabled == 1);
        TruePBR restarted;
        restarted.Load(saved);
        Check(restarted.settings.VertexAOStrength == strength && restarted.settings.Enabled == 0);
        Check(restarted.settings.GrassEnabled == 1);
    }
    const json defaults = { { "Enabled", 1 }, { "GrassEnabled", 0 }, { "VertexAOStrength", 1.0f } };
    for (json legacy : { json::object(), json{ { "True PBR", json::object() } },
             json{ { "True PBR", nullptr } }, json{ { "True PBR", "invalid" } },
             json{ { "True PBR", { { "VertexAOStrength", "invalid" } } } } }) {
        feature.settings.VertexAOStrength = 0.25f;
        feature.settings.Enabled = 0;
        feature.enableVerboseJsonLogging = true;
        dispatch.Load(legacy);
        Check(json(feature.settings) == defaults);
        Check(globals::state->pbrMetalReflectionScale == 0.5f);
        Check(globals::state->pbrMetalHighlightScale == 1.5f);
        Check(feature.enableVerboseJsonLogging);
    }
    const float values[] = { -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
    const float expected[] = { 0.0f, 1.0f, 1.0f, 1.0f };
    for (unsigned index = 0; index < std::size(values); ++index) {
        json input = { { "True PBR", { { "Enabled", 7 }, { "VertexAOStrength", values[index] } } } };
        dispatch.Load(input);
        Check(feature.settings.VertexAOStrength == expected[index] && feature.settings.Enabled == 1);
    }
    dispatch.RestoreDefaultSettings();
    Check(json(feature.settings) == defaults && !feature.enableVerboseJsonLogging);
    Check(globals::state->pbrMetalReflectionScale == State::kDefaultPbrMetalReflectionScale);
    Check(globals::state->pbrMetalHighlightScale == State::kDefaultPbrMetalHighlightScale);
    json authored = { { "True PBR", { { "GrassEnabled", 7 } } } };
    dispatch.Load(authored);
    Check(feature.settings.GrassEnabled == 1);
    std::cout << "Save/restore, restart, legacy defaults, invalid input and explicit defaults passed\n";
}
'''
        for marker, value in (
            ("STATE_DEFAULTS", state_defaults),
            ("BASE_METHODS", base_methods), ("DERIVED_METHODS", derived_methods),
            ("SETTINGS", block(header, "struct alignas(16) Settings")),
            ("LOAD_DISPATCH", block(base_source, "if (HasFeatureSettings())")),
            ("SAVE_DISPATCH", block(base_source, "void Feature::Save(")),
            ("SERIALIZATION", serialization), ("METHODS", methods),
        ):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-pbr-settings-") as temporary:
            directory = Path(temporary)
            cpp = directory / "settings.cpp"
            exe = directory / "settings.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", "/wd4324",
                       f"/I{ROOT / 'src'}", f"/I{include}", str(cpp), f"/Fe:{exe}"]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
