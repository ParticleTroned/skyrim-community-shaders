#include "ScreenSpaceGI.h"

#include <DirectXTex.h>
#include <cmath>

#include "Deferred.h"
#include "FoveatedCommon.h"
#include "State.h"
#include "Upscaling.h"
#include "Util.h"
#include "Utils/D3D.h"
#include "Utils/UI.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceGI::Settings,
	Enabled,
	EnableGI,
	EnableExperimentalSpecularGI,
	EnableVanillaSSAO,
	AOInteriorsOnly,
	ILInteriorsOnly,
	NumSlices,
	NumSteps,
	EnableAdaptiveSampling,
	ResolutionMode,
	ResourceProfile,
	VRCullDistance,
	EnableFoveated,
	EnableStereoSync,
	MinScreenRadius,
	AORadius,
	GIRadius,
	Thickness,
	DepthFadeRange,
	GISaturation,
	GIDistanceCompensation,
	AOPower,
	GIStrength,
	EnableTemporalDenoiser,
	EnableBlur,
	DepthDisocclusion,
	NormalDisocclusion,
	MaxAccumFrames,
	BlurRadius,
	DistanceNormalisation)

namespace
{
	constexpr float kVRCullDistanceMin = 0.0f;
	constexpr float kVRCullDistanceMax = 20480.0f;
	constexpr int kResolutionModeMin = 0;
	constexpr int kResolutionModeMax = 2;

	float ClampVRCullDistance(float a_distance)
	{
		return std::clamp(a_distance, kVRCullDistanceMin, kVRCullDistanceMax);
	}

	int ClampResolutionMode(int a_resolutionMode)
	{
		return std::clamp(a_resolutionMode, kResolutionModeMin, kResolutionModeMax);
	}

	int ClampResourceProfile(int a_profile)
	{
		return std::clamp(a_profile, ScreenSpaceGI::kResourceProfileFullGI, ScreenSpaceGI::kResourceProfileAOOnly);
	}

	bool IsSharedFoveatedMaskActive()
	{
		if (!REL::Module::IsVR())
			return false;

		auto& upscaling = globals::features::upscaling;
		if (!upscaling.loaded)
			return false;

		const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
		return profile.available && FoveatedCommon::IsActiveCoverage(profile.coverageScale);
	}

	bool IsRuntimeFoveatedActive(const ScreenSpaceGI::Settings& a_settings)
	{
		return REL::Module::IsVR() && a_settings.EnableFoveated && IsSharedFoveatedMaskActive();
	}

	uint32_t QuantizeCenterOffset(float a_value)
	{
		return static_cast<uint32_t>(std::lround((a_value + 1.0f) * 10000.0f));
	}

	float GetUpscalingActiveCenterMaskScale()
	{
		return globals::features::upscaling.GetActiveFoveatedCenterScale();
	}

	float GetSharedUpscalingCenterMaskHorizontalScale()
	{
		return globals::features::upscaling.GetActiveFoveatedCenterHorizontalScale();
	}

	float ResolveFoveatedCenterMaskScale(const ScreenSpaceGI::Settings& a_settings)
	{
		if (!IsRuntimeFoveatedActive(a_settings))
			return 0.0f;

		return GetUpscalingActiveCenterMaskScale();
	}

	std::array<float2, 2> GetSharedUpscalingMaskOffsetsForSsgi()
	{
		return globals::features::upscaling.GetActiveResolvedFoveatedMaskCenterOffsets();
	}

	void SyncResolvedCenterMaskScale(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.CenterFullResMaskScale = ResolveFoveatedCenterMaskScale(a_settings);
	}

	void ResetVRSpecificSettings(ScreenSpaceGI::Settings& a_settings)
	{
		const ScreenSpaceGI::Settings defaults{};
		a_settings.VRCullDistance = defaults.VRCullDistance;
		a_settings.CenterFullResMaskScale = defaults.CenterFullResMaskScale;
		a_settings.EnableFoveated = defaults.EnableFoveated;
		a_settings.EnableStereoSync = defaults.EnableStereoSync;
	}

	void StripVRSpecificSettings(json& o_json)
	{
		o_json.erase("VRCullDistance");
		o_json.erase("CenterFullResMaskScale");
		o_json.erase("EnableFoveated");
		o_json.erase("EnableStereoSync");
	}

	void DisableGIEffects(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.EnableGI = false;
		a_settings.EnableExperimentalSpecularGI = false;
	}

	bool ApproximatelyEqual(float a_lhs, float a_rhs)
	{
		return std::abs(a_lhs - a_rhs) <= 0.001f;
	}

	void ApplyAOOnlyPreset(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.NumSlices = 3;
		a_settings.NumSteps = 6;
		a_settings.ResolutionMode = 0;
		a_settings.CenterFullResMaskScale = 0.0f;
		a_settings.VRCullDistance = 1500.0f;
		a_settings.AOPower = 1.8f;
		a_settings.EnableBlur = false;
		a_settings.EnableTemporalDenoiser = false;
		a_settings.ResourceProfile = ScreenSpaceGI::kResourceProfileAOOnly;
		DisableGIEffects(a_settings);
	}

	void ApplyAOGIPreset(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.NumSlices = 4;
		a_settings.NumSteps = 6;
		a_settings.ResolutionMode = 0;
		a_settings.CenterFullResMaskScale = 0.0f;
		a_settings.VRCullDistance = 1500.0f;
		a_settings.AOPower = 1.8f;
		a_settings.ResourceProfile = ScreenSpaceGI::kResourceProfileFullGI;
		a_settings.EnableGI = true;
		a_settings.EnableExperimentalSpecularGI = false;
		a_settings.EnableBlur = true;
		a_settings.EnableTemporalDenoiser = false;
	}

	void ApplyReferencePreset(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.NumSlices = 8;
		a_settings.NumSteps = 10;
		a_settings.ResolutionMode = 0;
		a_settings.CenterFullResMaskScale = 0.0f;
		a_settings.EnableBlur = true;
		a_settings.ResourceProfile = ScreenSpaceGI::kResourceProfileFullGI;
		a_settings.EnableGI = true;
	}

	bool IsAOOnlyPreset(const ScreenSpaceGI::Settings& a_settings, bool a_isVR)
	{
		return a_settings.ResolutionMode == 0 &&
		       a_settings.NumSlices == 3 &&
		       a_settings.NumSteps == 6 &&
		       a_settings.ResourceProfile == ScreenSpaceGI::kResourceProfileAOOnly &&
		       !a_settings.EnableGI &&
		       !a_settings.EnableBlur &&
		       !a_settings.EnableTemporalDenoiser &&
		       ApproximatelyEqual(a_settings.AOPower, 1.8f) &&
		       (!a_isVR || ApproximatelyEqual(a_settings.VRCullDistance, 1500.0f));
	}

	bool IsAOGIPreset(const ScreenSpaceGI::Settings& a_settings, bool a_isVR)
	{
		return a_settings.ResolutionMode == 0 &&
		       a_settings.NumSlices == 4 &&
		       a_settings.NumSteps == 6 &&
		       a_settings.ResourceProfile == ScreenSpaceGI::kResourceProfileFullGI &&
		       a_settings.EnableGI &&
		       !a_settings.EnableExperimentalSpecularGI &&
		       a_settings.EnableBlur &&
		       !a_settings.EnableTemporalDenoiser &&
		       ApproximatelyEqual(a_settings.AOPower, 1.8f) &&
		       (!a_isVR || ApproximatelyEqual(a_settings.VRCullDistance, 1500.0f));
	}

	bool IsReferencePreset(const ScreenSpaceGI::Settings& a_settings)
	{
		return a_settings.ResolutionMode == 0 &&
		       a_settings.NumSlices == 8 &&
		       a_settings.NumSteps == 10 &&
		       a_settings.ResourceProfile == ScreenSpaceGI::kResourceProfileFullGI &&
		       a_settings.EnableGI &&
		       a_settings.EnableBlur;
	}

	void ApplyPlatformSettingOverrides(ScreenSpaceGI::Settings& a_settings)
	{
		a_settings.ResolutionMode = ClampResolutionMode(a_settings.ResolutionMode);
		a_settings.ResourceProfile = ClampResourceProfile(a_settings.ResourceProfile);
		a_settings.VRCullDistance = ClampVRCullDistance(a_settings.VRCullDistance);
		if (!REL::Module::IsVR()) {
			a_settings.EnableFoveated = false;
			a_settings.CenterFullResMaskScale = 0.0f;
		} else {
			a_settings.EnableVanillaSSAO = false;
		}
		if (IsRuntimeFoveatedActive(a_settings)) {
			a_settings.CenterFullResMaskScale = GetUpscalingActiveCenterMaskScale();
		} else {
			a_settings.CenterFullResMaskScale = 0.0f;
		}
		if (a_settings.ResourceProfile == ScreenSpaceGI::kResourceProfileAOOnly)
			DisableGIEffects(a_settings);
	}

