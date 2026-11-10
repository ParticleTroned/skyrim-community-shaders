#include "CloudShadows.h"
#include "AdaptiveBrightness.h"
#include "GpuPass.h"
#include "Menu/SettingsPage.h"
#include "Utils/Finite.h"
#include "Utils/ResourceName.h"
#include "Utils/UI.h"

#include <algorithm>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	CloudShadows::Settings,
	Enabled,
	Opacity)

namespace
{
	bool IsCloudShadowSceneReady()
	{
		auto* sky = globals::game::sky;
		return sky && sky->mode.get() == RE::Sky::Mode::kFull && sky->currentClimate;
	}

	bool DrawEnabledCheckbox(CloudShadows::Settings& a_settings)
	{
		bool enabled = a_settings.Enabled != 0;
		if (Util::Widgets::Checkbox("Enabled", &enabled))
			a_settings.Enabled = enabled ? 1u : 0u;
		return enabled;
	}
}

CloudShadows::Settings CloudShadows::GetCommonBufferData() const
{
	auto effective = settings;
	const auto appearance = globals::features::adaptiveBrightness.GetEffectiveSharedLightingSettings().appearance;
	effective.Opacity = Util::ClampFinite(settings.Opacity * appearance.cloudShadowStrength, 0.0f, 1.0f, 0.8f);
	return effective;
}

void CloudShadows::DrawSettings()
{
	MenuUI::SettingsPage page("CloudShadows", {
												  { "appearance", "Appearance", "Enable cloud shadows, then choose how dark they look.", "Moving shade and opacity", true, true, "Shape cloud shadows" },
											  });
	if (!page.Is("appearance"))
		return;

	ImGui::BeginDisabled(settings.Enabled == 0);
	Util::Widgets::SliderFloat("Opacity", &settings.Opacity, 0.0f, 1.0f, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Higher values make cloud shadows darker.");
	}
	ImGui::EndDisabled();
}

void CloudShadows::DrawPerformanceSettings(bool)
{
	DrawEnabledCheckbox(settings);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(
			"Disabling skips cloud-shadow cubemap updates and projection. Measure outdoors under a full sky.");
	}
	if (!IsCloudShadowSceneReady())
		ImGui::TextDisabled("Actual cost measurement requires an outdoor scene with a full sky.");
}

json CloudShadows::CapturePerformanceSettingsState() const
{
	return {
		{ "Enabled", settings.Enabled != 0 }
	};
}

bool CloudShadows::IsPerformanceCostMeasurementEnabled() const
{
	return settings.Enabled != 0 && IsCloudShadowSceneReady();
}

bool CloudShadows::IsPerformanceCostMeasurementReady() const
{
	return IsCloudShadowSceneReady();
}

void CloudShadows::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.Enabled = settings.Enabled ? 1u : 0u;
}

void CloudShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void CloudShadows::RestoreDefaultSettings()
{
	settings = {};
}

void CloudShadows::CheckResourcesSide(int side)
{
	static Util::FrameChecker frame_checker[6];
	if (!frame_checker[side].IsNewFrame())
		return;

	auto context = globals::d3d::context;

	float black[4] = { 0, 0, 0, 0 };
	CS_GPU_PASS("CloudShadows::ClearCubemapFace");
	context->ClearRenderTargetView(cubemapCloudOccRTVs[side], black);
}

void CloudShadows::SkyShaderHacks()
{
	if (!settings.Enabled) {
		overrideSky = false;
		return;
	}

	if (overrideSky) {
		overrideSky = false;
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;

		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		// render targets
		ID3D11RenderTargetView* rtvs[4]{};
		ID3D11DepthStencilView* dsv = nullptr;
		context->OMGetRenderTargets(3, rtvs, &dsv);
		// Only the three queried targets and depth view carry acquired references.
		const SKSE::stl::scope_exit releaseAcquiredViews([&]() noexcept {
			for (int i = 0; i < 3; ++i) {
				if (rtvs[i])
					rtvs[i]->Release();
			}
			if (dsv)
				dsv->Release();
		});

		int side = -1;
		for (int i = 0; i < 6; ++i)
			if (rtvs[0] == reflections.cubeSideRTV[i]) {
				side = i;
				break;
			}
		if (side == -1)
			return;

		occlusionClearedWithoutClouds = false;
		CheckResourcesSide(side);

		rtvs[3] = cubemapCloudOccRTVs[side];
		context->OMSetRenderTargets(4, rtvs, nullptr);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		UINT sampleMask = 0xffffffff;

		context->OMSetBlendState(cloudShadowBlendState, blendFactor, sampleMask);
		// This override follows the engine state flush; the next draw must restore its output state.
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		auto cubemapDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kCUBEMAP_REFLECTIONS];
		context->PSSetShaderResources(17, 1, &cubemapDepth.depthSRV);
	}
}

