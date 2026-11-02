"""Exercise production SE upscaling selectors and frame-generation controls."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from extract_adaptive_balance_toggle import function

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
using uint = unsigned;
#define T(key, fallback) fallback
constexpr int ImGuiCol_Text = 0;
namespace input {
    std::string control, selection, click, open, section;
    int disabled = 0, comboDepth = 0, tooltipDepth = 0, selectables = 0, reflexDraws = 0;
    std::vector<std::string> combos, checkboxes, texts, tabs;
    bool blocked = false;
    void Reset() { control.clear(); selection.clear(); click.clear(); open.clear(); combos.clear(); checkboxes.clear(); texts.clear(); selectables = 0; }
    bool Has(const std::vector<std::string>& values, const char* value) { return std::ranges::find(values, value) != values.end(); }
}
namespace ImGui {
    void BeginDisabled() { ++input::disabled; }
    void EndDisabled() { --input::disabled; }
    void PushStyleColor(int, int) {} void PopStyleColor() {}
    template<class... A> void Text(const char* text, A&&...) { input::texts.emplace_back(text); }
    template<class... A> void TextWrapped(const char* text, A&&...) { input::texts.emplace_back(text); }
    template<class... A> void TextDisabled(const char* text, A&&...) { input::texts.emplace_back(text); }
    template<class... A> void TextColored(int, const char* text, A&&...) { input::texts.emplace_back(text); }
    void TextUnformatted(const char* text) { input::texts.emplace_back(text); }
    bool Selectable(const char* text, bool) { ++input::selectables; return !input::disabled && input::selection == text; }
    void SetItemDefaultFocus() {}
}
namespace Util {
    struct DisableGuard { bool active; explicit DisableGuard(bool value): active(value) { input::disabled += active; } ~DisableGuard() { input::disabled -= active; } };
    struct TooltipGuard {
        TooltipGuard() { ++input::tooltipDepth; }
        ~TooltipGuard() { --input::tooltipDepth; }
        explicit operator bool() const { return false; }
    };
    TooltipGuard HoverTooltipWrapper() { return {}; }
    void AddTooltip(const char*) {}
    namespace Colors { int GetWarning() { return 0; } int GetSuccess() { return 0; } int GetInfo() { return 0; } }
    namespace Text { template<class... A> void Warning(const char* text, A&&...) { input::texts.emplace_back(text); } }
    namespace Widgets {
        bool Checkbox(const char* label, bool* value) {
            input::checkboxes.emplace_back(label);
            if (input::disabled || input::click != label) return false;
            input::click.clear(); *value = !*value; return true;
        }
        bool Combo(const char* label, int* selected, const char* const items[], int count) {
            input::combos.emplace_back(label);
            if (input::disabled || input::control != label) return false;
            for (int i = 0; i < count; ++i) if (input::selection == items[i]) {
                const bool changed = *selected != i; *selected = i; return changed;
            }
            return false;
        }
        struct ComboBox {
            bool open;
            ComboBox(const char* label, const char*): open(!input::disabled && input::open == label) { input::combos.emplace_back(label); input::comboDepth += open; }
            ~ComboBox() { input::comboDepth -= open; }
            explicit operator bool() const { return open; }
        };
        bool SliderInt(const char*, int*, int, int, const char*) { return false; }
    }
}
namespace MenuUI {
    struct Section { const char *id, *title, *description, *summary; bool visible = true, overview = true; const char* stage = nullptr; };
    struct SettingsPage {
        SettingsPage(const char*, std::initializer_list<Section> sections) {
            input::tabs.clear(); for (const auto& section : sections) if (section.visible) input::tabs.emplace_back(section.title);
        }
        bool Is(std::string_view id) const { return id == input::section; }
    };
    void SectionHeading(const char*) {}
    void DetailNote(const char* text) { input::texts.emplace_back(text); }
}
namespace globals::game { struct GraphicsState { uint screenWidth = 1920, screenHeight = 1080; }; GraphicsState state; GraphicsState* graphicsState = &state; }
namespace sl { enum class DLSSGStatus { eOk }; }
struct Streamline {
    bool featureDLSS = true, featureDLSSG = false;
    static constexpr int MAX_RESOLUTION = 16384, kDLSSGDiagnosticFrameThreshold = 10;
    struct { uint maximumFramesToGenerate = 1, consecutiveMissingMarkerStartFrames = 0, consecutiveNoGeneratedPresents = 0; sl::DLSSGStatus status = sl::DLSSGStatus::eOk; } dlssgState;
};
struct FidelityFX {
    bool nvidia = true, runtime = true, autoEligible = true, featureFSR3FG = true;
    bool IsNvidiaAdapterDetected() const { return nvidia; }
    bool IsRuntimeUpscalerPresent() const { return runtime; }
    bool IsRuntimeFsr4AutoEligible() const { return autoEligible; }
    static constexpr int Fsr3Version = 3;
    static std::string GetHostFsrSdkLabel() { return "FSR3"; }
    static std::string GetRuntimeUpscalerLabel(int) { return "FSR3"; }
    std::string GetDisplayedFsrPathLabel() const { return "FSR3"; }
    bool IsRuntimeUpscalerFailureLatched() const { return false; }
    bool IsRuntimeFsr4FailureLatched() const { return false; }
    bool HasRuntimeUpscalerSupportCheckResult() const { return false; }
    bool IsRuntimeUpscalerSupportConfirmed() const { return true; }
};
bool IsRenderDocUpscalingBlocked() { return input::blocked; }
const char* GetRenderDocUpscalingBlockReason() { return "RenderDoc is active"; }
struct Upscaling {
    UPSCALING_ENUM
    UPSCALING_CONSTANTS
    QUALITY_SCALE
    struct Settings {
        uint upscaleMethod = 3, upscaleMethodNoDLSS = 2, qualityMode = 3, dlssPreset = 1;
        bool fsr4RuntimeEnable = false;
        uint frameGenerationMode = 0, frameGenerationForceEnable = 0, frameLimitMode = 0, dlssgFramesToGenerate = 1;
        bool frameGenerationAllowInMenus = false, preferFSRFrameGeneration = false;
    } settings;
    FidelityFX fidelityFX;
    Streamline streamline, streamlineDX12;
    bool isWindowed = true, lowRefreshRate = false, fidelityFXMissing = false, dx12Active = false, dlssgActive = false;
    float refreshRate = 144;
    UpscaleMethod GetUpscaleMethod() const { return input::blocked ? UpscaleMethod::kNONE : static_cast<UpscaleMethod>(streamline.featureDLSS ? settings.upscaleMethod : settings.upscaleMethodNoDLSS); }
    struct Size { uint x = 1920, y = 1080; };
    struct Resolution { Size engineRenderSize, finalOutputSize; };
    Resolution GetRuntimeResolutionPlan() const { return {}; }
    bool IsFrameGenerationDx12PathActive() const { return dx12Active; }
    bool UsesDLSSGFrameGeneration() const { return dx12Active && dlssgActive; }
    bool HasFrameGenModule() const;
    void DrawSettings();
    void DrawUpscalingModeSettings(bool, bool);
    void DrawFrameGenerationSettings(bool);
    void DrawFsrTemporalTuningSettings(bool) {} void DrawUpscalingLookSettings() {} void DrawUpscalingDiagnostics() {}
    void DrawReflexSettings(bool) { ++input::reflexDraws; }
};
'''

CHECKS = r'''
int main() {
    Upscaling upscaling;
    upscaling.settings.upscaleMethod = static_cast<uint>(Upscaling::UpscaleMethod::kDLSS);
    upscaling.DrawUpscalingModeSettings(true, true);
    assert(input::combos == std::vector<std::string>({ "Method", "Upscale Preset", "DLSS Profile" }));
    for (uint mode = 0; mode <= Upscaling::kQualityModeMaxIndex; ++mode) {
        input::Reset(); input::open = "Upscale Preset";
        input::selection = std::format("{} ( {:.2f}x )", GetQualityModeName(mode, true), Upscaling::GetQualityModeResolutionScale(mode));
        upscaling.DrawUpscalingModeSettings(true, true);
        assert(upscaling.settings.qualityMode == mode);
        assert(input::selectables == 7 && input::comboDepth == 0 && input::tooltipDepth == 0);
    }
    for (uint preset : kDLSSProfileDisplayOrder) {
        input::Reset(); input::control = "DLSS Profile"; input::selection = GetDLSSPresetLabel(preset);
        upscaling.DrawUpscalingModeSettings(true, true);
        assert(upscaling.settings.dlssPreset == preset && upscaling.settings.qualityMode == 6);
    }
    for (const char* method : { "None", "TAA", "AMD FSR3", "AMD FSR (Automatic)", "NVIDIA DLSS" }) {
        input::Reset(); input::control = "Method"; input::selection = method;
        upscaling.DrawUpscalingModeSettings(true, true);
        const auto selected = upscaling.GetUpscaleMethod();
        if (input::selection == "None") assert(selected == Upscaling::UpscaleMethod::kNONE);
        else if (input::selection == "TAA") assert(selected == Upscaling::UpscaleMethod::kTAA);
        else if (input::selection == "NVIDIA DLSS") assert(selected == Upscaling::UpscaleMethod::kDLSS);
        else assert(selected == Upscaling::UpscaleMethod::kFSR && upscaling.settings.fsr4RuntimeEnable == (input::selection == "AMD FSR (Automatic)"));
    }
    input::Reset(); input::blocked = true; input::control = "Method"; input::selection = "TAA";
    const uint savedMethod = upscaling.settings.upscaleMethod;
    upscaling.DrawUpscalingModeSettings(true, true);
    assert(upscaling.settings.upscaleMethod == savedMethod && input::disabled == 0);
    input::blocked = false;
    upscaling.streamline.featureDLSS = false; upscaling.fidelityFX.nvidia = false; upscaling.fidelityFX.autoEligible = false;
    input::Reset(); input::control = "Method"; input::selection = "AMD FSR3";
    upscaling.DrawUpscalingModeSettings(true, true);
    assert(upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kFSR);
    assert(!input::Has(input::combos, "DLSS Profile"));

    // NVIDIA users can choose FSR before any DLSS-G device has been created.
    upscaling.fidelityFX.nvidia = true;
    input::Reset(); input::section = "motion"; input::click = "Prefer AMD FSR Frame Generation";
    upscaling.DrawSettings();
    assert(input::Has(input::tabs, "Frame Generation") && !input::Has(input::tabs, "Motion"));
    assert(upscaling.settings.preferFSRFrameGeneration && !upscaling.settings.frameGenerationMode && !upscaling.streamlineDX12.featureDLSSG);
    assert(input::checkboxes[0] == "Frame Generation" && input::checkboxes[1] == "Prefer AMD FSR Frame Generation");
    assert(input::reflexDraws == 1);
    input::Reset(); input::click = "Frame Generation"; upscaling.DrawFrameGenerationSettings(true);
    assert(upscaling.settings.frameGenerationMode == 1);
    for (bool blocked : { false, true }) {
        upscaling.settings.preferFSRFrameGeneration = false;
        upscaling.fidelityFX.featureFSR3FG = blocked; input::blocked = blocked;
        input::Reset(); input::click = "Prefer AMD FSR Frame Generation";
        upscaling.DrawFrameGenerationSettings(true);
        assert(input::Has(input::checkboxes, "Prefer AMD FSR Frame Generation"));
        assert(!upscaling.settings.preferFSRFrameGeneration && input::disabled == 0);
    }
    input::blocked = false; upscaling.fidelityFX.featureFSR3FG = true; upscaling.fidelityFX.nvidia = false;
    input::Reset(); upscaling.DrawFrameGenerationSettings(true);
    assert(!input::Has(input::checkboxes, "Prefer AMD FSR Frame Generation"));

    upscaling.streamline.featureDLSS = true;
    input::Reset();
    for (int frame = 0; frame < 1000; ++frame) upscaling.DrawUpscalingModeSettings(false, true);
    assert(input::selectables == 0 && input::comboDepth == 0 && input::disabled == 0);
    std::cout << "Upscaling dropdown mappings, blocked methods, pre-restart NVIDIA FSR preference, tab routing and 1000 closed-dropdown frames passed\n";
}
'''


class SEUpscalingUI(unittest.TestCase):
    def test_production_ui(self):
        compiler = os.environ.get("CXX", "g++")
        if not shutil.which(compiler):
            self.skipTest(f"C++ compiler unavailable: {compiler}")
        source = (ROOT / "src/Features/Upscaling.cpp").read_text()
        header = (ROOT / "src/Features/Upscaling.h").read_text()
        fixture_text = PRELUDE.replace("UPSCALING_ENUM", function(header, "enum class UpscaleMethod") + ";")
        constants_start = header.index("static constexpr uint32_t kQualityModeMaxIndex")
        fixture_text = fixture_text.replace("UPSCALING_CONSTANTS", header[constants_start:header.index("static constexpr float GetQualityModeResolutionScale", constants_start)])
        fixture_text = fixture_text.replace("QUALITY_SCALE", function(header, "static constexpr float GetQualityModeResolutionScale"))
        order_start = source.index("constexpr std::array kDLSSProfileDisplayOrder")
        fixture_text += source[order_start:source.index("};", order_start) + 2]
        for signature in ("uint ClampQualityModeUInt(", "const char* GetQualityModeName(", "const char* GetDLSSPresetLabel(",
                          "void DrawDLSSPresetTooltip(", "void DrawFrameGenerationEnabledToggle(", "void DrawFrameGenerationForceEnableToggle(",
                          "bool Upscaling::HasFrameGenModule()", "void Upscaling::DrawSettings()",
                          "void Upscaling::DrawUpscalingModeSettings(", "void Upscaling::DrawFrameGenerationSettings("):
            fixture_text += function(source, signature) + "\n"
        fixture_text += CHECKS
        with tempfile.TemporaryDirectory(prefix="se-upscaling-ui-") as directory:
            fixture = Path(directory) / "fixture.cpp"
            binary = Path(directory) / "fixture"
            fixture.write_text(fixture_text)
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(fixture), "-o", str(binary)]
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                binary = binary.with_suffix(".exe")
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", str(fixture), f"/Fo{directory}\\", f"/Fe:{binary}"]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
