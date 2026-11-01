#include "I18n/I18n.h"
#include "DynamicCubemaps.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include "GpuPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DynamicCubemaps::Settings,
	EnabledSSR,
	EnabledCreator,
	CubemapResolution);

uint32_t DynamicCubemaps::SanitizeCubemapResolution(uint32_t a_resolution)
{
	return a_resolution == kQualityCubemapResolution || a_resolution == kPerformanceCubemapResolution ? a_resolution : Settings{}.CubemapResolution;
}

void DynamicCubemaps::RefreshActiveCubemapResolution()
{
	settings.CubemapResolution = SanitizeCubemapResolution(settings.CubemapResolution);
	if (!cubemapResolutionLocked) {
		activeCubemapResolution = settings.CubemapResolution;
		activeCubemapMipLevels = std::bit_width(activeCubemapResolution);
	}
}

bool DynamicCubemaps::SetCubemapResolution(uint32_t a_resolution)
{
	if (a_resolution != kPerformanceCubemapResolution && a_resolution != kQualityCubemapResolution)
		return false;

	settings.CubemapResolution = a_resolution;
	RefreshActiveCubemapResolution();
	return true;
}

uint32_t DynamicCubemaps::GetCubemapResolutionForResourceCreation()
{
	if (!cubemapResolutionLocked) {
		logger::info(
			"Using {}x{} dynamic cubemap reflections with {} mip levels",
			activeCubemapResolution,
			activeCubemapResolution,
			activeCubemapMipLevels);
	}
	cubemapResolutionLocked = true;
	return activeCubemapResolution;
}

namespace
{
	RE::NiPoint3 GetCubemapCaptureAnchorPosition()
	{
		return Util::GetEyePosition();
	}
}

std::vector<std::pair<std::string_view, std::string_view>> DynamicCubemaps::GetShaderDefineOptions()
{
	std::vector<std::pair<std::string_view, std::string_view>> result;
	if (settings.EnabledSSR) {
		result.push_back({ "ENABLESSR", "" });
	}

	return result;
}

