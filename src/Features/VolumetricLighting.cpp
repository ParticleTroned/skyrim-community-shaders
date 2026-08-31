#include "VolumetricLighting.h"

#include <algorithm>
#include <memory>

#include "LocationContext.h"
#include "RE/N/NiDirectionalLight.h"
#include "SkySync.h"
#include "State.h"
#include "VolumetricLightingTuningMigration.h"

namespace
{
	constexpr float kWeatherTransitionEpsilon = 0.001f;
	constexpr int32_t kTextureWidthMin = 32;
	constexpr int32_t kTextureWidthMax = 640;
	constexpr int32_t kTextureHeightMin = 32;
	constexpr int32_t kTextureHeightMax = 640;
	constexpr int32_t kTextureDepthMin = 10;
	constexpr int32_t kTextureDepthMax = 640;

	bool IsImageSpaceReplacementEnabled()
	{
		auto* state = globals::state;
		if (!state)
			return false;

		const int classCount = static_cast<int>(sizeof(state->enabledClasses) / sizeof(state->enabledClasses[0]));
		const int imageSpaceClassIndex = static_cast<int>(RE::BSShader::Type::ImageSpace) - 1;
		if (imageSpaceClassIndex >= 0 && imageSpaceClassIndex < classCount && !state->enabledClasses[imageSpaceClassIndex]) {
			return false;
		}

		return state->enablePShaders;
	}

	bool IsRainWeatherActive(const RE::TESWeather* a_weather, float a_weight)
	{
		return a_weather &&
		       a_weather->precipitationData &&
		       a_weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
		       a_weight > kWeatherTransitionEpsilon;
	}

	bool IsRainTransitionActive()
	{
		auto* sky = globals::game::sky;
		if (!sky || !sky->precip || sky->mode.get() != RE::Sky::Mode::kFull)
			return false;

		const float currentWeight = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		const float lastWeight = 1.0f - currentWeight;
		return IsRainWeatherActive(sky->currentWeather, currentWeight) ||
		       IsRainWeatherActive(sky->lastWeather, lastWeight);
	}

	VolumetricLightingTuning::Color ToTuningColor(const RE::NiColor& color)
	{
		return { color.red, color.green, color.blue };
	}

	RE::NiColor ToNiColor(const VolumetricLightingTuning::Color& color)
	{
		return { color.red, color.green, color.blue };
	}

	bool TryGetCurrentSunColor(VolumetricLightingTuning::Color& color)
	{
		auto* sky = globals::game::sky;
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		color = ToTuningColor(sky->sun->light->GetLightRuntimeData().diffuse);
		if (!VolumetricLightingTuning::IsFinite(color))
			return false;

		color = VolumetricLightingTuning::SanitizeColor(color);
		return true;
	}

	void ApplyGodrayColorTuning(
		RE::BSVolumetricLightingRenderData& descriptor,
		const VolumetricLighting::GodrayProfile& profile)
	{
		const VolumetricLightingTuning::ColorBlend authoredColor{
			ToTuningColor(descriptor.color),
			descriptor.customColor.contribution
		};
		const VolumetricLightingTuning::Color userColor{
			profile.CustomColorRed,
			profile.CustomColorGreen,
			profile.CustomColorBlue
		};
		const auto applyComposedUserColor = [&]() {
			const auto composedColor = VolumetricLightingTuning::ComposeUserColor(
				authoredColor,
				userColor,
				profile.CustomColorContribution);
			descriptor.color = ToNiColor(composedColor.color);
			descriptor.customColor.contribution = composedColor.contribution;
		};

		if (VolumetricLightingTuning::IsNear(profile.Saturation, 1.0f)) {
			applyComposedUserColor();
			return;
		}

		VolumetricLightingTuning::Color sunColor{};
		if (!TryGetCurrentSunColor(sunColor)) {
			if (!VolumetricLightingTuning::IsNear(profile.CustomColorContribution, 0.0f))
				applyComposedUserColor();
			return;
		}

		const auto baselineColor = VolumetricLightingTuning::ResolveEffectiveColor(authoredColor, std::addressof(sunColor));
		const auto saturatedColor = VolumetricLightingTuning::SaturateColor(baselineColor, profile.Saturation);
		const auto finalColor = VolumetricLightingTuning::LerpColor(
			saturatedColor,
			VolumetricLightingTuning::ClampColor01(userColor),
			profile.CustomColorContribution);

		// A local descriptor can force the already-resolved result without losing authored state.
		descriptor.customColor.contribution = 1.0f;
		descriptor.color = ToNiColor(VolumetricLightingTuning::SanitizeColor(finalColor));
	}

}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricLighting::TextureSize,
	Width,
	Height,
	Depth);

