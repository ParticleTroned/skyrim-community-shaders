#include "TerrainBlending.h"
#include "GpuPass.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "Deferred.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <cmath>

#define I18N_KEY_PREFIX "feature.terrain_blending."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainBlending::Settings,
	Enabled,
	TerrainCullDistance,
	BlendStrength,
	LandscapeSeams)

namespace
{
	uint32_t GetDepthBlendExtent(float a_sceneExtent, uint32_t a_resourceExtent)
	{
		if (!a_resourceExtent)
			return 0;

		if (!std::isfinite(a_sceneExtent) || a_sceneExtent <= 0.0f)
			return 0;

		return static_cast<uint32_t>(std::clamp(std::round(static_cast<double>(a_sceneExtent)), 1.0, static_cast<double>(a_resourceExtent)));
	}

	bool IsTerrainBlendingCapturedPass(const RE::BSRenderPass* a_pass, bool& a_isTerrainBlend, bool& a_failed)
	{
		a_isTerrainBlend = false;
		a_failed = false;
#if defined(_MSC_VER)
		__try
#endif
		{
			if (!a_pass) {
				a_failed = true;
				return false;
			}

			auto* shaderProperty = a_pass->shaderProperty;
			if (!shaderProperty) {
				return false;
			}

			const auto flags = shaderProperty->flags;
			if (flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
				a_isTerrainBlend = true;
				return true;
			}
			if (flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample)) {
				return true;
			}
			return false;
		}
#if defined(_MSC_VER)
		__except (1) {
			a_failed = true;
			return false;
		}
#else
		a_failed = true;
		return false;
#endif
	}

	bool ShouldRenderTerrainDepthPass(const RE::BSRenderPass* a_pass, const RE::NiPoint3& a_eyePosition, float a_cullDistance, bool& a_failed)
	{
		a_failed = false;
#if defined(_MSC_VER)
		__try
#endif
		{
			if (!a_pass || !a_pass->geometry) {
				a_failed = true;
				return false;
			}

			if (a_cullDistance <= 0.0f) {
				return true;
			}

			return (a_pass->geometry->worldBound.center.GetDistance(a_eyePosition) - a_pass->geometry->worldBound.radius) <= a_cullDistance;
		}
#if defined(_MSC_VER)
		__except (1) {
			a_failed = true;
			return false;
		}
#else
		a_failed = true;
		return false;
#endif
	}

	bool CanReplayRenderPass(const TerrainBlending::RenderPass& a_renderPass, bool a_expectedTerrainBlend, bool& a_skipOriginal)
	{
		a_skipOriginal = false;
#if defined(_MSC_VER)
		__try
#endif
		{
			auto* pass = a_renderPass.a_pass;
			if (!pass) {
				a_skipOriginal = true;
				return false;
			}
			if (!pass->geometry) {
				a_skipOriginal = true;
				return false;
			}
			auto& geometryRuntimeData = pass->geometry->GetGeometryRuntimeData();
			if (!geometryRuntimeData.shaderProperty) {
				return false;
			}
			if (!pass->shader) {
				a_skipOriginal = true;
				return false;
			}
			if (!pass->shaderProperty) {
				a_skipOriginal = true;
				return false;
			}
			if (pass->shader->shaderType.get() != RE::BSShader::Type::Lighting) {
				return false;
			}
			bool isTerrainBlend = false;
			bool classificationFailed = false;
			const bool capturedPass = IsTerrainBlendingCapturedPass(pass, isTerrainBlend, classificationFailed);
			if (classificationFailed) {
				a_skipOriginal = true;
				return false;
			}
			if (!capturedPass || isTerrainBlend != a_expectedTerrainBlend) {
				return false;
			}
			return true;
		}
#if defined(_MSC_VER)
		__except (1) {
			a_skipOriginal = true;
			return false;
		}
#endif
		a_skipOriginal = true;
		return false;
	}

	void RestoreTerrainBlendingMainDepthSrv(RE::BSGraphics::Renderer* a_renderer, ID3D11ShaderResourceView* a_mainDepthSrv)
	{
		if (!a_renderer) {
			return;
		}

		auto& mainDepth = a_renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		mainDepth.depthSRV = a_mainDepthSrv;
	}

	void RestoreTerrainBlendingDepthSrvs(RE::BSGraphics::Renderer* a_renderer, ID3D11ShaderResourceView* a_mainDepthSrv, ID3D11ShaderResourceView* a_prepassSrv)
	{
		if (!a_renderer) {
			return;
		}

		RestoreTerrainBlendingMainDepthSrv(a_renderer, a_mainDepthSrv);

		auto& zPrepassCopy = a_renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		zPrepassCopy.depthSRV = a_prepassSrv;
	}

}

