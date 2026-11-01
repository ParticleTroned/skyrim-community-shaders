"""Execute the production SE sweep-restoration and diagnostic serialization helpers.

The renderer requires the game and ImGui. This fixture extracts its unchanged
helpers and supplies observable settings owners; the measurement arithmetic and
JSON equivalence code are compiled directly from the production translation units.
Set CSX_JSON_INCLUDE to the include root containing nlohmann/json.hpp.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    state = "code"
    escaped = False
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if following == "*/":
                state = "code"
                index += 1
        elif state in ('"', "'"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == state:
                state = "code"
        elif following == "//":
            state = "line"
            index += 1
        elif following == "/*":
            state = "block"
            index += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
        index += 1
    raise AssertionError(f"Unclosed production helper: {signature}")


PRELUDE = r'''
#include "Menu/PerformanceTuningJson.h"
#include "Menu/PerformanceTuningMeasurement.h"
#include "Menu/PerformanceTuningStatistics.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using json = nlohmann::json;
struct FrameGenerationSettings { int mode = 0; bool forceEnable = false, allowInMenus = false; };
struct UpscalingOwner {
    json state;
    FrameGenerationSettings generation;
    bool refuseSettings = false, refuseGeneration = false, throwSettings = false;
    unsigned settingsWrites = 0, generationWrites = 0;
    json CapturePerformanceCostMeasurementState() const { return state; }
    FrameGenerationSettings CaptureFrameGenerationSettingsForPerformanceMeasurement() const { return generation; }
    void RestorePerformanceCostMeasurementState(const json& saved) {
        ++settingsWrites;
        if (throwSettings) throw std::runtime_error("settings owner rejected restoration");
        if (!refuseSettings) state = saved;
    }
    void RestoreFrameGenerationSettingsAfterPerformanceMeasurement(FrameGenerationSettings saved) {
        ++generationWrites;
        if (!refuseGeneration) generation = saved;
    }
};
namespace globals::features { UpscalingOwner upscaling; }
namespace logger { template<class... Args> void error(const char*, Args&&...) {} }
enum class MonoSweepPhase { Idle, Staging, Measuring, Cooldown, Restoring, Complete, Failed, Cancelled };
struct MonoSweepState {
    MonoSweepPhase phase = MonoSweepPhase::Restoring, terminalPhase = MonoSweepPhase::Cancelled;
    bool restorePending = true;
    json originalState;
    FrameGenerationSettings originalFrameGeneration;
    bool Active() const {
        return restorePending || phase == MonoSweepPhase::Staging || phase == MonoSweepPhase::Measuring ||
            phase == MonoSweepPhase::Cooldown || phase == MonoSweepPhase::Restoring;
    }
} g_monoSweep;
bool g_closedMenuMeasurement = true, otherMeasurementLocked = false;
unsigned profilerRestores = 0;
bool IsAnyFeatureCostMeasurementLocked() { return otherMeasurementLocked; }
void SyncProfilerCaptureModeLimit() {}
void SyncFeatureCostVanityCameraSuppression() {}
void RestoreProfilerStateAfterPerformanceTuning() { ++profilerRestores; }
struct FeatureCostMeasurementState {
    PerformanceTuning::SampleWindow currentBefore, comparison;
};
'''

CHECKS = r'''
void Reset() {
    globals::features::upscaling = {};
    g_monoSweep = {};
    g_monoSweep.originalState = { {"quality", 3}, {"method", 2}, {"tuning", 0.75} };
    g_monoSweep.originalFrameGeneration = { 2, true, false };
    globals::features::upscaling.state = { {"quality", 6}, {"method", 3}, {"tuning", 0.75} };
    otherMeasurementLocked = false;
    g_closedMenuMeasurement = true;
    profilerRestores = 0;
}
PerformanceTuning::SampleWindow Window(double frame, double gpu, double cpu) {
    PerformanceTuning::SampleWindow result;
    PerformanceTuning::BeginSampleWindow(result, 0, 0, 0.0);
    for (uint64_t id = 1; id < 1000; ++id) {
        auto accepted = PerformanceTuning::AddPresentSample(result, id, frame, false);
        assert(accepted == PerformanceTuning::AddSampleResult::Added || accepted == PerformanceTuning::AddSampleResult::Complete);
        auto whole = PerformanceTuning::AddWholeFrameSample(result, id, id, gpu, cpu);
        assert(whole == PerformanceTuning::AddSampleResult::Added || whole == PerformanceTuning::AddSampleResult::Complete);
        if (accepted == PerformanceTuning::AddSampleResult::Complete) return result;
    }
    throw std::runtime_error("measurement window did not finish");
}
int main() {
    Reset();
    assert(RelinquishMonoSweepForConfiguration());
    assert(!g_monoSweep.Active() && !g_monoSweep.restorePending);
    assert(g_monoSweep.phase == MonoSweepPhase::Cancelled);
    assert(MonoSweepOriginalSettingsMatch());
    assert(!g_closedMenuMeasurement && profilerRestores == 1);
    auto& owner = globals::features::upscaling;
    assert(owner.settingsWrites == 1 && owner.generationWrites == 1);
    owner.state["quality"] = 1; // A newly loaded configuration owns this value now.
    assert(RelinquishMonoSweepForConfiguration());
    assert(owner.state["quality"] == 1 && owner.settingsWrites == 1);

    Reset();
    owner.refuseSettings = true;
    assert(!RelinquishMonoSweepForConfiguration());
    assert(g_monoSweep.restorePending && g_monoSweep.Active());
    assert(profilerRestores == 0);
    owner.refuseSettings = false;
    assert(RelinquishMonoSweepForConfiguration() && MonoSweepOriginalSettingsMatch());

    Reset();
    owner.refuseGeneration = true;
    assert(!RelinquishMonoSweepForConfiguration());
    assert(g_monoSweep.restorePending && owner.state == g_monoSweep.originalState);
    owner.refuseGeneration = false;
    assert(RelinquishMonoSweepForConfiguration() && MonoSweepOriginalSettingsMatch());

    Reset();
    owner.throwSettings = true;
    assert(!RelinquishMonoSweepForConfiguration());
    assert(g_monoSweep.restorePending && profilerRestores == 0);
    owner.throwSettings = false;
    assert(RelinquishMonoSweepForConfiguration());

    Reset();
    g_monoSweep.phase = MonoSweepPhase::Measuring;
    assert(!RelinquishMonoSweepForConfiguration() && owner.settingsWrites == 0);
    g_monoSweep.phase = MonoSweepPhase::Restoring;
    otherMeasurementLocked = true;
    assert(RelinquishMonoSweepForConfiguration() && g_closedMenuMeasurement);

    FeatureCostMeasurementState measured{ Window(20.0, 12.0, 8.0), Window(16.0, 9.0, 5.0) };
    const auto cost = PerformanceTuning::CalculateCostResult(measured.currentBefore, measured.comparison);
    const auto output = CostResultJson(cost);
    assert(output["frame"]["available"] && output["frame"]["reliable"]);
    assert(std::abs(output["frame"]["valueMs"].get<double>() - 4.0) < 1e-8);
    assert(std::abs(output["frame"]["costPercent"].get<double>() - 20.0) < 1e-8);
    assert(std::abs(output["gpu"]["valueMs"].get<double>() - 3.0) < 1e-8);
    assert(output["frame"]["availableBlocks"] == PerformanceTuning::kMeasurementBlockCount);
    const auto significance = BlockSignificanceJson(measured, PerformanceTuning::MetricKind::WholeFrameGpu);
    assert(significance["hasStandardError"] && significance["significant"]);
    const auto missing = CostResultJson({});
    assert(missing["frame"]["valueMs"].is_null() && missing["gpu"]["costPercent"].is_null());
    assert(BlockSignificanceJson({}, PerformanceTuning::MetricKind::WholeFrameGpu).is_null());
    std::cout << "SE sweep restoration, ownership transfer and real measurement JSON checks passed\n";
}
'''


class SEPerformanceOwnership(unittest.TestCase):
    def test_production_restoration_and_json(self):
        compiler = os.environ.get("CXX", "g++")
        if not shutil.which(compiler):
            self.skipTest(f"C++ compiler unavailable: {compiler}")
        include = Path(os.environ.get("CSX_JSON_INCLUDE", "/usr/include"))
        if not (include / "nlohmann/json.hpp").is_file():
            self.skipTest("Set CSX_JSON_INCLUDE to the nlohmann include directory")
        source = (ROOT / "src/Menu/PerformanceTuningRenderer.cpp").read_text()
        signatures = [
            "bool MonoSweepOriginalSettingsMatch()",
            "bool RestoreMonoSweepOriginalSettings()",
            "bool RelinquishMonoSweepForConfiguration()",
            "json OptionalNumber(",
            "json MetricDeltaJson(",
            "json CostResultJson(",
            "json BlockSignificanceJson(",
        ]
        with tempfile.TemporaryDirectory(prefix="se-performance-ownership-") as directory:
            fixture = Path(directory) / "fixture.cpp"
            binary = Path(directory) / "fixture"
            # The supported single-header JSON distribution contains all forward declarations.
            # Supply only an include redirect when its separate forward header is absent.
            if not (include / "nlohmann/json_fwd.hpp").is_file():
                forward = Path(directory) / "nlohmann/json_fwd.hpp"
                forward.parent.mkdir()
                forward.write_text('#include <nlohmann/json.hpp>\n')
            fixture.write_text(PRELUDE + "\n".join(extract_function(source, marker) for marker in signatures) + CHECKS)
            command = [compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-I", str(include),
                       "-I", directory, "-I", str(ROOT / "src"), str(fixture), str(ROOT / "src/Menu/PerformanceTuningJson.cpp"),
                       str(ROOT / "src/Menu/PerformanceTuningMeasurement.cpp"), "-o", str(binary)]
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                binary = binary.with_suffix(".exe")
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX",
                           f"/I{include}", f"/I{directory}", f"/I{ROOT / 'src'}", str(fixture),
                           str(ROOT / "src/Menu/PerformanceTuningJson.cpp"),
                           str(ROOT / "src/Menu/PerformanceTuningMeasurement.cpp"),
                           f"/Fo{directory}\\", f"/Fe:{binary}"]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
