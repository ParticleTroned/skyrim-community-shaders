#include "TerrainBlending.h"

#include <algorithm>
#include <atomic>

#include "Deferred.h"
#include "ShaderCache.h"
#include "State.h"

namespace
{
	struct TbDebugStats
	{
		uint32_t prepassEnter = 0;
		uint32_t prepassExit = 0;
		uint32_t terrainPasses = 0;
		uint32_t terrainAccepted = 0;
		uint32_t terrainRejected = 0;
		uint32_t terrainToggleTrue = 0;
		uint32_t terrainToggleFalse = 0;
		uint32_t queuedTerrain = 0;
		uint32_t queuedExtra = 0;
		uint32_t blendDispatch = 0;
		uint32_t renderCalls = 0;
		uint32_t renderCallsWithWork = 0;
		size_t maxTerrainQueue = 0;
		size_t maxExtraQueue = 0;
		float terrainDistMin = 0.0f;
		float terrainDistMax = 0.0f;
		bool terrainDistInit = false;
		uint32_t mainWidth = 0;
		uint32_t mainHeight = 0;
		uint32_t mainArraySize = 0;
		int mainFormat = 0;
		int mainSrvDim = 0;
		bool mainInfoValid = false;

		void ResetCounts()
		{
			prepassEnter = 0;
			prepassExit = 0;
			terrainPasses = 0;
			terrainAccepted = 0;
			terrainRejected = 0;
			terrainToggleTrue = 0;
			terrainToggleFalse = 0;
			queuedTerrain = 0;
			queuedExtra = 0;
			blendDispatch = 0;
			renderCalls = 0;
			renderCallsWithWork = 0;
			maxTerrainQueue = 0;
			maxExtraQueue = 0;
			terrainDistMin = 0.0f;
			terrainDistMax = 0.0f;
			terrainDistInit = false;
		}
	};

	std::atomic<bool> g_tbStatsEnabled{ false };
	TbDebugStats g_tbStats{};

	bool TbStatsEnabled()
	{
		return g_tbStatsEnabled.load(std::memory_order_relaxed);
	}
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

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		mainDepth.depthSRV->GetDesc(&srvDesc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(terrainDepth.texture, &srvDesc, &terrainDepth.depthSRV));

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		mainDepth.views[0]->GetDesc(&dsvDesc);
		DX::ThrowIfFailed(device->CreateDepthStencilView(terrainDepth.texture, &dsvDesc, &terrainDepth.views[0]));

		g_tbStats.mainWidth = texDesc.Width;
		g_tbStats.mainHeight = texDesc.Height;
		g_tbStats.mainArraySize = texDesc.ArraySize;
		g_tbStats.mainFormat = static_cast<int>(texDesc.Format);
		g_tbStats.mainSrvDim = static_cast<int>(srvDesc.ViewDimension);
		g_tbStats.mainInfoValid = true;
	}

	{
		auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		blendedDepthTexture = new Texture2D(texDesc);

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

		blendedDepthTexture16 = new Texture2D(texDesc);
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
			GET_INSTANCE_MEMBER(currentVertexShader, shadowState)
			context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
		}
		renderAltTerrain = !renderAltTerrain;
	}
}

void TerrainBlending::ResetDepth()
{
	auto context = globals::d3d::context;

	auto dsv = terrainDepth.views[0];
	context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0u);
}

void TerrainBlending::ResetTerrainDepth()
{
	auto context = globals::d3d::context;

	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto currentVertexShader = *globals::game::currentVertexShader;
	context->VSSetShader((ID3D11VertexShader*)currentVertexShader->shader, NULL, NULL);
}