void TerrainBlending::DrawSettings()
{
	MenuUI::SettingsPage page("TerrainBlending", {
													 { "appearance", T("feature.terrain_blending.ui_blending", "Blending"), T("feature.terrain_blending.ui_choose_which_surfaces_blend_then_tune_their_appearance", "Choose which surfaces blend, then tune their appearance."), T("feature.terrain_blending.ui_object_to_ground_transitions", "Object-to-ground transitions"), true, true, T("feature.terrain_blending.ui_shape_terrain_transitions", "Shape terrain transitions") },
												 });
	if (!page.Is("appearance"))
		return;

	Util::Widgets::SliderFloat(T("feature.terrain_blending.ui_blend_strength", "Blend Strength"), &settings.BlendStrength, 0.125f, 1.25f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("feature.terrain_blending.ui_scales_blending_strength_lower_values_make_blending_tighter", "Scales blending strength. Lower values make blending tighter."));
	}

	ImGui::Spacing();
	bool seams = settings.LandscapeSeams != 0;
	if (Util::Widgets::Checkbox(T("feature.terrain_blending.landscape_seams", "Landscape Seam Blending"), &seams))
		settings.LandscapeSeams = seams ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped("%s", T("feature.terrain_blending.ui_blends_texture_borders_between_terrain_quads_using_up_b99680db", "Blends texture borders between terrain quads using up to two neighbouring layers. Requires Terrain Blending and True PBR enabled. Adds GPU and terrain-loading work. Changes apply next frame; off releases seam resources and restores ordinary terrain shading."));
	}

	ImGui::Spacing();
	if (ImGui::TreeNodeEx(T("feature.terrain_blending.performance_options", "Performance Options"))) {
		Util::Widgets::SliderFloat(T("feature.terrain_blending.terrain_cull_distance", "Terrain Depth Culling Distance"), &settings.TerrainCullDistance, 0.0f, 8192.0f, T("feature.terrain_blending.ui_0f_units", "%.0f units"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("feature.terrain_blending.ui_terrain_farther_than_this_distance_skips_tb_depth_rend_4b75c7fe", "Terrain farther than this distance skips TB depth rendering. Set to 0 to disable culling."));
		}
		ImGui::TreePop();
	}
}

void TerrainBlending::DrawEssentialSettings()
{
	bool enabled = settings.Enabled != 0;
	if (ImGui::Checkbox(T("feature.terrain_blending.enable", "Enable"), &enabled))
		settings.Enabled = enabled ? 1u : 0u;
}

void TerrainBlending::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.Enabled = settings.Enabled ? 1u : 0u;
	settings.LandscapeSeams = settings.LandscapeSeams ? 1u : 0u;

	if (!std::isfinite(settings.TerrainCullDistance)) {
		settings.TerrainCullDistance = 1024.0f;
	} else {
		settings.TerrainCullDistance = std::clamp(settings.TerrainCullDistance, 0.0f, 8192.0f);
	}

	if (!std::isfinite(settings.BlendStrength)) {
		settings.BlendStrength = 1.0f;
	} else {
		settings.BlendStrength = std::clamp(settings.BlendStrength, 0.125f, 1.25f);
	}
}

void TerrainBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

