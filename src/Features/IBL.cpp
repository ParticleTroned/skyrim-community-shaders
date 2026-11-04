#include "IBL.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "Deferred.h"
#include "DynamicCubemaps.h"
#include "GpuPass.h"
#include "LocationContext.h"
#include "Shadercache.h"
#include "State.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
	constexpr uint32_t kIblPsSrvSlot = 76u;
	constexpr uint32_t kIblPsSrvCount = 4u;
	constexpr float kIBLScaleMin = 0.0f;
	constexpr float kIBLScaleMax = 2.0f;
	constexpr uint kDALCLuminanceRatioMode = 0;
	constexpr uint kDALCColorRatioMode = 1;
	constexpr uint kDALCPlusSkyMode = 2;
	constexpr uint kDALCPlusSkyDirectionalMode = 3;

	void SetIblPsSrvs(ID3D11DeviceContext* a_context,
		ID3D11ShaderResourceView* a_env,
		ID3D11ShaderResourceView* a_sky,
		ID3D11ShaderResourceView* a_staticDiffuse,
		ID3D11ShaderResourceView* a_staticSpecular)
	{
		ID3D11ShaderResourceView* srvs[kIblPsSrvCount] = { a_env, a_sky, a_staticDiffuse, a_staticSpecular };
		a_context->PSSetShaderResources(kIblPsSrvSlot, kIblPsSrvCount, srvs);
	}

	void ClearIblPsSrvs(ID3D11DeviceContext* a_context)
	{
		SetIblPsSrvs(a_context, nullptr, nullptr, nullptr, nullptr);
	}

	bool IsDALCModeDisabled(const IBL::Settings& a_settings)
	{
		return a_settings.DALCAmount <= 0.0f;
	}

	bool IsStaticIBLEnabled(const IBL::Settings& a_settings)
	{
		return a_settings.EnableIBL != 0 && a_settings.UseStaticIBL != 0;
	}

	float ClampFinite(float a_value, float a_min, float a_max, float a_fallback)
	{
		return std::clamp(std::isfinite(a_value) ? a_value : a_fallback, a_min, a_max);
	}

	uint ClampBool(uint a_value)
	{
		return a_value != 0 ? 1u : 0u;
	}

	uint ClampDALCMode(uint a_value)
	{
		return std::min(a_value, kDALCPlusSkyDirectionalMode);
	}

	void DrawEnableCheckbox(const char* a_label, uint& a_disableSetting)
	{
		bool enableSetting = a_disableSetting == 0;
		if (Util::Widgets::Checkbox(a_label, &enableSetting)) {
			a_disableSetting = enableSetting ? 0u : 1u;
		}
	}

	void SanitizeSettings(IBL::Settings& a_settings)
	{
		const IBL::Settings defaults{};
		a_settings.EnableIBL = ClampBool(a_settings.EnableIBL);
		a_settings.PreserveFogLuminance = ClampBool(a_settings.PreserveFogLuminance);
		a_settings.UseStaticIBL = ClampBool(a_settings.UseStaticIBL);
		a_settings.DALCAmount = ClampFinite(a_settings.DALCAmount, 0.0f, 1.0f, defaults.DALCAmount);
		a_settings.EnvIBLScale = ClampFinite(a_settings.EnvIBLScale, kIBLScaleMin, kIBLScaleMax, defaults.EnvIBLScale);
		a_settings.SkyIBLScale = ClampFinite(a_settings.SkyIBLScale, kIBLScaleMin, kIBLScaleMax, defaults.SkyIBLScale);
		a_settings.EnvIBLSaturation = ClampFinite(a_settings.EnvIBLSaturation, 0.0f, 2.0f, defaults.EnvIBLSaturation);
		a_settings.SkyIBLSaturation = ClampFinite(a_settings.SkyIBLSaturation, 0.0f, 2.0f, defaults.SkyIBLSaturation);
		a_settings.FogAmount = ClampFinite(a_settings.FogAmount, 0.0f, 1.0f, defaults.FogAmount);
		a_settings.DALCMode = ClampDALCMode(a_settings.DALCMode);
		a_settings.DisableInInteriors = ClampBool(a_settings.DisableInInteriors);
		a_settings.DisableInWorldMap = ClampBool(a_settings.DisableInWorldMap);
		a_settings.DisableInLoadingScreen = ClampBool(a_settings.DisableInLoadingScreen);
	}

	IBL::Settings GetSanitizedSettings(const IBL::Settings& a_settings)
	{
		IBL::Settings result = a_settings;
		SanitizeSettings(result);
		return result;
	}

	uint GetEffectiveDALCMode(const IBL::Settings& a_settings)
	{
		const IBL::Settings settings = GetSanitizedSettings(a_settings);
		if (IsDALCModeDisabled(settings) && settings.DALCMode >= kDALCPlusSkyMode)
			return kDALCLuminanceRatioMode;

		return settings.DALCMode;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	IBL::Settings,
	EnableIBL,
	PreserveFogLuminance,
	UseStaticIBL,
	DALCAmount,
	EnvIBLScale,
	SkyIBLScale,
	EnvIBLSaturation,
	SkyIBLSaturation,
	FogAmount,
	DALCMode,
	DisableInInteriors,
	DisableInWorldMap,
	DisableInLoadingScreen,
	CaptureWeatherBaselineOnSliderChange)