void TerrainBlending::BlendPrepassDepths()
{
	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto dispatchCount = Util::GetScreenDispatchCount();
	if (TbStatsEnabled()) {
		g_tbStats.blendDispatch++;
	}

	{
		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { blendedDepthTexture->uav.get(), blendedDepthTexture16->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDepthBlendShader(), nullptr, 0);

		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
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
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;

	// Keep updating for distance-based terrain gating inside BSBatchRenderer hook.
	singleton.averageEyePosition = Util::GetAverageEyePosition();

	// IMPORTANT (VR fix): do NOT drive Terrain Blending from Main_RenderDepth.
	// In VR this hook can correspond to shadow/aux depth phases; we only use it for debug/cleanup.
	if (!shaderCache || !shaderCache->IsEnabled()) {
		// Ensure we restore original SRVs when the shader cache / feature is disabled.
		if (renderer) {
			auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
			auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
			mainDepth.depthSRV = singleton.depthSRVBackup;
			zPrepassCopy.depthSRV = singleton.prepassSRVBackup;
		}

		singleton.renderDepth = false;
		if (singleton.renderTerrainDepth) {
			singleton.renderTerrainDepth = false;
			singleton.ResetTerrainDepth();
		}
	}

	func(a1, a2);
}

void TerrainBlending::Hooks::BSBatchRenderer__RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	const bool statsEnabled = TbStatsEnabled();

	// VR fix: detect and drive Terrain Blending from the *main camera* depth-only z-prepass,
	// identified by "kMAIN DSV bound" AND "no RTVs bound".
	if (shaderCache && shaderCache->IsEnabled() && renderer && context) {
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* dsv = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

		bool depthOnly = true;
		for (auto* rtv : rtvs) {
			if (rtv) {
				depthOnly = false;
				break;
			}
		}

		bool matchesMain = false;
		if (dsv) {
			ID3D11Resource* res = nullptr;
			dsv->GetResource(&res);
			if (res) {
				ID3D11Texture2D* tex = nullptr;
				if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
					matchesMain = (tex == mainDepth.texture);
					tex->Release();
				}
				res->Release();
			}
		}

		for (auto*& rtv : rtvs) {
			if (rtv) {
				rtv->Release();
				rtv = nullptr;
			}
		}
		if (dsv) {
			dsv->Release();
			dsv = nullptr;
		}

		const bool isMainDepthPrepass = depthOnly && matchesMain;

		// Enter main depth prepass
		if (isMainDepthPrepass && !singleton.renderDepth) {
			singleton.averageEyePosition = Util::GetAverageEyePosition();

			// Redirect depth SRVs to our blended depth (the effect needs these SRVs for later passes).
			mainDepth.depthSRV = singleton.blendedDepthTexture->srv.get();
			zPrepassCopy.depthSRV = singleton.blendedDepthTexture->srv.get();

			singleton.renderDepth = true;
			singleton.ResetDepth();
			if (statsEnabled) {
				g_tbStats.prepassEnter++;
			}
		}
		// Exit main depth prepass
		else if (!isMainDepthPrepass && singleton.renderDepth) {
			singleton.renderDepth = false;

			if (singleton.renderTerrainDepth) {
				singleton.renderTerrainDepth = false;
				singleton.ResetTerrainDepth();
			}

			singleton.BlendPrepassDepths();
			if (statsEnabled) {
				g_tbStats.prepassExit++;
			}
		}
	}

	// --- Original Terrain Blending pass classification logic ---
	if (shaderCache->IsEnabled()) {
		if (singleton.renderDepth) {
			// Entering or exiting terrain depth section
			bool inTerrain = a_pass->shaderProperty && a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape);

			if (inTerrain) {
				if (statsEnabled) {
					g_tbStats.terrainPasses++;
				}
				const float terrainDist = a_pass->geometry->worldBound.center.GetDistance(singleton.averageEyePosition) - a_pass->geometry->worldBound.radius;
				if (statsEnabled) {
					if (!g_tbStats.terrainDistInit) {
						g_tbStats.terrainDistMin = terrainDist;
						g_tbStats.terrainDistMax = terrainDist;
						g_tbStats.terrainDistInit = true;
					} else {
						g_tbStats.terrainDistMin = std::min(g_tbStats.terrainDistMin, terrainDist);
						g_tbStats.terrainDistMax = std::max(g_tbStats.terrainDistMax, terrainDist);
					}
				}
				if (terrainDist > 2048.0f) {
					inTerrain = false;
					if (statsEnabled) {
						g_tbStats.terrainRejected++;
					}
				} else if (statsEnabled) {
					g_tbStats.terrainAccepted++;
				}
			}

			if (singleton.renderTerrainDepth != inTerrain) {
				if (statsEnabled) {
					if (inTerrain) {
						g_tbStats.terrainToggleTrue++;
					} else {
						g_tbStats.terrainToggleFalse++;
					}
				}
				if (!inTerrain)
					singleton.ResetTerrainDepth();
				singleton.renderTerrainDepth = inTerrain;
			}

			if (inTerrain)
				func(a_pass, a_technique, a_alphaTest, a_renderFlags);  // Run terrain twice
		} else if (globals::state->inWorld) {
			if (auto shaderProperty = a_pass->shaderProperty) {
				if (a_pass->shader->shaderType.get() == RE::BSShader::Type::Lighting) {
					if (shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
						RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
						singleton.terrainRenderPasses.push_back(call);
						if (statsEnabled) {
							g_tbStats.queuedTerrain++;
						}
						return;
					}

					// Detect meshes which should not get terrain blending using an unused flag (kNoTransparencyMultiSample)
					if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample)) {
						RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
						singleton.renderPasses.push_back(call);
						if (statsEnabled) {
							g_tbStats.queuedExtra++;
						}
						return;
					}
				}
			}
		}
	}
	func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void TerrainBlending::RenderTerrainBlendingPasses()
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto shadowState = globals::game::shadowState;
	auto stateUpdateFlags = globals::game::stateUpdateFlags;
	const bool statsEnabled = TbStatsEnabled();
	const bool hasWork = !terrainRenderPasses.empty() || !renderPasses.empty();
	if (statsEnabled) {
		g_tbStats.renderCalls++;
		if (hasWork) {
			g_tbStats.renderCallsWithWork++;
		}
		g_tbStats.maxTerrainQueue = std::max(g_tbStats.maxTerrainQueue, terrainRenderPasses.size());
		g_tbStats.maxExtraQueue = std::max(g_tbStats.maxExtraQueue, renderPasses.size());
	}

	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& zPrepassCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	const bool hasBlendedDepthSRV = blendedDepthTexture && blendedDepthTexture->srv;
	ID3D11ShaderResourceView* blendedDepthSRV = hasBlendedDepthSRV ? blendedDepthTexture->srv.get() : nullptr;

	struct ScopedDepthSRVOverride
	{
		ScopedDepthSRVOverride(
			RE::BSGraphics::DepthStencilData& a_mainDepth,
			RE::BSGraphics::DepthStencilData& a_prepassDepth,
			ID3D11ShaderResourceView* a_srv)
			: mainDepth(a_mainDepth),
			  prepassDepth(a_prepassDepth),
			  prevMainDepthSRV(a_mainDepth.depthSRV),
			  prevPrepassCopySRV(a_prepassDepth.depthSRV)
		{
			mainDepth.depthSRV = a_srv;
			prepassDepth.depthSRV = a_srv;
		}

		~ScopedDepthSRVOverride()
		{
			mainDepth.depthSRV = prevMainDepthSRV;
			prepassDepth.depthSRV = prevPrepassCopySRV;
		}

		RE::BSGraphics::DepthStencilData& mainDepth;
		RE::BSGraphics::DepthStencilData& prepassDepth;
		ID3D11ShaderResourceView* prevMainDepthSRV;
		ID3D11ShaderResourceView* prevPrepassCopySRV;
	};

	auto drawPass = [&](const RenderPass& renderPass) {
		auto invoke = [&]() {
			Hooks::BSBatchRenderer__RenderPassImmediately::func(
				renderPass.a_pass,
				renderPass.a_technique,
				renderPass.a_alphaTest,
				renderPass.a_renderFlags);
		};

		if (hasBlendedDepthSRV) {
			ScopedDepthSRVOverride scope(mainDepth, zPrepassCopy, blendedDepthSRV);
			invoke();
			return;
		}

		invoke();
	};

	// Used to get the distance of the surface to the lowest depth
	auto view = terrainDepth.depthSRV;
	context->PSSetShaderResources(55, 1, &view);

	if (!terrainRenderPasses.empty() || !renderPasses.empty()) {
		GET_INSTANCE_MEMBER(alphaBlendMode, shadowState)
		GET_INSTANCE_MEMBER(alphaBlendWriteMode, shadowState)
		GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState)

		// Reset alpha write and enable alpha blending
		alphaBlendWriteMode = 1;
		alphaBlendMode = 1;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Enable rendering for depth below the surface
		context->OMSetDepthStencilState(terrainDepthStencilState, 0xFF);

		for (auto& renderPass : terrainRenderPasses)
			drawPass(renderPass);

		// Reset alpha blending
		alphaBlendMode = 0;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Reset depth testing
		depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

		for (auto& renderPass : renderPasses)
			drawPass(renderPass);

		terrainRenderPasses.clear();
		renderPasses.clear();
	}

}