	float2 GetHardenedSsgiFrameDim(float2 a_renderTexSize)
	{
		float2 frameDim = Util::ConvertToDynamic(a_renderTexSize);
		frameDim = { floor(frameDim.x), floor(frameDim.y) };

		auto* depthSRV = Util::GetCurrentSceneDepthSRV();
		uint32_t depthWidth = 0;
		uint32_t depthHeight = 0;
		if (!Util::TryGetDepthSrvDimensions(depthSRV, depthWidth, depthHeight))
			return { std::max(1.0f, frameDim.x), std::max(1.0f, frameDim.y) };

		float scaleX = frameDim.x / a_renderTexSize.x;  // runtime ratio fallback
		float scaleY = frameDim.y / static_cast<float>(depthHeight);
		scaleY = std::clamp(scaleY, 0.25f, 2.0f);

		if (!REL::Module::IsVR()) {
			scaleX = frameDim.x / static_cast<float>(depthWidth);
		} else {
			const float perEyeFrameWidth = frameDim.x * 0.5f;
			const float combinedX = (perEyeFrameWidth * 2.0f) / static_cast<float>(depthWidth);
			const float perEyeX = perEyeFrameWidth / static_cast<float>(depthWidth);

			const int viewportWidthPerEye = static_cast<int>(std::floor(a_renderTexSize.x * 0.5f));
			switch (Util::DetectVRDepthLayout(depthWidth, viewportWidthPerEye)) {
			case Util::VRDepthLayout::CombinedStereo:
				scaleX = combinedX;
				break;
			case Util::VRDepthLayout::PerEye:
				scaleX = perEyeX;
				break;
			case Util::VRDepthLayout::Unknown:
			default:
				scaleX = std::abs(combinedX - scaleX) <= std::abs(perEyeX - scaleX) ? combinedX : perEyeX;
				break;
			}
		}

		scaleX = std::clamp(scaleX, 0.25f, 2.0f);

		float2 hardenedFrameDim = {
			floor(a_renderTexSize.x * scaleX),
			floor(a_renderTexSize.y * scaleY)
		};
		hardenedFrameDim.x = std::max(1.0f, hardenedFrameDim.x);
		hardenedFrameDim.y = std::max(1.0f, hardenedFrameDim.y);
		return hardenedFrameDim;
	}
}

////////////////////////////////////////////////////////////////////////////////////

void ScreenSpaceGI::RestoreDefaultSettings()
{
	settings = {};
	ApplyPlatformSettingOverrides(settings);
	recompileFlag = true;
}

