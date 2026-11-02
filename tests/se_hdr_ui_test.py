"""Exercise production HDR UI transitions with observable widgets and output calls."""

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
#include <cassert>
#include <format>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "Utils/Finite.h"
using uint = unsigned;
using json = nlohmann::json;
#define T(key, fallback) fallback
#define TKEY(key) key
constexpr int ImGuiWindowFlags_AlwaysAutoResize = 1, ImGuiWindowFlags_NoMove = 2;
constexpr int ImGuiSliderFlags_AlwaysClamp = 1;
struct ImVec2 { float x, y; };
namespace input {
    std::string click, section, slider;
    int integer = 0, disabled = 0, popupDepth = 0, gridDepth = 0, popupRequests = 0;
    float real = 1;
    bool popupOpen = false;
    std::vector<std::string> checkboxes, texts;
    void Reset() { click.clear(); slider.clear(); checkboxes.clear(); texts.clear(); }
}
namespace ImGui {
    bool IsPopupOpen(const char*) { return input::popupOpen; }
    void OpenPopup(const char*) { input::popupOpen = true; ++input::popupRequests; }
    void CloseCurrentPopup() { input::popupOpen = false; }
    bool Button(const char* label, ImVec2) {
        if (input::disabled || input::click != label) return false;
        input::click.clear(); return true;
    }
    struct Style { ImVec2 FramePadding{4, 4}, ItemSpacing{4, 4}; };
    const Style& GetStyle() { static Style style; return style; }
    ImVec2 CalcTextSize(const char* text) { return { static_cast<float>(std::string_view(text).size()), 12 }; }
    void Separator() {} void Spacing() {} void SameLine() {}
    void TextWrapped(const char*, const char*) {}
    void TextDisabled(const char*, float) {}
    float GetWindowWidth() { return 600; }
    void SetCursorPosX(float) {}
    bool Checkbox(const char*, bool*);
}
namespace SKSE::stl {
    template<class F> struct scope_exit {
        F action;
        explicit scope_exit(F value): action(std::move(value)) {}
        ~scope_exit() { action(); }
    };
}
namespace logger { template<class... A> void info(const char*, A&&...) {} }
namespace ThemeManager::Constants { constexpr float POPUP_BUTTON_WIDTH = 100; }
namespace Util {
    float GetUIScale() { return 1; }
    void AddTooltip(const char*) {}
    struct DisableGuard {
        bool disabled;
        explicit DisableGuard(bool value): disabled(value) { input::disabled += disabled; }
        ~DisableGuard() { input::disabled -= disabled; }
    };
    struct CenteredPopupModal {
        bool open;
        CenteredPopupModal(const char*, bool* requested, int = 0): open(*requested && input::popupOpen) {
            if (open && input::click == "Close popup") {
                input::click.clear(); *requested = false; input::popupOpen = false; open = false;
            }
            input::popupDepth += open;
        }
        ~CenteredPopupModal() { input::popupDepth -= open; }
        explicit operator bool() const { return open; }
    };
    namespace Text {
        void Warning(const char* text) { input::texts.emplace_back(text); }
        void WrappedWarning(const char* text) { input::texts.emplace_back(text); }
        void Success(const char* text) { input::texts.emplace_back(text); }
    }
    namespace Widgets {
        bool Checkbox(const char* label, bool* value) {
            input::checkboxes.emplace_back(label);
            if (input::disabled || input::click != label) return false;
            input::click.clear(); *value = !*value; return true;
        }
        bool SliderInt(const char* label, int* value, int low, int high, const char*, int) {
            if (input::disabled || input::slider != label) return false;
            *value = std::clamp(input::integer, low, high); input::slider.clear(); return true;
        }
        bool SliderFloat(const char* label, float* value, float low, float high, const char*, int) {
            if (input::disabled || input::slider != label) return false;
            *value = std::clamp(input::real, low, high); input::slider.clear(); return true;
        }
    }
}
bool ImGui::Checkbox(const char* label, bool* value) { return Util::Widgets::Checkbox(label, value); }
namespace Util { CONFIRMATION_POPUP_DECLARATION }
namespace MenuUI {
    struct Section { const char *id, *title, *description, *summary; bool visible, overview; const char* stage = nullptr; };
    struct SettingsPage {
        SettingsPage(const char*, std::initializer_list<Section> sections, const char*, const char*) {
            assert(sections.size() == 3);
            for (const auto& step : sections) assert(step.visible && step.overview);
        }
        bool Is(std::string_view id) const { return input::section == id; }
    };
    struct DetailGrid {
        explicit DetailGrid(const char*) { ++input::gridDepth; }
        ~DetailGrid() { --input::gridDepth; }
        void Next() {}
    };
    void SectionHeading(const char*) {}
    void DetailText(const char* text) { input::texts.emplace_back(text); }
    void DetailNote(const char* text) { input::texts.emplace_back(text); }
}
namespace globals::features {
    struct {
        bool loaded = true, isWindowed = true, d3d12SwapChainActive = false;
        struct {
            bool hdrActive = false;
            bool IsHDRColorSpaceActive() const { return hdrActive; }
        } dx12SwapChain;
    } upscaling;
}
struct Feature { struct SettingsHeaderStatus { std::string_view label; bool needsAttention; }; };
struct HDRDisplay : Feature {
    CALIBRATION_LIMITS
    SETTINGS_MEMBERS
    Settings settings;
    std::mutex settingsMutex;
    bool isHDRMonitor = false, isHDRCapableMonitor = false, wasExclusiveFullscreen = false, requestHDREnable = false;
    bool pendingAutoDetect = false, d3d11HDRColorSpaceActive = false, bufferOutputActive = false;
    bool loaded = true;
    float cachedDisplayMaxLuminance = 800;
    int dataUpdates = 0, colorSpaceUpdates = 0;
    void UpdateHDRData() { ++dataUpdates; bufferOutputActive = IsHDROutputActive() && settings.enableHDR; }
    void UpdateSwapChainColorSpace() { ++colorSpaceUpdates; d3d11HDRColorSpaceActive = settings.enableHDR; }
    bool DetectHDR() const { return isHDRMonitor; }
    static void SanitizeSettings(Settings&);
    void LoadSettings(json&);
    void SaveSettings(json&);
    void RestoreDefaultSettings();
    SettingsHeaderStatus GetSettingsHeaderStatus() const;
    void SetHDREnabled(bool);
    void DrawSettingsEnabledControl();
    void DrawHDRWarningPopup();
    void DrawSettings();
    bool IsHDROutputActive() const;
};
'''

CHECKS = r'''
int main() {
    HDRDisplay hdr;
    assert(hdr.GetSettingsHeaderStatus().needsAttention);
    hdr.isHDRCapableMonitor = true;
    assert(hdr.GetSettingsHeaderStatus().label == "Windows HDR off");
    hdr.settings.hdrPaperWhite = 250;
    input::click = "Enabled";
    hdr.DrawSettingsEnabledControl();
    assert(input::checkboxes == std::vector<std::string>{"Enabled"});
    assert(!hdr.settings.enableHDR && hdr.requestHDREnable && hdr.dataUpdates == 0);
    input::Reset(); input::click = "Cancel";
    hdr.DrawSettings();
    assert(!hdr.settings.enableHDR && !hdr.requestHDREnable && !input::popupOpen);
    input::Reset(); input::click = "Enabled";
    hdr.DrawSettingsEnabledControl();
    input::click = "Don't ask me again";
    hdr.DrawSettings();
    assert(!hdr.settings.dontShowHDRWarning);
    input::click = "Cancel";
    hdr.DrawSettings();
    assert(!hdr.settings.dontShowHDRWarning && !hdr.settings.enableHDR);
    input::Reset(); input::click = "Enabled";
    hdr.DrawSettingsEnabledControl();
    input::click = "Force Enable HDR";
    hdr.DrawSettings();
    assert(hdr.settings.enableHDR && !hdr.requestHDREnable && hdr.dataUpdates == 1 && hdr.colorSpaceUpdates == 1);
    input::Reset(); input::click = "Enabled";
    hdr.DrawSettingsEnabledControl();
    assert(!hdr.settings.enableHDR && hdr.settings.hdrPaperWhite == 250 && hdr.colorSpaceUpdates == 2);
    hdr.settings.dontShowHDRWarning = true;
    input::click = "Enabled"; hdr.DrawSettingsEnabledControl();
    assert(hdr.settings.enableHDR && !hdr.requestHDREnable && hdr.colorSpaceUpdates == 3);
    hdr.SetHDREnabled(false); hdr.isHDRMonitor = true; hdr.settings.dontShowHDRWarning = false;
    assert(!hdr.GetSettingsHeaderStatus().needsAttention);
    input::click = "Enabled"; hdr.DrawSettingsEnabledControl();
    assert(hdr.settings.enableHDR && !hdr.requestHDREnable);

    input::Reset(); input::section = "brightness";
    hdr.settings.hdrPeakNits = 400;
    input::slider = "Paper White (nits)"; input::integer = 500; hdr.DrawSettings();
    assert(hdr.settings.hdrPaperWhite == 399 && hdr.settings.hdrPeakNits == 400);
    hdr.settings.hdrPaperWhite = 500; hdr.settings.hdrPeakNits = 800;
    input::slider = "Peak Brightness (nits)"; input::integer = 400; hdr.DrawSettings();
    assert(hdr.settings.hdrPeakNits == 501);
    hdr.settings.hdrPeakNits = 0;
    input::slider = "Paper White (nits)"; input::integer = 80; hdr.DrawSettings();
    assert(hdr.settings.hdrPaperWhite == 80 && hdr.settings.hdrPeakNits == 400);
    hdr.settings.hdrPaperWhite = 500; hdr.settings.hdrPeakNits = 800;
    input::slider = "Peak Brightness (nits)"; input::integer = 50000; hdr.DrawSettings();
    assert(hdr.settings.hdrPaperWhite == 500 && hdr.settings.hdrPeakNits == 10000);
    input::section = "interface"; input::slider = "UI Brightness Multiplier"; input::real = 3; hdr.DrawSettings();
    assert(hdr.settings.hdrUIBrightness == 3);
    hdr.SetHDREnabled(false);
    input::slider = "UI Brightness Multiplier"; input::real = 4; hdr.DrawSettings();
    assert(hdr.settings.hdrUIBrightness == 3);
    input::Reset(); input::section = "output"; hdr.wasExclusiveFullscreen = true;
    globals::features::upscaling.loaded = false;
    hdr.DrawSettings();
    assert(input::checkboxes.empty());
    assert(input::disabled == 0 && input::gridDepth == 0 && input::popupDepth == 0);

    // Dismissing the shared modal never grants a later forced-output request.
    hdr.isHDRMonitor = false; hdr.settings.dontShowHDRWarning = false;
    input::click = "Enabled"; hdr.DrawSettingsEnabledControl();
    input::click = "Don't ask me again"; hdr.DrawSettings();
    input::click = "Close popup"; hdr.DrawSettings();
    assert(!hdr.settings.dontShowHDRWarning && !hdr.settings.enableHDR);
    input::click = "Enabled"; hdr.DrawSettingsEnabledControl();
    input::click = "Don't ask me again"; hdr.DrawSettings();
    input::click = "Force Enable HDR"; hdr.DrawSettings();
    assert(hdr.settings.dontShowHDRWarning && hdr.settings.enableHDR && hdr.bufferOutputActive);

    // The configuration boundary rejects invalid calibration before any GPU update.
    for (float invalid : { -10.0f, 10.0f, std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
        json saved = { { "enableHDR", true }, { "hdrAutoDetected", true },
                       { "hdrPaperWhite", std::numeric_limits<uint>::max() },
                       { "hdrPeakNits", 0 }, { "hdrUIBrightness", invalid } };
        hdr.LoadSettings(saved);
        assert(hdr.settings.hdrPeakNits == 400 && hdr.settings.hdrPaperWhite == 399);
        assert(std::isfinite(hdr.settings.hdrUIBrightness) && hdr.settings.hdrUIBrightness >= 0.5f && hdr.settings.hdrUIBrightness <= 5);
        json roundTrip; hdr.SaveSettings(roundTrip);
        assert(roundTrip["hdrPaperWhite"] == 399 && roundTrip["hdrPeakNits"] == 400);
    }
    json unchanged; hdr.SaveSettings(unchanged);
    const int beforeReload = hdr.dataUpdates, colorsBeforeReload = hdr.colorSpaceUpdates;
    hdr.LoadSettings(unchanged);
    assert(hdr.dataUpdates == beforeReload && hdr.colorSpaceUpdates == colorsBeforeReload);
    unchanged["hdrUIBrightness"] = 2.0f;
    hdr.LoadSettings(unchanged);
    assert(hdr.dataUpdates == beforeReload + 1 && hdr.colorSpaceUpdates == colorsBeforeReload);
    hdr.isHDRMonitor = false; hdr.RestoreDefaultSettings();
    assert(!hdr.settings.enableHDR && !hdr.IsHDROutputActive() && !hdr.bufferOutputActive);
    hdr.isHDRMonitor = true; hdr.RestoreDefaultSettings();
    assert(hdr.settings.enableHDR && hdr.IsHDROutputActive() && hdr.bufferOutputActive);
    assert(hdr.settings.hdrPaperWhite == 203 && hdr.settings.hdrPeakNits == 1000 && hdr.settings.hdrUIBrightness == 1);

    // The page must distinguish the requested setting from the applied output.
    const int dataBeforeStatus = hdr.dataUpdates, colorsBeforeStatus = hdr.colorSpaceUpdates;
    for (bool d3d12 : { false, true }) {
        globals::features::upscaling.d3d12SwapChainActive = d3d12;
        for (bool requested : { false, true }) {
            for (bool active : { false, true }) {
                hdr.settings.enableHDR = requested;
                hdr.d3d11HDRColorSpaceActive = active;
                globals::features::upscaling.dx12SwapChain.hdrActive = active;
                input::Reset(); input::section = "output";
                hdr.DrawSettings();
                const std::string expected = active ?
                    (requested ? "HDR10 output is active." : "HDR output is still active after switching it off.") :
                    (requested ? "HDR is enabled in settings, but HDR output is not active." : "HDR output is off.");
                assert(std::ranges::find(input::texts, expected) != input::texts.end());
                assert(std::ranges::none_of(input::texts, [](const auto& text) {
                    return text.starts_with("Use Enabled in the header to control HDR output.");
                }));
                if (requested)
                    assert(std::ranges::none_of(input::texts, [](const auto& text) { return text.starts_with("Enable HDR in the header."); }));
            }
        }
    }
    globals::features::upscaling.d3d12SwapChainActive = false;
    assert(hdr.dataUpdates == dataBeforeStatus && hdr.colorSpaceUpdates == colorsBeforeStatus);

    // Idle menu frames may draw widgets, but must not issue output or popup requests.
    const int dataBeforeIdle = hdr.dataUpdates, colorsBeforeIdle = hdr.colorSpaceUpdates, popupsBeforeIdle = input::popupRequests;
    input::Reset();
    for (const char* section : { "overview", "output", "brightness", "interface" }) {
        input::section = section;
        for (int frame = 0; frame < 2500; ++frame) {
            hdr.DrawSettingsEnabledControl(); hdr.DrawSettings();
        }
    }
    assert(hdr.dataUpdates == dataBeforeIdle && hdr.colorSpaceUpdates == colorsBeforeIdle && input::popupRequests == popupsBeforeIdle);
    assert(input::disabled == 0 && input::gridDepth == 0 && input::popupDepth == 0);
    std::cout << "HDR confirmation/cancel/close, output updates, D3D11/D3D12 status messages, defaults, invalid calibration and 10000 idle UI frames passed\n";
}
'''


class SEHDRUI(unittest.TestCase):
    def test_production_ui(self):
        compiler = os.environ.get("CXX", "g++")
        if not shutil.which(compiler):
            self.skipTest(f"C++ compiler unavailable: {compiler}")
        include = Path(os.environ.get("CSX_JSON_INCLUDE", "/usr/include"))
        if not (include / "nlohmann/json.hpp").is_file():
            self.skipTest("Set CSX_JSON_INCLUDE to the nlohmann include directory")
        source = (ROOT / "src/Features/HDRDisplay.cpp").read_text()
        header = (ROOT / "src/Features/HDRDisplay.h").read_text()
        signatures = ["Feature::SettingsHeaderStatus HDRDisplay::GetSettingsHeaderStatus()",
                      "void HDRDisplay::SetHDREnabled(", "void HDRDisplay::DrawSettingsEnabledControl()",
                      "void HDRDisplay::DrawHDRWarningPopup()", "void HDRDisplay::DrawSettings()",
                      "void HDRDisplay::SanitizeSettings(", "void HDRDisplay::LoadSettings(",
                      "void HDRDisplay::SaveSettings(", "void HDRDisplay::RestoreDefaultSettings()",
                      "bool HDRDisplay::IsHDROutputActive()"]
        fixture_text = PRELUDE.replace("SETTINGS_MEMBERS", function(header, "struct Settings\n") + ";")
        limits_start = header.index("static constexpr uint kPaperWhiteMin")
        fixture_text = fixture_text.replace("CALIBRATION_LIMITS", header[limits_start:header.index("struct Settings", limits_start)])
        ui_header = (ROOT / "src/Utils/UI.h").read_text()
        ui_source = (ROOT / "src/Utils/UI.cpp").read_text()
        fixture_text = fixture_text.replace("CONFIRMATION_POPUP_DECLARATION", function(ui_header, "struct ConfirmationPopup\n") + ";")
        fixture_text += "namespace Util {\n" + function(ui_source, "void ConfirmationPopup::Request()") + function(ui_source, "bool ConfirmationPopup::Draw()") + "}\n"
        serialization_start = source.index("NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(")
        fixture_text += source[serialization_start:source.index(");", serialization_start) + 2]
        fixture_text += "\n".join(function(source, marker) for marker in signatures) + CHECKS
        with tempfile.TemporaryDirectory(prefix="se-hdr-ui-") as directory:
            fixture = Path(directory) / "fixture.cpp"
            binary = Path(directory) / "fixture"
            fixture.write_text(fixture_text)
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(include), "-I", str(ROOT / "src"), str(fixture), "-o", str(binary)]
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                binary = binary.with_suffix(".exe")
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", f"/I{include}", f"/I{ROOT / 'src'}", str(fixture),
                           f"/Fo{directory}\\", f"/Fe:{binary}"]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
