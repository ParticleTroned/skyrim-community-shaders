#include "Features/FoveatedCommon.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}

#include "fov_defaults.h"

struct State
{
	std::unordered_map<std::string, bool> disabled;
	bool IsFeatureDisabled(const std::string& name) { return disabled[name]; }
};
struct Menu
{
	int dirty = 0;
	void RequestSettingsDirtyCheck() { ++dirty; }
};
struct ScreenSpaceGI
{
	struct Settings
	{
		bool Enabled = false, ExperimentalOCUEffectFoveation = false, EnableGI = false;
		float CenterFullResMaskScale = 0;
		bool EnableFoveated = ScreenSpaceGIFovDefault();
	} settings;
	bool loaded = true, recompileFlag = false;
	std::atomic<bool> queuedResetHistory{ false }, ocuEffectActive{ false };
	std::atomic<const char*> ocuEffectStatus{ "" };
	bool HasGIResources() { return true; }
	bool IsResourceProfileRestartPending() { return false; }
	bool IsRuntimeEnabled() const;
	void SetFoveationEnabled(bool enabled);
	void SetOCUEffectFoveationEnabled(bool enabled);
	void DrawOCUEffectFoveationSettings();
	void DrawFoveationSettings();
};
struct ScreenSpaceShadows
{
	struct BendSettings
	{
		unsigned Enable = 0;
		unsigned EnableFoveated = ScreenSpaceShadowsFovDefault();
	} bendSettings;
	bool loaded = true;
	bool IsRuntimeEnabled() const;
	void DrawFoveationSettings();
};
struct Upscaling
{
	struct Settings
	{
		bool foveatedVendorDispatch = false;
	} settings;
	bool loaded = true;
	struct Profile
	{
		bool available = true;
		float sharedVisibleScale = 0.8f;
	} testProfile;
	Profile GetActiveUpscalingFoveatedProfile() const
	{
		auto result = testProfile;
		result.available &= settings.foveatedVendorDispatch && SupportsFoveatedVendorDispatch(method);
		return result;
	}
	float GetActiveFoveatedSharedVisibleScale() const { return testProfile.sharedVisibleScale; }
	int method = 2, invalidations = 0;
	std::string GetShortName() const { return "Upscaling"; }
	int GetUpscaleMethod() const { return method; }
	static bool SupportsFoveatedVendorDispatch(int method) { return method == 2 || method == 3; }
	void InvalidateFrameScopedUpscalingState() { ++invalidations; }
	bool SetFoveatedUpscalingEnabled(bool enabled);
	bool IsSharedFoveatedMaskActive() const;
};
namespace globals
{
	inline State storage;
	inline Menu ui;
	inline State* state = &storage;
	inline Menu* menu = &ui;
	namespace game
	{
		inline bool isVR = false;
	}
	namespace features
	{
		inline Upscaling upscaling;
		inline ScreenSpaceGI screenSpaceGI;
		inline ScreenSpaceShadows screenSpaceShadows;
	}
}

namespace Util
{
	struct DisableGuard
	{
		explicit DisableGuard(bool disabled) { ImGui::BeginDisabled(disabled); }
		~DisableGuard() { ImGui::EndDisabled(); }
	};
	inline std::unordered_map<ImGuiID, ImVec2> controlCenters;
	bool HoverTooltipWrapper()
	{
		const auto& item = ImGui::GetCurrentContext()->LastItemData;
		controlCenters[item.ID] = item.Rect.GetCenter();
		return false;
	}
	namespace Text
	{
		void Warning(const char*) {}
	}
}
void ApplyPlatformSettingOverrides(ScreenSpaceGI::Settings&) {}
void SyncResolvedSharedMaskScale(ScreenSpaceGI::Settings& settings);
void drawSection(const char*) { ImGui::Spacing(); }