ID3D11VertexShader* TerrainBlending::GetTerrainOffsetVertexShader()
{
	return terrainOffsetVertexShader.Get(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" }, { "OFFSET_DEPTH", "" } }, "vs_5_0");
}

ID3D11ComputeShader* TerrainBlending::GetDepthBlendShader()
{
	return depthBlendShader.Get(L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl", {}, "cs_5_0");
}

void TerrainBlending::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != device) {
		ClearShaderCache();
		shaderDevice = device;
	}

	if (terrainDepth.texture) {
		terrainDepth.texture->Release();
		terrainDepth.texture = nullptr;
	}
	if (terrainDepth.depthSRV) {
		terrainDepth.depthSRV->Release();
		terrainDepth.depthSRV = nullptr;
	}
	if (terrainDepth.views[0]) {
		terrainDepth.views[0]->Release();
		terrainDepth.views[0] = nullptr;
	}
	delete blendedDepthTexture;
	blendedDepthTexture = nullptr;
	delete blendedDepthTexture16;
	blendedDepthTexture16 = nullptr;
	delete mainDepthCopy;
	mainDepthCopy = nullptr;
	if (terrainDepthStencilState) {
		terrainDepthStencilState->Release();
		terrainDepthStencilState = nullptr;
	}

	{
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc;
		mainDepth.texture->GetDesc(&texDesc);
		DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, NULL, &terrainDepth.texture));
		Util::SetResourceName(terrainDepth.texture, "TerrainBlending::TerrainDepth");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		mainDepth.depthSRV->GetDesc(&srvDesc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(terrainDepth.texture, &srvDesc, &terrainDepth.depthSRV));
		Util::SetResourceName(terrainDepth.depthSRV, "TerrainBlending::TerrainDepth SRV");

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		mainDepth.views[0]->GetDesc(&dsvDesc);
		DX::ThrowIfFailed(device->CreateDepthStencilView(terrainDepth.texture, &dsvDesc, &terrainDepth.views[0]));
		Util::SetResourceName(terrainDepth.views[0], "TerrainBlending::TerrainDepth DSV");
	}

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		blendedDepthTexture = new Texture2D(texDesc, "TerrainBlending::BlendedDepth");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		main.SRV->GetDesc(&srvDesc);
		srvDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		main.UAV->GetDesc(&uavDesc);
		uavDesc.Format = texDesc.Format;
		blendedDepthTexture->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R16_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		blendedDepthTexture16 = new Texture2D(texDesc, "TerrainBlending::BlendedDepth16");
		blendedDepthTexture16->CreateSRV(srvDesc);
		blendedDepthTexture16->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		mainDepthCopy = new Texture2D(texDesc, "TerrainBlending::MainDepthCopy");
		mainDepthCopy->CreateSRV(srvDesc);
		mainDepthCopy->CreateUAV(uavDesc);

		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		depthSRVBackup = mainDepth.depthSRV;

		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
		prepassSRVBackup = zPrepassCopy.depthSRV;
	}

	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthStencilDesc.StencilEnable = false;
		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &terrainDepthStencilState));
		Util::SetResourceName(terrainDepthStencilState, "TerrainBlending::DepthStencilState");
	}

	depthBlendCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<DepthBlendCB>(), "TerrainBlending::DepthBlendCB");
}

void TerrainBlending::PostPostLoad()
{
	Hooks::Install();
}

void TerrainBlending::DataLoaded()
{
	auto bEnableLandFade = RE::GetINISetting("bEnableLandFade:Display");
	bEnableLandFade->data.b = false;
}

void TerrainBlending::TerrainShaderHacks()
{
	if (renderTerrainDepth) {
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;
		auto shadowState = globals::game::shadowState;
		auto& currentVertexShader = shadowState->GetRuntimeData().currentVertexShader;
		if (renderAltTerrain) {
			auto dsv = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			auto* offsetVertexShader = GetTerrainOffsetVertexShader();
			context->VSSetShader(offsetVertexShader ? offsetVertexShader : reinterpret_cast<ID3D11VertexShader*>(currentVertexShader->shader), nullptr, 0);
		} else {
			auto dsv = terrainDepth.views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
		}
		renderAltTerrain = !renderAltTerrain;
	}
}

void TerrainBlending::ResetDepth()
{
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Reset Depth");
	auto context = globals::d3d::context;

	auto dsv = terrainDepth.views[0];
	context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0u);
}