namespace VolumetricLightingTuning
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		Profile,
		ShaftIntensity,
		Opacity,
		Saturation,
		CustomColorContribution,
		CustomColorRed,
		CustomColorGreen,
		CustomColorBlue);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VolumetricLighting::Settings,
	ExteriorEnabled,
	DisableWeatherInteractionDuringRain,
	ExteriorGodrays,
	ExteriorQuality,
	ExteriorCustomSize,
	InteriorEnabled,
	InteriorGodrays,
	InteriorQuality,
	InteriorCustomSize);

void VolumetricLighting::DrawSettings()
{
	SanitizeSettings();

	auto drawVRRestartHint = [] {
		if (!globals::game::isVR) {
			return;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(VR restart required)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("In VR, this change needs a restart before it fully applies.");
		}
	};

	if (REL::Module::IsVR()) {
		if (ImGui::Checkbox("Disable Weather-Driven Volumetric Lighting During Rain", &settings.DisableWeatherInteractionDuringRain))
			SetupVL();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Turns off rain-driven volumetric lighting while it is raining, then restores it after rain.");
	}

	DrawGodrayTuningSettings();
	ImGui::Separator();

	if (ImGui::Checkbox("Enable in Exteriors", &settings.ExteriorEnabled))
		SetupVL();
	drawVRRestartHint();

	if (settings.ExteriorEnabled)
		DrawVolumetricLightingSettings(settings.ExteriorQuality, settings.ExteriorCustomSize, false, !inInterior);

	if (ImGui::Checkbox("Enable in Interiors", &settings.InteriorEnabled))
		SetupVL();
	drawVRRestartHint();

	if (settings.InteriorEnabled)
		DrawVolumetricLightingSettings(settings.InteriorQuality, settings.InteriorCustomSize, true, inInterior);
}

