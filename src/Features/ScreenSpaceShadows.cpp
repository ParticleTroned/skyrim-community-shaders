#include "ScreenSpaceShadows.h"

#include "Features/TerrainBlending.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Utils/D3D.h"

#define I18N_KEY_PREFIX "feature.screen_space_shadows."

#pragma warning(push)
#pragma warning(disable: 4838 4244)
#include "ScreenSpaceShadows/bend_sss_cpu.h"
#pragma warning(pop)

using RE::RENDER_TARGETS;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ScreenSpaceShadows::BendSettings,
	Enable,
	SampleCount,
	SurfaceThickness,
	BilinearThreshold,
	ShadowContrast)

void ScreenSpaceShadows::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("general"), "General"), ImGuiTreeNodeFlags_DefaultOpen)) {
		bool enabled = bendSettings.Enable != 0;
		if (ImGui::Checkbox(T(TKEY("enable"), "Enable"), &enabled))
			bendSettings.Enable = enabled ? 1u : 0u;
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_tooltip"), "Enable screen-space contact shadows from the sun/moon direction."));

		int sampleCount = static_cast<int>(bendSettings.SampleCount);
		if (ImGui::SliderInt(T(TKEY("sample_count"), "Sample Count Multiplier"), &sampleCount, 1, 4))
			bendSettings.SampleCount = static_cast<uint>(std::clamp(sampleCount, 1, 4));
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("sample_count_tooltip"), "Multiplier for shadow ray sample count. Higher values increase shadow reach at the cost of performance. Adapts to render resolution."));

		ImGui::SliderFloat(T(TKEY("surface_thickness"), "Surface Thickness"), &bendSettings.SurfaceThickness, 0.005f, 0.05f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("surface_thickness_tooltip"), "Assumed thickness of surfaces for shadow detection. Lower values produce thinner, more precise shadows."));

		ImGui::SliderFloat(T(TKEY("bilinear_threshold"), "Bilinear Threshold"), &bendSettings.BilinearThreshold, 0.02f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("bilinear_threshold_tooltip"), "Depth threshold for edge detection during bilinear interpolation. Higher values smooth more aggressively across edges."));

		ImGui::SliderFloat(T(TKEY("shadow_contrast"), "Shadow Contrast"), &bendSettings.ShadowContrast, 0.0f, 4.0f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("shadow_contrast_tooltip"), "Contrast boost for the shadow transition. Higher values produce harder shadow edges."));

		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::TreePop();
	}
}

void ScreenSpaceShadows::InvalidateRaymarchShaders()
{
	raymarchCS.Reset();
}

void ScreenSpaceShadows::ClearShaderCache()
{
	InvalidateRaymarchShaders();
}

