"""Execute the production startup preference serializer and DevBench adapter."""

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


class ShadowBatchToggleTests(unittest.TestCase):
    def test_persistence_adapter_and_contract(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        source = (ROOT / "src/MenuDevBenchBridge.cpp").read_text(encoding="utf-8")
        vr_source = (ROOT / "src/Features/VR.cpp").read_text(encoding="utf-8")
        vr_header = (ROOT / "src/Features/VR.h").read_text(encoding="utf-8")
        serialization = re.search(
            r"NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT\(\s*VR::Settings,.*?\)",
            vr_source, re.S,
        ).group()
        names = serialization[serialization.index(",") + 1:-1].replace("\n", "").replace("\t", "").split(",")
        self.assertIn("EnableVRShadowBatch", names)
        default = re.search(r"bool EnableVRShadowBatch\s*=\s*[^;]+;", vr_header).group()
        # Unrelated settings are placeholders; the actual macro and toggle default are executed.
        members = "\n".join("json " + name + ";" for name in names if name != "EnableVRShadowBatch")
        descriptor = json.loads(re.search(r'R"\((\{"description":.*?\})\)";', source).group(1))
        start = source.index('document["inputSchema"]["properties"]["action"]["enum"].push_back("set_vr_shadow_batch_enabled")')
        end = source.index('document["inputSchema"]["properties"]["action"]["enum"].push_back("set_sky_sync_disabled_worldspaces")', start)
        handler = block(source, 'if (action == "set_vr_shadow_batch_enabled")')
        driver = r'''
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <string>
#include <utility>
using json = nlohmann::json;
struct VR {
    struct Settings { DEFAULT MEMBERS };
    Settings settings;
    bool loaded = true;
};
SERIALIZATION
namespace VRShadowBatch { bool installed = true; bool IsInstalled() { return installed; } }
struct State { bool blocked = false; bool IsPersistentMutationBlocked() const { return blocked; } } fixtureState;
struct Menu { unsigned dirty = 0; void RequestSettingsDirtyCheck() { ++dirty; } } menu;
struct PerformanceTuningRenderer { static inline bool measuring = false; static bool HasActiveMeasurements() { return measuring; } };
namespace globals {
    State* state = &fixtureState; Menu* menu = &::menu;
    namespace game { bool isVR = true; }
    namespace features { VR vr; }
}
template<class F> json RunOnMainThread(F&& function) { return std::forward<F>(function)(); }
STATUS
json Handle(const json& a_args) {
    const std::string action = "set_vr_shadow_batch_enabled";
    HANDLER
    std::abort();
}
json Descriptor() {
    auto document = json::parse(R"descriptor(BASE_DESCRIPTOR)descriptor");
    CONTRACT
    return document;
}
void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) { std::cerr << "toggle assertion at line " << where.line() << '\n'; std::exit(1); }
}
int main() {
    auto& vr = globals::features::vr;
    Check(VR::Settings{}.EnableVRShadowBatch);
    Check(json::object().get<VR::Settings>().EnableVRShadowBatch);
    vr.settings.EnableVRShadowBatch = false;
    const json saved = vr.settings;
    Check(!json::parse(saved.dump()).get<VR::Settings>().EnableVRShadowBatch);
    vr.settings = VR::Settings{};
    Check(vr.settings.EnableVRShadowBatch);
    Check(!Handle(json::object()).at("ok").get<bool>());
    for (const json invalid : { json(nullptr), json(0), json("false"), json::array(), json::object() }) {
        Check(!Handle({{"enabled", invalid}}).at("ok").get<bool>());
        Check(vr.settings.EnableVRShadowBatch && menu.dirty == 0);
        bool rejected = false;
        try { const auto settings = json{{"EnableVRShadowBatch", invalid}}.get<VR::Settings>(); (void)settings; }
        catch (const json::exception&) { rejected = true; }
        Check(rejected);
    }
    for (unsigned condition = 0; condition < 6; ++condition) {
        globals::game::isVR = condition != 0;
        vr.loaded = condition != 1;
        globals::state = condition == 2 ? nullptr : &fixtureState;
        fixtureState.blocked = condition == 3;
        PerformanceTuningRenderer::measuring = condition == 4;
        globals::menu = condition == 5 ? nullptr : &menu;
        Check(!Handle({{"enabled", false}}).at("ok").get<bool>());
        Check(vr.settings.EnableVRShadowBatch && menu.dirty == 0 && VRShadowBatch::installed);
    }
    globals::menu = &menu;
    for (bool active : {false, true}) {
        VRShadowBatch::installed = active;
        for (bool requested : {false, true}) {
            const auto result = Handle({{"enabled", requested}});
            Check(result.at("ok") && !result.at("persisted").get<bool>());
            const auto status = result.at("shadowBatch");
            Check(status.at("configuredEnabled") == requested && status.at("activeEnabled") == active);
            Check(status.at("restartRequired") == (requested != active));
            Check(VRShadowBatch::installed == active && vr.settings.EnableVRShadowBatch == requested);
        }
    }
    Check(menu.dirty == 4);
    globals::game::isVR = false;
    Check(!ShadowBatchStatus().at("supported").get<bool>());
    Check(!ShadowBatchStatus().at("restartRequired").get<bool>());
    const auto descriptor = Descriptor();
    const auto& input = descriptor.at("inputSchema");
    Check(input.at("properties").at("enabled").at("type") == "boolean");
    bool declared = false;
    for (const auto& action : input.at("properties").at("action").at("enum"))
        declared |= action == "set_vr_shadow_batch_enabled";
    Check(declared);
    const auto& conditional = input.at("allOf").back();
    Check(conditional.at("if").at("properties").at("action").at("const") == "set_vr_shadow_batch_enabled");
    Check(conditional.at("then").at("required") == json::array({"enabled"}));
    const auto& output = descriptor.at("outputSchema").at("properties");
    Check(output.at("shadowBatch") == output.at("status").at("properties").at("shadowBatch"));
    for (const auto* field : {"supported", "configuredEnabled", "activeEnabled", "restartRequired"})
        Check(output.at("shadowBatch").at("properties").at(field).at("type") == "boolean");
    std::cout << "Startup settings, rejected mutations, all active/requested pairs and tool schema passed\n";
}
'''
        for marker, value in (
            ("DEFAULT", default), ("MEMBERS", members), ("SERIALIZATION", serialization),
            ("STATUS", block(source, "json ShadowBatchStatus()")), ("HANDLER", handler),
            ("BASE_DESCRIPTOR", json.dumps(descriptor)), ("CONTRACT", source[start:end]),
        ):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-shadow-batch-toggle-") as temporary:
            directory = Path(temporary)
            cpp, exe = directory / "toggle.cpp", directory / "toggle.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                       f"/I{os.environ['CSX_JSON_INCLUDE']}", str(cpp), f"/Fe:{exe}"]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