void VolumetricLighting::DrawPerformanceSettings(bool a_advanced)
{
	SanitizeSettings();

	auto drawVRRestartHint = [] {
		if (!globals::game::isVR) {
			return;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(VR restart required)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("In VR, this change needs a restart before it fully applies.");
		}
	};

	auto drawQuality = [&](const char* label, int32_t& quality, TextureSize& customSize, bool isInterior, bool inLocationType) {
		quality = ClampQualityIndex(quality);
		if (ImGui::SliderInt(label, &quality, 0, static_cast<uint8_t>(Quality::Count) - 1, QualityNames[quality])) {
			if (inLocationType)
				SetupVL();
		}

		if (!a_advanced || static_cast<Quality>(quality) != Quality::Custom) {
			return;
		}

		auto& [Width, Height, Depth] = FetchCurrentSizeInUnits(isInterior);
		if (ImGui::SliderInt(isInterior ? "Interior Width" : "Exterior Width", &Width, 1, 20, FromUnits(Width, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
			customSize.Width = Width * 32;
			if (inLocationType)
				SetupVL();
		}
		if (ImGui::SliderInt(isInterior ? "Interior Height" : "Exterior Height", &Height, 1, 20, FromUnits(Height, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
			customSize.Height = Height * 32;
			if (inLocationType)
				SetupVL();
		}
		if (ImGui::SliderInt(isInterior ? "Interior Depth" : "Exterior Depth", &Depth, 1, 64, FromUnits(Depth, 10), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
			customSize.Depth = Depth * 10;
			if (inLocationType)
				SetupVL();
		}
	};

	if (ImGui::Checkbox("Enable in Exteriors", &settings.ExteriorEnabled))
		SetupVL();
	drawVRRestartHint();
	if (settings.ExteriorEnabled)
		drawQuality("Exterior Quality", settings.ExteriorQuality, settings.ExteriorCustomSize, false, !inInterior);

	if (ImGui::Checkbox("Enable in Interiors", &settings.InteriorEnabled))
		SetupVL();
	drawVRRestartHint();
	if (settings.InteriorEnabled)
		drawQuality("Interior Quality", settings.InteriorQuality, settings.InteriorCustomSize, true, inInterior);

	if (REL::Module::IsVR()) {
		if (ImGui::Checkbox("Disable Weather-Driven Volumetric Lighting During Rain", &settings.DisableWeatherInteractionDuringRain))
			SetupVL();
	}
}

void VolumetricLighting::DrawEssentialSettings()
{
	SanitizeSettings();

	auto drawVRRestartHint = [] {
		if (!globals::game::isVR) {
			return;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(VR restart required)");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("In VR, this change needs a restart before it fully applies.");
		}
	};

	if (ImGui::Checkbox("Enable in Exteriors", &settings.ExteriorEnabled))
		SetupVL();
	drawVRRestartHint();

	if (ImGui::Checkbox("Enable in Interiors", &settings.InteriorEnabled))
		SetupVL();
	drawVRRestartHint();
}

json VolumetricLighting::CapturePerformanceSettingsState() const
{
	return settings;
}

void VolumetricLighting::DrawGodrayTuningSettings()
{
	ImGui::SeparatorText("Godray Tuning");
	const bool tuningAvailable = IsImageSpaceReplacementEnabled();
	if (!tuningAvailable) {
		ImGui::TextDisabled("Godray tuning requires ImageSpace pixel-shader replacement.");
	}

	ImGui::BeginDisabled(!tuningAvailable);
	DrawGodrayProfileSettings("Exterior Godrays", settings.ExteriorGodrays);
	DrawGodrayProfileSettings("Interior Godrays", settings.InteriorGodrays);
	ImGui::EndDisabled();
}

void VolumetricLighting::DrawGodrayProfileSettings(const char* label, GodrayProfile& profile)
{
	if (!ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::PushID(label);
	auto drawSlider = [](const char* sliderLabel, float& value, float minValue, float maxValue, const char* tooltip) {
		ImGui::SliderFloat(sliderLabel, &value, minValue, maxValue, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(tooltip);
	};

	drawSlider("Godray Intensity", profile.ShaftIntensity, 0.0f, VolumetricLightingTuning::kShaftIntensityMax, "Linearly scales volumetric godray brightness.");
	drawSlider("Godray Opacity", profile.Opacity, 0.0f, VolumetricLightingTuning::kOpacityMax, "Shapes shaft visibility after temporal blending without changing weather density. 1.0 is default.");
	drawSlider("Godray Saturation", profile.Saturation, 0.0f, VolumetricLightingTuning::kSaturationMax, "Adjusts the authored godray color with gamut-preserving saturation. 1.0 is default.");

	drawSlider("Custom Color Contribution", profile.CustomColorContribution, 0.0f, 1.0f, "Blends your custom color into the authored weather godray color.");
	const bool customColorDisabled = profile.CustomColorContribution <= VolumetricLightingTuning::kFloatEpsilon;
	ImGui::BeginDisabled(customColorDisabled);
	drawSlider("Custom Color Red", profile.CustomColorRed, 0.0f, 1.0f, "Red channel for custom volumetric color.");
	drawSlider("Custom Color Green", profile.CustomColorGreen, 0.0f, 1.0f, "Green channel for custom volumetric color.");
	drawSlider("Custom Color Blue", profile.CustomColorBlue, 0.0f, 1.0f, "Blue channel for custom volumetric color.");
	ImGui::EndDisabled();
	ImGui::PopID();
	ImGui::TreePop();
}

void VolumetricLighting::DrawVolumetricLightingSettings(int32_t& quality, TextureSize& customSize, const bool isInterior, const bool inLocationType)
{
	quality = ClampQualityIndex(quality);
	auto& [Width, Height, Depth] = FetchCurrentSizeInUnits(isInterior);

	if (ImGui::SliderInt(isInterior ? "Interior Quality" : "Exterior Quality", &quality, 0, static_cast<uint8_t>(Quality::Count) - 1, QualityNames[quality])) {
		if (inLocationType)
			SetupVL();
	}

	const bool isCustomQuality = static_cast<Quality>(quality) == Quality::Custom;
	if (!isCustomQuality)
		ImGui::BeginDisabled();

	if (ImGui::SliderInt(isInterior ? "Interior Width" : "Exterior Width", &Width, 1, 20, FromUnits(Width, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Width = Width * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? "Interior Height" : "Exterior Height", &Height, 1, 20, FromUnits(Height, 32), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Height = Height * 32;
		if (inLocationType)
			SetupVL();
	}

	if (ImGui::SliderInt(isInterior ? "Interior Depth" : "Exterior Depth", &Depth, 1, 64, FromUnits(Depth, 10), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
		customSize.Depth = Depth * 10;
		if (inLocationType)
			SetupVL();
	}

	if (!isCustomQuality)
		ImGui::EndDisabled();
}

inline const char* VolumetricLighting::FromUnits(const int32_t value, const int32_t unitScale)
{
	static std::string s;
	s = std::to_string(value * unitScale);
	return s.c_str();
}

VolumetricLighting::TextureSize& VolumetricLighting::FetchCurrentSizeInUnits(const bool interior)
{
	auto& size = interior ? interiorSizeInUnits : exteriorSizeInUnits;
	if (interior) {
		const int32_t quality = ClampQualityIndex(settings.InteriorQuality);
		switch (static_cast<Quality>(quality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.InteriorCustomSize;
			break;
		default:
			break;
		}
	} else {
		const int32_t quality = ClampQualityIndex(settings.ExteriorQuality);
		switch (static_cast<Quality>(quality)) {
		case Quality::Low:
			size = *gVolumetricLightingSizeLow;
			break;
		case Quality::Medium:
			size = *gVolumetricLightingSizeMedium;
			break;
		case Quality::High:
			size = defaultSizeHigh;
			break;
		case Quality::Custom:
			size = settings.ExteriorCustomSize;
			break;
		default:
			break;
		}
	}

	size.Height /= 32;
	size.Width /= 32;
	size.Depth /= 10;

	return size;
}

void VolumetricLighting::LoadSettings(json& o_json)
{
	if (!o_json.is_object()) {
		settings = {};
		SanitizeSettings();
		return;
	}

	const auto legacyGodrays = VolumetricLightingTuning::ReadLegacyProfile(o_json);
	auto exteriorGodrays = legacyGodrays;
	if (const auto it = o_json.find("ExteriorGodrays"); it != o_json.end())
		exteriorGodrays = VolumetricLightingTuning::ReadProfile(*it);
	auto interiorGodrays = legacyGodrays;
	if (const auto it = o_json.find("InteriorGodrays"); it != o_json.end())
		interiorGodrays = VolumetricLightingTuning::ReadProfile(*it);

	auto baseSettings = o_json;
	baseSettings.erase("ExteriorGodrays");
	baseSettings.erase("InteriorGodrays");
	settings = baseSettings;
	settings.ExteriorGodrays = exteriorGodrays;
	settings.InteriorGodrays = interiorGodrays;
	if (!REL::Module::IsVR()) {
		settings.DisableWeatherInteractionDuringRain = false;
	}

	SanitizeSettings();
}

void VolumetricLighting::SaveSettings(json& o_json)
{
	SanitizeSettings();
	o_json = settings;
	if (!REL::Module::IsVR()) {
		o_json.erase("DisableWeatherInteractionDuringRain");
	}
}

void VolumetricLighting::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings();
	if (globals::game::isVR) {
		Util::ResetGameSettingsToDefaults(hiddenVREnableSettings);
		Util::ResetGameSettingsToDefaults(hiddenVRWeatherUpdateSettings);
	}
	if (initialised)
		SetupVL();
}

int32_t VolumetricLighting::ClampQualityIndex(int32_t quality)
{
	return std::clamp(quality, static_cast<int32_t>(Quality::Low), static_cast<int32_t>(Quality::Custom));
}

VolumetricLighting::TextureSize VolumetricLighting::ClampTextureSize(const TextureSize& size)
{
	return {
		.Width = std::clamp(size.Width, kTextureWidthMin, kTextureWidthMax),
		.Height = std::clamp(size.Height, kTextureHeightMin, kTextureHeightMax),
		.Depth = std::clamp(size.Depth, kTextureDepthMin, kTextureDepthMax),
	};
}

void VolumetricLighting::SanitizeSettings()
{
	settings.ExteriorGodrays = VolumetricLightingTuning::SanitizeProfile(settings.ExteriorGodrays);
	settings.InteriorGodrays = VolumetricLightingTuning::SanitizeProfile(settings.InteriorGodrays);
	settings.ExteriorQuality = ClampQualityIndex(settings.ExteriorQuality);
	settings.InteriorQuality = ClampQualityIndex(settings.InteriorQuality);
	settings.ExteriorCustomSize = ClampTextureSize(settings.ExteriorCustomSize);
	settings.InteriorCustomSize = ClampTextureSize(settings.InteriorCustomSize);
}

bool VolumetricLighting::IsExteriorEnabled() const
{
	return settings.ExteriorEnabled;
}

bool VolumetricLighting::TryGetActiveGodrayProfile(GodrayProfile& profile) const
{
	const bool currentlyInInterior = LocationContext::HasInteriorCell();
	if (currentlyInInterior) {
		if (!settings.InteriorEnabled || !LocationContext::IsInteriorWithSun())
			return false;
		profile = VolumetricLightingTuning::SanitizeProfile(settings.InteriorGodrays);
	} else {
		if (!settings.ExteriorEnabled)
			return false;
		profile = VolumetricLightingTuning::SanitizeProfile(settings.ExteriorGodrays);
	}

	return true;
}

float VolumetricLighting::GetRuntimeGodrayOpacity() const
{
	if (!loaded || !IsImageSpaceReplacementEnabled())
		return 1.0f;

	GodrayProfile profile{};
	return TryGetActiveGodrayProfile(profile) ? profile.Opacity : 1.0f;
}

bool VolumetricLighting::IsPerformanceCostMeasurementEnabled() const
{
	return inInterior ? settings.InteriorEnabled : settings.ExteriorEnabled;
}

void VolumetricLighting::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	const Settings defaults{};
	if (inInterior) {
		settings.InteriorEnabled = a_enabled;
		if (a_enabled) {
			settings.InteriorQuality = defaults.InteriorQuality;
			settings.InteriorCustomSize = defaults.InteriorCustomSize;
		}
	} else {
		settings.ExteriorEnabled = a_enabled;
		if (a_enabled) {
			settings.ExteriorQuality = defaults.ExteriorQuality;
			settings.ExteriorCustomSize = defaults.ExteriorCustomSize;
		}
	}

	SetupVL();
}

json VolumetricLighting::CapturePerformanceCostMeasurementState() const
{
	return settings;
}

void VolumetricLighting::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	settings = a_state.get<Settings>();
	SanitizeSettings();
	SetupVL();
}

void VolumetricLighting::SetExteriorEnabled(bool enabled)
{
	settings.ExteriorEnabled = enabled;

	if (initialised && !inInterior && globals::game::bEnableVolumetricLighting && gVolumetricLightingSizeHigh) {
		SetupVL();
	}
}

void VolumetricLighting::PostPostLoad()
{
	if (REL::Module::IsVR()) {
		if (settings.ExteriorEnabled || settings.InteriorEnabled) {
			EnableBooleanSettings(hiddenVREnableSettings, GetName());
			const bool weatherInteractionEnabled = !(settings.DisableWeatherInteractionDuringRain && IsRainTransitionActive());
			SetBooleanSettings(hiddenVRWeatherUpdateSettings, GetName(), weatherInteractionEnabled);
		}
		auto address = REL::RelocationID(100475, 0).address() + 0x45b;  // AE not needed, VR only hook
		logger::info("[{}] Hooking CopyResource at {:x}", GetName(), address);
		REL::safe_fill(address, REL::NOP, 7);
		stl::write_thunk_call<CopyResource>(address);

		// Skip volumetric lighting rendering
		REL::safe_write(REL::RelocationID(35560, 0).address() + REL::Relocate(0x254, 0), &REL::JMP8, 1);
		// Move it to render after depth to ensure camera matches rest of scene
		stl::write_thunk_call<RenderDepth>(REL::RelocationID(35560, 0).address() + REL::Relocate(0x2EE, 0));
	}

	stl::write_thunk_call<ApplyVolumetricLighting_VolumetricLightingDescriptor_Get>(REL::RelocationID(100475, 107193).address() + 0x354);

	gVolumetricLightingSizeLow = reinterpret_cast<TextureSize*>(REL::RelocationID(527970, 414916).address());
	gVolumetricLightingSizeMedium = reinterpret_cast<TextureSize*>(REL::RelocationID(527973, 414919).address());
	gVolumetricLightingSizeHigh = reinterpret_cast<TextureSize*>(REL::RelocationID(527976, 414922).address());
	defaultSizeHigh = *gVolumetricLightingSizeHigh;

	// Ensure the VL raymarch compute shader is only dispatched once, rather than once for every level of depth
	// The updated raymarch shader iterates through the depth now instead
	// Skip the first call, the second call has read/write texture setup in the correct order
	REL::safe_fill(REL::RelocationID(100309, 107023).address() + REL::Relocate(0xA4, 0x406), REL::NOP, 3);
	// Exit the loop after the first iteration
	REL::safe_fill(REL::RelocationID(100309, 107023).address() + REL::Relocate(0x147, 0x4A9), REL::NOP, 6);
}

void VolumetricLighting::SetupResources()
{
	vlDataCB = new ConstantBuffer(ConstantBufferDesc<VLData>());
}

void VolumetricLighting::EarlyPrepass()
{
	auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);

	int32_t width = static_cast<int32_t>(renderSize.x);
	int32_t height = static_cast<int32_t>(renderSize.y);

	if (width != vlData.screenX || height != vlData.screenY) {
		blurHCS = nullptr;
		blurVCS = nullptr;
	}

	vlData.screenX = width;
	vlData.screenY = height;
	vlData.screenXMin1 = width - 1;
	vlData.screenYMin1 = height - 1;
	vlDataCB->Update(vlData);

	const bool currentlyInInterior = LocationContext::HasInteriorCell();
	const bool nextInteriorWithSun = LocationContext::IsInteriorWithSun();
	const bool nextRainSuppressionActive =
		globals::game::isVR &&
		settings.DisableWeatherInteractionDuringRain &&
		!currentlyInInterior &&
		IsRainTransitionActive();

	if (initialised &&
		currentlyInInterior == inInterior &&
		nextInteriorWithSun == inInteriorWithSun &&
		nextRainSuppressionActive == rainOnlySuppressionActive)
		return;

	initialised = true;
	inInterior = currentlyInInterior;
	inInteriorWithSun = nextInteriorWithSun;
	rainOnlySuppressionActive = nextRainSuppressionActive;
	SetupVL();
}

void VolumetricLighting::SetupVL()
{
	SanitizeSettings();

	auto* bEnableVolumetricLighting = globals::game::bEnableVolumetricLighting;
	if (!gVolumetricLightingSizeHigh || (!globals::game::isVR && !bEnableVolumetricLighting)) {
		return;
	}

	const bool runtimeEnabled = LocationContext::AllowsEnabledLocations(settings.InteriorEnabled && inInteriorWithSun, settings.ExteriorEnabled, inInterior);
	const int32_t quality = ClampQualityIndex(LocationContext::SelectInteriorExterior(inInterior, settings.InteriorQuality, settings.ExteriorQuality));
	const TextureSize customSize = LocationContext::SelectInteriorExterior(inInterior, settings.InteriorCustomSize, settings.ExteriorCustomSize);

	if (globals::game::isVR) {
		rainOnlySuppressionActive =
			settings.DisableWeatherInteractionDuringRain &&
			!inInterior &&
			IsRainTransitionActive();
		const bool weatherInteractionEnabled = !rainOnlySuppressionActive;
		const bool effectiveWeatherUpdateEnabled = runtimeEnabled && weatherInteractionEnabled;
		SetBooleanSettings(hiddenVREnableSettings, GetName(), runtimeEnabled);
		SetBooleanSettings(hiddenVRWeatherUpdateSettings, GetName(), effectiveWeatherUpdateEnabled);
		if (runtimeEnabled && !effectiveWeatherUpdateEnabled) {
			// Drop stale volumetric history immediately when weather updates are suppressed.
			ClearVolumetricLightingTargets();
		}
	} else {
		*bEnableVolumetricLighting = runtimeEnabled;
	}

	*gVolumetricLightingSizeHigh = static_cast<Quality>(quality) == Quality::Custom ? customSize : defaultSizeHigh;
	SetVLQuality(GetVLDescriptor(), quality);

	if (!runtimeEnabled)
		ClearVolumetricLightingTargets();
}

void VolumetricLighting::ClearVolumetricLightingTargets()
{
	auto* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;
	if (!context || !renderer) {
		return;
	}

	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	auto clearRT = [&](RE::RENDER_TARGET index) {
		auto& target = renderer->GetRuntimeData().renderTargets[index];
		if (target.RTV) {
			context->ClearRenderTargetView(target.RTV, clearColor);
		}
		if (target.UAV) {
			context->ClearUnorderedAccessViewFloat(target.UAV, clearColor);
		}
	};

	clearRT(RE::RENDER_TARGETS::kIMAGESPACE_VOLUMETRIC_LIGHTING);
	clearRT(RE::RENDER_TARGETS::kIMAGESPACE_VOLUMETRIC_LIGHTING_PREVIOUS);
	clearRT(RE::RENDER_TARGETS::kIMAGESPACE_VOLUMETRIC_LIGHTING_COPY);
	clearRT(RE::RENDER_TARGETS::kVOLUMETRIC_LIGHTING_HALF_RES);
	clearRT(RE::RENDER_TARGETS::kVOLUMETRIC_LIGHTING_BLUR_HALF_RES);
	clearRT(RE::RENDER_TARGETS::kVOLUMETRIC_LIGHTING_QUARTER_RES);
	clearRT(RE::RENDER_TARGETS::kVOLUMETRIC_LIGHTING_BLUR_QUARTER_RES);
}

VolumetricLighting::VolumetricLightingDescriptor& VolumetricLighting::GetVLDescriptor()
{
	using func_t = decltype(&VolumetricLighting::GetVLDescriptor);
	static REL::Relocation<func_t> func{ REL::RelocationID(100297, 107014) };
	return func();
}

void VolumetricLighting::SetVLQuality(VolumetricLightingDescriptor& descriptor, const uint32_t quality)
{
	using func_t = decltype(&VolumetricLighting::SetVLQuality);
	static REL::Relocation<func_t> func{ REL::RelocationID(100299, 107016).address() };
	func(descriptor, std::clamp<uint32_t>(quality, 0, 2));
}

void VolumetricLighting::RenderVolumetricLighting(VolumetricLightingDescriptor* descriptor, RE::NiCamera* camera, bool flag)
{
	using func_t = decltype(&VolumetricLighting::RenderVolumetricLighting);
	static REL::Relocation<func_t> func{ REL::RelocationID(100306, 0) };
	func(descriptor, camera, flag);
}

void VolumetricLighting::RenderDepth::thunk()
{
	func();
	if (globals::game::bEnableVolumetricLighting && *globals::game::bEnableVolumetricLighting)
		RenderVolumetricLighting(&GetVLDescriptor(), RE::Main::WorldRootCamera(), false);
}

VolumetricLighting::VolumetricLightingDescriptor* VolumetricLighting::ApplyVolumetricLighting_VolumetricLightingDescriptor_Get::thunk()
{
	auto* descriptor = func();
	if (!descriptor)
		return nullptr;

	auto& feature = globals::features::volumetricLighting;
	if (!IsImageSpaceReplacementEnabled()) {
		return descriptor;
	}

	GodrayProfile profile{};
	const bool hasActiveProfile = feature.TryGetActiveGodrayProfile(profile);
	const float skySyncIntensity = globals::features::skySync.GetVolumetricLightingIntensityFactor();
	const float intensityScale = skySyncIntensity * (hasActiveProfile ? profile.ShaftIntensity : 1.0f);
	const bool needsColorTuning =
		hasActiveProfile &&
		(!VolumetricLightingTuning::IsNear(profile.Saturation, 1.0f) ||
		 !VolumetricLightingTuning::IsNear(profile.CustomColorContribution, 0.0f));
	if (VolumetricLightingTuning::IsNear(intensityScale, 1.0f) && !needsColorTuning)
		return descriptor;

	feature.runtimeDescriptor = *descriptor;
	auto& runtimeDescriptor = feature.runtimeDescriptor;
	if (!VolumetricLightingTuning::IsNear(intensityScale, 1.0f))
		runtimeDescriptor.intensity *= intensityScale;
	if (needsColorTuning)
		ApplyGodrayColorTuning(runtimeDescriptor, profile);

	return std::addressof(runtimeDescriptor);
}

RE::BSImagespaceShader* VolumetricLighting::CreateShader(const std::string_view& name, const std::string_view& fileName, RE::BSComputeShader* computeShader)
{
	auto shader = RE::BSImagespaceShader::Create();
	shader->shaderType = RE::BSShader::Type::ImageSpace;
	shader->fxpFilename = fileName.data();
	shader->name = name.data();
	shader->originalShaderName = fileName.data();
	shader->computeShader = computeShader;
	shader->isComputeShader = true;
	return shader;
}

RE::BSImagespaceShader* VolumetricLighting::GetOrCreateGenerateCS(RE::BSComputeShader* computeShader)
{
	if (generateCS == nullptr)
		generateCS = CreateShader("BSImagespaceShaderVolumetricLightingGenerateCS", "ISVolumetricLightingGenerateCS", computeShader);
	return generateCS;
}

RE::BSImagespaceShader* VolumetricLighting::GetOrCreateRaymarchCS(RE::BSComputeShader* computeShader)
{
	if (raymarchCS == nullptr)
		raymarchCS = CreateShader("BSImagespaceShaderVolumetricLightingRaymarchCS", "ISVolumetricLightingRaymarchCS", computeShader);
	return raymarchCS;
}

RE::BSImagespaceShader* VolumetricLighting::GetOrCreateBlurHCS(RE::BSComputeShader* computeShader)
{
	if (blurHCS == nullptr)
		blurHCS = CreateShader("BSImagespaceShaderVolumetricLightingBlurHCS", "ISVolumetricLightingBlurHCS", computeShader);
	return blurHCS;
}

RE::BSImagespaceShader* VolumetricLighting::GetOrCreateBlurVCS(RE::BSComputeShader* computeShader)
{
	if (blurVCS == nullptr)
		blurVCS = CreateShader("BSImagespaceShaderVolumetricLightingBlurVCS", "ISVolumetricLightingBlurVCS", computeShader);
	return blurVCS;
}

void VolumetricLighting::SetDimensionsCB() const
{
	auto cb = vlDataCB->CB();
	globals::d3d::context->CSSetConstantBuffers(1, 1, &cb);
}

void VolumetricLighting::SetGroupCountsHCS(uint32_t& threadGroupCountX) const
{
	threadGroupCountX = (vlData.screenX + BlurThreadGroupSizeX - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeX - BlurWindow * 2u);
}

void VolumetricLighting::SetGroupCountsVCS(uint32_t& threadGroupCountY) const
{
	threadGroupCountY = (vlData.screenY + BlurThreadGroupSizeY - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeY - BlurWindow * 2u);
}

void VolumetricLighting::CopyResource::thunk(ID3D11DeviceContext* a_this, ID3D11Resource* a_renderTarget, ID3D11Resource* a_renderTargetSource)
{
	// In VR with dynamic resolution enabled, there's a bug with the depth stencil.
	// The depth stencil passed to IsFullScreenVR is scaled down incorrectly.
	// The fix is to stop a CopyResource from replacing kMAIN_COPY with kMAIN after
	// ISApplyVolumetricLighting because it clobbers a properly scaled kMAIN_COPY.
	// The kMAIN_COPY does not appear to be used in the remaining frame after
	// ISApplyVolumetricLighting except for IsFullScreenVR.
	// But, the copy might have to be done manually later after IsFullScreenVR if
	// used in the next frame.

	if (!(Util::IsDynamicResolution() && globals::game::bEnableVolumetricLighting && *globals::game::bEnableVolumetricLighting)) {
		a_this->CopyResource(a_renderTarget, a_renderTargetSource);
	}
}
