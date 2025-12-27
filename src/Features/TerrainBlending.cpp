
#include "TerrainBlending.h"
#include <string_view>
#include <unordered_map>
#include <wrl/client.h>

#include "Deferred.h"
#include "ShaderCache.h"
#include "State.h"
#include "imgui.h"

namespace
{
	struct TBOMSnapshot
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> dsvTex;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv0;  // holds +1 ref from OMGetRenderTargets
		D3D11_VIEWPORT vp0{};
		UINT vpCount = 0;
	};

	static TBOMSnapshot GetOMSnapshot(ID3D11DeviceContext* ctx)
	{
		TBOMSnapshot s{};

		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11RenderTargetView* rtv0 = nullptr;
		// NumViews=1 is important: some drivers/paths won't reliably populate DSV if NumViews==0.
		ctx->OMGetRenderTargets(1, &rtv0, &dsv);
		s.rtv0.Attach(rtv0);

		if (dsv) {
			Microsoft::WRL::ComPtr<ID3D11Resource> res;
			dsv->GetResource(res.GetAddressOf());
			res.As(&s.dsvTex);
			dsv->Release();
		}

		s.vpCount = 1;
		s.vp0 = {};
		ctx->RSGetViewports(&s.vpCount, &s.vp0);

		return s;
	}

	static bool IsMainDepthBound(ID3D11DeviceContext* ctx)
	{
		auto renderer = globals::game::renderer;
		if (!renderer)
			return false;

		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (!mainDepth.texture)
			return false;

		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11RenderTargetView* rtv0 = nullptr;
		ctx->OMGetRenderTargets(1, &rtv0, &dsv);
		if (rtv0)
			rtv0->Release();

		bool match = false;
		if (dsv) {
			Microsoft::WRL::ComPtr<ID3D11Resource> res;
			dsv->GetResource(res.GetAddressOf());
			Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
			if (SUCCEEDED(res.As(&tex)) && tex) {
				match = (tex.Get() == mainDepth.texture);
			}
			dsv->Release();
		}
		return match;
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainBlending::Settings,
	clearBlendedDepthUAVs,
	useMaxDepthBlend)

void TerrainBlending::DrawSettings()
{
	{
		const bool oldEnabled = settings.clearBlendedDepthUAVs;
		ImGui::Checkbox("Clear blended depth UAVs", (bool*)&settings.clearBlendedDepthUAVs);

		if (oldEnabled != settings.clearBlendedDepthUAVs) {
			logger::info("[Terrain Blending] Clear blended depth UAVs changed to: {}", settings.clearBlendedDepthUAVs);
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"When enabled, the blended depth UAVs are cleared each frame before the depth-blend compute pass.\n"
				"This is a safety measure against stale/undefined pixels in VR (FFR/DFR, streaming/cell loads).\n"
				"Experimental: Disable for slightly better performance - might bring back FFR/DFR bug.");
		}
	}

	ImGui::Separator();

	{
		const bool oldUseMax = settings.useMaxDepthBlend;
		ImGui::Checkbox("Use MAX depth blend (reversed-Z test)", (bool*)&settings.useMaxDepthBlend);

		if (oldUseMax != settings.useMaxDepthBlend) {
			logger::info("[Terrain Blending] Use MAX depth blend changed to: {}", settings.useMaxDepthBlend);
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Switches the depth blend operation from min(main, terrain) to max(main, terrain).\n"
				"Useful if the active VR path behaves like reversed-Z (near=1, far=0) or uses a different depth convention.\n"
				"If enabling this changes the popping/vanishing behavior, the correct long-term fix is to auto-detect depth convention.");
		}
	}

	ImGui::Separator();
}

void TerrainBlending::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainBlending::RestoreDefaultSettings()
{
	settings = {};
}