void DynamicCubemaps::DrawSettings()
{
	MenuUI::SettingsPage page("DynamicCubemaps", {
													 { "quality", T("feature.dynamic_cubemaps.quality", "Quality"), T("feature.dynamic_cubemaps.choose_reflection_resolution_first", "Choose reflection resolution first."), T("feature.dynamic_cubemaps.reflection_resolution_and_cost", "Reflection resolution and cost"), true, true, T("feature.dynamic_cubemaps.choose_reflection_quality", "Choose reflection quality") },
													 { "reflections", T("feature.dynamic_cubemaps.reflections", "Reflections"), T("feature.dynamic_cubemaps.choose_nearby_reflection_detail_and_quality", "Choose nearby reflection detail and quality."), T("feature.dynamic_cubemaps.nearby_water_reflections", "Nearby water reflections"), true, true, nullptr },
													 { "environment", T("feature.dynamic_cubemaps.authoring", "Authoring"), T("feature.dynamic_cubemaps.create_and_export_a_reflection_texture", "Create and export a reflection texture."), T("feature.dynamic_cubemaps.cubemap_creation_and_export", "Cubemap creation and export"), true, true, T("feature.dynamic_cubemaps.refine_updates_and_authoring", "Refine updates and authoring") },
												 });

	if (page.Is("quality")) {
		const std::array<MenuUI::Choice, 2> choices{ { { "performance", T("feature.dynamic_cubemaps.performance", "Performance"), T("feature.dynamic_cubemaps.128_x_128_reflections", "128 x 128 reflections"), T("feature.dynamic_cubemaps.lower_reflection_cost_and_detail", "Lower reflection cost and detail.") },
			{ "quality", T("feature.dynamic_cubemaps.quality", "Quality"), T("feature.dynamic_cubemaps.256_x_256_reflections", "256 x 256 reflections"), T("feature.dynamic_cubemaps.more_reflection_detail_at_higher_cost", "More reflection detail at higher cost.") } } };
		const int choice = MenuUI::ChoiceCards("Reflection resolution", settings.CubemapResolution == kPerformanceCubemapResolution ? 0 : 1, choices);
		if (choice >= 0)
			SetCubemapResolution(choice == 0 ? kPerformanceCubemapResolution : kQualityCubemapResolution);
		if (IsCubemapResolutionRestartRequired()) {
			Util::Text::WrappedWarning(
				T("feature.dynamic_cubemaps.resolution_restart_required", "Restart the game to apply the new reflection resolution. The current session remains at %u x %u."),
				activeCubemapResolution,
				activeCubemapResolution);
		}
	}
	if (page.Is("reflections")) {
		bool enabledSSR = settings.EnabledSSR != 0;
		if (Util::Widgets::Checkbox(T("feature.dynamic_cubemaps.enable_screen_space_reflections", "Enable Screen Space Reflections"), &enabledSSR)) {
			settings.EnabledSSR = enabledSSR ? 1u : 0u;
			recompileFlag = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("feature.dynamic_cubemaps.enable_ssr_tooltip", "Enable Screen Space Reflections on Water"));

		}
	}

	if (page.Is("environment")) {
		static std::string exportMessage;
		static bool exportFailed = false;
		ImGui::Text(T("feature.dynamic_cubemaps.creator_info", "You must enable creator mode by adding the shader define CREATOR"));
		bool enabledCreator = settings.EnabledCreator != 0;
		if (Util::Widgets::Checkbox(T("feature.dynamic_cubemaps.enable_creator", "Enable Creator"), &enabledCreator))
			settings.EnabledCreator = enabledCreator ? 1u : 0u;
		if (settings.EnabledCreator) {
			Util::Widgets::ColorEdit3(T("feature.dynamic_cubemaps.color", "Color"), reinterpret_cast<float*>(&settings.CubemapColor));
			Util::Widgets::SliderFloat(T("feature.dynamic_cubemaps.roughness", "Roughness"), &settings.CubemapColor.w, 0.0f, 1.0f, "%.2f");
			if (ImGui::Button(T("feature.dynamic_cubemaps.export", "Export"))) {
				auto device = globals::d3d::device;
				auto context = globals::d3d::context;

				D3D11_TEXTURE2D_DESC texDesc{};
				texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				texDesc.Height = 1;
				texDesc.Width = 1;
				texDesc.ArraySize = 6;
				texDesc.MipLevels = 1;
				texDesc.SampleDesc.Count = 1;
				texDesc.Usage = D3D11_USAGE_DEFAULT;
				texDesc.BindFlags = 0;
				texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

				D3D11_SUBRESOURCE_DATA subresourceData[6];

				struct PixelData
				{
					uint8_t r, g, b, a;
				};

				static PixelData colorPixel{};

				colorPixel = { (uint8_t)((settings.CubemapColor.x * 255.0f) + 0.5f),
					(uint8_t)((settings.CubemapColor.y * 255.0f) + 0.5f),
					(uint8_t)((settings.CubemapColor.z * 255.0f) + 0.5f),
					std::min((uint8_t)254u, (uint8_t)((settings.CubemapColor.w * 255.0f) + 0.5f)) };

				static PixelData emptyPixel{};

				subresourceData[0].pSysMem = &colorPixel;
				subresourceData[0].SysMemPitch = sizeof(PixelData);
				subresourceData[0].SysMemSlicePitch = sizeof(PixelData);

				for (uint i = 1; i < 6; i++) {
					subresourceData[i].pSysMem = &emptyPixel;
					subresourceData[i].SysMemPitch = sizeof(PixelData);
					subresourceData[i].SysMemSlicePitch = sizeof(PixelData);
				}

				winrt::com_ptr<ID3D11Texture2D> tempTexture;
				DirectX::ScratchImage image;

				try {
					DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, subresourceData, tempTexture.put()));
					Util::SetResourceName(tempTexture.get(), "DynamicCubemaps::CreatorExport");
					DX::ThrowIfFailed(CaptureTexture(device, context, tempTexture.get(), image));

					if (std::filesystem::create_directories(defaultDynamicCubeMapSavePath)) {
						logger::info("Missing DynamicCubeMap Creator directory created: {}", defaultDynamicCubeMapSavePath);
					}

					std::filesystem::path DynamicCubeMapSavePath = defaultDynamicCubeMapSavePath;
					std::filesystem::path filename(std::format("R{:03d}G{:03d}B{:03d}A{:03d}.dds", colorPixel.r, colorPixel.g, colorPixel.b, colorPixel.a));
					DynamicCubeMapSavePath /= filename;

					if (std::filesystem::exists(DynamicCubeMapSavePath)) {
						logger::info("DynamicCubeMap Creator file for {} already exists, skipping.", filename.string());
						exportMessage = std::format("Already exists: {}", DynamicCubeMapSavePath.string());
						exportFailed = false;
					} else {
						DX::ThrowIfFailed(SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS::DDS_FLAGS_NONE, DynamicCubeMapSavePath.c_str()));
						logger::info("DynamicCubeMap Creator file for {} written", filename.string());
						exportMessage = std::format("Saved: {}", DynamicCubeMapSavePath.string());
						exportFailed = false;
					}

				} catch (const std::exception& e) {
					logger::error("Failed in DynamicCubeMap Creator file: {} {}", defaultDynamicCubeMapSavePath, e.what());
					exportMessage = std::format("Cubemap export failed: {}", e.what());
					exportFailed = true;
				}

				image.Release();
			}
		}
		if (!exportMessage.empty()) {
			if (exportFailed)
				Util::Text::WrappedError("%s", exportMessage.c_str());
			else
				MenuUI::DetailText(exportMessage.c_str());
		}
	}

}