const char* IBL::GetPerformanceToggleBlockReason() const
{
	return WeatherManager::GetSingleton()->IsVariableOverrideActive("ImageBasedLighting", "EnableIBL") ?
	           "Pause this feature's weather overrides before toggling or measuring it." :
	           nullptr;
}

void IBL::SetPerformanceToggleEnabled(bool a_enabled)
{
	SetPerformanceCostMeasurementEnabled(a_enabled);
	if (settings.CaptureWeatherBaselineOnSliderChange)
		WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(GetShortName(), { "EnableIBL" });
}

void IBL::RestorePerformanceToggleState(const json& a_state)
{
	if (a_state.is_boolean())
		SetPerformanceToggleEnabled(a_state.get<bool>());
}

void IBL::DrawSettingsEnabledControl()
{
	bool enabled = settings.EnableIBL != 0;
	if (Util::WeatherUI::Checkbox("Enabled", this, "EnableIBL", &enabled))
		SetPerformanceToggleEnabled(enabled);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled) &&
		!Util::WeatherUI::IsWeatherControlled(this, "EnableIBL"))
		Util::AddTooltip("On: shaded surfaces receive color and light from their surroundings. Off: uses the original ambient lighting.");
}

void IBL::DrawSettings()
{
	MenuUI::SettingsPage page("IBL", {
										 { "coverage", "Coverage", "Choose where environment lighting is enabled.", "Interior and exterior coverage", true, true, "Choose coverage and light" },
										 { "lighting", "Lighting", "Balance light from the environment and sky.", "Environment and sky contribution", true, true, nullptr },
										 { "colour", "Colour", "Refine colour, brightness matching and fog.", "Colour matching and fog", true, true, "Refine the shared picture" },
									 });

	SanitizeSettings(settings);
	std::set<std::string> changedWeatherBaselines;

	ImGui::BeginDisabled(settings.EnableIBL == 0);
	if (page.Is("coverage")) {
		DrawEnableCheckbox("Enable Interior IBL", settings.DisableInInteriors);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: indoor surfaces receive light and color from their surroundings. Off: uses the usual interior ambient lighting.");
		}
		DrawEnableCheckbox("Enable World Map IBL", settings.DisableInWorldMap);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: environment lighting also affects the world map. Off: keeps its usual lighting.");
		}
		DrawEnableCheckbox("Enable Loading Screen IBL", settings.DisableInLoadingScreen);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: environment lighting also affects loading-screen objects and the main menu. Off: keeps their usual lighting.");
		}
	}
	if (page.Is("lighting")) {
		if (Util::WeatherUI::SliderFloat("Env IBL Scale", this, "EnvIBLScale", &settings.EnvIBLScale, kIBLScaleMin, kIBLScaleMax, "%.2f"))
			changedWeatherBaselines.insert("EnvIBLScale");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Brightness of soft light from nearby surroundings. Higher values brighten shaded surfaces. Requires Dynamic Cubemaps.");
		}
		if (Util::WeatherUI::SliderFloat("Sky IBL Scale", this, "SkyIBLScale", &settings.SkyIBLScale, kIBLScaleMin, kIBLScaleMax, "%.2f"))
			changedWeatherBaselines.insert("SkyIBLScale");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Brightness of soft light from the sky. Higher values brighten surfaces exposed to it.");
		}
	}
	if (page.Is("colour")) {
		if (Util::WeatherUI::SliderFloat("Env IBL Saturation", this, "EnvIBLSaturation", &settings.EnvIBLSaturation, 0.0f, 2.0f, "%.2f"))
			changedWeatherBaselines.insert("EnvIBLSaturation");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Environment IBL saturation: lower gives neutral ambient light; higher gives vivid color.");
		}
		if (Util::WeatherUI::SliderFloat("Sky IBL Saturation", this, "SkyIBLSaturation", &settings.SkyIBLSaturation, 0.0f, 2.0f, "%.2f"))
			changedWeatherBaselines.insert("SkyIBLSaturation");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Sky IBL saturation: lower gives neutral ambient light; higher gives vivid color.");
		}
		if (Util::WeatherUI::SliderFloat("Original ambient matching", this, "DALCAmount", &settings.DALCAmount, 0.0f, 1.0f, "%.2f"))
			changedWeatherBaselines.insert("DALCAmount");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Matches environment-light brightness to the original scene lighting. 0 keeps the new brightness; 1 fully matches the original.");
		}
		{
			int dalcMode = static_cast<int>(settings.DALCMode);
			auto _ = Util::DisableGuard(IsDALCModeDisabled(settings));
			if (Util::Widgets::Combo("Ambient matching style", &dalcMode, "Brightness\0Color\0Original + Sky\0Original + Sky (Directional)\0"))
				settings.DALCMode = static_cast<uint>(dalcMode);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Brightness matches the original light level; Color also preserves its tint. Original + Sky keeps the original ambient light and adds sky light. Directional also shades that original light by direction.");
			}
		}
	}
	if (page.Is("colour")) {
		Util::UIntCheckbox("Use Static IBL For Out-of-World Objects", settings.UseStaticIBL);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: inventory and loading-screen objects use a fixed lighting environment. Off: they use the current environment lighting where available.");
		}
		if (Util::WeatherUI::SliderFloat("Fog Mix", this, "FogAmount", &settings.FogAmount, 0.0f, 1.0f, "%.2f"))
			changedWeatherBaselines.insert("FogAmount");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Blends the fog color toward the IBL ambient color.\n0 = vanilla fog, 1 = fog fully tinted by IBL.");
		}
		Util::UIntCheckbox("Preserve Fog Luminance", settings.PreserveFogLuminance);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("On: changing Fog Mix changes fog color without changing its original brightness. Off: the added tint can also brighten or darken fog.");
		}
		Util::Widgets::Checkbox("Sync Slider Edits to Weather Fallback", &settings.CaptureWeatherBaselineOnSliderChange);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Keeps your slider edits as the default for weather without its own setting. Off: weather changes can restore the previous default.");
		}
	}
	ImGui::EndDisabled();

	if (settings.CaptureWeatherBaselineOnSliderChange &&
		!changedWeatherBaselines.empty()) {
		WeatherVariables::GlobalWeatherRegistry::GetSingleton()
			->CaptureFeatureUserSettings(
				GetShortName(), changedWeatherBaselines);
	}
}