uint ScreenSpaceShadows::GetScaledSampleCount() const
{
	float2 renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });

	// Scale sample count based on both dimensions relative to 1920x1080 reference
	float2 referenceRes = { 1920.0f, 1080.0f };
	float referenceArea = referenceRes.x * referenceRes.y;
	float currentArea = renderSize.x * renderSize.y;
	float areaScale = std::sqrt(currentArea / referenceArea);
	uint scaledSampleCount = static_cast<uint>(std::round(bendSettings.SampleCount * 60 * areaScale));

	// Quantize to steps of 8 to prevent frequent recompilation from small DRS oscillations
	scaledSampleCount = ((scaledSampleCount + 7u) / 8u) * 8u;
	scaledSampleCount = std::max(scaledSampleCount, 8u);

	return scaledSampleCount;
}

ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarch()
{
	uint scaledSampleCount = GetScaledSampleCount();

	if (scaledSampleCount != lastCompiledSampleCount) {
		lastCompiledSampleCount = scaledSampleCount;
		InvalidateRaymarchShaders();
	}

	auto sampleCount = std::format("{}", scaledSampleCount);
	std::vector<std::pair<const char*, const char*>> defines{ { "SAMPLE_COUNT", sampleCount.c_str() } };
	// TERRAIN_BLENDING flips DepthTexture's HLSL type from `Texture2D<unorm float>`
	// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
	if (globals::features::terrainBlending.loaded)
		defines.push_back({ "TERRAIN_BLENDING", "" });
	return raymarchCS.Get(L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl", defines, "cs_5_0");
}

ScreenSpaceShadows::RuntimeReadiness ScreenSpaceShadows::GetRuntimeReadiness(
	bool a_requireCompiledShader,
	RuntimeContext* a_context) const
{
	if (a_context)
		*a_context = {};

	const auto* sky = globals::game::sky;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull)
		return RuntimeReadiness::NoFullSky;

	auto* d3dContext = globals::d3d::context;
	auto* viewport = globals::game::graphicsState;
	if (!globals::d3d::device ||
		!d3dContext ||
		!viewport ||
		!globals::state ||
		!globals::profiler ||
		!globals::shaderCache ||
		!pointBorderSampler ||
		!raymarchCB ||
		!raymarchCB->CB() ||
		!screenSpaceShadowsTexture ||
		!screenSpaceShadowsTexture->resource ||
		!screenSpaceShadowsTexture->srv ||
		!screenSpaceShadowsTexture->uav) {
		return RuntimeReadiness::NoRuntimeResources;
	}

	auto* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	const auto renderSize = Util::ConvertToDynamic(
		float2{ static_cast<float>(viewport->screenWidth), static_cast<float>(viewport->screenHeight) });
	const auto dynamicResolution = float2{
		viewport->GetRuntimeData().dynamicResolutionWidthRatio,
		viewport->GetRuntimeData().dynamicResolutionHeightRatio
	};
	if (!depthSRV ||
		!std::isfinite(renderSize.x) ||
		!std::isfinite(renderSize.y) ||
		renderSize.x < 1.0f ||
		renderSize.y < 1.0f ||
		static_cast<double>(renderSize.x) >
			std::min<double>(
				screenSpaceShadowsTexture->desc.Width,
				std::numeric_limits<int>::max()) ||
		static_cast<double>(renderSize.y) >
			std::min<double>(
				screenSpaceShadowsTexture->desc.Height,
				std::numeric_limits<int>::max()) ||
		!std::isfinite(dynamicResolution.x) ||
		!std::isfinite(dynamicResolution.y) ||
		dynamicResolution.x <= 0.0f ||
		dynamicResolution.y <= 0.0f) {
		return RuntimeReadiness::NoRuntimeResources;
	}

	auto** accumulatorSlot = globals::game::currentAccumulator.get();
	if (!accumulatorSlot || !*accumulatorSlot)
		return RuntimeReadiness::NoDirectionalLight;

	auto* shadowSceneNode = (*accumulatorSlot)->GetRuntimeData().activeShadowSceneNode;
	if (!shadowSceneNode)
		return RuntimeReadiness::NoDirectionalLight;

	const auto& sunLight = shadowSceneNode->GetRuntimeData().sunLight;
	if (!sunLight || !sunLight->light)
		return RuntimeReadiness::NoDirectionalLight;

	auto* directionalLight = skyrim_cast<RE::NiDirectionalLight*>(sunLight->light.get());
	if (!directionalLight)
		return RuntimeReadiness::NoDirectionalLight;

	if (a_requireCompiledShader &&
		(!raymarchCS || lastCompiledSampleCount != GetScaledSampleCount())) {
		return RuntimeReadiness::ShaderUnavailable;
	}

	if (a_context) {
		a_context->context = d3dContext;
		a_context->depthSRV = depthSRV;
		a_context->raymarchShader = raymarchCS.get();
		a_context->directionalLight = directionalLight;
		a_context->renderSize = renderSize;
		a_context->dynamicResolution = dynamicResolution;
	}

	return RuntimeReadiness::Ready;
}

