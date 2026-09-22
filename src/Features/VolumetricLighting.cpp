#include "VolumetricLighting.h"

#include <algorithm>
#include <memory>

#include "LocationContext.h"
#include "ShaderCache.h"
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

	bool ShouldSuppressExteriorDuringRain(const VolumetricLighting::Settings& settings, bool inInterior)
	{
		return settings.DisableWeatherInteractionDuringRain &&
		       !inInterior &&
		       IsRainTransitionActive();
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

	if (ImGui::Checkbox("Disable Exterior Volumetric Lighting During Rain", &settings.DisableWeatherInteractionDuringRain))
		SetupVL();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted("Disables exterior volumetric lighting while rain is active and restores it after rain.");
	}

	DrawGodrayTuningSettings();
	ImGui::Separator();

	if (ImGui::Checkbox("Enable Volumetric Lighting in Exteriors", &settings.ExteriorEnabled))
		SetupVL();

	if (settings.ExteriorEnabled)
		DrawVolumetricLightingSettings(settings.ExteriorQuality, settings.ExteriorCustomSize, false, !inInterior);

	if (ImGui::Checkbox("Enable Volumetric Lighting in Interiors", &settings.InteriorEnabled))
		SetupVL();

	if (settings.InteriorEnabled)
		DrawVolumetricLightingSettings(settings.InteriorQuality, settings.InteriorCustomSize, true, inInterior);
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
	drawSlider("Godray Saturation", profile.Saturation, 0.0f, VolumetricLightingTuning::kSaturationMax, "Adjusts weather godray saturation while preserving brightness. 1.0 is default.");

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

void VolumetricLighting::DrawEssentialSettings()
{
	SanitizeSettings();
	if (ImGui::Checkbox("Enable in Exteriors", &settings.ExteriorEnabled))
		SetupVL();
	if (ImGui::Checkbox("Enable in Interiors", &settings.InteriorEnabled))
		SetupVL();
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

	SanitizeSettings();
}

void VolumetricLighting::SaveSettings(json& o_json)
{
	SanitizeSettings();
	o_json = settings;
}

bool VolumetricLighting::NormalizePerformanceTuningUserSettings(json& a_settings) const
{
	if (!a_settings.is_object())
		return false;

	if (!a_settings.contains("ExteriorGodrays"))
		a_settings["ExteriorGodrays"] = VolumetricLightingTuning::ReadLegacyProfile(a_settings);
	if (!a_settings.contains("InteriorGodrays"))
		a_settings["InteriorGodrays"] = VolumetricLightingTuning::ReadLegacyProfile(a_settings);

	for (const auto* legacyKey : { "GodrayIntensity", "GodrayShaftIntensity", "GodrayOpacity", "GodraySaturation",
			 "CustomColorContribution", "CustomColorRed", "CustomColorGreen", "CustomColorBlue" }) {
		a_settings.erase(legacyKey);
	}
	return true;
}

void VolumetricLighting::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings();
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

VolumetricLighting::GodrayProfile VolumetricLighting::GetRuntimeGodrayProfile() const
{
	GodrayProfile profile{};
	if (loaded && IsImageSpaceReplacementEnabled())
		TryGetActiveGodrayProfile(profile);
	return profile;
}

bool VolumetricLighting::IsPerformanceTuningApplicable() const
{
	if (!initialised)
		return false;
	if (!gVolumetricLightingSizeHigh || !globals::game::bEnableVolumetricLighting)
		return false;
	if (inInterior)
		return inInteriorWithSun;

	return !rainOnlySuppressionActive;
}

const char* VolumetricLighting::GetPerformanceTuningApplicabilityReason() const
{
	if (IsPerformanceTuningApplicable())
		return nullptr;
	if (!initialised) {
		return T(
			"menu.performance_tuning.feature.volumetric_lighting.not_initialized",
			"Volumetric Lighting has not initialized for the current scene yet.");
	}
	if (!gVolumetricLightingSizeHigh || !globals::game::bEnableVolumetricLighting) {
		return T(
			"menu.performance_tuning.feature.volumetric_lighting.runtime_unavailable",
			"Volumetric Lighting cannot be measured because its runtime controls are unavailable.");
	}
	if (inInterior && !inInteriorWithSun) {
		return T(
			"menu.performance_tuning.feature.volumetric_lighting.interior_without_sun",
			"The current interior does not support sunlight volumetric lighting, so there is no runtime work to measure.");
	}
	if (rainOnlySuppressionActive) {
		return T(
			"menu.performance_tuning.feature.volumetric_lighting.rain_suppressed",
			"Exterior Volumetric Lighting is currently suppressed by the Disable During Rain setting.");
	}

	return T(
		"menu.performance_tuning.feature.volumetric_lighting.not_applicable",
		"Volumetric Lighting has no measurable runtime work in the current scene.");
}