void IBL::LoadSettings(json& o_json)
{
	settings = o_json;
	SanitizeSettings(settings);
}

void IBL::SaveSettings(json& o_json)
{
	o_json = GetSanitizedSettings(settings);
}

void IBL::RestoreDefaultSettings()
{
	settings = {};
	SanitizeSettings(settings);
}

void IBL::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()
	                     ->GetOrCreateFeatureRegistry(GetShortName());
	// Toggle IBL for this weather (SH-based ambient replaces vanilla)
	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"EnableIBL",
		"Enable IBL",
		"Enable or disable SH-based ambient lighting for this weather",
		(bool*)&settings.EnableIBL,
		true,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;  // Switch at transition midpoint
		}));

	// Intensity of environment IBL (from Dynamic Cubemaps)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvIBLScale",
		"Env IBL Scale",
		"Intensity of environment IBL from the Dynamic Cubemaps environment cubemap",
		&settings.EnvIBLScale,
		0.75f,
		kIBLScaleMin, kIBLScaleMax));

	// Intensity of sky IBL (from the game's native reflections cubemap)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyIBLScale",
		"Sky IBL Scale",
		"Intensity of sky IBL from the game's native reflections cubemap",
		&settings.SkyIBLScale,
		1.5f,
		kIBLScaleMin, kIBLScaleMax));

	// Color saturation of environment IBL
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"EnvIBLSaturation",
		"Env IBL Saturation",
		"Color saturation of the environment IBL ambient contribution",
		&settings.EnvIBLSaturation,
		1.0f,
		0.0f, 2.0f));

	// Color saturation of sky IBL
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"SkyIBLSaturation",
		"Sky IBL Saturation",
		"Color saturation of the sky IBL ambient contribution",
		&settings.SkyIBLSaturation,
		1.0f,
		0.0f, 2.0f));

	// How much IBL brightness is matched to vanilla ambient (DALC)
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"DALCAmount",
		"Original ambient matching",
		"Blend factor toward vanilla ambient brightness (0 = pure IBL, 1 = fully matched to DALC)",
		&settings.DALCAmount,
		0.0f,
		0.0f, 1.0f));

	// Fog color blending toward IBL ambient color
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"FogAmount",
		"Fog Mix",
		"Blends fog color toward IBL ambient color (0 = vanilla fog, 1 = fully IBL-tinted)",
		&settings.FogAmount,
		0.0f,
		0.0f, 1.0f));
}