void CloudShadows::ModifySky(RE::BSRenderPass* Pass)
{
	if (!settings.Enabled) {
		overrideSky = false;
		return;
	}

	auto shadowState = globals::game::shadowState;

	GET_INSTANCE_MEMBER(cubeMapRenderTarget, shadowState);

	if (cubeMapRenderTarget != RE::RENDER_TARGETS_CUBEMAP::kREFLECTIONS)
		return;

	auto skyProperty = static_cast<const RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS) {
		overrideSky = true;
	}
}

bool CloudShadows::ClearOcclusionWithoutClouds()
{
	auto* sky = globals::game::sky;
	auto* context = globals::d3d::context;
	if (!sky || !context || !texCubemapCloudOcc || !texCubemapCloudOcc->resource || !texCubemapCloudOcc->srv ||
		!std::ranges::all_of(cubemapCloudOccRTVs, [](auto* rtv) { return rtv != nullptr; }))
		return false;

	if (sky->clouds && sky->clouds->numLayers != 0) {
		occlusionClearedWithoutClouds = false;
	} else if (!occlusionClearedWithoutClouds) {
		// Without cloud draws, per-face capture cannot remove the previous weather's occlusion.
		CS_GPU_PASS("CloudShadows::ClearCloudlessOcclusion");
		const float black[4]{};
		for (auto* rtv : cubemapCloudOccRTVs)
			context->ClearRenderTargetView(rtv, black);
		occlusionClearedWithoutClouds = true;
	}
	return true;
}

void CloudShadows::ReflectionsPrepass()
{
	if (!settings.Enabled)
		return;

	Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		if (!IsCloudShadowSceneReady() || !ClearOcclusionWithoutClouds() ||
			!texCubemapCloudOccCopy || !texCubemapCloudOccCopy->resource || !texCubemapCloudOccCopy->srv)
			return;

		auto context = globals::d3d::context;

		{
			CS_GPU_PASS("CloudShadows::CopyCubemap");
			context->CopyResource(texCubemapCloudOccCopy->resource.get(), texCubemapCloudOcc->resource.get());
		}

		ID3D11ShaderResourceView* srv = texCubemapCloudOccCopy->srv.get();
		context->PSSetShaderResources(25, 1, &srv);
		context->CSSetShaderResources(25, 1, &srv);
	}
}

void CloudShadows::EarlyPrepass()
{
	if (!settings.Enabled)
		return;

	if (!IsCloudShadowSceneReady() || !ClearOcclusionWithoutClouds())
		return;

	auto context = globals::d3d::context;

	ID3D11ShaderResourceView* srv = texCubemapCloudOcc->srv.get();
	context->PSSetShaderResources(25, 1, &srv);
	context->CSSetShaderResources(25, 1, &srv);
}

void CloudShadows::SetupResources()
{
	occlusionClearedWithoutClouds = false;
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};

		reflections.texture->GetDesc(&texDesc);
		reflections.SRV->GetDesc(&srvDesc);

		texDesc.Format = srvDesc.Format = DXGI_FORMAT_R8_UNORM;

		texCubemapCloudOcc = new Texture2D(texDesc);
		texCubemapCloudOcc->CreateSRV(srvDesc);

		for (int i = 0; i < 6; ++i) {
			reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
			rtvDesc.Format = texDesc.Format;
			DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOcc->resource.get(), &rtvDesc, cubemapCloudOccRTVs + i));
		}

		texCubemapCloudOccCopy = new Texture2D(texDesc);
		texCubemapCloudOccCopy->CreateSRV(srvDesc);

		for (int i = 0; i < 6; ++i) {
			reflections.cubeSideRTV[i]->GetDesc(&rtvDesc);
			rtvDesc.Format = texDesc.Format;
			DX::ThrowIfFailed(device->CreateRenderTargetView(texCubemapCloudOccCopy->resource.get(), &rtvDesc, cubemapCloudOccCopyRTVs + i));
		}
	}
	{
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;

		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &cloudShadowBlendState));
		Util::SetResourceName(cloudShadowBlendState, "CloudShadows::BlendState");
	}
}

void CloudShadows::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::cloudShadows.ModifySky(Pass);
	func(This, Pass, RenderFlags);
}
