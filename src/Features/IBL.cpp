#include "IBL.h"

#include "Deferred.h"
#include "DynamicCubemaps.h"
#include "LocationContext.h"
#include "Shadercache.h"
#include "State.h"
#include "WeatherVariableRegistry.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cmath>

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
		if (ImGui::Checkbox(a_label, &enableSetting)) {
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

void IBL::DrawSettings()
{
	SanitizeSettings(settings);
	bool recaptureWeatherBaseline = false;
	bool enableIBL = settings.EnableIBL != 0;
	if (Util::WeatherUI::Checkbox("Enable IBL", this, "EnableIBL", &enableIBL)) {
		settings.EnableIBL = enableIBL ? 1u : 0u;
		recaptureWeatherBaseline = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggle IBL. When enabled, ambient lighting is derived from cubemap spherical harmonics instead of the vanilla system.");
	}

	ImGui::BeginDisabled(settings.EnableIBL == 0);
	if (ImGui::TreeNodeEx("Enable IBL Options", ImGuiTreeNodeFlags_None)) {
		DrawEnableCheckbox("Enable Interior IBL", settings.DisableInInteriors);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables IBL in interior cells.");
		}
		DrawEnableCheckbox("Enable World Map IBL", settings.DisableInWorldMap);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables IBL while the world map is open.");
		}
		DrawEnableCheckbox("Enable Loading Screen IBL", settings.DisableInLoadingScreen);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Enables IBL during loading screens and the main menu.");
		}
		ImGui::TreePop();
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("Env IBL Scale", this, "EnvIBLScale", &settings.EnvIBLScale, kIBLScaleMin, kIBLScaleMax, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Intensity multiplier for the environment IBL (from Dynamic Cubemaps).\nControls how strongly the surrounding environment contributes to ambient lighting.");
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("Sky IBL Scale", this, "SkyIBLScale", &settings.SkyIBLScale, kIBLScaleMin, kIBLScaleMax, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Intensity multiplier for the sky IBL (from the game's native reflections cubemap).\nControls how strongly the sky contributes to ambient lighting.");
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("Env IBL Saturation", this, "EnvIBLSaturation", &settings.EnvIBLSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Color saturation of the environment IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("Sky IBL Saturation", this, "SkyIBLSaturation", &settings.SkyIBLSaturation, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Color saturation of the sky IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("DALC Amount", this, "DALCAmount", &settings.DALCAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Blends the IBL brightness toward the game's vanilla ambient (DALC) level.\n"
			"0 = no matching (pure IBL brightness), 1 = fully matched to vanilla ambient.");
	}
	{
		int dalcMode = static_cast<int>(settings.DALCMode);
		auto _ = Util::DisableGuard(IsDALCModeDisabled(settings));
		ImGui::Text("DALC Mode");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"How the DALC-to-IBL brightness ratio is computed:\n"
				"Luminance Ratio: Scalar ratio from overall luminance (loses DALC color tint).\n"
				"Color Ratio: Per-channel ratio (preserves DALC color tint).\n"
				"DALC + Sky: Uses vanilla ambient as base, sky IBL on top. Skylighting only affects sky.\n"
				"DALC + Sky (Directional): Same, but Skylighting also dims vanilla ambient per-direction.");
		}
		if (ImGui::BeginTable("##IBLDALCMode", 2, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("Luminance Ratio", &dalcMode, static_cast<int>(kDALCLuminanceRatioMode))) {
				settings.DALCMode = static_cast<uint>(dalcMode);
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("DALC + Sky", &dalcMode, static_cast<int>(kDALCPlusSkyMode))) {
				settings.DALCMode = static_cast<uint>(dalcMode);
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("Color Ratio", &dalcMode, static_cast<int>(kDALCColorRatioMode))) {
				settings.DALCMode = static_cast<uint>(dalcMode);
			}
			ImGui::TableNextColumn();
			if (ImGui::RadioButton("DALC + Sky (Directional)", &dalcMode, static_cast<int>(kDALCPlusSkyDirectionalMode))) {
				settings.DALCMode = static_cast<uint>(dalcMode);
			}
			ImGui::EndTable();
		}
	}
	ImGui::Checkbox("Use Static IBL For Out-of-World Objects", (bool*)&settings.UseStaticIBL);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Uses pre-baked static IBL cubemap textures for objects rendered outside the game world (e.g. inventory items, loading screens).");
	}
	recaptureWeatherBaseline |= Util::WeatherUI::SliderFloat("Fog Mix", this, "FogAmount", &settings.FogAmount, 0.0f, 1.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Blends the fog color toward the IBL ambient color.\n0 = vanilla fog, 1 = fog fully tinted by IBL.");
	}
	ImGui::Checkbox("Preserve Fog Luminance", (bool*)&settings.PreserveFogLuminance);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("When Fog Mix is active, rescales the IBL-tinted fog to keep the original fog brightness.\nPrevents fog from becoming too bright or too dark.");
	}
	ImGui::Checkbox("Sync Slider Edits to Weather Fallback", &settings.CaptureWeatherBaselineOnSliderChange);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Default: OFF.");
		ImGui::Text("When enabled, manual IBL slider edits update the weather fallback baseline.");
		ImGui::Text("This prevents values from snapping back after interior/exterior transitions");
		ImGui::Text("when the active weather has no override for that setting.");
	}
	ImGui::EndDisabled();

	if (settings.CaptureWeatherBaselineOnSliderChange && recaptureWeatherBaseline) {
		WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(GetShortName());
	}
}