#include "fov_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void TestTransitions()
{
	auto& up = globals::features::upscaling;
	auto& gi = globals::features::screenSpaceGI;
	auto& shadows = globals::features::screenSpaceShadows;
	Require(!globals::game::isVR && gi.settings.EnableFoveated && shadows.bendSettings.EnableFoveated, "Defaults must not depend on cached VR initialization");
	REL::Module::vr = false;
	Require(!ScreenSpaceGIFovDefault() && !ScreenSpaceShadowsFovDefault(), "Flat runtimes must not default to FOV");
	REL::Module::vr = globals::game::isVR = true;
	gi.settings.EnableFoveated = false;
	shadows.bendSettings.EnableFoveated = 0;
	Require(up.SetFoveatedUpscalingEnabled(true), "VR enable rejected");
	Require(gi.settings.EnableFoveated && shadows.bendSettings.EnableFoveated, "Master enable must select both children");
	Require(!gi.settings.Enabled && !shadows.bendSettings.Enable, "Master must preserve parent effects");
	Require(gi.recompileFlag && gi.settings.CenterFullResMaskScale == 0.8f, "SSGI shader settings must be synchronized");
	Require(up.invalidations == 1 && globals::ui.dirty == 1, "Master must notify resources and dirty state");
	gi.SetFoveationEnabled(false);
	shadows.bendSettings.EnableFoveated = 0;
	gi.recompileFlag = false;
	Require(up.SetFoveatedUpscalingEnabled(true), "Repeated enable rejected");
	Require(!gi.settings.EnableFoveated && !shadows.bendSettings.EnableFoveated && !gi.recompileFlag, "Repeated enable must preserve opt-outs");
	Require(up.invalidations == 1 && globals::ui.dirty == 1, "No-op must not invalidate");
	Require(up.SetFoveatedUpscalingEnabled(false), "Disable rejected");
	Require(!gi.settings.EnableFoveated && !shadows.bendSettings.EnableFoveated, "Disable must preserve opt-outs");
	Require(up.SetFoveatedUpscalingEnabled(true), "Re-enable rejected");
	Require(gi.settings.EnableFoveated && shadows.bendSettings.EnableFoveated, "Re-enable must restore defaults");
	Require(up.SetFoveatedUpscalingEnabled(false), "Second disable rejected");
	Require(gi.settings.EnableFoveated && shadows.bendSettings.EnableFoveated, "Disable must preserve selections");
	for (int unavailable = 0; unavailable < 4; ++unavailable) {
		REL::Module::vr = globals::game::isVR = unavailable != 0;
		up.loaded = unavailable != 1;
		globals::state = unavailable == 2 ? nullptr : &globals::storage;
		up.method = unavailable == 3 ? 0 : 2;
		const int before = up.invalidations;
		Require(!up.SetFoveatedUpscalingEnabled(true), "Unavailable enable must fail");
		Require(!up.settings.foveatedVendorDispatch && gi.settings.EnableFoveated && shadows.bendSettings.EnableFoveated && up.invalidations == before, "Rejected enable must not mutate settings");
	}
	REL::Module::vr = globals::game::isVR = up.loaded = true;
	globals::state = &globals::storage;
	up.settings.foveatedVendorDispatch = true;
	Require(up.SetFoveatedUpscalingEnabled(false), "Must allow disabling after switching to None/TAA");
	up.method = 3;
	globals::storage.disabled["Upscaling"] = true;
	Require(up.SetFoveatedUpscalingEnabled(true), "Pending boot disable must not block loaded FSR");
	for (float scale : { 1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		up.testProfile.sharedVisibleScale = scale;
		Require(!up.IsSharedFoveatedMaskActive(), "Full or invalid coverage must be inactive");
	}
	up.testProfile.sharedVisibleScale = 0.8f;
}

void Click(const char* label)
{
	auto& io = ImGui::GetIO();
	ImVec2 target{};
	for (int frame = 0; frame < 5; ++frame) {
		if (frame == 1)
			io.AddMousePosEvent(target.x, target.y);
		if (frame == 2)
			io.AddMouseButtonEvent(0, true);
		if (frame == 3)
			io.AddMouseButtonEvent(0, false);
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("FOV", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		DrawScreenSpaceControls();
		target = Util::controlCenters.at(ImGui::GetID(label));
		ImGui::End();
		ImGui::Render();
	}
}

void TestUi()
{
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = io.LogFilename = nullptr;
	io.DisplaySize = { 1000, 900 };
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->SetTexID(ImTextureID{ 1 });
	auto& up = globals::features::upscaling;
	auto& gi = globals::features::screenSpaceGI;
	auto& shadows = globals::features::screenSpaceShadows;
	for (unsigned bits = 0; bits < 64; ++bits) {
		gi.loaded = shadows.loaded = (bits & 1) != 0;
		gi.settings.Enabled = (bits & 2) != 0;
		shadows.bendSettings.Enable = (bits & 2) ? 1u : 0u;
		globals::storage.disabled["ScreenSpaceGI"] = globals::storage.disabled["ScreenSpaceShadows"] = (bits & 4) != 0;
		up.testProfile.available = (bits & 8) != 0;
		up.testProfile.sharedVisibleScale = (bits & 16) ? 0.8f : 1.0f;
		up.loaded = (bits & 32) != 0;
		const bool available = (bits & 1) && (bits & 2) && (bits & 8) && (bits & 16) && (bits & 32);
		gi.settings.EnableFoveated = false;
		shadows.bendSettings.EnableFoveated = 0;
		Click("SSGI FOV");
		Click("Screen Space Shadows FOV");
		Require(gi.settings.EnableFoveated == available && (shadows.bendSettings.EnableFoveated != 0) == available, "UI availability mismatch");
		const auto status = FovSettingsStatus();
		Require(status.at("ssgiAvailable") == available && status.at("screenSpaceShadowsAvailable") == available, "DevBench availability must match clickable controls");
		Click("OCU peripheral sampling (experimental)");
		Require(gi.settings.ExperimentalOCUEffectFoveation == (gi.loaded && gi.settings.Enabled), "OCU requires SSGI, but not shared-mask upscaling");
		gi.SetOCUEffectFoveationEnabled(false);
	}
	up.loaded = gi.loaded = gi.settings.Enabled = true;
	up.testProfile = { true, 0.8f };
	gi.SetOCUEffectFoveationEnabled(true);
	gi.settings.EnableFoveated = false;
	Click("SSGI FOV");
	Require(!gi.settings.EnableFoveated && !FovSettingsStatus().at("ssgiAvailable").get<bool>(), "OCU must take priority over shared-mask SSGI");
	ImGui::DestroyContext();
}

int main()
{
	try {
		TestTransitions();
		TestUi();
		std::cout << "PASS: FOV transitions, defaults, SSGI synchronization, 64 ImGui availability combinations, DevBench status, and independent OCU controls\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
