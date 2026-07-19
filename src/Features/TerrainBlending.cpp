#include "TerrainBlending.h"

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
	BlendStrength)

namespace
{
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
	bool enabled = settings.Enabled != 0;
	if (ImGui::Checkbox(T(TKEY("enable"), "Enable Terrain Blending"), &enabled)) {
		settings.Enabled = enabled ? 1u : 0u;
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_tooltip"), "Enable seamless blending between terrain and objects."));
	}

	ImGui::SliderFloat(T(TKEY("blend_depth"), "Blend Depth"), &settings.BlendStrength, 0.125f, 1.25f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("blend_depth_tooltip"), "Scales the terrain blend depth. Lower values make the blend tighter."));
	}

	ImGui::Spacing();
	if (ImGui::TreeNodeEx(T(TKEY("performance_options"), "Performance Options"))) {
		ImGui::SliderFloat(T(TKEY("terrain_cull_distance"), "Terrain Depth Culling Distance"), &settings.TerrainCullDistance, 0.0f, 8192.0f, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("terrain_cull_distance_tooltip"), "Terrain farther than this distance skips terrain depth rendering. Set to 0 to disable culling."));
		}
		ImGui::TreePop();
	}
}

void TerrainBlending::DrawEssentialSettings()
{
	bool enabled = settings.Enabled != 0;
	if (ImGui::Checkbox("Enable Terrain Blending", &enabled))
		settings.Enabled = enabled ? 1u : 0u;
}

void TerrainBlending::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.Enabled = settings.Enabled ? 1u : 0u;

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

ID3D11VertexShader* TerrainBlending::GetTerrainVertexShader()
{
	if (!terrainVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" } }, "vs_5_0");
	}
	return terrainVertexShader;
}

ID3D11VertexShader* TerrainBlending::GetTerrainOffsetVertexShader()
{
	if (!terrainOffsetVertexShader) {
		logger::debug("Compiling Utility.hlsl");
		terrainOffsetVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\Utility.hlsl", { { "RENDER_DEPTH", "" }, { "OFFSET_DEPTH", "" } }, "vs_5_0");
	}
	return terrainOffsetVertexShader;
}

ID3D11ComputeShader* TerrainBlending::GetDepthBlendShader()
{
	if (!depthBlendShader) {
		logger::debug("Compiling DepthBlend.hlsl");
		depthBlendShader = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl", {}, "cs_5_0");
	}
	return depthBlendShader;
}

void TerrainBlending::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

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
		if (renderAltTerrain) {
			auto dsv = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			context->VSSetShader(GetTerrainOffsetVertexShader(), NULL, NULL);
		} else {
			auto dsv = terrainDepth.views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			auto shadowState = globals::game::shadowState;
			auto& currentVertexShader = shadowState->GetRuntimeData().currentVertexShader;
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
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Blend Prepass Depths");
	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("Terrain Blending - Blend Prepass Depths");

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();

	{
		TracyD3D11Zone(globals::state->tracyCtx, "Terrain Blending - Depth Blend CS");

		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { blendedDepthTexture->uav.get(), blendedDepthTexture16->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDepthBlendShader(), nullptr, 0);

		globals::profiler->BeginPass("TerrainBlending::DepthBlend");
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		globals::profiler->EndPass();
	}

	ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto renderer = globals::game::renderer;
	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	context->CopyResource(terrainDepth.texture, mainDepth.texture);

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}

void TerrainBlending::ClearShaderCache()
{
	if (terrainVertexShader) {
		terrainVertexShader->Release();
		terrainVertexShader = nullptr;
	}
	if (terrainOffsetVertexShader) {
		terrainOffsetVertexShader->Release();
		terrainOffsetVertexShader = nullptr;
	}
	if (depthBlendShader) {
		depthBlendShader->Release();
		depthBlendShader = nullptr;
	}
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

	context->PSSetShaderResources(55, 1, &terrainDepth.depthSRV);

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

	RestoreTerrainBlendingMainDepthSrv(renderer, depthSRVBackup);
}
#undef I18N_KEY_PREFIX