void TerrainBlending::ToggleDebugCapture()
{
	const bool newEnabled = !g_tbStatsEnabled.load(std::memory_order_relaxed);
	g_tbStatsEnabled.store(newEnabled, std::memory_order_relaxed);
	g_tbStats.ResetCounts();
	logger::info("[TB] Debug stats capture {}", newEnabled ? "enabled" : "disabled");
}

void TerrainBlending::DumpDebugStats()
{
	const bool enabled = TbStatsEnabled();
	const float distMin = g_tbStats.terrainDistInit ? g_tbStats.terrainDistMin : -1.0f;
	const float distMax = g_tbStats.terrainDistInit ? g_tbStats.terrainDistMax : -1.0f;
	const char* depthInfo = g_tbStats.mainInfoValid ? "" : " (main depth info unavailable)";

	logger::info(
		"[TB][STAT] enabled={} prepass enter={} exit={} terrainPass={} accept={} reject={} toggleT={} toggleF={} queuedTerrain={} queuedExtra={} maxTerrainQueue={} maxExtraQueue={} blendDispatch={} renderCalls={} workCalls={} renderDepth={} renderTerrainDepth={} distMin={} distMax={} mainDepth={}x{} fmt={} array={} srvDim={}{}",
		enabled,
		g_tbStats.prepassEnter,
		g_tbStats.prepassExit,
		g_tbStats.terrainPasses,
		g_tbStats.terrainAccepted,
		g_tbStats.terrainRejected,
		g_tbStats.terrainToggleTrue,
		g_tbStats.terrainToggleFalse,
		g_tbStats.queuedTerrain,
		g_tbStats.queuedExtra,
		g_tbStats.maxTerrainQueue,
		g_tbStats.maxExtraQueue,
		g_tbStats.blendDispatch,
		g_tbStats.renderCalls,
		g_tbStats.renderCallsWithWork,
		renderDepth,
		renderTerrainDepth,
		distMin,
		distMax,
		g_tbStats.mainWidth,
		g_tbStats.mainHeight,
		g_tbStats.mainFormat,
		g_tbStats.mainArraySize,
		g_tbStats.mainSrvDim,
		depthInfo);

	g_tbStats.ResetCounts();
}
