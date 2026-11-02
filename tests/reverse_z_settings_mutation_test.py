"""Exercise the production Reverse Z action against Menu's lazy dirty baseline."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from grass_collision_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class ReverseZSettingsMutationTests(unittest.TestCase):
    def test_lazy_baseline_and_mutation_boundaries(self):
        bridge = (ROOT / "src/Api/FeatureDevBenchBridge.cpp").read_text(encoding="utf-8")
        menu = (ROOT / "src/Menu.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/Menu.h").read_text(encoding="utf-8")
        action = block(bridge, 'if (action == "reverse_z")')
        methods = "\n".join(block(menu, signature) for signature in (
            "bool Menu::CaptureCurrentSettingsSnapshot(",
            "bool Menu::EnsureSettingsDirtyBaseline(",
            "void Menu::ResetSettingsDirtyState(",
            "void Menu::UpdateDirtySettingsSections(",
            "void Menu::UpdateSettingsDirtyState(",
        ))
        inline_methods = "\n".join(block(header, signature) for signature in (
            "bool HasUnsavedSettings()",
            "bool HasUnsavedFeatureSettings(",
            "void RequestSettingsDirtyCheck()",
            "bool PrepareSettingsMutation()",
        ))
        driver = r'''
#include <nlohmann/json.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
using json = nlohmann::json;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
namespace logger { template<class... Args> void warn(Args&&...) {} }
struct Menu {
    json settingsDirtyBaseline = json::object();
    bool settingsDirtyBaselineInitialized = false, settingsDirty = false;
    std::unordered_set<std::string> dirtySettingsSections;
    bool settingsDirtyCheckRequested = false, settingsSaveMessageIsError = false;
    bool CaptureCurrentSettingsSnapshot(json&);
    bool EnsureSettingsDirtyBaseline();
    void ResetSettingsDirtyState();
    void UpdateDirtySettingsSections(const json&);
    void UpdateSettingsDirtyState();
    void ClearSettingsSaveResult() {}
INLINE_METHODS
};
struct ReverseZ {
    struct Settings { bool EnableReverseZBuffer = false; } settings;
    json GetDiagnostics() const { return {{"requested", settings.EnableReverseZBuffer}}; }
};
struct State {
    enum class CaptureFailure { None, Standard, Unknown } captureFailure = CaptureFailure::None;
    bool mutationBlocked = false;
    unsigned snapshots = 0;
    bool IsPersistentMutationBlocked() const { return mutationBlocked; }
    void SaveToJson(json&, bool);
};
namespace globals {
    Menu* menu = nullptr;
    State* state = nullptr;
    namespace features { ReverseZ reverseZ; }
}
namespace PerformanceTuningRenderer {
    bool active = false;
    bool HasActiveMeasurements() { return active; }
}
void State::SaveToJson(json& result, bool saveFeatures) {
    ++snapshots;
    Require(!saveFeatures, "Dirty checking attempted a persistent save");
    if (captureFailure == CaptureFailure::Standard) throw std::runtime_error("snapshot unavailable");
    if (captureFailure == CaptureFailure::Unknown) throw 1;
    result = {{"Reverse Z", {{"EnableReverseZBuffer", globals::features::reverseZ.settings.EnableReverseZBuffer}}},
        {"Unrelated", {{"setting", 7}}}};
}
MENU_METHODS
struct ServiceFoundation {
    json MakeEnvelope(const json&, bool ok) const { return {{"ok", ok}}; }
    json MakeError(const json&, const char* code, const char* message, const char*, bool, const char*) const {
        return {{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
    }
};
ServiceFoundation& Foundation() { static ServiceFoundation foundation; return foundation; }
unsigned mainDispatches = 0;
json OnMain(std::function<json()> run) { ++mainDispatches; return run(); }
json BuildResult(const json& args) {
    const auto action = args.value("action", std::string{});
REVERSE_Z_ACTION
    throw std::runtime_error("Fixture only supports the reverse_z action");
}
json SetRequested(const json& value) { return BuildResult({{"action", "reverse_z"}, {"reverseZEnabled", value}}); }
struct Fixture {
    Menu menu;
    State state;
    explicit Fixture(bool enabled = false) {
        globals::menu = &menu;
        globals::state = &state;
        globals::features::reverseZ.settings.EnableReverseZBuffer = enabled;
        PerformanceTuningRenderer::active = false;
        mainDispatches = 0;
    }
    ~Fixture() { globals::menu = nullptr; globals::state = nullptr; }
    void DrawDirtyState() {
        menu.EnsureSettingsDirtyBaseline();
        menu.UpdateSettingsDirtyState();
    }
    bool Requested() const { return globals::features::reverseZ.settings.EnableReverseZBuffer; }
};
void CheckLazyBaseline() {
    for (bool initial : {false, true}) {
        Fixture fixture(initial);
        const auto result = SetRequested(!initial);
        Require(result.at("ok") && fixture.Requested() == !initial, "Initial mutation was not accepted");
        Require(result.at("result").at("restartRequired"), "Mutation lost its restart requirement");
        fixture.DrawDirtyState();
        Require(fixture.menu.HasUnsavedSettings() && fixture.menu.HasUnsavedFeatureSettings("Reverse Z"),
            "Mutation before the first menu draw was incorrectly marked clean");
        Require(!fixture.menu.HasUnsavedFeatureSettings("Unrelated"), "Mutation dirtied an unrelated section");
        Require(!fixture.menu.settingsDirtyCheckRequested, "Dirty comparison was not consumed");

        Require(SetRequested(initial).at("ok"), "Restoring the saved value was rejected");
        fixture.DrawDirtyState();
        Require(!fixture.menu.HasUnsavedSettings(), "Restoring the baseline left a false dirty indication");

        fixture.menu.ResetSettingsDirtyState();
        Require(SetRequested(!initial).at("ok"), "Mutation after a baseline reset was rejected");
        fixture.DrawDirtyState();
        Require(fixture.menu.HasUnsavedSettings() && fixture.menu.HasUnsavedFeatureSettings("Reverse Z"),
            "Mutation after a save/load baseline reset was incorrectly marked clean");
    }
}
void CheckReadOnlyAndNoOp() {
    for (bool initialized : {false, true}) {
        Fixture fixture;
        if (initialized) fixture.DrawDirtyState();
        const auto snapshots = fixture.state.snapshots;
        fixture.state.mutationBlocked = true;
        PerformanceTuningRenderer::active = true;
        fixture.state.captureFailure = State::CaptureFailure::Standard;
        Require(BuildResult({{"action", "reverse_z"}}).at("ok"), "Read-only diagnostics were blocked");
        Require(SetRequested(false).at("ok"), "A no-op request was blocked");
        Require(!fixture.Requested() && !fixture.menu.settingsDirtyCheckRequested &&
            !fixture.menu.HasUnsavedSettings(), "Read-only or no-op request scheduled dirty work");
        Require(fixture.state.snapshots == snapshots && fixture.menu.settingsDirtyBaselineInitialized == initialized,
            "Read-only or no-op request captured a settings snapshot");
    }
}
void CheckCaptureFailures() {
    for (auto failure : {State::CaptureFailure::Standard, State::CaptureFailure::Unknown}) {
        Fixture fixture;
        fixture.state.captureFailure = failure;
        const auto result = SetRequested(true);
        Require(!result.at("ok").get<bool>() && result.at("result").contains("error"),
            "Baseline capture failure was reported as success");
        Require(!fixture.Requested() && !fixture.menu.settingsDirtyCheckRequested &&
            !fixture.menu.settingsDirtyBaselineInitialized && fixture.state.snapshots == 1,
            "Baseline capture failure mutated or acknowledged settings");
        fixture.state.captureFailure = State::CaptureFailure::None;
        Require(SetRequested(true).at("ok"), "Recovered snapshot capture could not retry the mutation");
        fixture.DrawDirtyState();
        Require(fixture.menu.HasUnsavedSettings(), "Retried mutation lost its original baseline");
    }
}
void CheckBlockedBoundaries() {
    for (int boundary = 0; boundary < 4; ++boundary) {
        Fixture fixture;
        if (boundary == 0) globals::menu = nullptr;
        if (boundary == 1) globals::state = nullptr;
        if (boundary == 2) fixture.state.mutationBlocked = true;
        if (boundary == 3) PerformanceTuningRenderer::active = true;
        const auto result = SetRequested(true);
        Require(!result.at("ok").get<bool>() && result.at("result").contains("error"),
            "Unavailable settings or an active mutation boundary accepted a change");
        Require(!fixture.Requested() && !fixture.menu.settingsDirtyCheckRequested &&
            !fixture.menu.settingsDirtyBaselineInitialized && fixture.state.snapshots == 0,
            "Blocked mutation changed the value or captured a new baseline");
    }
}
void CheckInvalidBooleans() {
    for (const auto& invalid : {json(nullptr), json(1), json(0.0), json("true"), json::array(), json::object()}) {
        Fixture fixture;
        const auto result = SetRequested(invalid);
        Require(!result.at("ok").get<bool>() && result.at("error").at("code") == "invalid_settings",
            "Malformed boolean did not produce a validation error");
        Require(mainDispatches == 0 && !fixture.Requested() && fixture.state.snapshots == 0 &&
            !fixture.menu.settingsDirtyCheckRequested, "Malformed boolean reached the settings mutation boundary");
    }
}
int main() {
    try {
        CheckLazyBaseline();
        CheckReadOnlyAndNoOp();
        CheckCaptureFailures();
        CheckBlockedBoundaries();
        CheckInvalidBooleans();
        std::cout << "Reverse Z settings passed: lazy/reset baselines, no-op/read-only, failed capture, blocked boundaries and invalid types\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        for marker, value in (("INLINE_METHODS", inline_methods), ("MENU_METHODS", methods), ("REVERSE_Z_ACTION", action)):
            driver = driver.replace(marker, value)
        compiler = os.environ.get("CXX") or shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="csx-reverse-settings-") as temporary:
            directory = Path(temporary)
            cpp = directory / "settings.cpp"
            executable = directory / ("settings.exe" if os.name == "nt" else "settings")
            cpp.write_text(driver, encoding="utf-8")
            include = os.environ["CSX_JSON_INCLUDE"]
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", f"/I{include}", str(cpp), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{include}", str(cpp), "-o", str(executable)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