void ScreenSpaceGI::DrawSettings()
{
	ApplyPlatformSettingOverrides(settings);
	SyncResolvedCenterMaskScale(settings);
	static bool showAdvanced;
	const bool isVR = REL::Module::IsVR();

	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	auto drawCenteredSeparatorText = [](const char* a_label) {
		ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f, 0.5f));
		ImGui::SeparatorText(a_label);
		ImGui::PopStyleVar();
	};

	///////////////////////////////
	ImGui::Checkbox("Enable", &settings.Enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable Screen Space Global Illumination. When disabled, all other settings are ignored.");
	}

	ImGui::SameLine();
	{
		auto advancedGuard = Util::DisableGuard(!settings.Enabled);
		ImGui::Checkbox("Advanced Options", &showAdvanced);
	}

	if (!isVR) {
		ImGui::SameLine();
		auto ssaoToggleGuard = Util::DisableGuard(!settings.Enabled);
		ImGui::Checkbox("Vanilla SSAO", &settings.EnableVanillaSSAO);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enable Skyrim's built-in SSAO. Usually disabled when using SSGI to avoid double-darkening.");
		}
	}

	///////////////////////////////
	drawCenteredSeparatorText("Presets");
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
	ImGui::TextWrapped("These presets keep SSGI baselines close to the regular quality and resource controls.");
	ImGui::PopStyleColor();

	{
		auto presetsAndQualityGuard = Util::DisableGuard(!settings.Enabled);
		auto drawThemePresetButton = [&](const char* a_label, bool a_active, const ImVec2& a_size) {
			[[maybe_unused]] auto style = Util::PresetButtonStyle(a_active);
			return ImGui::Button(a_label, a_size);
		};

		if (ImGui::BeginTable("Presets", 4, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("PresetAO", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("PresetAOGI", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("PresetReference", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("PresetUser", ImGuiTableColumnFlags_WidthStretch, 1.0f);

			ImGui::TableNextColumn();
			const bool aoOnlyActive = IsAOOnlyPreset(settings, isVR);
			const bool aoGiActive = IsAOGIPreset(settings, isVR);
			const bool referenceActive = IsReferencePreset(settings);
			const bool userActive = !aoOnlyActive && !aoGiActive && !referenceActive;
			if (drawThemePresetButton("AO only", aoOnlyActive, { -1, 0 })) {
				ApplyAOOnlyPreset(settings);
				recompileFlag = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Full Res, no GI.");

			ImGui::TableNextColumn();
			if (drawThemePresetButton("AO + GI", aoGiActive, { -1, 0 })) {
				ApplyAOGIPreset(settings);
				recompileFlag = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Lighter AO + GI baseline: Full Res, GI resources, 4 slices and 6 steps.");

			ImGui::TableNextColumn();
			if (drawThemePresetButton("Reference", referenceActive, { -1, 0 })) {
				ApplyReferencePreset(settings);
				recompileFlag = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("High-quality baseline: Full Res with GI and blur enabled, 8 slices and 10 steps.");

			ImGui::TableNextColumn();
			if (drawThemePresetButton("User", userActive, { -1, 0 })) {
				ApplyAOGIPreset(settings);
				recompileFlag = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Copies the AO + GI preset as a starting point.");
				ImGui::TextUnformatted("Custom settings are shown as User and remain unrestricted.");
			}

			ImGui::EndTable();
		}

		///////////////////////////////
		drawCenteredSeparatorText("SSGI Effects & Resources");

		const int previousResourceProfile = settings.ResourceProfile;
		if (ImGui::BeginTable("SSGIEffectsResources", 3, ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				auto effectModeGuard = Util::DisableGuard(!settings.Enabled);
				if (ImGui::RadioButton("AO-only", !settings.EnableGI)) {
					DisableGIEffects(settings);
					recompileFlag = true;
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("AO-only mode disables GI/IL rendering.");
			}

			ImGui::TableNextColumn();
			{
				auto resourceProfileGuard = Util::DisableGuard(!settings.Enabled);
				if (ImGui::RadioButton("AO-only Resources", settings.ResourceProfile == kResourceProfileAOOnly))
					settings.ResourceProfile = kResourceProfileAOOnly;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Keeps all AO modes available but does not allocate IL/GI/specular buffers.");
			}

			ImGui::TableNextColumn();
			{
				auto aoInteriorsGuard = Util::DisableGuard(!settings.Enabled);
				ImGui::Checkbox("AO Interiors Only", &settings.AOInteriorsOnly);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Run AO only in interiors to improve exterior performance.");
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				auto effectModeGuard = Util::DisableGuard(!settings.Enabled);
				if (ImGui::RadioButton("AO + GI", settings.EnableGI)) {
					settings.ResourceProfile = kResourceProfileFullGI;
					settings.EnableGI = true;
					recompileFlag = true;
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("AO + GI enables indirect lighting for global illumination.");
			}

			ImGui::TableNextColumn();
			{
				auto resourceProfileGuard = Util::DisableGuard(!settings.Enabled);
				if (ImGui::RadioButton("AO + GI Resources", settings.ResourceProfile == kResourceProfileFullGI))
					settings.ResourceProfile = kResourceProfileFullGI;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Keeps IL/specular buffers resident so GI can be toggled at runtime.");
			}

			ImGui::TableNextColumn();
			{
				auto ilInteriorsGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
				ImGui::Checkbox("GI Interiors Only", &settings.ILInteriorsOnly);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Run indirect lighting only in interiors to improve exterior performance.");
			}

			ImGui::EndTable();
		}

		if (showAdvanced) {
			auto hqSpecGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
			recompileFlag |= ImGui::Checkbox("(Experimental) HQ Specular IL", &settings.EnableExperimentalSpecularGI);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("An experimental specular GI that is more accurate but requires more samples. Won't be blurred.");
		}

		settings.ResourceProfile = ClampResourceProfile(settings.ResourceProfile);
		if (settings.ResourceProfile != previousResourceProfile) {
			if (settings.ResourceProfile == kResourceProfileAOOnly)
				DisableGIEffects(settings);
			recompileFlag = true;
		}
		if (settings.EnableGI && !HasGIResources())
			ImGui::TextColored({ 1.0f, 0.75f, 0.25f, 1.0f }, "Full GI resources are not allocated. Restart required to allocate resources and compile GI shaders.");
		if (IsResourceProfileRestartPending()) {
			ImGui::TextColored({ 1.0f, 0.75f, 0.25f, 1.0f }, "Resource profile changes require restart to allocate/free VRAM and recompile SSGI shaders.");
		}

		drawCenteredSeparatorText("Quality/Performance");

		if (isVR) {
			ImGui::SliderFloat("AO/IL Cull Distance", &settings.VRCullDistance, kVRCullDistanceMin, kVRCullDistanceMax, "%.0f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("0 disables. Lower values improve performance but reduce distant AO/IL.");
			}
			settings.VRCullDistance = ClampVRCullDistance(settings.VRCullDistance);
		}

		if (showAdvanced) {
			ImGui::SliderInt("Slices", (int*)&settings.NumSlices, 1, 10);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"How many directions do the samples take.\n"
					"Controls noise.");

			ImGui::SliderInt("Steps Per Slice", (int*)&settings.NumSteps, 1, 20);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"How many samples does it take in one direction.\n"
					"Controls accuracy of lighting, and noise when effect radius is large.");
		}

		recompileFlag |= ImGui::Checkbox("Adaptive Sampling", &settings.EnableAdaptiveSampling);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reduces AO sample count in far distance and low-variance regions to improve performance.");
		}

		const int previousResolutionMode = settings.ResolutionMode;
		settings.ResolutionMode = ClampResolutionMode(settings.ResolutionMode);

		bool clickedFullRes = false;
		bool clickedHalfRes = false;
		bool clickedQuarterRes = false;
		if (ImGui::BeginTable("SSGIResolutionMode", 3, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("FullRes", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("HalfRes", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("QuarterRes", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			clickedFullRes = ImGui::RadioButton("Full Res", &settings.ResolutionMode, 0);
			ImGui::TableNextColumn();
			clickedHalfRes = ImGui::RadioButton("Half Res", &settings.ResolutionMode, 1);
			ImGui::TableNextColumn();
			clickedQuarterRes = ImGui::RadioButton("Quarter Res", &settings.ResolutionMode, 2);
			ImGui::EndTable();
		}

		settings.ResolutionMode = ClampResolutionMode(settings.ResolutionMode);
		if (clickedFullRes || clickedHalfRes || clickedQuarterRes) {
			settings.CenterFullResMaskScale = 0.0f;  // Pure Full/Half/Quarter.
		}
		recompileFlag |= (settings.ResolutionMode != previousResolutionMode);
	}

	///////////////////////////////
	drawCenteredSeparatorText("Visual");

	{
		auto visualGuard = Util::DisableGuard(!settings.Enabled);

		ImGui::SliderFloat("AO Power", &settings.AOPower, 0.f, 6.f, "%.2f");

		{
			auto ilGuard = Util::DisableGuard(!settings.EnableGI);
			ImGui::SliderFloat("IL Source Brightness", &settings.GIStrength, 0.f, 6.f, "%.2f");
		}

		ImGui::Separator();

		ImGui::SliderFloat("AO radius", &settings.AORadius, 10.f, 1024.0f, "%.1f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			std::vector<std::string> tooltipLines = {
				"A smaller radius produces tighter AO.",
				Util::Units::FormatDistance(settings.AORadius)
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}

		{
			auto ilRadiusGuard = Util::DisableGuard(!settings.EnableGI);

			ImGui::SliderFloat("IL radius", &settings.GIRadius, 10.f, 1024.0f, "%.1f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					"A larger radius produces wider IL.",
					Util::Units::FormatDistance(settings.GIRadius)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}
		}

		if (showAdvanced) {
			ImGui::SliderFloat("Min Screen Radius", &settings.MinScreenRadius, 0.f, 0.05f, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"The minimum screen-space effect radius as proportion of display width, to prevent far field AO being too small.");
		}

		ImGui::SliderFloat2("Depth Fade Range", &settings.DepthFadeRange.x, 1e4, 5e4, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			std::vector<std::string> tooltipLines = {
				"Distance range where depth-based effects fade out.",
				"Near: " + Util::Units::FormatDistance(settings.DepthFadeRange.x),
				"Far: " + Util::Units::FormatDistance(settings.DepthFadeRange.y)
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}

		if (showAdvanced) {
			ImGui::Separator();

			ImGui::SliderFloat("Thickness", &settings.Thickness, 0.f, 128.0f, "%.1f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					"How thick the occluders are. Only affects AO.",
					Util::Units::FormatDistance(settings.Thickness)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}
		}
	}

	///////////////////////////////
	drawCenteredSeparatorText("Visual - IL");

	{
		auto visualILGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);

		if (showAdvanced) {
			ImGui::SliderFloat("IL Distance Compensation", &settings.GIDistanceCompensation, -5.0f, 5.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Brighten/Dimming further radiance samples.");

			ImGui::Separator();
		}

		Util::PercentageSlider("IL Saturation", &settings.GISaturation);
	}

	///////////////////////////////
	drawCenteredSeparatorText("Denoising");

	{
		auto denoiseGuard = Util::DisableGuard(!settings.Enabled);

		if (ImGui::BeginTable("denoisers", 2)) {
			ImGui::TableNextColumn();
			recompileFlag |= ImGui::Checkbox("Temporal Denoiser", &settings.EnableTemporalDenoiser);

			ImGui::TableNextColumn();
			ImGui::Checkbox("Blur", &settings.EnableBlur);

			ImGui::EndTable();
		}

		if (showAdvanced) {
			ImGui::Separator();

			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				ImGui::SliderInt("Max Frame Accumulation", (int*)&settings.MaxAccumFrames, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("How many past frames to accumulate results with. Higher values are less noisy but potentially cause ghosting.");
			}

			ImGui::Separator();

			{
				auto disocclusionGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser && !settings.EnableGI);

				Util::PercentageSlider("Movement Disocclusion", &settings.DepthDisocclusion, 0.f, 20.f);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(
						"If a pixel has moved too far from the last frame, its radiance will not be carried to this frame.\n"
						"Lower values are stricter.");

				ImGui::Separator();
			}

			{
				auto blurGuard = Util::DisableGuard(!settings.EnableBlur);
				ImGui::SliderFloat("Blur Radius", &settings.BlurRadius, 0.f, 30.f, "%.1f px");

				ImGui::SliderFloat("Geometry Weight", &settings.DistanceNormalisation, 0.f, 5.f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(
						"Higher value makes the blur more sensitive to differences in geometry.");
			}
		}
	}

	///////////////////////////////
	drawCenteredSeparatorText("Debug");

	if (ImGui::TreeNode("Buffer Viewer")) {
		static float debugRescale = .3f;
		ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

		BUFFER_VIEWER_NODE(texNoise, debugRescale)
		BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
		BUFFER_VIEWER_NODE(texPrevGeo, debugRescale)
		BUFFER_VIEWER_NODE(texRadiance, debugRescale)
		BUFFER_VIEWER_NODE(texAo[0], debugRescale)
		BUFFER_VIEWER_NODE(texAo[1], debugRescale)
		BUFFER_VIEWER_NODE(texIlY[0], debugRescale)
		BUFFER_VIEWER_NODE(texIlY[1], debugRescale)
		BUFFER_VIEWER_NODE(texIlCoCg[0], debugRescale)
		BUFFER_VIEWER_NODE(texIlCoCg[1], debugRescale)

		ImGui::TreePop();
	}
}

void ScreenSpaceGI::DrawFoveationSettings()
{
	if (!REL::Module::IsVR()) {
		ImGui::TextDisabled("SSGI FOV is available only in VR.");
		return;
	}

	ApplyPlatformSettingOverrides(settings);
	SyncResolvedCenterMaskScale(settings);
	const bool featureRuntimeActive = loaded && settings.Enabled;
	const auto profile = globals::features::upscaling.GetActiveUpscalingFoveatedProfile();
	const bool foveatedAvailable = profile.available && FoveatedCommon::IsActiveCoverage(profile.coverageScale);
	bool foveatedEnabled = settings.EnableFoveated;
	{
		auto foveatedGuard = Util::DisableGuard(!featureRuntimeActive || !foveatedAvailable);
		Util::BlueFrameStyleWrapper accentFrameStyle(true);
		if (ImGui::Checkbox("SSGI FOV", &foveatedEnabled)) {
			settings.EnableFoveated = foveatedEnabled;
			if (settings.EnableFoveated) {
				settings.CenterFullResMaskScale = GetUpscalingActiveCenterMaskScale();
			} else {
				settings.CenterFullResMaskScale = 0.0f;
			}
			recompileFlag = true;
		}
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Uses the active upscaling FOV mask for Screen Space GI.");
		ImGui::TextUnformatted("When enabled, SSGI is computed only inside the mask; outside receives no SSGI.");
		ImGui::TextUnformatted("AO-only mode; IL/GI and denoisers are disabled while active.");
		if (!loaded)
			ImGui::TextUnformatted("Requires Screen Space GI.");
		else if (!settings.Enabled)
			ImGui::TextUnformatted("Requires Screen Space GI to be enabled.");
		else if (!foveatedAvailable)
			ImGui::TextUnformatted("Requires active foveated upscaling.");
	}
	if (!loaded)
		ImGui::TextDisabled("SSGI FOV requires Screen Space GI.");
	else if (!settings.Enabled)
		ImGui::TextDisabled("Enable Screen Space GI to use SSGI FOV.");

	ImGui::Spacing();

	SyncResolvedCenterMaskScale(settings);
	ImGui::TextDisabled("%s", settings.EnableFoveated && featureRuntimeActive && IsRuntimeFoveatedActive(settings) ? "active" : "inactive");
	if (settings.EnableGI && !HasGIResources())
		ImGui::TextColored({ 1.0f, 0.75f, 0.25f, 1.0f }, "Full GI resources are not allocated. Restart required to allocate resources and compile GI shaders.");
	if (IsResourceProfileRestartPending())
		ImGui::TextColored({ 1.0f, 0.75f, 0.25f, 1.0f }, "Resource profile changes require restart to allocate/free VRAM and recompile SSGI shaders.");
}

void ScreenSpaceGI::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.ResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	if (!o_json.contains("EnableFoveated") && o_json.contains("FoveatedPresetMode")) {
		// Backward compatibility: legacy foveated preset modes map to the new single toggle.
		const int legacyFoveatedMode = std::clamp(o_json.value("FoveatedPresetMode", 0), 0, 2);
		settings.EnableFoveated = legacyFoveatedMode != 0;
	}
	if (!o_json.contains("ResourceProfile")) {
		// Existing VR configs that already run GI keep full resources; AO-only VR configs move to the lean profile.
		settings.ResourceProfile = (REL::Module::IsVR() && !settings.EnableGI) ? kResourceProfileAOOnly : kResourceProfileFullGI;
	}
	// Backward compatibility: older configs used a single InteriorsOnly toggle.
	if (o_json.contains("InteriorsOnly") &&
	    !o_json.contains("AOInteriorsOnly") &&
	    !o_json.contains("ILInteriorsOnly")) {
		const bool legacyInteriorsOnly = o_json.value("InteriorsOnly", settings.AOInteriorsOnly);
		settings.AOInteriorsOnly = legacyInteriorsOnly;
		settings.ILInteriorsOnly = legacyInteriorsOnly;
	}
	if (!REL::Module::IsVR()) {
		ResetVRSpecificSettings(settings);
	}
	ApplyPlatformSettingOverrides(settings);

	recompileFlag = true;
}

void ScreenSpaceGI::SaveSettings(json& o_json)
{
	ApplyPlatformSettingOverrides(settings);
	o_json = settings;
	if (!REL::Module::IsVR()) {
		StripVRSpecificSettings(o_json);
	}
}

void ScreenSpaceGI::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	ApplyPlatformSettingOverrides(settings);
	const int previousActiveResourceProfile = activeResourceProfile;
	activeResourceProfile = ClampResourceProfile(settings.ResourceProfile);
	const bool allocateGIResources = HasGIResources();
	logger::info("SSGI resource profile: {}", allocateGIResources ? "Full GI resources" : "AO-only resources");

	// SetupResources can run multiple times during runtime/device resets.
	// Drop profile-specific allocations first so stale GI resources are not kept alive accidentally.
	texRadiance = nullptr;
	texRadianceTemp = nullptr;
	for (auto& uav : uavRadiance)
		uav = nullptr;
	texNormal = nullptr;
	for (auto& uav : uavNormal)
		uav = nullptr;
	texIlY[0] = nullptr;
	texIlY[1] = nullptr;
	texIlCoCg[0] = nullptr;
	texIlCoCg[1] = nullptr;
	texGiSpecular[0] = nullptr;
	texGiSpecular[1] = nullptr;
	texCenterIlY = nullptr;
	texCenterIlCoCg = nullptr;
	texCenterGiSpecular = nullptr;
	outputAoIdx = 0;
	outputIlIdx = 0;
	centerRectCache = {};

	if (previousActiveResourceProfile != activeResourceProfile && globals::deferred) {
		globals::deferred->ClearShaderCache();
	}

	logger::debug("Creating buffers...");
	{
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSGICB>());
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.CPUAccessFlags = 0;
		texDesc.MiscFlags = 0;

		if (allocateGIResources) {
			srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;
			uavDesc.Texture2D.MipSlice = 0;

			texRadiance = eastl::make_unique<Texture2D>(texDesc);
			texRadiance->CreateSRV(srvDesc);

			// Create individual UAVs for each mip level for prefiltering
			for (uint i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = DXGI_FORMAT_R11G11B10_FLOAT,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &mipUavDesc, uavRadiance[i].put()));
			}

			// Staging texture for mip 0 radiance. radianceDisocc writes it directly,
			// prefilterRadiance reads it as SRV and writes the mip chain back to texRadiance.
			// Avoids a full-texture CopySubresourceRegion each frame.
			D3D11_TEXTURE2D_DESC tempTexDesc = texDesc;
			tempTexDesc.MipLevels = 1;
			tempTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			D3D11_SHADER_RESOURCE_VIEW_DESC tempSrvDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC tempUavDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			texRadianceTemp = eastl::make_unique<Texture2D>(tempTexDesc);
			texRadianceTemp->CreateSRV(tempSrvDesc);
			texRadianceTemp->CreateUAV(tempUavDesc);
		}

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;

		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc);
			texWorkingDepth->CreateSRV(srvDesc);
			for (int i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormal = eastl::make_unique<Texture2D>(texDesc);
			texNormal->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavNormal[i].put()));
			}
		}

		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		if (allocateGIResources) {
			srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			{
				texIlY[0] = eastl::make_unique<Texture2D>(texDesc);
				texIlY[0]->CreateSRV(srvDesc);
				texIlY[0]->CreateUAV(uavDesc);

				texIlY[1] = eastl::make_unique<Texture2D>(texDesc);
				texIlY[1]->CreateSRV(srvDesc);
				texIlY[1]->CreateUAV(uavDesc);

				texGiSpecular[0] = eastl::make_unique<Texture2D>(texDesc);
				texGiSpecular[0]->CreateSRV(srvDesc);
				texGiSpecular[0]->CreateUAV(uavDesc);

				texGiSpecular[1] = eastl::make_unique<Texture2D>(texDesc);
				texGiSpecular[1]->CreateSRV(srvDesc);
				texGiSpecular[1]->CreateUAV(uavDesc);
			}
			srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
			{
				texIlCoCg[0] = eastl::make_unique<Texture2D>(texDesc);
				texIlCoCg[0]->CreateSRV(srvDesc);
				texIlCoCg[0]->CreateUAV(uavDesc);

				texIlCoCg[1] = eastl::make_unique<Texture2D>(texDesc);
				texIlCoCg[1]->CreateSRV(srvDesc);
				texIlCoCg[1]->CreateUAV(uavDesc);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8_UNORM;
		{
			texAo[0] = eastl::make_unique<Texture2D>(texDesc);
			texAo[0]->CreateSRV(srvDesc);
			texAo[0]->CreateUAV(uavDesc);

			texAo[1] = eastl::make_unique<Texture2D>(texDesc);
			texAo[1]->CreateSRV(srvDesc);
			texAo[1]->CreateUAV(uavDesc);

			texAccumFrames[0] = eastl::make_unique<Texture2D>(texDesc);
			texAccumFrames[0]->CreateSRV(srvDesc);
			texAccumFrames[0]->CreateUAV(uavDesc);

			texAccumFrames[1] = eastl::make_unique<Texture2D>(texDesc);
			texAccumFrames[1]->CreateSRV(srvDesc);
			texAccumFrames[1]->CreateUAV(uavDesc);

			texCenterAo = eastl::make_unique<Texture2D>(texDesc);
			texCenterAo->CreateSRV(srvDesc);
			texCenterAo->CreateUAV(uavDesc);
		}

		if (allocateGIResources) {
			srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			{
				texCenterIlY = eastl::make_unique<Texture2D>(texDesc);
				texCenterIlY->CreateSRV(srvDesc);
				texCenterIlY->CreateUAV(uavDesc);

				texCenterGiSpecular = eastl::make_unique<Texture2D>(texDesc);
				texCenterGiSpecular->CreateSRV(srvDesc);
				texCenterGiSpecular->CreateUAV(uavDesc);
			}

			srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
			{
				texCenterIlCoCg = eastl::make_unique<Texture2D>(texDesc);
				texCenterIlCoCg->CreateSRV(srvDesc);
				texCenterIlCoCg->CreateUAV(uavDesc);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		{
			texPrevGeo = eastl::make_unique<Texture2D>(texDesc);
			texPrevGeo->CreateSRV(srvDesc);
			texPrevGeo->CreateUAV(uavDesc);
		}
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ "Data\\Shaders\\ScreenSpaceGI\\fast_2uges.dds" };

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texNoise = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texNoise->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texNoise->CreateSRV(srvDesc);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
	}

	CompileComputeShaders();
}

void ScreenSpaceGI::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&prefilterDepthsCompute,
		&prefilterRadianceCompute,
		&prefilterNormalCompute,
		&radianceDisoccCompute,
		&radianceDisoccAOOnlyCompute,
		&giCompute,
		&giAOOnlyCompute,
		&centerGIMaskedCompute,
		&centerGIMaskedAOOnlyCompute,
		&blurCompute,
		&stereoSyncCompute,
		&stereoSyncAOOnlyCompute,
		&centerStereoSyncCompute,
		&centerStereoSyncAOOnlyCompute,
		&upsampleCompute,
		&upsampleAOOnlyCompute,
		&centerBlendCompute,
		&centerBlendAOOnlyCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void ScreenSpaceGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
		bool includeResolutionDefines = true;
		bool includeTemporalDefines = true;
		bool includeGIDefines = true;
		bool includeAdaptiveSamplingDefines = false;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &prefilterDepthsCompute, "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "" } } },
			{ &prefilterNormalCompute, "prefilterNormal.cs.hlsl", {} },
			{ &radianceDisoccAOOnlyCompute, "radianceDisocc.cs.hlsl", {}, true, true, false },
			{ &giAOOnlyCompute, "gi.cs.hlsl", {}, true, true, false, true },
			{ &centerGIMaskedAOOnlyCompute, "gi.cs.hlsl", { { "CENTER_FULL_PASS", "" } }, false, false, false, true },
			{ &upsampleAOOnlyCompute, "upsample.cs.hlsl", {}, true, false, false },
			{ &centerBlendAOOnlyCompute, "centerBlend.cs.hlsl", {}, false, false, false },
		};
	if (REL::Module::IsVR()) {
		shaderInfos.push_back({ &stereoSyncAOOnlyCompute, "stereoSync.cs.hlsl", { { "FRAMEBUFFER", "" } }, true, false, false });
		shaderInfos.push_back({ &centerStereoSyncAOOnlyCompute, "stereoSync.cs.hlsl", { { "FRAMEBUFFER", "" }, { "CENTER_DISPATCH", "" } }, false, false, false });
	}
	if (HasGIResources()) {
		shaderInfos.push_back({ &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", {} });
		shaderInfos.push_back({ &radianceDisoccCompute, "radianceDisocc.cs.hlsl", {} });
		shaderInfos.push_back({ &giCompute, "gi.cs.hlsl", {}, true, true, true, true });
		shaderInfos.push_back({ &centerGIMaskedCompute, "gi.cs.hlsl", { { "CENTER_FULL_PASS", "" } }, false, false, true, true });
		shaderInfos.push_back({ &blurCompute, "blur.cs.hlsl", {} });
		if (REL::Module::IsVR()) {
			shaderInfos.push_back({ &stereoSyncCompute, "stereoSync.cs.hlsl", { { "FRAMEBUFFER", "" } }, true, false, true });
			shaderInfos.push_back({ &centerStereoSyncCompute, "stereoSync.cs.hlsl", { { "FRAMEBUFFER", "" }, { "CENTER_DISPATCH", "" } }, false, false, true });
		}
		shaderInfos.push_back({ &upsampleCompute, "upsample.cs.hlsl", {} });
		shaderInfos.push_back({ &centerBlendCompute, "centerBlend.cs.hlsl", {}, false, false });
	}
	for (auto& info : shaderInfos) {
		if (REL::Module::IsVR())
			info.defines.push_back({ "VR", "" });
		if (info.includeResolutionDefines) {
			if (settings.ResolutionMode == 1)
				info.defines.push_back({ "HALF_RES", "" });
			if (settings.ResolutionMode == 2)
				info.defines.push_back({ "QUARTER_RES", "" });
		}
		if (info.includeTemporalDefines && settings.EnableTemporalDenoiser)
			info.defines.push_back({ "TEMPORAL_DENOISER", "" });
		if (info.includeGIDefines && settings.EnableGI)
			info.defines.push_back({ "GI", "" });
		if (info.includeGIDefines && settings.EnableExperimentalSpecularGI)
			info.defines.push_back({ "GI_SPECULAR", "" });
		if (info.includeAdaptiveSamplingDefines && settings.EnableAdaptiveSampling)
			info.defines.push_back({ "ADAPTIVE_SAMPLING", "" });
	}

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceGI") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}

	recompileFlag = false;
}

bool ScreenSpaceGI::ShadersOK()
{
	const bool baseShadersOK = texNoise &&
	                           texWorkingDepth &&
	                           texPrevGeo &&
	                           texNormal &&
	                           texAo[0] &&
	                           texAo[1] &&
	                           texAccumFrames[0] &&
	                           texAccumFrames[1] &&
	                           prefilterDepthsCompute &&
	                           prefilterNormalCompute &&
	                           radianceDisoccAOOnlyCompute &&
	                           giAOOnlyCompute &&
	                           upsampleAOOnlyCompute &&
	                           (!REL::Module::IsVR() || stereoSyncAOOnlyCompute);

	const float centerScale = ResolveFoveatedCenterMaskScale(settings);
	const bool foveatedSsgiActive = IsRuntimeFoveatedActive(settings);
	const bool runtimeGIActive = !foveatedSsgiActive && IsGIActive();
	const bool fullGIShadersOK = !runtimeGIActive ||
	                             (texRadiance &&
	                              texRadianceTemp &&
	                              texIlY[0] &&
	                              texIlY[1] &&
	                              texIlCoCg[0] &&
	                              texIlCoCg[1] &&
	                              texGiSpecular[0] &&
	                              texGiSpecular[1] &&
	                              prefilterRadianceCompute &&
	                              radianceDisoccCompute &&
	                              giCompute &&
	                              blurCompute &&
	                              upsampleCompute &&
	                              (!REL::Module::IsVR() || stereoSyncCompute));

	const bool centerAOShadersOK = texCenterAo &&
	                               centerGIMaskedAOOnlyCompute &&
	                               centerBlendAOOnlyCompute &&
	                               (!REL::Module::IsVR() || centerStereoSyncAOOnlyCompute);
	const bool centerGIShadersOK = !runtimeGIActive ||
	                               (texCenterIlY &&
	                                texCenterIlCoCg &&
	                                texCenterGiSpecular &&
	                                centerGIMaskedCompute &&
	                                centerBlendCompute &&
	                                (!REL::Module::IsVR() || centerStereoSyncCompute));
	const bool centerMaskActive = foveatedSsgiActive && centerScale > 0.0f;

	if (!centerMaskActive)
		return baseShadersOK && fullGIShadersOK;

	return baseShadersOK && fullGIShadersOK && centerAOShadersOK && centerGIShadersOK;
}

void ScreenSpaceGI::UpdateSB()
{
	float2 res = { (float)texWorkingDepth->desc.Width, (float)texWorkingDepth->desc.Height };
	float2 dynres = GetHardenedSsgiFrameDim(res);
	const bool isVR = REL::Module::IsVR();
	const float centerMaskScale = ResolveFoveatedCenterMaskScale(settings);

	static float4x4 prevInvView[2] = {};

	SSGICB& data = ssgiCBData;
	data = {};
	{
		const bool useUnjitteredCamera = isVR;

		for (int eyeIndex = 0; eyeIndex < (1 + isVR); ++eyeIndex) {
			const auto eye = Util::GetCameraData(eyeIndex);
			float proj11 = eye.projMat(0, 0);
			float proj22 = eye.projMat(1, 1);
			float4x4 currentInvView = eye.viewMat.Invert();

			if (useUnjitteredCamera) {
				const auto& projUnjittered = globals::game::frameBufferCached.GetCameraProjUnjittered(eyeIndex);
				proj11 = projUnjittered._11;
				proj22 = projUnjittered._22;
				currentInvView = globals::game::frameBufferCached.GetCameraViewInverse(eyeIndex);
			}

			data.PrevInvViewMat[eyeIndex] = prevInvView[eyeIndex];
			data.NDCToViewMul[eyeIndex] = { 2.0f / proj11, -2.0f / proj22 };
			data.NDCToViewAdd[eyeIndex] = { -1.0f / proj11, 1.0f / proj22 };
			if (isVR)
				data.NDCToViewMul[eyeIndex].x *= 2;

			prevInvView[eyeIndex] = currentInvView;
		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		data.FrameIndex = globals::state->frameCount;

		data.NumSlices = settings.NumSlices;
		data.NumSteps = settings.NumSteps;
		data.MinScreenRadius = settings.MinScreenRadius * dynres.x;

		data.EffectRadius = std::max(settings.AORadius, settings.GIRadius);
		const float safeEffectRadius = std::max(data.EffectRadius, 1e-3f);
		data.EffectRadius = safeEffectRadius;
		data.AORadius = settings.AORadius / safeEffectRadius;
		data.GIRadius = settings.GIRadius / safeEffectRadius;
		data.Thickness = settings.Thickness;
		const float depthFadeStart = std::min(settings.DepthFadeRange.x, settings.DepthFadeRange.y);
		const float depthFadeEnd = std::max(settings.DepthFadeRange.x, settings.DepthFadeRange.y);
		data.DepthFadeRange = { depthFadeStart, depthFadeEnd };
		const float depthFadeSpan = std::max(depthFadeEnd - depthFadeStart, 1.0f);
		data.DepthFadeScaleConst = 1.0f / depthFadeSpan;

		data.GISaturation = settings.GISaturation;
		data.GIDistanceCompensation = settings.GIDistanceCompensation;
		data.GICompensationMaxDist = settings.AORadius;

		data.AOPower = settings.AOPower;
		data.GIStrength = settings.GIStrength;

		data.DepthDisocclusion = settings.DepthDisocclusion;
		data.NormalDisocclusion = settings.NormalDisocclusion;
		data.MaxAccumFrames = settings.MaxAccumFrames;
		data.BlurRadius = settings.BlurRadius;
		data.DistanceNormalisation = settings.DistanceNormalisation;
		data.VRCullDistance = isVR ? ClampVRCullDistance(settings.VRCullDistance) : 0.0f;
		data.CenterFullResMaskScale = centerMaskScale;
		data.CenterFullResMaskHorizontalScale = GetSharedUpscalingCenterMaskHorizontalScale();
		data.CenterFullResMaskFeather = FoveatedCommon::kCenterFeather;
		auto centerOffsets = GetSharedUpscalingMaskOffsetsForSsgi();
		data.CenterFullResMaskOffsets = { centerOffsets[0].x, centerOffsets[0].y, centerOffsets[1].x, centerOffsets[1].y };
		data.CenterDispatchOffsetX = 0.0f;
		data.CenterDispatchOffsetY = 0.0f;
		data.CenterDispatchSizeX = dynres.x;
		data.CenterDispatchSizeY = dynres.y;
	}

	ssgiCB->Update(data);
}

void ScreenSpaceGI::DrawSSGI()
{
	ApplyPlatformSettingOverrides(settings);
	SyncResolvedCenterMaskScale(settings);

	auto context = globals::d3d::context;
	if (!context)
		return;
	const bool isVR = REL::Module::IsVR();
	const int resolutionMode = ClampResolutionMode(settings.ResolutionMode);
	const float centerScale = ResolveFoveatedCenterMaskScale(settings);
	const bool foveatedSsgiActive = IsRuntimeFoveatedActive(settings);

	auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
	GET_INSTANCE_MEMBER(BSImagespaceShaderISSAOBlurH, imageSpaceManager);

	// Toggle vanilla SSAO
	static bool* enableSSAO = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(BSImagespaceShaderISSAOBlurH.get()) + 0x50LL);
	*enableSSAO = settings.EnableVanillaSSAO;

	const bool isInterior = Util::IsInterior();
	const bool allowAOSpace = !settings.AOInteriorsOnly || isInterior;
	const bool allowILSpace = !settings.ILInteriorsOnly || isInterior;
	const bool runILPath = !foveatedSsgiActive && IsGIActive() && allowILSpace;
	const bool temporalEnabled = !foveatedSsgiActive && settings.EnableTemporalDenoiser;
	const bool runRadianceDisoccPass = !foveatedSsgiActive && (runILPath || temporalEnabled);
	const bool runPrefilterRadiancePass = runILPath;
	const bool blurEnabled = !foveatedSsgiActive && settings.EnableBlur && runILPath;
	const bool ssgiOutputNeeded = allowAOSpace || runILPath;
	const bool vrStereoSyncEnabled = isVR && settings.EnableStereoSync;
	const bool stereoSyncBaseEnabled = vrStereoSyncEnabled && !foveatedSsgiActive && ssgiOutputNeeded;
	ID3D11ComputeShader* activeRadianceDisoccCompute = nullptr;
	ID3D11ComputeShader* activeGICompute = nullptr;
	ID3D11ComputeShader* activeCenterGICompute = nullptr;
	ID3D11ComputeShader* activeCenterBlendCompute = nullptr;
	ID3D11ComputeShader* activeUpsampleCompute = nullptr;
	ID3D11ComputeShader* activeStereoSyncCompute = nullptr;
	ID3D11ComputeShader* activeCenterStereoSyncCompute = nullptr;
	auto refreshActiveShaders = [&]() {
		activeRadianceDisoccCompute = runILPath ? radianceDisoccCompute.get() : radianceDisoccAOOnlyCompute.get();
		activeGICompute = runILPath ? giCompute.get() : giAOOnlyCompute.get();
		activeCenterGICompute = runILPath ? centerGIMaskedCompute.get() : centerGIMaskedAOOnlyCompute.get();
		activeCenterBlendCompute = runILPath ? centerBlendCompute.get() : centerBlendAOOnlyCompute.get();
		activeUpsampleCompute = runILPath ? upsampleCompute.get() : upsampleAOOnlyCompute.get();
		activeStereoSyncCompute = runILPath ? stereoSyncCompute.get() : stereoSyncAOOnlyCompute.get();
		activeCenterStereoSyncCompute = runILPath ? centerStereoSyncCompute.get() : centerStereoSyncAOOnlyCompute.get();
	};
	refreshActiveShaders();
	static uint lastFrameAoTexIdx = 0;
	static uint lastFrameGITexIdx = 0;
	static uint lastFrameAccumTexIdx = 0;
	static bool skippedLastFrame = false;

	auto resetHistoryState = [&](const char* a_reason) {
		lastFrameAoTexIdx = 0;
		lastFrameGITexIdx = 0;
		lastFrameAccumTexIdx = 0;
		outputAoIdx = 0;
		outputIlIdx = 0;

		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		auto clearUavIfValid = [&](auto& a_texture) {
			if (a_texture && a_texture->uav)
				context->ClearUnorderedAccessViewFloat(a_texture->uav.get(), clr);
		};
		clearUavIfValid(texAccumFrames[0]);
		clearUavIfValid(texAccumFrames[1]);
		clearUavIfValid(texAo[0]);
		clearUavIfValid(texAo[1]);
		clearUavIfValid(texIlY[0]);
		clearUavIfValid(texIlY[1]);
		clearUavIfValid(texIlCoCg[0]);
		clearUavIfValid(texIlCoCg[1]);
		clearUavIfValid(texGiSpecular[0]);
		clearUavIfValid(texGiSpecular[1]);
		clearUavIfValid(texPrevGeo);
		logger::debug("SSGI history reset ({})", a_reason);
	};

	auto clearOutputsAndReturn = [&]() {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		auto clearOutputIfValid = [&](auto& a_textureArray, uint a_index) {
			if (a_index < 2 && a_textureArray[a_index] && a_textureArray[a_index]->uav)
				context->ClearUnorderedAccessViewFloat(a_textureArray[a_index]->uav.get(), clr);
		};
		clearOutputIfValid(texAo, outputAoIdx);
		clearOutputIfValid(texIlY, outputIlIdx);
		clearOutputIfValid(texIlCoCg, outputIlIdx);
		clearOutputIfValid(texGiSpecular, outputAoIdx);
	};

	if (!(settings.Enabled && ssgiOutputNeeded)) {
		skippedLastFrame = true;
		clearOutputsAndReturn();
		return;
	}

	static uint64_t lastModeSignature = 0;
	static bool hasModeSignature = false;
	const uint modeCenterScaleMilli = static_cast<uint>(std::round(centerScale * 1000.0f));
	const uint modeCenterHorizontalScaleMilli = static_cast<uint>(std::round(GetSharedUpscalingCenterMaskHorizontalScale() * 1000.0f));
	uint64_t modeSignature = 1469598103934665603ull;
	auto hashCombine = [&](uint64_t a_value) {
		modeSignature ^= a_value + 0x9e3779b97f4a7c15ull + (modeSignature << 6) + (modeSignature >> 2);
	};
	hashCombine(static_cast<uint64_t>(resolutionMode));
	hashCombine(static_cast<uint64_t>(foveatedSsgiActive));
	hashCombine(static_cast<uint64_t>(modeCenterScaleMilli));
	if (modeCenterScaleMilli > 0) {
		hashCombine(static_cast<uint64_t>(modeCenterHorizontalScaleMilli));
		const auto modeCenterOffsets = GetSharedUpscalingMaskOffsetsForSsgi();
		hashCombine(static_cast<uint64_t>(QuantizeCenterOffset(modeCenterOffsets[0].x)));
		hashCombine(static_cast<uint64_t>(QuantizeCenterOffset(modeCenterOffsets[0].y)));
		if (isVR) {
			hashCombine(static_cast<uint64_t>(QuantizeCenterOffset(modeCenterOffsets[1].x)));
			hashCombine(static_cast<uint64_t>(QuantizeCenterOffset(modeCenterOffsets[1].y)));
		}
	}
	hashCombine(static_cast<uint64_t>(runILPath));
	hashCombine(static_cast<uint64_t>(temporalEnabled));
	hashCombine(static_cast<uint64_t>(blurEnabled));
	hashCombine(static_cast<uint64_t>(settings.EnableExperimentalSpecularGI));
	hashCombine(static_cast<uint64_t>(allowAOSpace));
	hashCombine(static_cast<uint64_t>(allowILSpace));
	hashCombine(static_cast<uint64_t>(vrStereoSyncEnabled));
	const bool modeSignatureChanged = !hasModeSignature || modeSignature != lastModeSignature;
	if (modeSignatureChanged) {
		resetHistoryState("runtime mode switch");
		lastModeSignature = modeSignature;
		hasModeSignature = true;
	}
	if (skippedLastFrame) {
		if (!modeSignatureChanged)
			resetHistoryState("output resumed");
		skippedLastFrame = false;
	}

	if (recompileFlag) {
		ClearShaderCache();
		refreshActiveShaders();
		resetHistoryState("shader recompile");
		clearOutputsAndReturn();
		return;
	}

	if (!ShadersOK()) {
		logger::warn("SSGI shader set incomplete for current runtime mode; skipping frame.");
		clearOutputsAndReturn();
		return;
	}

	const bool centerAOShadersReady = texCenterAo &&
	                                  centerGIMaskedAOOnlyCompute &&
	                                  centerBlendAOOnlyCompute;
	const bool centerGIShadersReady = !runILPath ||
	                                  (texCenterIlY &&
	                                   texCenterIlCoCg &&
	                                   texCenterGiSpecular &&
	                                   centerGIMaskedCompute &&
	                                   centerBlendCompute);
	const bool centerShadersReady = centerAOShadersReady && centerGIShadersReady;
	const bool centerMaskEnabled = centerShadersReady &&
	                               foveatedSsgiActive &&
	                               (centerScale > 0.0f);
	const bool stereoSyncCenterEnabled = vrStereoSyncEnabled && centerMaskEnabled && ssgiOutputNeeded;
	const bool centerBlendNeeded = centerMaskEnabled && (centerScale < 0.99f);
	const bool centerDirectWrite = centerMaskEnabled && !centerBlendNeeded;

	auto requireActiveShader = [&](bool a_needed, ID3D11ComputeShader* a_shader, const char* a_name) {
		if (!a_needed)
			return true;
		if (a_shader)
			return true;
		logger::warn("SSGI runtime shader missing ({}); skipping frame.", a_name);
		clearOutputsAndReturn();
		return false;
	};
	if (!requireActiveShader(runRadianceDisoccPass, activeRadianceDisoccCompute, "radianceDisocc(active)") ||
	    !requireActiveShader(!foveatedSsgiActive, activeGICompute, "gi(active)") ||
	    !requireActiveShader(blurEnabled, blurCompute.get(), "blur") ||
	    !requireActiveShader(stereoSyncBaseEnabled, activeStereoSyncCompute, "stereoSync(active)") ||
	    !requireActiveShader((resolutionMode != 0) && !centerDirectWrite && !foveatedSsgiActive, activeUpsampleCompute, "upsample(active)") ||
	    !requireActiveShader(centerMaskEnabled, activeCenterGICompute, "centerGI(active)") ||
	    !requireActiveShader(stereoSyncCenterEnabled, activeCenterStereoSyncCompute, "centerStereoSync(active)") ||
	    !requireActiveShader(centerBlendNeeded, activeCenterBlendCompute, "centerBlend(active)")) {
		return;
	}

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "SSGI");

	uint inputAoTexIdx = lastFrameAoTexIdx;
	uint inputGITexIdx = lastFrameGITexIdx;

	UpdateSB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = {
		(float)texWorkingDepth->desc.Width,
		(float)texWorkingDepth->desc.Height
	};
	size = GetHardenedSsgiFrameDim(size);
	auto resolution = std::array{ (uint)size.x, (uint)size.y };
	auto resChoices = std::array{
		resolution, std::array{ resolution[0] >> 1, resolution[1] >> 1 }, std::array{ resolution[0] >> 2, resolution[1] >> 2 }
	};
	auto internalRes = resChoices[resolutionMode];
	using DispatchRect = CenterDispatchRect;
	auto centerOffsets = GetSharedUpscalingMaskOffsetsForSsgi();
	const float centerHorizontalScale = GetSharedUpscalingCenterMaskHorizontalScale();

	auto buildCenterDispatchRect = [&](uint a_eyeIndex) -> DispatchRect {
		DispatchRect rect{};
		const uint frameWidth = resolution[0];
		const uint frameHeight = resolution[1];
		if (frameWidth == 0 || frameHeight == 0)
			return rect;

		uint eyeMinX = 0;
		uint eyeMaxX = frameWidth;
		if (isVR) {
			const uint midX = frameWidth >> 1;
			if (a_eyeIndex == 0) {
				eyeMinX = 0;
				eyeMaxX = midX;
			} else {
				eyeMinX = midX;
				eyeMaxX = frameWidth;
			}
		}

		const uint eyeWidth = (eyeMaxX > eyeMinX) ? (eyeMaxX - eyeMinX) : 0;
		if (eyeWidth == 0)
			return rect;

		const float2 centerOffset = centerOffsets[a_eyeIndex];
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(eyeMinX, eyeMaxX, frameHeight, centerScale, centerOffset.x, centerOffset.y, FoveatedCommon::kCenterFeather, centerHorizontalScale);
		const int minX = bounds.minX;
		const int maxX = bounds.maxX;
		const int minY = bounds.minY;
		const int maxY = bounds.maxY;

		if (maxX <= minX || maxY <= minY)
			return rect;

		rect.x = static_cast<uint>(minX);
		rect.y = static_cast<uint>(minY);
		rect.width = static_cast<uint>(maxX - minX);
		rect.height = static_cast<uint>(maxY - minY);
		return rect;
	};

	auto& cache = centerRectCache;
	const float centerScaleDelta = cache.scale - centerScale;
	const bool centerCacheDirty =
		cache.frameWidth != resolution[0] ||
		cache.frameHeight != resolution[1] ||
		cache.isVR != isVR ||
		(centerScaleDelta < 0.0f ? -centerScaleDelta : centerScaleDelta) > 1e-6f ||
		std::abs(cache.horizontalScale - centerHorizontalScale) > 1e-6f ||
		std::abs(cache.centerOffsets[0].x - centerOffsets[0].x) > 1e-6f ||
		std::abs(cache.centerOffsets[0].y - centerOffsets[0].y) > 1e-6f ||
		(isVR && (std::abs(cache.centerOffsets[1].x - centerOffsets[1].x) > 1e-6f ||
		          std::abs(cache.centerOffsets[1].y - centerOffsets[1].y) > 1e-6f));
	if (centerCacheDirty) {
		cache.frameWidth = resolution[0];
		cache.frameHeight = resolution[1];
		cache.isVR = isVR;
		cache.scale = centerScale;
		cache.horizontalScale = centerHorizontalScale;
		cache.centerOffsets = centerOffsets;
		cache.rects[0] = buildCenterDispatchRect(0);
		cache.rects[1] = isVR ? buildCenterDispatchRect(1) : DispatchRect{};
	}

	auto forEachCenterRect = [&](auto&& a_fn) {
		a_fn(cache.rects[0]);
		if (isVR)
			a_fn(cache.rects[1]);
	};

	auto dispatchCenterShader = [&](ID3D11ComputeShader* a_shader, std::string_view a_profileName) {
		CS_PROFILE_SCOPE(a_profileName);
		forEachCenterRect([&](const DispatchRect& rect) {
			if (rect.width == 0 || rect.height == 0)
				return;

			ssgiCBData.CenterDispatchOffsetX = static_cast<float>(rect.x);
			ssgiCBData.CenterDispatchOffsetY = static_cast<float>(rect.y);
			ssgiCBData.CenterDispatchSizeX = static_cast<float>(rect.width);
			ssgiCBData.CenterDispatchSizeY = static_cast<float>(rect.height);
			ssgiCB->Update(ssgiCBData);
			ID3D11Buffer* centerCb = ssgiCB->CB();
			context->CSSetConstantBuffers(1, 1, &centerCb);

			context->CSSetShader(a_shader, nullptr, 0);
			context->Dispatch((rect.width + 7u) >> 3, (rect.height + 7u) >> 3, 1);
		});
	};

	auto copyTextureRects = [&](ID3D11Resource* a_dst, ID3D11Resource* a_src) {
		forEachCenterRect([&](const DispatchRect& rect) {
			if (rect.width == 0 || rect.height == 0)
				return;
			D3D11_BOX srcBox{
				rect.x,
				rect.y,
				0u,
				rect.x + rect.width,
				rect.y + rect.height,
				1u
			};
			context->CopySubresourceRegion(a_dst, 0, rect.x, rect.y, 0, a_src, 0, &srcBox);
		});
	};

	std::array<ID3D11ShaderResourceView*, 11> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 6> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssgiCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	auto runStereoSync = [&](ID3D11ComputeShader* a_shader, bool a_centerOnly, std::string_view a_profileName) {
		const uint dstAoIdx = !inputAoTexIdx;
		const uint dstGITexIdx = !inputGITexIdx;

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texAo[inputAoTexIdx]->srv.get();
		if (runILPath) {
			srvs.at(2) = texIlY[inputGITexIdx]->srv.get();
			srvs.at(3) = texIlCoCg[inputGITexIdx]->srv.get();
		}

		uavs.at(0) = texAo[dstAoIdx]->uav.get();
		if (runILPath) {
			uavs.at(1) = texIlY[dstGITexIdx]->uav.get();
			uavs.at(2) = texIlCoCg[dstGITexIdx]->uav.get();
		}

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		if (a_centerOnly) {
			dispatchCenterShader(a_shader, a_profileName);
			resetViews();
			copyTextureRects(texAo[inputAoTexIdx]->resource.get(), texAo[dstAoIdx]->resource.get());
			if (runILPath) {
				copyTextureRects(texIlY[inputGITexIdx]->resource.get(), texIlY[dstGITexIdx]->resource.get());
				copyTextureRects(texIlCoCg[inputGITexIdx]->resource.get(), texIlCoCg[dstGITexIdx]->resource.get());
			}
			return;
		}

		context->CSSetShader(a_shader, nullptr, 0);
		{
			CS_PROFILE_SCOPE(a_profileName);
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		}

		inputAoTexIdx = dstAoIdx;
		if (runILPath)
			inputGITexIdx = dstGITexIdx;
	};

	//////////////////////////////////////////////////////

	context->CSSetConstantBuffers(1, 1, &cb);
	Util::BindGlobalConstantBuffersForCS(context);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Depths");

		srvs.at(0) = Util::GetCurrentSceneDepthSRV();
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::PrefilterDepths");
			context->Dispatch((resolution[0] + 15) >> 4, (resolution[1] + 15) >> 4, 1);
		}
	}

	// fetch radiance and disocclusion (optional in AO-only + no temporal mode)
	if (runRadianceDisoccPass) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Radiance Disocc");

		resetViews();
		srvs.at(0) = runILPath ? rts[deferred->forwardRenderTargets[0]].SRV : nullptr;
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = rts[NORMALROUGHNESS].SRV;
		if (temporalEnabled) {
			srvs.at(3) = texPrevGeo->srv.get();
			srvs.at(4) = rts[RE::RENDER_TARGET::kMOTION_VECTOR].SRV;
			srvs.at(5) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
			srvs.at(6) = texAo[inputAoTexIdx]->srv.get();
			if (runILPath) {
				srvs.at(7) = texIlY[inputGITexIdx]->srv.get();
				srvs.at(8) = texIlCoCg[inputGITexIdx]->srv.get();
				srvs.at(9) = texGiSpecular[inputAoTexIdx]->srv.get();
			}
		}
		srvs.at(10) = nullptr;

		// AO-only temporal mode does not need radiance or IL history traffic.
		uavs.at(0) = runILPath ? texRadianceTemp->uav.get() : nullptr;
		if (temporalEnabled) {
			uavs.at(1) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
			uavs.at(2) = texAo[!inputAoTexIdx]->uav.get();
			if (runILPath) {
				uavs.at(3) = texIlY[!inputGITexIdx]->uav.get();
				uavs.at(4) = texIlCoCg[!inputGITexIdx]->uav.get();
				uavs.at(5) = texGiSpecular[!inputAoTexIdx]->uav.get();
			}
		}

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(activeRadianceDisoccCompute, nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::RadianceDisocc");
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		}

		// Prefilter radiance texture only when GI is enabled.
		if (runPrefilterRadiancePass) {
			TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Radiance");

			// radianceDisocc wrote mip 0 directly to texRadianceTemp above.
			resetViews();
			srvs.at(0) = texRadianceTemp->srv.get();
			uavs.at(0) = uavRadiance[0].get();        // Mip 0
			uavs.at(1) = uavRadiance[1].get();        // Mip 1
			uavs.at(2) = uavRadiance[2].get();        // Mip 2
			uavs.at(3) = uavRadiance[3].get();        // Mip 3
			uavs.at(4) = uavRadiance[4].get();        // Mip 4

			context->CSSetShaderResources(0, 1, srvs.data());
			context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
			context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
			{
				CS_PROFILE_SCOPE("ScreenSpaceGI::PrefilterRadiance");
				context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
			}
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		if (temporalEnabled)
			lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	// Prefilter normals for the regular AO/GI path.
	if (!foveatedSsgiActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Prefilter Normals");

		resetViews();
		srvs.at(0) = rts[NORMALROUGHNESS].SRV;
		uavs.at(0) = uavNormal[0].get();
		uavs.at(1) = uavNormal[1].get();
		uavs.at(2) = uavNormal[2].get();
		uavs.at(3) = uavNormal[3].get();
		uavs.at(4) = uavNormal[4].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalCompute.get(), nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::PrefilterNormals");
			context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
		}
	}

	// GI
	if (!foveatedSsgiActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - GI");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = runILPath ? texRadiance->srv.get() : nullptr;
		srvs.at(3) = texNoise->srv.get();
		if (temporalEnabled) {
			srvs.at(4) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
			srvs.at(5) = texAo[inputAoTexIdx]->srv.get();
			if (runILPath) {
				srvs.at(6) = texIlY[inputGITexIdx]->srv.get();
				srvs.at(7) = texIlCoCg[inputGITexIdx]->srv.get();
				srvs.at(8) = texGiSpecular[inputAoTexIdx]->srv.get();
			}
		}
		srvs.at(10) = texNormal->srv.get();

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		if (runILPath) {
			uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
			uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
			uavs.at(3) = texGiSpecular[!inputAoTexIdx]->uav.get();
		}
		uavs.at(4) = texPrevGeo->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(activeGICompute, nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::GI");
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAoTexIdx = inputAoTexIdx;
	}

	// blur
	if (blurEnabled) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Diffuse Blur");

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = rts[NORMALROUGHNESS].SRV;
		srvs.at(2) = temporalEnabled ? texAccumFrames[lastFrameAccumTexIdx]->srv.get() : nullptr;
		srvs.at(3) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(4) = texIlCoCg[inputGITexIdx]->srv.get();

		uavs.at(0) = temporalEnabled ? texAccumFrames[!lastFrameAccumTexIdx]->uav.get() : nullptr;
		uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(blurCompute.get(), nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::Blur");
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		}

		inputGITexIdx = !inputGITexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		if (temporalEnabled)
			lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	if (stereoSyncBaseEnabled) {
		TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Stereo Sync");

		if (globals::state->frameAnnotations)
			globals::state->BeginPerfEvent("SSGI - Stereo Sync");

		runStereoSync(activeStereoSyncCompute, false, "ScreenSpaceGI::StereoSync");

		if (globals::state->frameAnnotations)
			globals::state->EndPerfEvent();
	}

	// upsample
	if (resolutionMode != 0 && !centerDirectWrite && !foveatedSsgiActive) {
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texAo[inputAoTexIdx]->srv.get();
		if (runILPath) {
			srvs.at(2) = texIlY[inputGITexIdx]->srv.get();
			srvs.at(3) = texIlCoCg[inputGITexIdx]->srv.get();
			srvs.at(4) = texGiSpecular[inputAoTexIdx]->srv.get();
		}

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		if (runILPath) {
			uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
			uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
			uavs.at(3) = texGiSpecular[!inputAoTexIdx]->uav.get();
		}

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(activeUpsampleCompute, nullptr, 0);
		{
			CS_PROFILE_SCOPE("ScreenSpaceGI::Upsample");
			context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
	}

	// FOV-only center pass, optionally feathered into a cleared output.
	if (centerMaskEnabled) {
		if (foveatedSsgiActive) {
			FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
			const uint clearAoIdx = centerBlendNeeded ? inputAoTexIdx : !inputAoTexIdx;
			if (clearAoIdx < 2 && texAo[clearAoIdx] && texAo[clearAoIdx]->uav)
				context->ClearUnorderedAccessViewFloat(texAo[clearAoIdx]->uav.get(), clr);
		}

		{
			TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Center FullRes GI");

			resetViews();
			srvs.at(0) = texWorkingDepth->srv.get();
			srvs.at(1) = rts[NORMALROUGHNESS].SRV;
			srvs.at(2) = runILPath ? texRadiance->srv.get() : nullptr;
			srvs.at(3) = texNoise->srv.get();
			srvs.at(9) = runILPath ? rts[deferred->forwardRenderTargets[0]].SRV : nullptr;
			srvs.at(10) = texNormal->srv.get();

			uavs.at(0) = centerBlendNeeded ? texCenterAo->uav.get() : texAo[!inputAoTexIdx]->uav.get();
			if (runILPath) {
				uavs.at(1) = centerBlendNeeded ? texCenterIlY->uav.get() : texIlY[!inputGITexIdx]->uav.get();
				uavs.at(2) = centerBlendNeeded ? texCenterIlCoCg->uav.get() : texIlCoCg[!inputGITexIdx]->uav.get();
			}
			const bool writeCenterSpecular = runILPath && settings.EnableExperimentalSpecularGI;
			uavs.at(3) = writeCenterSpecular ?
			                 (centerBlendNeeded ? texCenterGiSpecular->uav.get() : texGiSpecular[!inputAoTexIdx]->uav.get()) :
			                 nullptr;
			uavs.at(4) = texPrevGeo->uav.get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			dispatchCenterShader(activeCenterGICompute, "ScreenSpaceGI::CenterGI");
		}

		if (centerBlendNeeded) {
			TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Center Blend");

			resetViews();

			srvs.at(0) = texAo[inputAoTexIdx]->srv.get();
			srvs.at(4) = texCenterAo->srv.get();
			uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
			if (runILPath) {
				srvs.at(1) = texIlY[inputGITexIdx]->srv.get();
				srvs.at(2) = texIlCoCg[inputGITexIdx]->srv.get();
				srvs.at(3) = texGiSpecular[inputAoTexIdx]->srv.get();
				srvs.at(5) = texCenterIlY->srv.get();
				srvs.at(6) = texCenterIlCoCg->srv.get();
				srvs.at(7) = texCenterGiSpecular->srv.get();
				uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
				uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
				uavs.at(3) = texGiSpecular[!inputAoTexIdx]->uav.get();
			}

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			dispatchCenterShader(activeCenterBlendCompute, "ScreenSpaceGI::CenterBlend");

			// Blend pass runs center-rect only; copy just those rects back into the full-frame source.
			resetViews();
			copyTextureRects(texAo[inputAoTexIdx]->resource.get(), texAo[!inputAoTexIdx]->resource.get());
			if (runILPath) {
				copyTextureRects(texIlY[inputGITexIdx]->resource.get(), texIlY[!inputGITexIdx]->resource.get());
				copyTextureRects(texIlCoCg[inputGITexIdx]->resource.get(), texIlCoCg[!inputGITexIdx]->resource.get());
				copyTextureRects(texGiSpecular[inputAoTexIdx]->resource.get(), texGiSpecular[!inputAoTexIdx]->resource.get());
			}
		} else {
			// Center pass wrote directly into the destination full-res buffers.
			inputAoTexIdx = !inputAoTexIdx;
			inputGITexIdx = !inputGITexIdx;
		}

		if (stereoSyncCenterEnabled) {
			TracyD3D11Zone(globals::state->tracyCtx, "SSGI - Center Stereo Sync");

			if (globals::state->frameAnnotations)
				globals::state->BeginPerfEvent("SSGI - Center Stereo Sync");

			runStereoSync(activeCenterStereoSyncCompute, true, "ScreenSpaceGI::CenterStereoSync");

			if (globals::state->frameAnnotations)
				globals::state->EndPerfEvent();
		}
	}

	outputAoIdx = inputAoTexIdx;
	outputIlIdx = inputGITexIdx;

	// Apply split interior gating after the pipeline: AO and IL can now be gated independently.
	if (!allowAOSpace) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		context->ClearUnorderedAccessViewFloat(texAo[outputAoIdx]->uav.get(), clr);
	}
	if (!allowILSpace && IsGIActive()) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		context->ClearUnorderedAccessViewFloat(texIlY[outputIlIdx]->uav.get(), clr);
		context->ClearUnorderedAccessViewFloat(texIlCoCg[outputIlIdx]->uav.get(), clr);
		context->ClearUnorderedAccessViewFloat(texGiSpecular[outputAoIdx]->uav.get(), clr);
	}

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}