void IBL::ReflectionsPrepass()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && dynamicIBLValid && envIBLTexture && skyIBLTexture;
	const bool staticIBLEnabled = IsStaticIBLEnabled(runtimeSettings) && staticDiffuseIBLTexture && staticSpecularIBLTexture;
	auto* staticDiffuseSrv = staticIBLEnabled ? staticDiffuseIBLTexture->srv.get() : nullptr;
	auto* staticSpecularSrv = staticIBLEnabled ? staticSpecularIBLTexture->srv.get() : nullptr;
	auto* envSrv = dynamicIBLEnabled ? envIBLTexture->srv.get() : nullptr;
	auto* skySrv = dynamicIBLEnabled ? skyIBLTexture->srv.get() : nullptr;

	if (!envSrv && !skySrv && !staticDiffuseSrv && !staticSpecularSrv) {
		ClearIblPsSrvs(context);
		return;
	}

	SetIblPsSrvs(context, envSrv, skySrv, staticDiffuseSrv, staticSpecularSrv);
}

void IBL::Prepass()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;

	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && envIBLTexture && skyIBLTexture;
	const bool staticIBLEnabled = IsStaticIBLEnabled(runtimeSettings) && staticDiffuseIBLTexture && staticSpecularIBLTexture;
	auto* staticDiffuseSrv = staticIBLEnabled ? staticDiffuseIBLTexture->srv.get() : nullptr;
	auto* staticSpecularSrv = staticIBLEnabled ? staticSpecularIBLTexture->srv.get() : nullptr;

	SetIblPsSrvs(context, nullptr, nullptr, staticDiffuseSrv, staticSpecularSrv);
	dynamicIBLValid = false;

	if (!dynamicIBLEnabled)
		return;

	auto* diffuseIBLShader = GetDiffuseIBLCS();
	if (!diffuseIBLShader)
		return;

	std::array<ID3D11ShaderResourceView*, 1> srvs = { dynamicCubemaps.loaded ? dynamicCubemaps.GetPublishedCubemap() : nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { envIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// IBL - Environment cubemap SH projection (skip for DALC-based modes that don't use EnvIBL)
	if (GetEffectiveDALCMode(settings) < kDALCPlusSkyMode) {
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(diffuseIBLShader, nullptr, 0);
		{
			CS_GPU_PASS("IBL::EnvDiffuseIBL");
			context->Dispatch(1, 1, 1);
		}
	} else {
		// Still need to set sampler and shader for sky IBL dispatch below
		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShader(diffuseIBLShader, nullptr, 0);
	}

	// IBL with sky (use game's native reflections cubemap directly)
	{
		auto renderer = globals::game::renderer;
		auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];
		srvs.at(0) = reflections.SRV;
		uavs.at(0) = skyIBLTexture->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		{
			CS_GPU_PASS("IBL::SkyDiffuseIBL");
			context->Dispatch(1, 1, 1);
		}
	}

	// Reset
	{
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		samplers.fill(nullptr);

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Set PS shader resource
	dynamicIBLValid = true;
	SetIblPsSrvs(context, envIBLTexture->srv.get(), skyIBLTexture->srv.get(), staticDiffuseSrv, staticSpecularSrv);
}

void IBL::SetupResources()
{
	GetDiffuseIBLCS();

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
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

		envIBLTexture = new Texture2D(texDesc);
		envIBLTexture->CreateSRV(srvDesc);
		envIBLTexture->CreateUAV(uavDesc);
		skyIBLTexture = new Texture2D(texDesc);
		skyIBLTexture->CreateSRV(srvDesc);
		skyIBLTexture->CreateUAV(uavDesc);
	}

	auto device = globals::d3d::device;

	logger::debug("Loading static Diffuse IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\DiffuseIBL.dds";

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

		staticDiffuseIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "IBL::StaticDiffuse");

		staticDiffuseIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticDiffuseIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		staticDiffuseIBLTexture->CreateSRV(srvDesc);
	}

	logger::debug("Loading static Specular IBL textures...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path = "Data\\Shaders\\IBL\\SpecIBL.dds";

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

		staticSpecularIBLTexture = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "IBL::StaticSpecular");

		staticSpecularIBLTexture->desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = staticSpecularIBLTexture->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE,
			.TextureCube = {
				.MostDetailedMip = 0,
				.MipLevels = 8 }
		};
		staticSpecularIBLTexture->CreateSRV(srvDesc);
	}
}