void DynamicCubemaps::LoadSettings(json& o_json)
{
	settings = o_json;
	if (settings.CubemapResolution != kPerformanceCubemapResolution && settings.CubemapResolution != kQualityCubemapResolution) {
		logger::warn(
			"Unsupported dynamic cubemap resolution {}; using {}",
			settings.CubemapResolution,
			Settings{}.CubemapResolution);
	}
	RefreshActiveCubemapResolution();

	recompileFlag = true;
}

void DynamicCubemaps::SaveSettings(json& o_json)
{
	o_json = settings;
}



void DynamicCubemaps::RestoreDefaultSettings()
{
	settings = {};
	RefreshActiveCubemapResolution();

	recompileFlag = true;
}

bool DynamicCubemaps::IsSSRRuntimeActive() const
{
	return loaded && settings.EnabledSSR != 0;
}

void DynamicCubemaps::DataLoaded()
{

	MenuOpenCloseEventHandler::Register();
}



RE::BSEventNotifyControl MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When entering a new cell, reset the capture
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		if (!a_event->opening) {
			auto& dynamicCubemaps = globals::features::dynamicCubemaps;
			dynamicCubemaps.resetCapture[0] = true;
			dynamicCubemaps.resetCapture[1] = true;
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

bool MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = globals::game::ui;

	if (!ui) {
		logger::error("UI event source not found");
		return false;
	}

	ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&singleton);

	logger::info("Registered {}", typeid(singleton).name());

	return true;
}