void ScreenSpaceShadows::DrawShadows()
{
	RuntimeContext runtimeContext;
	if (GetRuntimeReadiness(false, &runtimeContext) != RuntimeReadiness::Ready)
		return;

	ZoneScopedS(8);
	TracyD3D11Zone(globals::state->tracyCtx, "Screen Space Shadows");

	if (!GetComputeRaymarch())
		return;

	// Shader compilation can span a runtime transition. Revalidate the complete
	// dispatch path and take a fresh pointer snapshot before binding anything.
	if (GetRuntimeReadiness(true, &runtimeContext) != RuntimeReadiness::Ready)
		return;

	auto* context = runtimeContext.context;
	auto* dirLight = runtimeContext.directionalLight;

	auto& directionNi = dirLight->GetWorldDirection();
	float3 light = { directionNi.x, directionNi.y, directionNi.z };
	light.Normalize();
	float4 lightProjection = float4(-light.x, -light.y, -light.z, 0.0f);

	// Helper lambda to calculate light projection
	auto CalculateLightProjection = [&]() -> std::array<float, 4> {
		auto viewProjMat = globals::game::frameBufferCached.GetCameraViewProj().Transpose();
		auto projectedLight = DirectX::SimpleMath::Vector4::Transform(lightProjection, viewProjMat);
		return { projectedLight.x, projectedLight.y, projectedLight.z, projectedLight.w };
	};

	auto lightProjectionF = CalculateLightProjection();

	const auto renderSize = runtimeContext.renderSize;
	int viewportSize[2] = { (int)renderSize.x, (int)renderSize.y };

	int minRenderBounds[2] = { 0, 0 };
	int maxRenderBounds[2] = { viewportSize[0], viewportSize[1] };

	// Setup common render state.
	// SSS always uses 24/32-bit depth, never the R16_UNORM half-precision path.
	// With TerrainBlending loaded the SRV is R32_FLOAT (blendedDepthTexture);
	// without it, the game's kPOST_ZPREPASS_COPY (R24_UNORM_X8_TYPELESS).
	// The shader's DepthTexture declaration is conditional on TERRAIN_BLENDING:
	// `<float>` for the R32_FLOAT path, `<unorm float>` for the R24_UNORM path.
	auto* depthSRV = runtimeContext.depthSRV;
	context->CSSetShaderResources(0, 1, &depthSRV);

	auto uav = screenSpaceShadowsTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	context->CSSetSamplers(0, 1, &pointBorderSampler);

	auto buffer = raymarchCB->CB();
	context->CSSetConstantBuffers(1, 1, &buffer);

	const auto dynamicRes = runtimeContext.dynamicResolution;

	// Shared dispatch logic
	auto Dispatch = [&](ID3D11ComputeShader* shader, const float* lightProj,
						float invTexSizeX, float invTexSizeY) {
		if (!shader)
			return;

		globals::profiler->BeginPass("ScreenSpaceShadows::RayMarch");

		if (globals::state->frameAnnotations) {
			globals::state->BeginPerfEvent("SSS - Ray March");
		}

		context->CSSetShader(shader, nullptr, 0);

		auto dispatchList = Bend::BuildDispatchList(const_cast<float*>(lightProj), viewportSize, minRenderBounds, maxRenderBounds);

		for (int i = 0; i < dispatchList.DispatchCount; i++) {
			auto dispatchData = dispatchList.Dispatch[i];

			{
				TracyD3D11Zone(globals::state->tracyCtx, "SSS - Dispatch CB");

				RaymarchCB data{};
				data.LightCoordinate[0] = dispatchList.LightCoordinate_Shader[0];
				data.LightCoordinate[1] = dispatchList.LightCoordinate_Shader[1];
				data.LightCoordinate[2] = dispatchList.LightCoordinate_Shader[2];
				data.LightCoordinate[3] = dispatchList.LightCoordinate_Shader[3];

				data.WaveOffset[0] = dispatchData.WaveOffset_Shader[0];
				data.WaveOffset[1] = dispatchData.WaveOffset_Shader[1];

				data.FarDepthValue = 1.0f;
				data.NearDepthValue = 0.0f;

				data.DynamicRes = dynamicRes;

				data.InvDepthTextureSize[0] = invTexSizeX;
				data.InvDepthTextureSize[1] = invTexSizeY;

				data.settings = bendSettings;

				raymarchCB->Update(data);
			}

			{
				TracyD3D11Zone(globals::state->tracyCtx, "SSS - Dispatch Sweep");
				context->Dispatch(dispatchData.WaveCount[0], dispatchData.WaveCount[1], dispatchData.WaveCount[2]);
			}
		}

		if (globals::state->frameAnnotations) {
			globals::state->EndPerfEvent();
		}

		globals::profiler->EndPass();
	};

	float InvTexSizeX = 1.0f / (float)viewportSize[0];
	float InvTexSizeY = 1.0f / (float)viewportSize[1];

	Dispatch(runtimeContext.raymarchShader, lightProjectionF.data(), InvTexSizeX, InvTexSizeY);

	ID3D11ShaderResourceView* views[1]{ nullptr };
	context->CSSetShaderResources(0, 1, views);

	ID3D11UnorderedAccessView* uavs[1]{ nullptr };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11SamplerState* sampler = nullptr;
	context->CSSetSamplers(0, 1, &sampler);

	buffer = nullptr;
	context->CSSetConstantBuffers(1, 1, &buffer);
}

void ScreenSpaceShadows::Prepass()
{
	auto* context = globals::d3d::context;
	if (!context)
		return;
	if (!screenSpaceShadowsTexture ||
		!screenSpaceShadowsTexture->uav ||
		!screenSpaceShadowsTexture->srv) {
		ID3D11ShaderResourceView* nullView = nullptr;
		context->PSSetShaderResources(45, 1, &nullView);
		return;
	}

	float white[4] = { 1, 1, 1, 1 };
	context->ClearUnorderedAccessViewFloat(screenSpaceShadowsTexture->uav.get(), white);

	if (bendSettings.Enable)
		DrawShadows();

	auto view = screenSpaceShadowsTexture->srv.get();
	context->PSSetShaderResources(45, 1, &view);
}