void VolumetricLighting::SetExteriorEnabled(bool enabled)
{
	settings.ExteriorEnabled = enabled;

	if (initialised && !inInterior && globals::game::bEnableVolumetricLighting && gVolumetricLightingSizeHigh) {
		SetupVL();
	}
}

void VolumetricLighting::DataLoaded()
{
}

void VolumetricLighting::PostPostLoad()
{
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

void VolumetricLighting::UpdateBlurDimensions()
{
	const auto* viewport = globals::game::graphicsState;
	blurDimensionsValid = false;
	if (!viewport || !vlDataCB)
		return;

	const float2 fullSize{ static_cast<float>(viewport->screenWidth), static_cast<float>(viewport->screenHeight) };
	const auto renderSize = Util::ConvertToDynamic(fullSize);
	const auto validDimension = [](float a_size, float a_minimum, float a_maximum) {
		return std::isfinite(a_size) && a_size >= a_minimum && a_size <= a_maximum;
	};
	if (!validDimension(fullSize.x, 1.0f, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) ||
		!validDimension(fullSize.y, 1.0f, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) ||
		!validDimension(renderSize.x, 1.0f, fullSize.x) ||
		!validDimension(renderSize.y, 1.0f, fullSize.y)) {
		return;
	}

	const auto fullWidth = static_cast<int32_t>(fullSize.x);
	const auto fullHeight = static_cast<int32_t>(fullSize.y);
	if (fullWidth != fullScreenX || fullHeight != fullScreenY) {
		blurHCS = nullptr;
		blurVCS = nullptr;
	}
	fullScreenX = fullWidth;
	fullScreenY = fullHeight;

	vlData.screenX = static_cast<int32_t>(renderSize.x);
	vlData.screenY = static_cast<int32_t>(renderSize.y);
	vlData.screenXMin1 = vlData.screenX - 1;
	vlData.screenYMin1 = vlData.screenY - 1;
	vlDataCB->Update(vlData);
	blurDimensionsValid = true;
}

void VolumetricLighting::EarlyPrepass()
{
	UpdateBlurDimensions();

	const bool currentlyInInterior = LocationContext::HasInteriorCell();
	const bool nextInteriorWithSun = LocationContext::IsInteriorWithSun();
	const bool nextRainSuppressionActive = ShouldSuppressExteriorDuringRain(settings, currentlyInInterior);

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
	if (!gVolumetricLightingSizeHigh || !bEnableVolumetricLighting) {
		return;
	}

	const bool requestedRuntimeEnabled = LocationContext::AllowsEnabledLocations(settings.InteriorEnabled && inInteriorWithSun, settings.ExteriorEnabled, inInterior);
	rainOnlySuppressionActive = ShouldSuppressExteriorDuringRain(settings, inInterior);
	const bool runtimeEnabled = requestedRuntimeEnabled && !rainOnlySuppressionActive;
	const int32_t quality = ClampQualityIndex(LocationContext::SelectInteriorExterior(inInterior, settings.InteriorQuality, settings.ExteriorQuality));
	const TextureSize customSize = LocationContext::SelectInteriorExterior(inInterior, settings.InteriorCustomSize, settings.ExteriorCustomSize);

	*bEnableVolumetricLighting = runtimeEnabled;

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

VolumetricLighting::VolumetricLightingDescriptor* VolumetricLighting::ApplyVolumetricLighting_VolumetricLightingDescriptor_Get::thunk()
{
	auto* descriptor = func();
	if (!descriptor)
		return nullptr;

	auto& feature = globals::features::volumetricLighting;
	if (!IsImageSpaceReplacementEnabled()) {
		return descriptor;
	}

	const auto profile = feature.GetRuntimeGodrayProfile();
	const float skySyncIntensity = globals::features::skySync.GetVolumetricLightingIntensityFactor();
	const float intensityScale = skySyncIntensity * profile.ShaftIntensity;
	if (VolumetricLightingTuning::IsNear(intensityScale, 1.0f))
		return descriptor;

	feature.runtimeDescriptor = *descriptor;
	auto& runtimeDescriptor = feature.runtimeDescriptor;
	runtimeDescriptor.intensity *= intensityScale;

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

void VolumetricLighting::SetGroupCountsHCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const
{
	threadGroupCountX = (vlData.screenX + BlurThreadGroupSizeX - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeX - BlurWindow * 2u);
	threadGroupCountY = static_cast<uint32_t>(vlData.screenY);
}

void VolumetricLighting::SetGroupCountsVCS(uint32_t& threadGroupCountX, uint32_t& threadGroupCountY) const
{
	threadGroupCountX = static_cast<uint32_t>(vlData.screenX);
	threadGroupCountY = (vlData.screenY + BlurThreadGroupSizeY - BlurWindow * 2u - 1u) / (BlurThreadGroupSizeY - BlurWindow * 2u);
}