void DynamicCubemaps::ClearShaderCache()
{
	resetCapture[0] = resetCapture[1] = true;
	cubemapValid[0] = cubemapValid[1] = false;
	nextTask = NextTask::kCapture;
	detectCaptureLightingCS.Reset();
	detectCaptureLightingReflectionsCS.Reset();
	updateCubemapCS.Reset();
	updateCubemapReflectionsCS.Reset();
	updateCubemapFakeReflectionsCS.Reset();
	inferCubemapCS.Reset();
	inferCubemapReflectionsCS.Reset();
	inferCubemapFakeReflectionsCS.Reset();
	specularIrradianceCS.Reset();
	bc6hEncodeCS.Reset();
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderDetectLighting(bool a_reflections)
{
	if (a_reflections && !fakeReflections)
		return detectCaptureLightingReflectionsCS.Get(
			L"Data\\Shaders\\DynamicCubemaps\\DetectCaptureLightingCS.hlsl", { { "REFLECTIONS", "" } }, "cs_5_0", "main",
			"DynamicCubemaps::DetectCaptureLightingReflectionsCS");
	return detectCaptureLightingCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\DetectCaptureLightingCS.hlsl", {}, "cs_5_0", "main",
		"DynamicCubemaps::DetectCaptureLightingCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdate()
{
	return updateCubemapCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl", {}, "cs_5_0", "main",
		"DynamicCubemaps::UpdateCubemapCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateReflections()
{
	return updateCubemapReflectionsCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl",
		{ { "REFLECTIONS", "" } }, "cs_5_0", "main",
		"DynamicCubemaps::UpdateCubemapReflectionsCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderUpdateFakeReflections()
{
	return updateCubemapFakeReflectionsCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\UpdateCubemapCS.hlsl",
		{ { "FAKEREFLECTIONS", "" } }, "cs_5_0", "main",
		"DynamicCubemaps::UpdateCubemapFakeReflectionsCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrence()
{
	return inferCubemapCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl", {}, "cs_5_0", "main",
		"DynamicCubemaps::InferCubemapCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceReflections()
{
	return inferCubemapReflectionsCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl",
		{ { "REFLECTIONS", "" } }, "cs_5_0", "main",
		"DynamicCubemaps::InferCubemapReflectionsCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderInferrenceFakeReflections()
{
	return inferCubemapFakeReflectionsCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\InferCubemapCS.hlsl",
		{ { "FAKEREFLECTIONS", "" } }, "cs_5_0", "main",
		"DynamicCubemaps::InferCubemapFakeReflectionsCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderSpecularIrradiance()
{
	return specularIrradianceCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\SpecularIrradianceCS.hlsl", {}, "cs_5_0", "main",
		"DynamicCubemaps::SpecularIrradianceCS");
}

ID3D11ComputeShader* DynamicCubemaps::GetComputeShaderBC6HEncode()
{
	return bc6hEncodeCS.Get(
		L"Data\\Shaders\\DynamicCubemaps\\BC6HEncodeCS.hlsl", {}, "cs_5_0", "main",
		"DynamicCubemaps::BC6HEncodeCS");
}

bool DynamicCubemaps::UpdateCubemapCapture(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto* detector = GetComputeShaderDetectLighting(a_reflections);
	auto* shader = a_reflections ?
	                   (fakeReflections ? GetComputeShaderUpdateFakeReflections() : GetComputeShaderUpdateReflections()) :
	                   GetComputeShaderUpdate();
	if (!shader || !detector || !captureLightingState || !updateCubemapCB || !context || !renderer)
		return false;

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!depth.depthSRV || !main.SRV)
		return false;

	const uint index = a_reflections ? 1 : 0;
	const auto captureAnchor = GetCubemapCaptureAnchorPosition();
	const float3 currentCaptureAnchor{ captureAnchor.x, captureAnchor.y, captureAnchor.z };
	const float captureTime = globals::state->timer;
	UpdateCubemapCB updateData{};
	updateData.CameraPosAdjustDelta = previousCaptureAnchor[index] - currentCaptureAnchor;
	updateData.CapturePadding0 = 0;
	updateData.CaptureIndex = index;
	updateData.CaptureDeltaTime = std::max(0.0f, captureTime - previousCaptureTime[index]);
	updateData.ResetCapture = resetCapture[index] || captureTime < previousCaptureTime[index];
	updateCubemapCB->Update(updateData);

	// Private bindings must be released even if capture exits before publication.
	const SKSE::stl::scope_exit releaseBindings([&]() noexcept {
		ID3D11UnorderedAccessView* uavs[4]{};
		ID3D11ShaderResourceView* srvs[2]{};
		ID3D11Buffer* buffer = nullptr;
		ID3D11SamplerState* sampler = nullptr;
		context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetConstantBuffers(0, 1, &buffer);
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(nullptr, nullptr, 0);
	});

	ID3D11ShaderResourceView* srvs[2] = { depth.depthSRV, main.SRV };
	ID3D11UnorderedAccessView* uavs[4] = {
		(a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->uav.get(),
		(a_reflections ? envCaptureRawReflectionsTexture : envCaptureRawTexture)->uav.get(),
		(a_reflections ? envCapturePositionReflectionsTexture : envCapturePositionTexture)->uav.get(),
		captureLightingState->UAV()
	};
	ID3D11Buffer* buffer = updateCubemapCB->CB();
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &buffer);
	context->CSSetSamplers(0, 1, &computeSampler);

	context->CSSetShader(detector, nullptr, 0);
	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::DetectLightingReflections", "DynamicCubemaps::DetectLighting");
		context->Dispatch(1, 1, 1);
	}
	context->CSSetShader(shader, nullptr, 0);
	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::CaptureReflections", "DynamicCubemaps::Capture");
		context->Dispatch((envCaptureTexture->desc.Width + 7u) / 8u, (envCaptureTexture->desc.Height + 7u) / 8u, 6);
	}
	previousCaptureAnchor[index] = currentCaptureAnchor;
	previousCaptureTime[index] = captureTime;
	resetCapture[index] = false;
	return true;
}

bool DynamicCubemaps::Inferrence(bool a_reflections)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto* shader = a_reflections ?
	                   (fakeReflections ? GetComputeShaderInferrenceFakeReflections() : GetComputeShaderInferrenceReflections()) :
	                   GetComputeShaderInferrence();
	if (!shader)
		return false;

	// Infer local reflection information
	ID3D11UnorderedAccessView* uav = envInferredTexture->uav.get();

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->GenerateMips((a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get());

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	ID3D11ShaderResourceView* srvs[3] = { (a_reflections ? envCaptureReflectionsTexture : envCaptureTexture)->srv.get(), cubemap.SRV, defaultCubemap };
	context->CSSetShaderResources(0, 3, srvs);

	context->CSSetSamplers(0, 1, &computeSampler);

	context->CSSetShader(shader, nullptr, 0);

	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::InferReflections", "DynamicCubemaps::Infer");
		context->Dispatch((uint32_t)std::ceil(envCaptureTexture->desc.Width / 8.0f), (uint32_t)std::ceil(envCaptureTexture->desc.Height / 8.0f), 6);
	}

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	srvs[2] = nullptr;
	context->CSSetShaderResources(0, 3, srvs);

	uav = nullptr;

	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetShader(nullptr, 0, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);
	return true;
}

bool DynamicCubemaps::Irradiance(bool a_reflections)
{
	auto context = globals::d3d::context;
	auto* shader = GetComputeShaderSpecularIrradiance();
	if (!shader)
		return false;
	const auto mipLevels = activeCubemapMipLevels;

	// Copy cubemap to other resources
	for (uint face = 0; face < 6; face++) {
		uint srcSubresourceIndex = D3D11CalcSubresource(0, face, mipLevels);
		context->CopySubresourceRegion(envFilteredTexture->resource.get(), D3D11CalcSubresource(0, face, mipLevels), 0, 0, 0, envInferredTexture->resource.get(), srcSubresourceIndex, nullptr);
	}

	// Compute pre-filtered specular environment map.
	{
		auto srv = envInferredTexture->srv.get();
		context->GenerateMips(srv);

		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetSamplers(0, 1, &computeSampler);
		context->CSSetShader(shader, nullptr, 0);

		ID3D11Buffer* buffer = spmapCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		float const delta_roughness = 1.0f / std::max(float(mipLevels - 1), 1.0f);

		std::uint32_t size = std::max(envTexture->desc.Width, envTexture->desc.Height) / 2;

		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::IrradianceReflections", "DynamicCubemaps::Irradiance");
		for (std::uint32_t level = 1; level < mipLevels; level++, size /= 2) {
			const UINT numGroups = (UINT)std::max(1u, (size + 7u) / 8u);

			const SpecularMapFilterSettingsCB spmapConstants = { level * delta_roughness };
			spmapCB->Update(spmapConstants);

			auto uav = filteredUAVs[level - 1].get();

			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->Dispatch(numGroups, numGroups, 6);
		}
	}

	ID3D11ShaderResourceView* nullSRV = { nullptr };
	ID3D11SamplerState* nullSampler = { nullptr };
	ID3D11Buffer* nullBuffer = { nullptr };
	ID3D11UnorderedAccessView* nullUAV = { nullptr };

	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetShader(nullptr, 0, 0);
	context->CSSetConstantBuffers(0, 1, &nullBuffer);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	return true;
}

bool DynamicCubemaps::CompressToBC6H(bool a_reflections)
{
	auto context = globals::d3d::context;

	auto shader = GetComputeShaderBC6HEncode();
	if (!shader) {
		logger::error("BC6HEncodeCS failed to compile; BC6H compression disabled");
		return false;
	}

	auto* srcSRV = envFilteredTextureArraySRV.get();

	context->CSSetShader(shader, nullptr, 0);
	context->CSSetShaderResources(0, 1, &srcSRV);

	ID3D11Buffer* cb = bc6hEncodeCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);

	std::uint32_t mipDim = std::max(envTexture->desc.Width, envTexture->desc.Height);

	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::BC6HReflections", "DynamicCubemaps::BC6H");
		for (std::uint32_t level = 0; level < bc6hMipLevels; ++level) {
			std::uint32_t srcWidth = std::max(1u, mipDim >> level);
			std::uint32_t srcHeight = std::max(1u, mipDim >> level);
			std::uint32_t blocksX = std::max(1u, srcWidth / 4);
			std::uint32_t blocksY = std::max(1u, srcHeight / 4);

			BC6HEncodeCB cbData{};
			cbData.TextureSizeInBlocksX = blocksX;
			cbData.TextureSizeInBlocksY = blocksY;
			cbData.MipLevel = level;
			bc6hEncodeCB->Update(cbData);

			auto* uav = bc6hScratchUAVs[level].get();
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

			std::uint32_t dispatchX = std::max(1u, (blocksX + 7) / 8);
			std::uint32_t dispatchY = std::max(1u, (blocksY + 7) / 8);
			context->Dispatch(dispatchX, dispatchY, 6);
		}
	}

	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Each UINT4 texel contains one BC6H block, including the padded 2x2 and 1x1 mips.
	auto* dst = (a_reflections ? envReflectionsTextureBC6H : envTextureBC6H).get();
	{
		CS_GPU_PASS_SELECT(a_reflections, "DynamicCubemaps::PublishReflections", "DynamicCubemaps::Publish");
		for (uint32_t level = 0; level < bc6hMipLevels; ++level) {
			const bool tail = level >= bc6hScratchMipLevels;
			auto* src = (tail ? bc6hTailScratchTexture : bc6hScratchTexture).get();
			for (uint32_t face = 0; face < dst->desc.ArraySize; ++face) {
				const auto srcSubresource = tail ?
				                                D3D11CalcSubresource(0, (level - bc6hScratchMipLevels) * dst->desc.ArraySize + face, 1) :
				                                D3D11CalcSubresource(level, face, bc6hScratchMipLevels);
				context->CopySubresourceRegion(dst->resource.get(), D3D11CalcSubresource(level, face, bc6hMipLevels),
					0, 0, 0, src->resource.get(), srcSubresource, nullptr);
			}
		}
		context->CopyResource((a_reflections ? envReflectionsTexture : envTexture)->resource.get(), envFilteredTexture->resource.get());
	}
	cubemapValid[a_reflections ? 1 : 0] = true;
	return true;
}











void DynamicCubemaps::UpdateCubemap()
{
	ZoneScoped;
	CS_GPU_PASS("DynamicCubemaps::UpdateCubemap");

	auto context = globals::d3d::context;
	if (!context || !captureResourcesReady)
		return;
	Util::BindSharedDataConstantBuffersForCS(context);
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (captureRefreshRequested.exchange(false, std::memory_order_acq_rel)) {
		resetCapture[0] = resetCapture[1] = true;
		nextTask = NextTask::kCapture;
	}
#endif

	// Reset capture when game time jumps (wait menu, timescale changes, console commands)
	if (auto calendar = globals::game::calendar) {
		float currentHoursPassed = calendar->GetHoursPassed();
		float hoursPassedDiff = std::abs(currentHoursPassed - previousHoursPassed);
		previousHoursPassed = currentHoursPassed;

		if (hoursPassedDiff >= 0.01f) {  // ~36 seconds game time
			resetCapture[0] = true;
			resetCapture[1] = true;
			nextTask = NextTask::kCapture;
		}
	}

	if (recompileFlag) {
		logger::debug("Recompiling for Dynamic Cubemaps");
		auto shaderCache = globals::shaderCache;
		if (!shaderCache->Clear("Data//Shaders//ISReflectionsRayTracing.hlsl"))
			// if can't find specific hlsl file cache, clear all image space files
			shaderCache->Clear(RE::BSShader::Types::ImageSpace);
		recompileFlag = false;
	}



	switch (nextTask) {
	case NextTask::kCapture:
		if (UpdateCubemapCapture(false))
			nextTask = NextTask::kInferrence;
		break;

	case NextTask::kInferrence:
		if (Inferrence(false))
			nextTask = NextTask::kIrradiance;
		break;

	case NextTask::kIrradiance:
		if (Irradiance(false))
			nextTask = NextTask::kBC6HCompress;
		break;

	case NextTask::kBC6HCompress:
		if (CompressToBC6H(false))
			nextTask = activeReflections ? NextTask::kCapture2 : NextTask::kCapture;
		break;

	case NextTask::kCapture2:
		if (UpdateCubemapCapture(true))
			nextTask = NextTask::kInferrence2;
		break;

	case NextTask::kInferrence2:
		if (Inferrence(true))
			nextTask = NextTask::kIrradiance2;
		break;

	case NextTask::kIrradiance2:
		if (Irradiance(true))
			nextTask = NextTask::kBC6HCompress2;
		break;

	case NextTask::kBC6HCompress2:
		if (CompressToBC6H(true))
			nextTask = NextTask::kCapture;
		break;
	}

}

ID3D11ShaderResourceView* DynamicCubemaps::GetPublishedCubemap(bool a_reflections) const
{
	const auto* texture = a_reflections ? envReflectionsTexture : envTexture;
	return cubemapValid[a_reflections ? 1 : 0] && texture ? texture->srv.get() : nullptr;
}

void DynamicCubemaps::PostDeferred()
{
	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* views[2] = {
		cubemapValid[activeReflections ? 1 : 0] ? (activeReflections ? envReflectionsTextureBC6H : envTextureBC6H)->srv.get() : nullptr,
		cubemapValid[0] ? envTextureBC6H->srv.get() : nullptr
	};
	context->PSSetShaderResources(30, 2, views);
}

bool DynamicCubemaps::SetupCapturePublicationResources(const D3D11_TEXTURE2D_DESC& a_desc, const D3D11_SHADER_RESOURCE_VIEW_DESC& a_srvDesc)
{
	if (!a_desc.Width || !a_desc.Height || a_desc.ArraySize != 6 ||
		a_desc.MipLevels < 2 || a_desc.MipLevels > filteredUAVs.size() + 1) {
		logger::error("Dynamic cubemap dimensions or mip count are unsupported; capture disabled");
		return false;
	}

	try {
		auto device = globals::d3d::device;
		auto filtered = std::make_unique<Texture2D>(a_desc, "DynamicCubemaps::EnvFiltered");
		filtered->CreateSRV(a_srvDesc);
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 8> views;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = a_desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.ArraySize = 6;
		for (uint32_t level = 1; level < a_desc.MipLevels; ++level) {
			uavDesc.Texture2DArray.MipSlice = level;
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(filtered->resource.get(), &uavDesc, views[level - 1].put()));
			Util::SetResourceName(views[level - 1].get(), "DynamicCubemaps::EnvFiltered UAV mip%u", level);
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC arrayDesc{};
		arrayDesc.Format = a_desc.Format;
		arrayDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		arrayDesc.Texture2DArray.ArraySize = 6;
		arrayDesc.Texture2DArray.MipLevels = a_desc.MipLevels;
		winrt::com_ptr<ID3D11ShaderResourceView> arrayView;
		DX::ThrowIfFailed(device->CreateShaderResourceView(filtered->resource.get(), &arrayDesc, arrayView.put()));
		Util::SetResourceName(arrayView.get(), "DynamicCubemaps::EnvFiltered ArraySRV");

		auto lighting = std::make_unique<StructuredBuffer>(
			StructuredBufferDesc<CaptureLightingState>(uint64_t(1), true, false), 1, "DynamicCubemaps::CaptureLightingState");
		lighting->CreateUAV();
		const UINT clear[4]{};
		globals::d3d::context->ClearUnorderedAccessViewUint(lighting->UAV(), clear);

		envFilteredTexture = std::move(filtered);
		filteredUAVs = std::move(views);
		envFilteredTextureArraySRV = std::move(arrayView);
		captureLightingState = std::move(lighting);
		return true;
	} catch (const winrt::hresult_error& error) {
		logger::error("Dynamic cubemap publication resources failed (HRESULT {}); capture disabled", error.code().value);
	} catch (const std::exception& error) {
		logger::error("Dynamic cubemap publication resources failed ({}); capture disabled", error.what());
	}
	return false;
}

bool DynamicCubemaps::SetupCompressionResources(const D3D11_TEXTURE2D_DESC& a_desc)
{
	if ((a_desc.Width != kPerformanceCubemapResolution && a_desc.Width != kQualityCubemapResolution) ||
		a_desc.Height != a_desc.Width || a_desc.ArraySize != 6 || a_desc.SampleDesc.Count != 1 ||
		a_desc.MipLevels != static_cast<UINT>(std::bit_width(a_desc.Width)) || a_desc.MipLevels > bc6hScratchUAVs.size()) {
		logger::error("Dynamic cubemap compression dimensions or mip count are unsupported; capture disabled");
		return false;
	}

	auto* device = globals::d3d::device;
	const uint32_t scratchMipLevels = std::bit_width(a_desc.Width / 4);
	D3D11_TEXTURE2D_DESC scratchDesc{};
	scratchDesc.Width = a_desc.Width / 4;
	scratchDesc.Height = a_desc.Height / 4;
	scratchDesc.MipLevels = scratchMipLevels;
	scratchDesc.ArraySize = a_desc.ArraySize;
	scratchDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	scratchDesc.SampleDesc.Count = 1;
	scratchDesc.Usage = D3D11_USAGE_DEFAULT;
	scratchDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	auto scratch = std::make_unique<Texture2D>(scratchDesc, "DynamicCubemaps::BC6HScratch");

	// Every mip below 4x4 still needs one block per face; it cannot shrink with the scratch chain.
	auto tailDesc = scratchDesc;
	tailDesc.Width = 1;
	tailDesc.Height = 1;
	tailDesc.MipLevels = 1;
	tailDesc.ArraySize = a_desc.ArraySize * (a_desc.MipLevels - scratchMipLevels);
	auto tailScratch = std::make_unique<Texture2D>(tailDesc, "DynamicCubemaps::BC6HTailScratch");

	decltype(bc6hScratchUAVs) views;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = scratchDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.ArraySize = a_desc.ArraySize;
	for (uint32_t level = 0; level < a_desc.MipLevels; ++level) {
		const bool tail = level >= scratchMipLevels;
		uavDesc.Texture2DArray.MipSlice = tail ? 0 : level;
		uavDesc.Texture2DArray.FirstArraySlice = tail ? a_desc.ArraySize * (level - scratchMipLevels) : 0;
		auto* texture = (tail ? tailScratch : scratch).get();
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture->resource.get(), &uavDesc, views[level].put()));
		Util::SetResourceName(views[level].get(), "DynamicCubemaps::%s UAV mip%u", tail ? "BC6HTailScratch" : "BC6HScratch", level);
	}

	D3D11_TEXTURE2D_DESC compressedDesc{};
	compressedDesc.Width = a_desc.Width;
	compressedDesc.Height = a_desc.Height;
	compressedDesc.MipLevels = a_desc.MipLevels;
	compressedDesc.ArraySize = a_desc.ArraySize;
	compressedDesc.Format = DXGI_FORMAT_BC6H_UF16;
	compressedDesc.SampleDesc.Count = 1;
	compressedDesc.Usage = D3D11_USAGE_DEFAULT;
	compressedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	compressedDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = compressedDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = a_desc.MipLevels;
	auto environment = std::make_unique<Texture2D>(compressedDesc, "DynamicCubemaps::EnvTextureBC6H");
	environment->CreateSRV(srvDesc);
	auto reflections = std::make_unique<Texture2D>(compressedDesc, "DynamicCubemaps::EnvReflectionsBC6H");
	reflections->CreateSRV(srvDesc);

	bc6hScratchTexture = std::move(scratch);
	bc6hTailScratchTexture = std::move(tailScratch);
	bc6hScratchUAVs = std::move(views);
	envTextureBC6H = std::move(environment);
	envReflectionsTextureBC6H = std::move(reflections);
	bc6hScratchMipLevels = scratchMipLevels;
	bc6hMipLevels = a_desc.MipLevels;
	return true;
}