void IBL::ClearShaderCache()
{
	diffuseIBLCS.Reset();
	dynamicIBLValid = false;
}

ID3D11ComputeShader* IBL::GetDiffuseIBLCS()
{
	std::vector<std::pair<const char*, const char*>> defines;
	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });
	return diffuseIBLCS.Get(
		L"Data\\Shaders\\IBL\\DiffuseIBLCS.hlsl",
		defines,
		"cs_5_0",
		"main",
		"IBL::DiffuseIBLCS");
}

IBL::CommonBufferData IBL::GetCommonBufferData() const
{
	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && dynamicIBLValid && envIBLTexture && skyIBLTexture;
	const bool staticIBLEnabled = IsStaticIBLEnabled(runtimeSettings) && staticDiffuseIBLTexture && staticSpecularIBLTexture;
	return {
		.EnableIBL = dynamicIBLEnabled ? 1u : 0u,
		.PreserveFogLuminance = runtimeSettings.PreserveFogLuminance,
		.pad0 = 0u,
		.DALCAmount = runtimeSettings.DALCAmount,
		.EnvIBLScale = runtimeSettings.EnvIBLScale,
		.SkyIBLScale = runtimeSettings.SkyIBLScale,
		.EnvIBLSaturation = runtimeSettings.EnvIBLSaturation,
		.SkyIBLSaturation = runtimeSettings.SkyIBLSaturation,
		.FogAmount = runtimeSettings.FogAmount,
		.DALCMode = GetEffectiveDALCMode(runtimeSettings),
		.DisableInInteriors = runtimeSettings.DisableInInteriors,
		.EnableStaticIBL = staticIBLEnabled ? 1u : 0u
	};
}

bool IBL::IsRuntimeEnabled() const
{
	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	return loaded &&
	       runtimeSettings.EnableIBL != 0 &&
	       !IsDisabledForCurrentScene(runtimeSettings);
}

bool IBL::IsDisabledForCurrentScene(const IBL::Settings& a_settings) const
{
	const auto state = globals::state;

	if (state && state->IsMainOrLoadingMenuOpen())
		return a_settings.DisableInLoadingScreen != 0;

	if (state && state->isMapMenuOpen)
		return a_settings.DisableInWorldMap != 0;

	return LocationContext::IsDisabledByLocation(a_settings.DisableInInteriors != 0, false);
}
