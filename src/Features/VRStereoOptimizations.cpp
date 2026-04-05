#include "VRStereoOptimizations.h"

#include "Features/FoveatedCommon.h"
#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"
#include "Features/WetnessEffects.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

void VRStereoOptimizations::SetupResources()
{
	if (!globals::game::isVR)
		return;

	if (!stereoOptimizationCB) {
		stereoOptimizationCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<StereoOptimizationCB>());
	}
	EnsureFallbackModeTexture();
}

void VRStereoOptimizations::ResetFrameState()
{
	preparedThisFrame = false;
	dispatchGroupsX = 0;
	dispatchGroupsY = 0;
}

void VRStereoOptimizations::ClearShaderCache()
{
	if (classifyCS) {
		classifyCS->Release();
		classifyCS = nullptr;
	}
	if (blendCS) {
		blendCS->Release();
		blendCS = nullptr;
	}
}

bool VRStereoOptimizations::EnsureModeTexture(uint32_t renderWidth, uint32_t renderHeight)
{
	if (!renderWidth || !renderHeight)
		return false;

	const bool needsRecreate = !modeTexture ||
		modeTexture->desc.Width != renderWidth ||
		modeTexture->desc.Height != renderHeight;

	if (!needsRecreate)
		return true;

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = renderWidth;
	texDesc.Height = renderHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_UINT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	modeTexture = eastl::make_unique<Texture2D>(texDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	modeTexture->CreateSRV(srvDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	modeTexture->CreateUAV(uavDesc);

	Util::SetResourceName(modeTexture->resource.get(), "VRStereoOptimizations_ModeTexture");
	if (modeTexture->srv)
		Util::SetResourceName(modeTexture->srv.get(), "VRStereoOptimizations_ModeTexture_SRV");
	if (modeTexture->uav)
		Util::SetResourceName(modeTexture->uav.get(), "VRStereoOptimizations_ModeTexture_UAV");

	return true;
}

void VRStereoOptimizations::EnsureFallbackModeTexture()
{
	if (!fallbackModeTexture) {
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = 1;
		texDesc.Height = 1;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_UINT;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.CPUAccessFlags = 0;
		texDesc.MiscFlags = 0;

		fallbackModeTexture = eastl::make_unique<Texture2D>(texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		fallbackModeTexture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		fallbackModeTexture->CreateUAV(uavDesc);

		Util::SetResourceName(fallbackModeTexture->resource.get(), "VRStereoOptimizations_FallbackModeTexture");
		if (fallbackModeTexture->srv)
			Util::SetResourceName(fallbackModeTexture->srv.get(), "VRStereoOptimizations_FallbackModeTexture_SRV");
		if (fallbackModeTexture->uav)
			Util::SetResourceName(fallbackModeTexture->uav.get(), "VRStereoOptimizations_FallbackModeTexture_UAV");

		fallbackModeCleared = false;
	}

	if (!fallbackModeCleared) {
		auto* context = globals::d3d::context;
		if (context && fallbackModeTexture && fallbackModeTexture->uav) {
			const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
			context->ClearUnorderedAccessViewUint(fallbackModeTexture->uav.get(), clearValues);
			fallbackModeCleared = true;
		}
	}
}

VRStereoOptimizations::PresetTuning VRStereoOptimizations::GetPresetTuning(VRStereoOptimizationSettings::Preset preset)
{
	switch (preset) {
	case VRStereoOptimizationSettings::Preset::Performance:
		return {
			0.0048f,  // disocclusionThreshold
			0.0028f,  // edgeDepthThreshold
			1.25f,    // edgeBandPixels
			0.40f,    // centerProtection
			0.72f     // centerFullBlendThreshold
		};
	case VRStereoOptimizationSettings::Preset::Quality:
	default:
		return {
			0.0025f,  // disocclusionThreshold
			0.0015f,  // edgeDepthThreshold
			2.0f,     // edgeBandPixels
			0.78f,    // centerProtection
			0.50f     // centerFullBlendThreshold
		};
	}
}

void VRStereoOptimizations::UpdateConstantBuffer(const VRStereoOptimizationSettings& settings, uint32_t renderWidth, uint32_t renderHeight)
{
	StereoOptimizationCB cb{};
	cb.renderDim = { static_cast<float>(renderWidth), static_cast<float>(renderHeight) };
	cb.invRenderDim = { 1.0f / cb.renderDim.x, 1.0f / cb.renderDim.y };

	const auto& upscaling = globals::features::upscaling;
	const auto centerOffsets = upscaling.GetResolvedFoveatedMaskCenterOffsets();
	cb.centerOffsetLeft = centerOffsets[0];
	cb.centerOffsetRight = centerOffsets[1];
	cb.centerArea = std::clamp(upscaling.settings.foveatedCenterArea, FoveatedCommon::kCenterAreaMin, FoveatedCommon::kCenterAreaMax);

	const auto presetTuning = GetPresetTuning(settings.Mode);
	cb.disocclusionThreshold = presetTuning.disocclusionThreshold;
	cb.edgeDepthThreshold = presetTuning.edgeDepthThreshold;
	cb.edgeBandPixels = presetTuning.edgeBandPixels;
	cb.centerProtection = presetTuning.centerProtection;
	cb.centerFullBlendThreshold = presetTuning.centerFullBlendThreshold;

	cb.enableNearFieldFullBlend = settings.EnableNearFieldFullBlend ? 1u : 0u;
	const float clampedDistance = std::clamp(settings.NearFieldBlendDistance, 1.0f, 5000.0f);
	const float clampedRange = std::clamp(settings.NearFieldBlendRange, 1.0f, 5000.0f);
	const float halfRange = 0.5f * clampedRange;
	cb.nearFieldBlendStart = std::max(1.0f, clampedDistance - halfRange);
	cb.nearFieldBlendEnd = std::max(cb.nearFieldBlendStart + 1.0f, clampedDistance + halfRange);
	cb.forwardOcclusionScale = std::clamp(settings.ForwardOcclusionScale, 0.0f, 1.0f);
	cb.dispatchXOffsetPixels = settings.EnableEye1OnlyDispatchOptimization ? (renderWidth / 2u) : 0u;

	stereoOptimizationCB->Update(cb);
}

bool VRStereoOptimizations::IsCompatibilityBlocked(const VRStereoOptimizationSettings& settings) const
{
	if (!settings.DisableWhenTerrainBlendingAndWetness)
		return false;

	const auto& terrainBlending = globals::features::terrainBlending;
	const auto& wetness = globals::features::wetnessEffects;
	const bool terrainBlendingEnabled = terrainBlending.loaded && terrainBlending.settings.Enabled != 0;
	const bool wetnessEnabled = wetness.loaded && wetness.settings.EnableWetnessEffects != 0;
	return terrainBlendingEnabled && wetnessEnabled;
}

bool VRStereoOptimizations::Prepare(const VRStereoOptimizationSettings& settings, ID3D11ShaderResourceView* depthSRV, uint32_t renderWidth, uint32_t renderHeight)
{
	ResetFrameState();

	if (!globals::game::isVR)
		return false;

	SetupResources();

	if (!settings.Enabled)
		return false;

	if (IsCompatibilityBlocked(settings)) {
		if (!warnedCompatibility) {
			logger::info("[VRStereoOptimizations] Disabled for compatibility: Terrain Blending + Wetness Effects are both active.");
			warnedCompatibility = true;
		}
		return false;
	}

	warnedCompatibility = false;

	if (!depthSRV || renderWidth == 0 || renderHeight == 0)
		return false;

	uint32_t depthWidth = 0;
	uint32_t depthHeight = 0;
	if (!Util::TryGetDepthSrvDimensions(depthSRV, depthWidth, depthHeight))
		return false;

	const int viewportWidthPerEye = static_cast<int>(renderWidth / 2u);
	if (Util::DetectVRDepthLayout(depthWidth, viewportWidthPerEye) != Util::VRDepthLayout::CombinedStereo ||
		depthWidth != renderWidth ||
		depthHeight != renderHeight) {
		if (!warnedDepthLayout) {
			logger::warn("[VRStereoOptimizations] Disabled: unsupported depth layout (depth={}x{}, color={}x{}).", depthWidth, depthHeight, renderWidth, renderHeight);
			warnedDepthLayout = true;
		}
		return false;
	}

	warnedDepthLayout = false;
	if (!stereoOptimizationCB || !EnsureModeTexture(renderWidth, renderHeight))
		return false;

	if (!classifyCS) {
		classifyCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\VR\\VRStereoClassifyCS.hlsl",
			{},
			"cs_5_0"));
	}
	if (!blendCS) {
		blendCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\VR\\VRStereoBlendCS.hlsl",
			{},
			"cs_5_0"));
	}
	if (!classifyCS || !blendCS)
		return false;

	UpdateConstantBuffer(settings, renderWidth, renderHeight);

	auto* state = globals::state;
	auto* context = globals::d3d::context;
	if (!state || !context)
		return false;

	const auto dispatchCount = Util::GetScreenDispatchCount(true);
	if (dispatchCount.x == 0 || dispatchCount.y == 0)
		return false;
	dispatchGroupsX = settings.EnableEye1OnlyDispatchOptimization ? ((dispatchCount.x + 1u) / 2u) : dispatchCount.x;
	dispatchGroupsY = dispatchCount.y;

	if (state && state->frameAnnotations)
		state->BeginPerfEvent("VR Stereo Reprojection - Classify");
	TracyD3D11Zone(state->tracyCtx, "VR Stereo Reprojection - Classify");

	const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
	context->ClearUnorderedAccessViewUint(modeTexture->uav.get(), clearValues);

	ID3D11ShaderResourceView* srvs[1] = { depthSRV };
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	ID3D11UnorderedAccessView* uavs[1] = { modeTexture->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11Buffer* cb = stereoOptimizationCB->CB();
	context->CSSetConstantBuffers(14, 1, &cb);

	context->CSSetShader(classifyCS, nullptr, 0);
	context->Dispatch(dispatchGroupsX, dispatchGroupsY, 1);

	ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);
	ID3D11Buffer* nullCBs[1] = { nullptr };
	context->CSSetConstantBuffers(14, 1, nullCBs);
	context->CSSetShader(nullptr, nullptr, 0);

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	preparedThisFrame = true;
	return true;
}

