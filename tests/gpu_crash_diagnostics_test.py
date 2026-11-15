"""Exercise the production failure logger and native diagnostic compile gates."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]
SHARED = (
    "src/Api/FeatureDevBenchBridge.cpp", "src/Features/Upscaling.cpp",
    "src/Features/Upscaling/Streamline.cpp", "src/Globals.cpp",
    "src/GpuPass.cpp", "src/GpuPass.h", "src/Hooks.cpp", "src/State.cpp",
)
DIAGNOSTICS = (
    "src/Diagnostics/GpuCrashDiagnostics.cpp",
    "src/Diagnostics/GpuCrashDiagnostics.h",
)


class GpuCrashDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get("CXX") or shutil.which("cl")
        if not cls.compiler:
            raise unittest.SkipTest("MSVC developer environment required")

    def test_first_present_failure(self):
        hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
        helper = block(hooks, "static void LogFirstPresentFailure(")
        driver = r'''
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003u);
constexpr HRESULT REMOVED = static_cast<HRESULT>(0x887a0005u);
#define SUCCEEDED(value) ((value) >= 0)
struct Upscaling {
    enum class Phase { Idle, Applying };
    struct VRRenderScaleTransitionSnapshot {
        Phase state = Phase::Idle;
        std::uint64_t targetEpoch = 0, revision = 0;
    };
    std::atomic<int> queries{0};
    VRRenderScaleTransitionSnapshot GetVRRenderScaleTransitionSnapshot() {
        ++queries;
        return { Phase::Applying, 37, 19 };
    }
    static const char* GetVRRenderScaleTransitionStateName(Phase phase) {
        return phase == Phase::Applying ? "Applying" : "Idle";
    }
};
struct Device {
    std::atomic<int> queries{0};
    HRESULT reason = REMOVED;
    HRESULT GetDeviceRemovedReason() { ++queries; return reason; }
} primary;
struct State { std::uint32_t frameCount = 91; } frame;
namespace globals {
    State* state = &frame;
    namespace d3d { Device* device = &primary; }
    namespace game { bool isVR = true; }
    namespace features { Upscaling upscaling; }
}
int writes = 0, flushes = 0;
std::uint32_t recordedResult = 0, recordedReason = 0, recordedFrame = 0;
bool recordedDevice = false;
std::string recordedState;
std::uint64_t recordedEpoch = 0, recordedRevision = 0;
namespace logger {
    void info(std::string_view format, std::uint32_t result, bool available,
        std::uint32_t reason, std::uint32_t atFrame, const char* state,
        std::uint64_t epoch, std::uint64_t revision) {
        if (format.find("HRESULT=0x{:08X}") == std::string_view::npos ||
            format.find("primaryDeviceRemovedReason=0x{:08X}") == std::string_view::npos)
            throw std::runtime_error("Missing HRESULT fields");
        ++writes;
        recordedResult = result; recordedDevice = available;
        recordedReason = reason; recordedFrame = atFrame;
        recordedState = state; recordedEpoch = epoch; recordedRevision = revision;
    }
}
namespace spdlog {
    struct Log { void flush() { ++flushes; } } sink;
    Log* default_logger() { return &sink; }
}
struct PresentHook { PRODUCTION_HELPER };
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "vr";
    for (int i = 0; i < 32; ++i) PresentHook::LogFirstPresentFailure(S_OK);
    Require(writes == 0 && flushes == 0 && primary.queries == 0 &&
        globals::features::upscaling.queries == 0, "Healthy Present performed diagnostic work");
    if (mode == "missing") globals::d3d::device = nullptr;
    if (mode == "flat") globals::game::isVR = false;
    if (mode == "other_error") primary.reason = S_OK;
    const HRESULT firstResult = mode == "other_error" ? E_POINTER : REMOVED;
    if (mode == "concurrent") {
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i)
            threads.emplace_back([] { PresentHook::LogFirstPresentFailure(REMOVED); });
        for (auto& thread : threads) thread.join();
    } else {
        PresentHook::LogFirstPresentFailure(firstResult);
    }
    for (int i = 0; i < 32; ++i) PresentHook::LogFirstPresentFailure(E_POINTER);
    Require(writes == 1 && flushes == 1, "First failure was not logged and flushed exactly once");
    Require(recordedResult == static_cast<std::uint32_t>(firstResult) && recordedFrame == 91,
        "Original failure or frame was overwritten");
    Require(recordedDevice == (mode != "missing") &&
        primary.queries == (mode == "missing" ? 0 : 1), "Incorrect primary-device query");
    Require(recordedReason == static_cast<std::uint32_t>(mode == "missing" ? E_POINTER : primary.reason),
        "Incorrect device-removal reason");
    Require(globals::features::upscaling.queries == (mode == "flat" ? 0 : 1),
        "Incorrect transition snapshot work");
    Require(recordedState == (mode == "flat" ? "not-applicable" : "Applying") &&
        recordedEpoch == (mode == "flat" ? 0u : 37u) &&
        recordedRevision == (mode == "flat" ? 0u : 19u), "Incorrect transition state");
    std::cout << "First Present failure case passed: " << mode << '\n';
}
'''.replace("PRODUCTION_HELPER", helper)
        with tempfile.TemporaryDirectory(prefix="csx-present-failure-") as directory:
            source = Path(directory) / "fixture.cpp"
            executable = Path(directory) / "fixture.exe"
            source.write_text(driver, encoding="utf-8")
            compiled = subprocess.run(
                [self.compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                 "/utf-8", str(source), f"/Fe:{executable}"],
                cwd=directory, capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for mode in ("vr", "flat", "missing", "concurrent", "other_error"):
                with self.subTest(mode=mode):
                    result = subprocess.run([str(executable), mode], capture_output=True,
                                            text=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_production_isolation(self):
        with tempfile.TemporaryDirectory(prefix="csx-gpu-diagnostics-gate-") as directory:
            unit = Path(directory) / "unit.cpp"
            for runtime in ("SE", "AE", "VR"):
                for name in SHARED + DIAGNOSTICS:
                    source = (ROOT / name).read_text(encoding="utf-8")
                    source = re.sub(r"^\s*#\s*(?:include|pragma)\b[^\n]*", "", source, flags=re.M)
                    unit.write_text(source, encoding="utf-8")
                    for enabled in (False, True):
                        result = subprocess.run(
                            [self.compiler, "/nologo", "/EP", "/TP", "/utf-8",
                             f"/DENABLE_SKYRIM_{runtime}=1",
                             "/DDEVBENCH_BRIDGE_ENABLED" if enabled else "/UDEVBENCH_BRIDGE_ENABLED",
                             str(unit)], capture_output=True, text=True, timeout=30,
                        )
                        self.assertEqual(result.returncode, 0, result.stderr)
                        diagnostics = re.search(r"GpuCrash|gpuCrash|gpu_crash|crashDiagnostic", result.stdout)
                        self.assertEqual(bool(diagnostics), enabled, (runtime, name, enabled))
                        if not enabled and name in DIAGNOSTICS:
                            self.assertFalse(result.stdout.strip(), (runtime, name))
                        if name == "src/Hooks.cpp":
                            self.assertIn("LogFirstPresentFailure", result.stdout)


if __name__ == "__main__":
    unittest.main()