bool TerrainBlending::DrawFailLoadMessage() const
{
	return false;
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

ID3D11ComputeShader* TerrainBlending::GetDepthBlendShader(bool useMax)
{
	if (useMax) {
		if (!depthBlendShaderMax) {
			logger::debug("Compiling DepthBlend.hlsl (max/reversed-Z)");
			static const std::vector<std::pair<const char*, const char*>> kMaxDefines = { { "DEPTHBLEND_USE_MAX", "" } };
			depthBlendShaderMax = (ID3D11ComputeShader*)Util::CompileShader(
				L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl",
				kMaxDefines,
				"cs_5_0");
		}
		return depthBlendShaderMax;
	}

	if (!depthBlendShader) {
		logger::debug("Compiling DepthBlend.hlsl (min/default)");
		depthBlendShader = (ID3D11ComputeShader*)Util::CompileShader(
			L"Data\\Shaders\\TerrainBlending\\DepthBlend.hlsl",
			{},
			"cs_5_0");
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
		// --- TB debug (rate-limited): verify this is running even without shadow-casting lights ---
		static uint32_t s_tbHacksCount = 0;
		if ((++s_tbHacksCount % 120u) == 0u) {
			logger::trace("[TB] TerrainShaderHacks active. renderAltTerrain={} renderTerrainDepth={}",
				renderAltTerrain, renderTerrainDepth);
		}
		// ----------------------------------------------------------------------------------------
		auto renderer = globals::game::renderer;
		auto context = globals::d3d::context;
		if (renderAltTerrain) {
			auto dsv = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			context->VSSetShader(GetTerrainOffsetVertexShader(), NULL, NULL);
		} else {
			auto dsv = terrainDepth.views[0];
			context->OMSetRenderTargets(0, nullptr, dsv);
			// Don't depend on the shadow pipeline being active; bind our own depth-only terrain VS.
			context->VSSetShader(GetTerrainVertexShader(), NULL, NULL);
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

	// IMPORTANT (VR): Dispatch based on the actual UAV size, not the "screen",
	// otherwise parts of the blended depth may never be written (stale/undefined values).
	Microsoft::WRL::ComPtr<ID3D11Resource> blendedRes;
	blendedDepthTexture->uav.get()->GetResource(blendedRes.GetAddressOf());

	Microsoft::WRL::ComPtr<ID3D11Texture2D> blendedTex;
	blendedRes.As(&blendedTex);

	D3D11_TEXTURE2D_DESC blendedDesc{};
	blendedTex->GetDesc(&blendedDesc);

	const UINT groupsX = (blendedDesc.Width + 8u - 1u) / 8u;
	const UINT groupsY = (blendedDesc.Height + 8u - 1u) / 8u;

	// Fix #3 (optional): deterministic initialization of blended depth UAVs.
	// This guards against stale/undefined pixels in VR (FFR/DFR, streaming/cell loads) at the cost of extra full-surface writes.
	if (settings.clearBlendedDepthUAVs) {
		const float clearVal[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		context->ClearUnorderedAccessViewFloat(blendedDepthTexture->uav.get(), clearVal);
		const UINT clearValU16[4] = { 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu };
		context->ClearUnorderedAccessViewUint(blendedDepthTexture16->uav.get(), clearValU16);
	}

	{
		ID3D11ShaderResourceView* views[2] = { depthSRVBackup, terrainDepth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { blendedDepthTexture->uav.get(), blendedDepthTexture16->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(GetDepthBlendShader(settings.useMaxDepthBlend), nullptr, 0);

		context->Dispatch(groupsX, groupsY, 1);
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
	if (depthBlendShaderMax) {
		depthBlendShaderMax->Release();
		depthBlendShaderMax = nullptr;
	}
}

void TerrainBlending::Hooks::Main_RenderDepth::thunk(bool a1, bool a2)
{
	// --- TB debug (rate-limited): confirm which depth pass this hook is running in ---
	static uint32_t s_tbDepthCallCount = 0;
	if ((++s_tbDepthCallCount % 120u) == 0u) {  // ~once every 2s @60fps
		auto shadowState = globals::game::shadowState;

		void* curVS = nullptr;
		if (auto cvs = *globals::game::currentVertexShader) {
			curVS = cvs->shader;
		}

		void* shadowVS = nullptr;
		if (shadowState) {
			GET_INSTANCE_MEMBER(currentVertexShader, shadowState);
			if (currentVertexShader) {
				shadowVS = currentVertexShader->shader;
			}
		}

		// Extra TB debug: log currently bound depth target + viewport (helps distinguish Z-prepass vs shadowmap in VR)
		ID3D11DepthStencilView* boundDSV = nullptr;
		ID3D11RenderTargetView* boundRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		globals::d3d::context->OMGetRenderTargets(1, boundRTVs, &boundDSV);
		for (auto* rtv : boundRTVs) {
			if (rtv) {
				rtv->Release();
			}
		}

		UINT vpCount = 0;
		globals::d3d::context->RSGetViewports(&vpCount, nullptr);
		D3D11_VIEWPORT vp0{};
		if (vpCount > 0) {
			UINT tmp = 1;
			globals::d3d::context->RSGetViewports(&tmp, &vp0);
		}

		// default values if we can't query
		UINT dsvW = 0, dsvH = 0;
		DXGI_FORMAT dsvFmt = DXGI_FORMAT_UNKNOWN;
		bool dsvMatchesMain = false;

		if (boundDSV) {
			Microsoft::WRL::ComPtr<ID3D11Resource> dsvRes;
			boundDSV->GetResource(dsvRes.GetAddressOf());

			Microsoft::WRL::ComPtr<ID3D11Texture2D> dsvTex;
			if (SUCCEEDED(dsvRes.As(&dsvTex)) && dsvTex) {
				D3D11_TEXTURE2D_DESC d{};
				dsvTex->GetDesc(&d);
				dsvW = d.Width;
				dsvH = d.Height;
				dsvFmt = d.Format;

				auto renderer = globals::game::renderer;
				if (renderer) {
					auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
					D3D11_TEXTURE2D_DESC md{};
					if (mainDepth.texture) {
						mainDepth.texture->GetDesc(&md);
						dsvMatchesMain = (dsvTex.Get() == mainDepth.texture);
					}
				}
			}

			boundDSV->Release();
		}

		logger::trace("[TB] Bound DSV {}x{} fmt={} vp0={}x{} vpCount={} matchesMain={}", dsvW, dsvH, (int)dsvFmt, (uint32_t)vp0.Width, (uint32_t)vp0.Height, vpCount, dsvMatchesMain);

		logger::trace("[TB] Main_RenderDepth thunk. inWorld={} shaderCache={} curVS={} shadowVS={}",
			globals::state->inWorld,
			globals::shaderCache->IsEnabled(),
			curVS,
			shadowVS);
	}
	// -------------------------------------------------------------------------------
	// We no longer drive TerrainBlending from this hook, because in VR this call site
	// often corresponds to a shadow/aux depth pass. Instead we drive it from the
	// BSBatchRenderer hook when kMAIN depth is actually bound.
	func(a1, a2);
}
void TerrainBlending::Hooks::BSBatchRenderer__RenderPassImmediately::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	auto& singleton = globals::features::terrainBlending;
	auto shaderCache = globals::shaderCache;

	// --- Prepass driver (VR-safe): detect when the main camera depth (kMAIN) is bound ---
	// We start TB at the first depth-only draw that uses kMAIN depth, and finish TB right
	// before the first non-depth-only draw (RTV0 != nullptr) or when kMAIN is no longer bound.
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	bool isMainDepth = false;
	bool depthOnly = false;
	UINT vpCount = 1;
	D3D11_VIEWPORT vp0{};
	ID3D11DepthStencilView* dsv = nullptr;
	ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

	bool rtvAny = false;
	uint32_t rtvBoundCount = 0;
	for (auto* r : rtvs) {
		if (r) {
			rtvAny = true;
			++rtvBoundCount;
		}
	}
	depthOnly = !rtvAny;

	vp0 = {};
	vpCount = 1;
	context->RSGetViewports(&vpCount, &vp0);

	// Gather DSV resource info (needed to confirm whether the VR prepass uses kMAIN or a different depth surface)
	Microsoft::WRL::ComPtr<ID3D11Texture2D> dsvTex;
	D3D11_TEXTURE2D_DESC dsvTexDesc{};
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvViewDesc{};
	bool hasDsvViewDesc = false;

	if (dsv) {
		dsv->GetDesc(&dsvViewDesc);
		hasDsvViewDesc = true;

		Microsoft::WRL::ComPtr<ID3D11Resource> dsvRes;
		dsv->GetResource(dsvRes.GetAddressOf());
		if (dsvRes) {
			dsvRes.As(&dsvTex);
			if (dsvTex) {
				dsvTex->GetDesc(&dsvTexDesc);
			}
		}
	}

	D3D11_TEXTURE2D_DESC mainDepthDesc{};
	bool hasMainDepthDesc = false;
	if (renderer) {
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
		if (mainDepth.texture) {
			mainDepth.texture->GetDesc(&mainDepthDesc);
			hasMainDepthDesc = true;
		}
		if (mainDepth.texture && dsvTex) {
			isMainDepth = (dsvTex.Get() == mainDepth.texture);
		}
	}

	// Rate-limited / change-based logging for depth-only passes:
	// This is the key diagnostic in VR: which DSV resource is actually bound during the headset-view depth prepass?
	if (depthOnly) {
		static ID3D11Texture2D* s_lastTex = nullptr;
		static bool s_lastIsMain = false;
		static uint32_t s_lastVpW = 0, s_lastVpH = 0;
		static uint32_t s_lastDsvW = 0, s_lastDsvH = 0;
		static DXGI_FORMAT s_lastTexFmt = DXGI_FORMAT_UNKNOWN;
		static DXGI_FORMAT s_lastViewFmt = DXGI_FORMAT_UNKNOWN;
		static uint32_t s_lastSamples = 0;
		static uint32_t s_lastRTVBoundCount = 0;

		ID3D11Texture2D* curTex = dsvTex.Get();
		uint32_t curVpW = (uint32_t)vp0.Width;
		uint32_t curVpH = (uint32_t)vp0.Height;
		uint32_t curDsvW = curTex ? dsvTexDesc.Width : 0;
		uint32_t curDsvH = curTex ? dsvTexDesc.Height : 0;
		DXGI_FORMAT curTexFmt = curTex ? dsvTexDesc.Format : DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT curViewFmt = (hasDsvViewDesc ? dsvViewDesc.Format : DXGI_FORMAT_UNKNOWN);
		uint32_t curSamples = curTex ? dsvTexDesc.SampleDesc.Count : 0;

		const bool changed =
			(curTex != s_lastTex) ||
			(isMainDepth != s_lastIsMain) ||
			(curVpW != s_lastVpW) || (curVpH != s_lastVpH) ||
			(curDsvW != s_lastDsvW) || (curDsvH != s_lastDsvH) ||
			(curTexFmt != s_lastTexFmt) ||
			(curViewFmt != s_lastViewFmt) ||
			(curSamples != s_lastSamples) ||
			(rtvBoundCount != s_lastRTVBoundCount);

		if (changed) {
			logger::trace("[TB][PrepassCheck] depthOnly=1 isMain(kMAIN)={} vp0={}x{} vpCount={} rtvBoundCount={} dsv={} dsvTex={} texDesc={}x{} fmt={} samples={} viewFmt={} mainTex={} mainDesc={}x{} fmt={}",
				isMainDepth,
				curVpW, curVpH, vpCount,
				rtvBoundCount,
				(void*)dsv,
				(void*)curTex,
				curDsvW, curDsvH, (int)curTexFmt, curSamples,
				(int)curViewFmt,
				hasMainDepthDesc ? (void*)renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture : (void*)nullptr,
				hasMainDepthDesc ? (uint32_t)mainDepthDesc.Width : 0,
				hasMainDepthDesc ? (uint32_t)mainDepthDesc.Height : 0,
				hasMainDepthDesc ? (int)mainDepthDesc.Format : (int)DXGI_FORMAT_UNKNOWN);
			s_lastTex = curTex;
			s_lastIsMain = isMainDepth;
			s_lastVpW = curVpW;
			s_lastVpH = curVpH;
			s_lastDsvW = curDsvW;
			s_lastDsvH = curDsvH;
			s_lastTexFmt = curTexFmt;
			s_lastViewFmt = curViewFmt;
			s_lastSamples = curSamples;
			s_lastRTVBoundCount = rtvBoundCount;
		}
	}

	// Release OM refs
	if (dsv)
		dsv->Release();
	for (auto*& r : rtvs) {
		if (r) {
			r->Release();
			r = nullptr;
		}
	}  // Enter main Z-prepass region
	if (shaderCache->IsEnabled() && !singleton.renderDepth && isMainDepth && depthOnly) {
		auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

		singleton.averageEyePosition = Util::GetAverageEyePosition();
		mainDepth.depthSRV = singleton.blendedDepthTexture->srv.get();

		singleton.renderDepth = true;
		singleton.ResetDepth();

		logger::trace("[TB] Enter main depth prepass (kMAIN). vp0={}x{} depthOnly={}", (uint32_t)vp0.Width, (uint32_t)vp0.Height, depthOnly);
	}

	// Exit main Z-prepass region before first color draw / before kMAIN depth is no longer bound
	if (shaderCache->IsEnabled() && singleton.renderDepth && (!isMainDepth || !depthOnly)) {
		singleton.renderDepth = false;

		if (singleton.renderTerrainDepth) {
			singleton.renderTerrainDepth = false;
			singleton.ResetTerrainDepth();
		}

		singleton.BlendPrepassDepths();

		logger::trace("[TB] Exit main depth prepass (kMAIN). vp0={}x{} depthOnly={}", (uint32_t)vp0.Width, (uint32_t)vp0.Height, depthOnly);
	}
	// -------------------------------------------------------------------------------

	if (shaderCache->IsEnabled()) {
		if (singleton.renderDepth) {
			// Entering or exiting terrain depth section
			bool inTerrain = false;
			if (a_pass->shaderProperty && a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape)) {
				inTerrain = true;
			}
			if (inTerrain) {
				if ((a_pass->geometry->worldBound.center.GetDistance(singleton.averageEyePosition) - a_pass->geometry->worldBound.radius) > 2048.0f) {
					inTerrain = false;
				}
			}

			if (singleton.renderTerrainDepth != inTerrain) {
				logger::trace("[TB] renderTerrainDepth {} -> {} (inTerrain={})", singleton.renderTerrainDepth, inTerrain, inTerrain);
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
						return;
					}

					// Detect meshes which should not get terrain blending using an unused flag (kNoTransparencyMultiSample)
					if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kNoTransparencyMultiSample)) {
						RenderPass call{ a_pass, a_technique, a_alphaTest, a_renderFlags };
						singleton.renderPasses.push_back(call);
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
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);

		// Reset alpha blending
		alphaBlendMode = 0;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);

		// Reset depth testing
		depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTestEqual;
		stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

		for (auto& renderPass : renderPasses)
			Hooks::BSBatchRenderer__RenderPassImmediately::func(renderPass.a_pass, renderPass.a_technique, renderPass.a_alphaTest, renderPass.a_renderFlags);

		terrainRenderPasses.clear();
		renderPasses.clear();
	}

	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	mainDepth.depthSRV = depthSRVBackup;
}