void VRStereoOptimizations::DispatchBlend(const VRStereoOptimizationSettings& settings, ID3D11UnorderedAccessView* mainUAV, ID3D11UnorderedAccessView* normalUAV, ID3D11UnorderedAccessView* motionUAV, ID3D11ShaderResourceView* depthSRV)
{
	if (!preparedThisFrame || !settings.Enabled || !blendCS || !modeTexture || !depthSRV || !mainUAV || !normalUAV || !motionUAV) {
		ResetFrameState();
		return;
	}

	auto* state = globals::state;
	auto* context = globals::d3d::context;
	if (!state || !context) {
		ResetFrameState();
		return;
	}

	const uint32_t renderWidth = modeTexture->desc.Width;
	const uint32_t renderHeight = modeTexture->desc.Height;
	if (renderWidth == 0 || renderHeight == 0 || dispatchGroupsX == 0 || dispatchGroupsY == 0) {
		ResetFrameState();
		return;
	}

	if (state && state->frameAnnotations)
		state->BeginPerfEvent("VR Stereo Reprojection - Blend");
	TracyD3D11Zone(state->tracyCtx, "VR Stereo Reprojection - Blend");

	ID3D11ShaderResourceView* srvs[2] = { depthSRV, modeTexture->srv.get() };
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

	ID3D11UnorderedAccessView* uavs[3] = { mainUAV, normalUAV, motionUAV };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11Buffer* cb = stereoOptimizationCB->CB();
	context->CSSetConstantBuffers(14, 1, &cb);

	context->CSSetShader(blendCS, nullptr, 0);
	context->Dispatch(dispatchGroupsX, dispatchGroupsY, 1);

	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
	ID3D11UnorderedAccessView* nullUAVs[3] = { nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);
	ID3D11Buffer* nullCBs[1] = { nullptr };
	context->CSSetConstantBuffers(14, 1, nullCBs);
	context->CSSetShader(nullptr, nullptr, 0);

	if (state && state->frameAnnotations)
		state->EndPerfEvent();

	ResetFrameState();
}
