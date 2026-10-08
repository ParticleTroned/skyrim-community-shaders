"""Exercise production water-profile serialization, migration and validation."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from extract_adaptive_balance_toggle import between, function


ROOT = Path(__file__).resolve().parents[1]


class WaterAppearanceSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or next(
            (path for name in ("cl", "c++", "clang++", "g++") if (path := shutil.which(name))), None)
        if compiler is None:
            raise unittest.SkipTest("A C++20 compiler is required")
        include = os.environ.get("CSX_JSON_INCLUDE")
        if not include:
            raise RuntimeError("CSX_JSON_INCLUDE must identify the nlohmann JSON include directory")
        adaptive = (ROOT / "src/Features/AdaptiveBrightness.cpp").read_text(encoding="utf-8-sig")
        water = (ROOT / "src/Features/WaterAppearance.cpp").read_text(encoding="utf-8-sig")
        migration = (ROOT / "src/SettingsMigrations.cpp").read_text(encoding="utf-8-sig")
        production = [
            between(water, "\tconstexpr float kWaterBrightnessMin", "\tvoid DrawTooltip("),
            function(water, "void WaterAppearance::SanitizeProfile("),
            function(migration, "bool SettingsMigrations::MatchesJsonSchema("),
        ]
        for signature in ("\tbool GetOptionalBool(", "\tstd::optional<WaterAppearance::Profile> TryGetWaterAppearanceProfile(",
                          "\tvoid SetProfileWaterAppearance(", "\tvoid MigrateLegacyProfileWaterAppearance("):
            production.append(function(adaptive, signature))
        prelude = r'''
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
using uint = unsigned int;
using json = nlohmann::json;
#include "Features/WaterAppearance.h"
#include "SettingsMigrations.h"
#include "Utils/Finite.h"
'''
        driver = r'''
void Check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
json LoadWater(json water, const WaterAppearance::Profile& legacy = {}, bool hasLegacy = false, bool force = false)
{
    json profile = { { "water", std::move(water) } };
    MigrateLegacyProfileWaterAppearance(profile, legacy, hasLegacy, force);
    return profile.at("water").get<WaterAppearance::Profile>();
}
int main(int argc, char** argv)
{
    Check(argc == 2, "Expected a test case");
    const std::string test = argv[1];
    const json defaults = WaterAppearance::Profile{};
    WaterAppearance::Profile edited;
    edited.WaterBrightness = 1.25f;
    edited.GlobalReflectionAmount = 0.5f;
    edited.RefractionAmount = 0.75f;
    edited.SunSpecularMultiplier = 2.5f;
    edited.WaveAmplitude = 0.5f;
    edited.FresnelMin = 0.25f;
    edited.FresnelMax = 0.75f;
    edited.Muddiness = 1.5f;
    edited.CausticsStrength = 0.5f;
    edited.CausticsTiling = 2.0f;
    edited.CausticsSpeed = 0.0f;
    edited.CausticsDispersion = 1.5f;
    edited.ParallaxStrength = 0.0f;
    edited.ParallaxQuality = 32;
    const json expected = edited;
    if (test == "roundtrip") {
        auto saved = expected;
        for (int repeat = 0; repeat < 3; ++repeat) {
            saved = LoadWater(json::parse(saved.dump()));
            Check(saved == expected, "Save/reload or preset import discarded water fields");
        }
    } else if (test == "inheritance") {
        WaterAppearance::Profile legacy;
        legacy.WaterBrightness = 1.5f;
        const json native = { { "ParallaxStrength", 0.5f }, { "ParallaxQuality", 64 }, { "CausticsSpeed", 0.0f } };
        auto result = LoadWater(native, legacy, true);
        Check(result.at("WaterBrightness") == 1.5f, "Missing legacy value did not inherit");
        Check(result.at("ParallaxStrength") == 0.5f && result.at("ParallaxQuality") == 64 && result.at("CausticsSpeed") == 0.0f,
            "Explicit modern values were lost during legacy migration");
        Check(LoadWater(expected, legacy, true, true) == json(legacy), "Forced legacy precedence changed");
        auto marked = expected;
        marked[SettingsMigrations::kLegacyWaterProfileExplicitKey.data()] = true;
        Check(LoadWater(marked, legacy, true, true) == expected, "Explicit profile precedence changed");
    } else if (test == "malformed") {
        auto invalid = expected;
        invalid["ParallaxQuality"] = "32";
        invalid["ParallaxStrength"] = nullptr;
        invalid["CausticsSpeed"] = false;
        invalid["CausticsTiling"] = json::array();
        auto sanitized = expected;
        for (const auto* key : { "ParallaxQuality", "ParallaxStrength", "CausticsSpeed", "CausticsTiling" })
            sanitized[key] = defaults.at(key);
        Check(LoadWater(invalid) == sanitized, "Malformed fields reset valid siblings or survived");
        Check(LoadWater(json::array()) == defaults, "Malformed container did not fall back");
        auto unknown = expected;
        unknown["UnknownSetting"] = 42;
        Check(LoadWater(unknown) == expected, "Unknown field altered the supported profile");
    } else if (test == "bounds") {
        const auto result = LoadWater({ { "ParallaxStrength", 4.0f }, { "ParallaxQuality", 100 }, { "CausticsSpeed", -1.0f } });
        Check(result.at("ParallaxStrength") == 2.0f && result.at("ParallaxQuality") == 64 && result.at("CausticsSpeed") == 0.0f,
            "Supported fields did not retain their validation bounds");
        Check(LoadWater({ { "ParallaxQuality", -10 } }).at("ParallaxQuality") == 4, "Minimum quality bound changed");
    } else if (test == "wide-numbers") {
        for (const auto& value : { json(std::numeric_limits<uint64_t>::max()), json(1e100), json(4294967328ull) })
            Check(LoadWater({ { "ParallaxQuality", value } }).at("ParallaxQuality") == 64, "Quality overflowed before clamping");
        for (const auto& value : { json(std::numeric_limits<int64_t>::min()), json(-1e100) })
            Check(LoadWater({ { "ParallaxQuality", value } }).at("ParallaxQuality") == 4, "Negative quality overflowed before clamping");
        Check(LoadWater({ { "ParallaxQuality", 24.5 } }).at("ParallaxQuality") == 24, "Fractional quality conversion changed");
    } else if (test == "legacy") {
        json old = json::object();
        auto migrated = defaults;
        for (const auto key : SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys)
            migrated[key.data()] = old[key.data()] = expected.at(key.data());
        Check(LoadWater(old) == migrated, "Legacy profile defaults changed");
        Check(LoadWater(json::object()) == defaults, "Empty profile defaults changed");
    } else {
        throw std::runtime_error("Unknown test case");
    }
    std::cout << test << " passed\n";
}
'''
        cls.temporary = tempfile.TemporaryDirectory(prefix="csx-water-settings-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = directory / "water-settings.cpp"
        cls.executable = directory / "water-settings.exe"
        source.write_text(prelude + "\n".join(production) + driver, encoding="utf-8")
        includes = [str(ROOT / "src"), *include.split(";")]
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                       *[f"/I{path}" for path in includes], str(source), f"/Fe:{cls.executable}"]
        else:
            command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                       *[f"-I{path}" for path in includes], str(source), "-o", str(cls.executable)]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def check_case(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_saved_profile_and_preset_roundtrip(self):
        self.check_case("roundtrip")

    def test_legacy_inheritance_and_explicit_precedence(self):
        self.check_case("inheritance")

    def test_malformed_fields_preserve_valid_siblings(self):
        self.check_case("malformed")

    def test_parallax_and_caustics_bounds(self):
        self.check_case("bounds")

    def test_older_profiles_keep_defaults(self):
        self.check_case("legacy")

    def test_quality_clamps_before_integer_conversion(self):
        self.check_case("wide-numbers")


if __name__ == "__main__":
    unittest.main()