void TerrainBlending::ResetTerrainDepth()
{
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Reset Terrain Depth");
	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("Terrain Blending - Reset Terrain Depth");

	auto context = globals::d3d::context;

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto currentVertexShader = *globals::game::currentVertexShader;
	context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void TerrainBlending::BlendPrepassDepths()
{
	ZoneScoped;
	CS_GPU_PASS("TerrainBlending::BlendPrepassDepths");

	auto context = globals::d3d::context;
	if (!context || !depthBlendCB || !depthSRVBackup || !terrainDepth.texture || !terrainDepth.depthSRV ||
		!blendedDepthTexture || !blendedDepthTexture->uav ||
		!blendedDepthTexture16 || !blendedDepthTexture16->uav ||
		!mainDepthCopy || !mainDepthCopy->uav) {
		return;
	}
	auto* depthBlend = GetDepthBlendShader();
	if (!depthBlend)
		return;

	D3D11_TEXTURE2D_DESC terrainDepthDesc{};
	terrainDepth.texture->GetDesc(&terrainDepthDesc);

	const auto blendSize = Util::ConvertToDynamic(globals::state->screenSize);
	const uint32_t maxBlendWidth = std::min({ terrainDepthDesc.Width, blendedDepthTexture->desc.Width, blendedDepthTexture16->desc.Width, mainDepthCopy->desc.Width });
	const uint32_t maxBlendHeight = std::min({ terrainDepthDesc.Height, blendedDepthTexture->desc.Height, blendedDepthTexture16->desc.Height, mainDepthCopy->desc.Height });
	const uint32_t blendWidth = GetDepthBlendExtent(blendSize.x, maxBlendWidth);
	const uint32_t blendHeight = GetDepthBlendExtent(blendSize.y, maxBlendHeight);
	if (!blendWidth || !blendHeight)
		return;

	DepthBlendCB depthBlendData{};
	depthBlendData.blendWidth = blendWidth;
	depthBlendData.blendHeight = blendHeight;
	depthBlendCB->Update(depthBlendData);

	context->OMSetRenderTargets(0, nullptr, nullptr);

	{
		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3] = { blendedDepthTexture->uav.get(), blendedDepthTexture16->uav.get(), mainDepthCopy->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		auto buffer = depthBlendCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		context->CSSetShader(depthBlend, nullptr, 0);

		const uint32_t dispatchX = (blendWidth + 7u) >> 3u;
		const uint32_t dispatchY = (blendHeight + 7u) >> 3u;
		{
			CS_GPU_PASS("TerrainBlending::DepthBlend");
			context->Dispatch(dispatchX, dispatchY, 1);
		}
	}

	ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11Buffer* buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &buffer);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void TerrainBlending::ClearShaderCache()
{
	terrainOffsetVertexShader.Reset();
	depthBlendShader.Reset();
}

void TerrainBlending::Hooks::Main_RenderDepth::thunk(bool a1, bool a2)
{
	ZoneScopedS(8);

	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;

	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	globals::game::graphicsState->SetCameraData(RE::Main::WorldRootCamera(), 1);

	singleton.eyePosition = Util::GetEyePosition();

	if (shaderCache->IsEnabled() && singleton.settings.Enabled) {
		mainDepth.depthSRV = singleton.blendedDepthTexture->srv.get();
		zPrepassCopy.depthSRV = singleton.blendedDepthTexture->srv.get();

		singleton.renderDepth = true;
		singleton.ResetDepth();

		{
			ZoneScopedN("Terrain Depth - Game Render");
			func(a1, a2);
		}

		singleton.renderDepth = false;

		if (singleton.renderTerrainDepth) {
			singleton.renderTerrainDepth = false;
			singleton.ResetTerrainDepth();
			singleton.renderAltTerrain = false;
		}

		singleton.BlendPrepassDepths();
	} else {
		mainDepth.depthSRV = singleton.depthSRVBackup;
		zPrepassCopy.depthSRV = singleton.prepassSRVBackup;

		{
			ZoneScopedN("Terrain Depth - Game Render");
			func(a1, a2);
		}
	}
}

void TerrainBlending::Hooks::BSBatchRenderer__RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;

	if (!a_pass) {
		return;
	}

	if (shaderCache->IsEnabled() && singleton.settings.Enabled) {
		auto leaveTerrainDepthSection = [&]() {
			if (singleton.renderTerrainDepth) {
				singleton.ResetTerrainDepth();
				singleton.renderTerrainDepth = false;
				singleton.renderAltTerrain = false;
			}
		};

		if (singleton.renderDepth) {
			// Entering or exiting terrain depth section
			bool classificationFailed = false;
			bool inTerrain = false;
			IsTerrainBlendingCapturedPass(a_pass, inTerrain, classificationFailed);
			if (classificationFailed) {
				leaveTerrainDepthSection();
				return;
			}

			if (inTerrain) {
				bool depthPassFailed = false;
				inTerrain = ShouldRenderTerrainDepthPass(a_pass, singleton.eyePosition, singleton.settings.TerrainCullDistance, depthPassFailed);
				if (depthPassFailed) {
					leaveTerrainDepthSection();
					return;
				}
			}

			if (singleton.renderTerrainDepth != inTerrain) {
				if (!inTerrain) {
					singleton.ResetTerrainDepth();
					singleton.renderAltTerrain = false;
				}
				singleton.renderTerrainDepth = inTerrain;
			}

			if (inTerrain) {
				func(a_pass, a_technique, a_alphaTest, a_renderFlags);  // Run terrain twice
			}
		} else if (globals::state->inWorld) {
			bool classificationFailed = false;
			bool isTerrainBlend = false;
			const bool capturedPass = IsTerrainBlendingCapturedPass(a_pass, isTerrainBlend, classificationFailed);
			if (classificationFailed) {
				return;
			}

			if (capturedPass) {
				TerrainBlending::RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
				bool skipOriginal = false;
				if (!CanReplayRenderPass(call, isTerrainBlend, skipOriginal)) {
					if (skipOriginal) {
						return;
					}
				} else {
					if (isTerrainBlend) {
						singleton.terrainRenderPasses.push_back(call);
					} else {
						singleton.renderPasses.push_back(call);
					}
					return;
				}
			}
		}
	}

	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void TerrainBlending::RenderTerrainBlendingPasses()
{
	ZoneScoped;

	if (!settings.Enabled) {
		renderDepth = false;
		renderTerrainDepth = false;
		renderAltTerrain = false;
		terrainRenderPasses.clear();
		renderPasses.clear();
		RestoreTerrainBlendingDepthSrvs(globals::game::renderer, depthSRVBackup, prepassSRVBackup);
		return;
	}

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto shadowState = globals::game::shadowState;
	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	if (!renderer || !context || !shadowState || !stateUpdateFlags || !terrainDepthStencilState) {
		renderDepth = false;
		renderTerrainDepth = false;
		renderAltTerrain = false;
		terrainRenderPasses.clear();
		renderPasses.clear();
		RestoreTerrainBlendingDepthSrvs(renderer, depthSRVBackup, prepassSRVBackup);
		return;
	}

	auto* mainDepthSnapshot = mainDepthCopy->srv.get();
	context->PSSetShaderResources(55, 1, &mainDepthSnapshot);

	const uint64_t terrainPassCount = static_cast<uint64_t>(terrainRenderPasses.size());
	const uint64_t noBlendPassCount = static_cast<uint64_t>(renderPasses.size());

	if (terrainPassCount != 0 || noBlendPassCount != 0) {
		TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Render Passes");
		if (globals::state->frameAnnotations)
			globals::state->BeginPerfEvent("Terrain Blending - Render Passes");

		auto& alphaBlendMode = shadowState->GetRuntimeData().alphaBlendMode;
		auto& alphaBlendWriteMode = shadowState->GetRuntimeData().alphaBlendWriteMode;
		auto& depthStencilDepthMode = shadowState->GetRuntimeData().depthStencilDepthMode;

		// Reset alpha write and enable alpha blending
		alphaBlendWriteMode = 1;
		alphaBlendMode = 1;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Enable rendering for depth below the surface
		context->OMSetDepthStencilState(terrainDepthStencilState, 0xFF);

		for (auto& renderPass : terrainRenderPasses) {
			bool skipOriginal = false;
			if (!CanReplayRenderPass(renderPass, true, skipOriginal)) {
				continue;
			}
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);
		}

		// Reset alpha blending
		alphaBlendMode = 0;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Reset depth testing
		depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

		for (auto& renderPass : renderPasses) {
			bool skipOriginal = false;
			if (!CanReplayRenderPass(renderPass, false, skipOriginal)) {
				continue;
			}
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);
		}

		terrainRenderPasses.clear();
		renderPasses.clear();

		if (globals::state->frameAnnotations)
			globals::state->EndPerfEvent();
	}

	RestoreTerrainBlendingDepthSrvs(renderer, depthSRVBackup, prepassSRVBackup);
}
#undef I18N_KEY_PREFIX