void DynamicCubemaps::SetupResources()
{
	captureResourcesReady = false;
	cubemapValid[0] = cubemapValid[1] = false;
	try {
		SetupResourcesImpl();
	} catch (const winrt::hresult_error& error) {
		logger::error("Dynamic cubemap resources failed (HRESULT {}); capture disabled", error.code().value);
	} catch (const std::exception& error) {
		logger::error("Dynamic cubemap resources failed ({}); capture disabled", error.what());
	}
}

void DynamicCubemaps::SetupResourcesImpl()
{
	cubemapValid[0] = cubemapValid[1] = false;
	resetCapture[0] = resetCapture[1] = true;
	nextTask = NextTask::kCapture;
	envFilteredTexture.reset();
	envFilteredTextureArraySRV = nullptr;
	captureLightingState.reset();
	for (auto& view : filteredUAVs)
		view = nullptr;
	const auto requestedCubemapResolution = GetCubemapResolutionForResourceCreation();
	auto mipLevels = activeCubemapMipLevels;

	GetComputeShaderDetectLighting(false);
	GetComputeShaderDetectLighting(true);
	GetComputeShaderUpdate();
	GetComputeShaderUpdateReflections();
	GetComputeShaderInferrence();
	GetComputeShaderInferrenceReflections();
	GetComputeShaderSpecularIrradiance();
	GetComputeShaderBC6HEncode();

	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &computeSampler));
		Util::SetResourceName(computeSampler, "DynamicCubemaps::ComputeSampler");
	}

	auto& cubemap = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS];

	{
		D3D11_TEXTURE2D_DESC texDesc;
		cubemap.texture->GetDesc(&texDesc);
		if (texDesc.Width != requestedCubemapResolution || texDesc.Height != requestedCubemapResolution) {
			logger::warn(
				"Dynamic cubemap target is {}x{}, expected {}x{}; using the renderer target dimensions",
				texDesc.Width,
				texDesc.Height,
				requestedCubemapResolution,
				requestedCubemapResolution);
			activeCubemapResolution = std::max(1u, std::min(texDesc.Width, texDesc.Height));
			activeCubemapMipLevels = std::bit_width(activeCubemapResolution);
			mipLevels = activeCubemapMipLevels;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		cubemap.SRV->GetDesc(&srvDesc);

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		// Create additional resources

		texDesc.MipLevels = mipLevels;
		texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.Format = texDesc.Format;
		srvDesc.TextureCube.MipLevels = mipLevels;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = texDesc.ArraySize;

		envCaptureTexture = new Texture2D(texDesc, "DynamicCubemaps::Capture");
		envCaptureTexture->CreateSRV(srvDesc);
		envCaptureTexture->CreateUAV(uavDesc);

		envCaptureRawTexture = new Texture2D(texDesc, "DynamicCubemaps::CaptureRaw");
		envCaptureRawTexture->CreateSRV(srvDesc);
		envCaptureRawTexture->CreateUAV(uavDesc);

		envCapturePositionTexture = new Texture2D(texDesc, "DynamicCubemaps::CapturePosition");
		envCapturePositionTexture->CreateSRV(srvDesc);
		envCapturePositionTexture->CreateUAV(uavDesc);

		envCaptureReflectionsTexture = new Texture2D(texDesc, "DynamicCubemaps::CaptureReflections");
		envCaptureReflectionsTexture->CreateSRV(srvDesc);
		envCaptureReflectionsTexture->CreateUAV(uavDesc);

		envCaptureRawReflectionsTexture = new Texture2D(texDesc, "DynamicCubemaps::CaptureRawReflections");
		envCaptureRawReflectionsTexture->CreateSRV(srvDesc);
		envCaptureRawReflectionsTexture->CreateUAV(uavDesc);

		envCapturePositionReflectionsTexture = new Texture2D(texDesc, "DynamicCubemaps::CapturePositionReflections");
		envCapturePositionReflectionsTexture->CreateSRV(srvDesc);
		envCapturePositionReflectionsTexture->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		envTexture = new Texture2D(texDesc, "DynamicCubemaps::EnvTexture");
		envTexture->CreateSRV(srvDesc);
		envTexture->CreateUAV(uavDesc);

		envReflectionsTexture = new Texture2D(texDesc, "DynamicCubemaps::EnvReflections");
		envReflectionsTexture->CreateSRV(srvDesc);
		envReflectionsTexture->CreateUAV(uavDesc);

		if (!SetupCapturePublicationResources(texDesc, srvDesc))
			return;

		const float clear[4]{};
		for (auto* texture : { envTexture, envReflectionsTexture }) {
			globals::d3d::context->ClearUnorderedAccessViewFloat(texture->uav.get(), clear);
			globals::d3d::context->GenerateMips(texture->srv.get());
		}

		envInferredTexture = new Texture2D(texDesc, "DynamicCubemaps::EnvInferred");
		envInferredTexture->CreateSRV(srvDesc);
		envInferredTexture->CreateUAV(uavDesc);

		if (!SetupCompressionResources(texDesc))
			return;

		updateCubemapCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpdateCubemapCB>(), "DynamicCubemaps::UpdateCubemapCB");
	}

	{
		bc6hEncodeCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BC6HEncodeCB>(), "DynamicCubemaps::BC6HEncodeCB");
	}

	{
		spmapCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<SpecularMapFilterSettingsCB>(), "DynamicCubemaps::SpmapCB");
	}

	{
		DirectX::CreateDDSTextureFromFile(device, L"Data\\Shaders\\DynamicCubemaps\\defaultcubemap.dds", nullptr, &defaultCubemap);
	}
	captureResourcesReady = true;
}

void DynamicCubemaps::Reset()
{
	realActiveReflections = globals::state->activeReflections;
	activeReflections = realActiveReflections;

	if (globals::game::sky)
		fakeReflections = activeReflections && globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
	else
		fakeReflections = false;

	if (!activeReflections && !Util::IsInterior()) {
		activeReflections = true;
		fakeReflections = true;
	}


}