bool ScreenSpaceShadows::IsPerformanceTuningApplicable() const
{
	return GetRuntimeReadiness(bendSettings.Enable != 0) ==
	       RuntimeReadiness::Ready;
}

bool ScreenSpaceShadows::IsPerformanceCostMeasurementReady() const
{
	return GetRuntimeReadiness(bendSettings.Enable != 0) ==
	       RuntimeReadiness::Ready;
}

const char* ScreenSpaceShadows::GetPerformanceTuningApplicabilityReason() const
{
	switch (GetRuntimeReadiness(bendSettings.Enable != 0)) {
	case RuntimeReadiness::Ready:
		return nullptr;
	case RuntimeReadiness::NoFullSky:
		return T(
			"menu.performance_tuning.feature.screen_space_shadows.no_full_sky",
			"Screen Space Shadows only perform runtime work while the current sky is in full exterior mode.");
	case RuntimeReadiness::NoRuntimeResources:
		return T(
			"menu.performance_tuning.feature.screen_space_shadows.no_runtime_resources",
			"Screen Space Shadows cannot be measured because their required rendering resources are unavailable.");
	case RuntimeReadiness::NoDirectionalLight:
		return T(
			"menu.performance_tuning.feature.screen_space_shadows.no_directional_light",
			"Screen Space Shadows cannot be measured because the current scene has no active directional sun or moon light.");
	case RuntimeReadiness::ShaderUnavailable:
		return T(
			"menu.performance_tuning.feature.screen_space_shadows.shader_unavailable",
			"Screen Space Shadows cannot be measured because the raymarch shader has not compiled successfully for the current resolution.");
	default:
		return T(
			"menu.performance_tuning.feature.screen_space_shadows.not_applicable",
			"Screen Space Shadows cannot perform runtime work in the current scene.");
	}
}

void ScreenSpaceShadows::DrawEssentialSettings()
{
	bool enabled = bendSettings.Enable != 0;
	if (ImGui::Checkbox("Enable", &enabled))
		bendSettings.Enable = enabled ? 1u : 0u;
}

void ScreenSpaceShadows::LoadSettings(json& o_json)
{
	bendSettings = o_json;
	const BendSettings defaults{};
	bendSettings.Enable = bendSettings.Enable != 0 ? 1u : 0u;
	bendSettings.SampleCount = std::clamp(bendSettings.SampleCount, 1u, 4u);
	bendSettings.SurfaceThickness = std::isfinite(bendSettings.SurfaceThickness) ?
	                                    std::clamp(bendSettings.SurfaceThickness, 0.005f, 0.05f) :
	                                    defaults.SurfaceThickness;
	bendSettings.BilinearThreshold = std::isfinite(bendSettings.BilinearThreshold) ?
	                                     std::clamp(bendSettings.BilinearThreshold, 0.02f, 1.0f) :
	                                     defaults.BilinearThreshold;
	bendSettings.ShadowContrast = std::isfinite(bendSettings.ShadowContrast) ?
	                                  std::clamp(bendSettings.ShadowContrast, 0.0f, 4.0f) :
	                                  defaults.ShadowContrast;
}

void ScreenSpaceShadows::SaveSettings(json& o_json)
{
	o_json = bendSettings;
}

void ScreenSpaceShadows::RestoreDefaultSettings()
{
	bendSettings = {};
}

bool ScreenSpaceShadows::HasShaderDefine(RE::BSShader::Type)
{
	return true;
}

void ScreenSpaceShadows::SetupResources()
{
	raymarchCB = new ConstantBuffer(ConstantBufferDesc<RaymarchCB>(), "SSS::RaymarchCB");

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		samplerDesc.BorderColor[0] = 1.0f;
		samplerDesc.BorderColor[1] = 1.0f;
		samplerDesc.BorderColor[2] = 1.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointBorderSampler));
		Util::SetResourceName(pointBorderSampler, "SSS::PointBorderSampler");
	}

	{
		auto renderer = globals::game::renderer;
		auto shadowMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		shadowMask.texture->GetDesc(&texDesc);
		shadowMask.SRV->GetDesc(&srvDesc);

		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		srvDesc.Format = texDesc.Format;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		screenSpaceShadowsTexture = new Texture2D(texDesc, "SSS::ShadowTexture");
		screenSpaceShadowsTexture->CreateSRV(srvDesc);
		screenSpaceShadowsTexture->CreateUAV(uavDesc);
	}
}
#undef I18N_KEY_PREFIX