void IBL::DrawEssentialSettings()
{
	SanitizeSettings(settings);
	bool enableIBL = settings.EnableIBL != 0;
	if (Util::WeatherUI::Checkbox("Enable IBL", this, "EnableIBL", &enableIBL)) {
		settings.EnableIBL = enableIBL ? 1u : 0u;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggle IBL. When enabled, ambient lighting is derived from cubemap spherical harmonics instead of the vanilla system.");
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
		"DALC Amount",
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
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && envIBLTexture && skyIBLTexture;
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

	auto& envTexture = dynamicCubemaps.envTexture;
	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && envIBLTexture && skyIBLTexture;
	const bool staticIBLEnabled = IsStaticIBLEnabled(runtimeSettings) && staticDiffuseIBLTexture && staticSpecularIBLTexture;
	auto* staticDiffuseSrv = staticIBLEnabled ? staticDiffuseIBLTexture->srv.get() : nullptr;
	auto* staticSpecularSrv = staticIBLEnabled ? staticSpecularIBLTexture->srv.get() : nullptr;

	SetIblPsSrvs(context, nullptr, nullptr, staticDiffuseSrv, staticSpecularSrv);

	if (!dynamicIBLEnabled)
		return;

	std::array<ID3D11ShaderResourceView*, 1> srvs = { (dynamicCubemaps.loaded && envTexture) ? envTexture->srv.get() : nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { envIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// IBL - Environment cubemap SH projection (skip for DALC-based modes that don't use EnvIBL)
	if (GetEffectiveDALCMode(settings) < kDALCPlusSkyMode) {
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
		{
			CS_PROFILE_SCOPE("ImageBasedLighting::EnvDiffuseIBL");
			context->Dispatch(1, 1, 1);
		}
	} else {
		// Still need to set sampler and shader for sky IBL dispatch below
		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
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
			CS_PROFILE_SCOPE("ImageBasedLighting::SkyDiffuseIBL");
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
	if (diffuseIBLCS)
		diffuseIBLCS->Release();
	diffuseIBLCS = nullptr;
}

ID3D11ComputeShader* IBL::GetDiffuseIBLCS()
{
	std::vector<std::pair<const char*, const char*>> defines;
	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });
	if (!diffuseIBLCS)
		diffuseIBLCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\IBL\\DiffuseIBLCS.hlsl", defines, "cs_5_0"));
	return diffuseIBLCS;
}

IBL::CommonBufferData IBL::GetCommonBufferData() const
{
	const IBL::Settings runtimeSettings = GetSanitizedSettings(settings);
	const bool dynamicIBLEnabled = IsRuntimeEnabled() && envIBLTexture && skyIBLTexture;
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
