#include "Deferred.h"
#include "Features/Upscaling/NeuralRendering/CharacterCategoryFormat.h"
#include "GpuPass.h"
#include "Utils/ComputeState.h"
#include "Utils/ResourceName.h"
#include <wrl/client.h>

#include <DDSTextureLoader.h>

#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

#include "Features/CSEditor.h"
#include "Features/DynamicCubemaps.h"
#include "Features/IBL.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"

#include "Hooks.h"

struct DepthStates
{
	ID3D11DepthStencilState* a[6][40];
};

struct BlendStates
{
	ID3D11BlendState* a[7][2][13][2];

	static BlendStates* GetSingleton()
	{
		static auto blendStates = reinterpret_cast<BlendStates*>(REL::RelocationID(524749, 411364).address());
		return blendStates;
	}
};

static void ReleaseRenderTargetSlot(RE::RENDER_TARGET target)
{
	auto renderer = globals::game::renderer;
	if (!renderer)
		return;

	auto& data = renderer->GetRuntimeData().renderTargets[target];
	if (data.SRVCopy) {
		data.SRVCopy->Release();
		data.SRVCopy = nullptr;
	}
	if (data.textureCopy) {
		data.textureCopy->Release();
		data.textureCopy = nullptr;
	}
	if (data.UAV) {
		data.UAV->Release();
		data.UAV = nullptr;
	}
	if (data.RTV) {
		data.RTV->Release();
		data.RTV = nullptr;
	}
	if (data.SRV) {
		data.SRV->Release();
		data.SRV = nullptr;
	}
	if (data.texture) {
		data.texture->Release();
		data.texture = nullptr;
	}
}

void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags)
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	texDesc.BindFlags = bindFlags;
	texDesc.Format = format;
	srvDesc.Format = format;
	rtvDesc.Format = format;
	uavDesc.Format = format;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
	const auto name = std::format("Deferred::{}", magic_enum::enum_name(target));
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, texture.GetAddressOf()));
	Util::SetResourceName(texture.Get(), "%s", name.c_str());

	if (texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture.Get(), &srvDesc, srv.GetAddressOf()));
		Util::SetResourceName(srv.Get(), "%s SRV", name.c_str());
	}
	if (texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		DX::ThrowIfFailed(device->CreateRenderTargetView(texture.Get(), &rtvDesc, rtv.GetAddressOf()));
		Util::SetResourceName(rtv.Get(), "%s RTV", name.c_str());
	}
	if (texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture.Get(), &uavDesc, uav.GetAddressOf()));
		Util::SetResourceName(uav.Get(), "%s UAV", name.c_str());
	}

	// Publish only a complete target; failures leave the previous layout usable.
	auto& data = renderer->GetRuntimeData().renderTargets[target];
	ReleaseRenderTargetSlot(target);
	data.texture = texture.Detach();
	data.SRV = srv.Detach();
	data.RTV = rtv.Detach();
	data.UAV = uav.Detach();
}

void Deferred::SetupResources()
{
	if (resourceDevice.get() != globals::d3d::device) {
		ClearShaderCache();
		resourceDevice.copy_from(globals::d3d::device);
	}
	shadowCopyContext = nullptr;
	resourceContext = globals::d3d::context;
	if (resourceContext)
		(void)resourceContext->QueryInterface(__uuidof(ID3D11DeviceContext1), shadowCopyContext.put_void());
	characterCategoryCaptureValid = false;
	authoredCategories.reset();
	authoredDepth.reset();
	categoryCopyFailureReported = false;
	globals::features::upscaling.SetNeuralSceneCategories(nullptr, nullptr, 0, 0, 0);
	finalSceneDepthFrame.reset();
	auto renderer = globals::game::renderer;

	{
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		main.texture->GetDesc(&texDesc);
		main.SRV->GetDesc(&srvDesc);
		main.RTV->GetDesc(&rtvDesc);
		main.UAV->GetDesc(&uavDesc);

		// Available targets:
		// MAIN ONLY ALPHA
		// WATER REFLECTIONS
		// BLURFULL_BUFFER
		// LENSFLAREVIS
		// SAO DOWNSCALED
		// SAO CAMERAZ+MIP_LEVEL_0_ESRAM
		// SAO_RAWAO_DOWNSCALED
		// SAO_RAWAO_PREVIOUS_DOWNSCALDE
		// SAO_TEMP_BLUR_DOWNSCALED
		// INDIRECT
		// INDIRECT_DOWNSCALED
		// RAWINDIRECT
		// RAWINDIRECT_DOWNSCALED
		// RAWINDIRECT_PREVIOUS
		// RAWINDIRECT_PREVIOUS_DOWNSCALED
		// RAWINDIRECT_SWAP
		// VOLUMETRIC_LIGHTING_HALF_RES
		// VOLUMETRIC_LIGHTING_BLUR_HALF_RES
		// VOLUMETRIC_LIGHTING_QUARTER_RES
		// VOLUMETRIC_LIGHTING_BLUR_QUARTER_RES
		// TEMPORAL_AA_WATER_1
		// TEMPORAL_AA_WATER_2

		// Albedo
		SetupRenderTarget(ALBEDO, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Specular
		SetupRenderTarget(SPECULAR, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Reflectance
		SetupRenderTarget(REFLECTANCE, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Normal + Roughness
		SetupRenderTarget(NORMALROUGHNESS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks
		SetupRenderTarget(MASKS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks2 (vertexAO; fp16 to allow blending)
		const bool categories = globals::features::upscaling.NeedsCharacterCategoryShaders();
		SetupRenderTarget(MASKS2, texDesc, srvDesc, rtvDesc, uavDesc, categories ? NeuralRendering::kCharacterCategoryFormat : NeuralRendering::kVertexAoFormat, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		characterCategoryEnabled = categories;
		failedCharacterCategoryMode.reset();

		// TAA water history buffers need RGBA16: alpha stores premultiplied coverage for ISWaterBlend
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_1, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	}

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		winrt::com_ptr<ID3D11SamplerState> linear, point;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linear.put()));
		Util::SetResourceName(linear.get(), "Deferred::LinearSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, point.put()));
		Util::SetResourceName(point.get(), "Deferred::PointSampler");
		linearSamplerOwner = std::move(linear);
		pointSamplerOwner = std::move(point);
		linearSampler = linearSamplerOwner.get();
		pointSampler = pointSamplerOwner.get();
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = 1;

		sbDesc.StructureByteStride = sizeof(PerGeometry);
		sbDesc.ByteWidth = sizeof(PerGeometry) * numElements;
		auto shadowData = std::make_unique<Buffer>(sbDesc, nullptr, "Deferred::UtilityShadowData");
		srvDesc.Buffer.NumElements = numElements;
		shadowData->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		shadowData->CreateUAV(uavDesc);
		perShadow = std::move(shadowData);

		copyShadowCS.Get(
			L"Data\\Shaders\\CopyShadowDataCS.hlsl",
			{},
			"cs_5_0",
			"main",
			"Deferred::CopyShadowDataCS");
	}

	// Directional shadow structured buffer (t98): CPU-written each frame, read-only on GPU.
	// One element holds the sun cascade data uploaded from BSShadowDirectionalLight.
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(DirectionalShadowLightData);
		sbDesc.ByteWidth = sizeof(DirectionalShadowLightData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;

		auto lightData = std::make_unique<Buffer>(sbDesc, nullptr, "Deferred::DirectionalShadowLights");
		lightData->CreateSRV(srvDesc);
		directionalShadowLights = std::move(lightData);
	}
}

void Deferred::ReflectionsPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Reflections Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto state = globals::state;

	state->activeReflections = true;
	state->UpdateSharedData(false, false);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	Feature::ForEachLoadedFeature("ReflectionsPrepass", [](Feature* feature) { feature->ReflectionsPrepass(); }, true);
}

void Deferred::EarlyPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Early Prepass");
	finalSceneDepthFrame.reset();

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false, true);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	// Shadow maps have just been rendered — upload BSShadowDirectionalLight data to t98.
	CopyShadowLightData();

	Feature::ForEachLoadedFeature("EarlyPrepass", [](Feature* feature) { feature->EarlyPrepass(); }, true);
}

void Deferred::PrepassPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	Feature::ForEachLoadedFeature("Prepass", [](Feature* feature) { feature->Prepass(); }, true);
}

void Deferred::StartDeferred()
{
	UpdateCharacterCategoryTarget();
	characterCategoryCaptureValid = characterCategoryEnabled;
	globals::features::upscaling.SetNeuralSceneCategories(nullptr, nullptr, 0, 0, 0);
	finalSceneDepthFrame.reset();
	if (!globals::state->inWorld)
		return;
	globals::state->UpdateSharedData(true, false);

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& setRenderTargetMode = shadowState->GetRuntimeData().setRenderTargetMode;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

	// Backup original render targets
	for (uint i = 0; i < 4; i++) {
		forwardRenderTargets[i] = renderTargets[i];
	}

	RE::RENDER_TARGET targets[8]{
		RE::RENDER_TARGET::kMAIN,
		RE::RENDER_TARGET::kMOTION_VECTOR,
		NORMALROUGHNESS,
		ALBEDO,
		SPECULAR,
		REFLECTANCE,
		MASKS,
		MASKS2
	};

	for (uint i = 2; i < 8; i++) {
		renderTargets[i] = targets[i];                                             // We must use unused targets to be indexable
		setRenderTargetMode[i] = RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR;  // Dirty from last frame, this calls ClearRenderTargetView once
	}

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = true;

	{
		auto context = globals::d3d::context;

		ID3D11Buffer* buffers[1] = { *globals::game::perFrame.get() };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	PrepassPasses();

	OverrideBlendStates();
}

bool Deferred::IsSceneDepthFinal() const
{
	return globals::state && finalSceneDepthFrame && *finalSceneDepthFrame == globals::state->frameCount;
}

bool Deferred::CopySceneDepth()
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	if (!renderer || !context || !globals::state)
		return false;

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	// A master-switch change can skip terrain rendering after its SRV redirection.
	auto& terrainBlending = globals::features::terrainBlending;
	if (terrainBlending.loaded && terrainBlending.blendedDepthTexture) {
		auto* blendedDepthSRV = terrainBlending.blendedDepthTexture->srv.get();
		if (blendedDepthSRV && depth.depthSRV == blendedDepthSRV)
			depth.depthSRV = terrainBlending.depthSRVBackup;
		if (blendedDepthSRV && depthCopy.depthSRV == blendedDepthSRV)
			depthCopy.depthSRV = terrainBlending.prepassSRVBackup;
	}

	if (!depth.texture || !depthCopy.texture || !depthCopy.depthSRV)
		return false;

	TracyD3D11Zone(globals::state->tracyCtx, "Deferred - Copy Scene Depth");
	// Water also consumes this copy, including pixels outside the active scaled area.
	context->CopyResource(depthCopy.texture, depth.texture);
	finalSceneDepthFrame = globals::state->frameCount;
	return true;
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	CopySceneDepth();

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];
	auto masks2 = renderer->GetRuntimeData().renderTargets[MASKS2];

	auto main = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[0]];
	auto normals = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[2]];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto reflectance = renderer->GetRuntimeData().renderTargets[REFLECTANCE];

	auto motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool interior = Util::IsInterior();

	auto& skylighting = globals::features::skylighting;

	auto& ssgi = globals::features::screenSpaceGI;
	if (ssgi.loaded)
		ssgi.DrawSSGI();
	auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec] = ssgi.GetOutputTextures();
	bool ssgi_hq_spec = ssgi.settings.EnableExperimentalSpecularGI;

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	auto& sss = globals::features::subsurfaceScattering;
	if (sss.loaded)
		sss.DrawSSS();

	auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	if (dynamicCubemaps.loaded)
		dynamicCubemaps.UpdateCubemap();

	auto& ibl = globals::features::ibl;

	// Deferred Composite
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite");

		ID3D11ShaderResourceView* srvs[16]{
			specular.SRV,                                                                                              // t0  SpecularTexture
			albedo.SRV,                                                                                                // t1  AlbedoTexture
			normalRoughness.SRV,                                                                                       // t2  NormalRoughnessTexture
			masks.SRV,                                                                                                 // t3  MasksTexture
			dynamicCubemaps.loaded ? Util::GetCurrentSceneDepthSRV(false) : nullptr,                                   // t4  DepthTexture (24/32-bit; HLSL type baked at compile via TERRAIN_BLENDING)
			dynamicCubemaps.loaded ? reflectance.SRV : nullptr,                                                        // t5  ReflectanceTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr,                                  // t6  EnvTexture
			dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr,                       // t7  EnvReflectionsTexture
			dynamicCubemaps.loaded && skylighting.IsRuntimeActive() ? skylighting.texProbeArray->srv.get() : nullptr,  // t8  SkylightingProbeArray
			masks2.SRV,                                                                                                // t9  Masks2Texture (vertexAO in .x)
			ssgi_ao,                                                                                                   // t10 SsgiAoTexture
			ssgi_hq_spec ? nullptr : ssgi_y,                                                                           // t11 SsgiYTexture
			ssgi_hq_spec ? nullptr : ssgi_cocg,                                                                        // t12 SsgiCoCgTexture
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,                                                                     // t13 SsgiSpecularTexture
			ibl.loaded ? ibl.envIBLTexture->srv.get() : nullptr,                                                       // t14 EnvIBLTexture
			ibl.loaded ? ibl.skyIBLTexture->srv.get() : nullptr,                                                       // t15 SkyIBLTexture
		};

		if (dynamicCubemaps.loaded)
			context->CSSetSamplers(0, 1, &linearSampler);

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[3]{ main.UAV, normals.UAV, motionVectors.UAV };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		if (auto* shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite()) {
			context->CSSetShader(shader, nullptr, 0);

			TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Dispatch");
			globals::profiler->BeginPass("DeferredComposite");
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
			globals::profiler->EndPass();
		}
	}

	// Clear
	{
		ID3D11ShaderResourceView* views[16]{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3]{ nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(12, 1, buffers);

		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (dynamicCubemaps.loaded)
		dynamicCubemaps.PostDeferred();
}

void Deferred::EndDeferred()
{
	if (!deferredPass)
		return;

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

	// Do not render to our targets past this point
	for (uint i = 0; i < 4; i++) {
		renderTargets[i] = forwardRenderTargets[i];
	}

	for (uint i = 4; i < 8; i++) {
		renderTargets[i] = RE::RENDER_TARGET::kNONE;
	}

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	DeferredPasses();  // Perform deferred passes and composite forward buffers

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = false;

	ResetBlendStates();
}

void Deferred::OverrideBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	static std::once_flag setup;
	std::call_once(setup, [&]() {
		auto device = globals::d3d::device;

		for (int a = 0; a < 7; a++) {
			for (int b = 0; b < 2; b++) {
				for (int c = 0; c < 13; c++) {
					for (int d = 0; d < 2; d++) {
						forwardBlendStates[a][b][c][d] = blendStates->a[a][b][c][d];

						if (auto blendState = forwardBlendStates[a][b][c][d]) {
							D3D11_BLEND_DESC blendDesc;
							forwardBlendStates[a][b][c][d]->GetDesc(&blendDesc);

							blendDesc.IndependentBlendEnable = true;

							// Default to original blending method
							for (int i = 1; i < 8; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = blendDesc.RenderTarget[0].SrcBlend;
								blendDesc.RenderTarget[i].DestBlend = blendDesc.RenderTarget[0].DestBlend;
								blendDesc.RenderTarget[i].BlendOp = blendDesc.RenderTarget[0].BlendOp;
								blendDesc.RenderTarget[i].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlendAlpha;
								blendDesc.RenderTarget[i].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlendAlpha;
								blendDesc.RenderTarget[i].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOpAlpha;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = blendDesc.RenderTarget[0].RenderTargetWriteMask;
							}

							// Normals and motion vectors must use alpha blending
							for (int i = 1; i < 3; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
							}

							DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &deferredBlendStates[a][b][c][d]));
						} else {
							deferredBlendStates[a][b][c][d] = nullptr;
						}
					}
				}
			}
		}
	});

	// Set modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = deferredBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::ResetBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	// Restore modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = forwardBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

template <typename T>
void Deferred::SetShadowCascadeParameters(T& lightData, DirectionalShadowLightData& dd)
{
	const auto count = std::min(lightData.shadowmapDescriptors.size(), static_cast<uint32_t>(std::size(dd.ShadowProj)));
	for (uint32_t i = 0; i < count; i++) {
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&lightData.shadowmapDescriptors[i].lightTransform));
		DirectX::XMStoreFloat4x4(&dd.ShadowProj[i], proj);

		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&dd.InvShadowProj[i], invProj);
	}
}

void Deferred::CopyShadowLightData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowLightData");

	auto* shadowSceneNode = globals::game::smState->shadowSceneNode[0];
	if (!shadowSceneNode)
		return;

	auto* sunShadowLight = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
	if (!sunShadowLight)
		return;

	DirectionalShadowLightData dd{};
	auto context = globals::d3d::context;

	auto& dirData = sunShadowLight->GetShadowDirectionalLightRuntimeData();
	dd.EndSplitDistances = { dirData.endSplitDistances[0], dirData.endSplitDistances[1] };
	dd.StartSplitDistances = { dirData.startSplitDistances[0], dirData.startSplitDistances[1] };

	SetShadowCascadeParameters(sunShadowLight->GetRuntimeData(), dd);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	DX::ThrowIfFailed(context->Map(directionalShadowLights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy(mapped.pData, &dd, sizeof(DirectionalShadowLightData));
	context->Unmap(directionalShadowLights->resource.get(), 0);

	ID3D11ShaderResourceView* srv = directionalShadowLights->srv.get();
	context->PSSetShaderResources(98, 1, &srv);
}

void Deferred::ClearShaderCache()
{
	copyShadowCS.Reset();
	mainCompositeCS.Reset();
	mainCompositeInteriorCS.Reset();
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	std::vector<std::pair<const char*, const char*>> defines;

	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

	if (globals::features::skylighting.loaded)
		defines.push_back({ "SKYLIGHTING", nullptr });

	if (globals::features::screenSpaceGI.loaded)
		defines.push_back({ "SSGI", nullptr });

	if (globals::features::ibl.loaded)
		defines.push_back({ "IBL", nullptr });

	// TERRAIN_BLENDING flips DepthTexture's HLSL type from `Texture2D<unorm float>`
	// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
	if (globals::features::terrainBlending.loaded)
		defines.push_back({ "TERRAIN_BLENDING", nullptr });

	return mainCompositeCS.Get(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0");
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeInterior()
{
	std::vector<std::pair<const char*, const char*>> defines;
	defines.push_back({ "INTERIOR", nullptr });

	if (globals::features::dynamicCubemaps.loaded)
		defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

	if (globals::features::screenSpaceGI.loaded)
		defines.push_back({ "SSGI", nullptr });

	if (globals::features::ibl.loaded)
		defines.push_back({ "IBL", nullptr });

	// TERRAIN_BLENDING flips DepthTexture's HLSL type from `Texture2D<unorm float>`
	// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
	if (globals::features::terrainBlending.loaded)
		defines.push_back({ "TERRAIN_BLENDING", nullptr });

	return mainCompositeInteriorCS.Get(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0");
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	func();
	globals::deferred->EarlyPrepasses();
};

void Deferred::Hooks::Main_RenderWorld::thunk(bool a1)
{
	auto* const state = globals::state;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	state->inWorld = true;
	func(a1);

	state->inWorld = false;
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
};

void Deferred::Hooks::Main_RenderWorld_Start::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
	// Each opaque pass invalidates the prior copy, even with deferred rendering disabled.
	globals::deferred->finalSceneDepthFrame.reset();
	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		// Here is where the first opaque objects start rendering
		globals::deferred->StartDeferred();
	}

	func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches
};

void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;

	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		auto& terrainBlending = globals::features::terrainBlending;
		// Defer terrain rendering until after everything else
		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			terrainBlending.RenderTerrainBlendingPasses();
		}
	}

	deferred->CaptureCharacterCategories();

	// Deferred blended decals

	func(This, RenderFlags);

	deferred->EndDeferred();

	// Water still needs completed opaque depth when no deferred pass produced it.
	if (!deferred->IsSceneDepthFinal())
		deferred->CopySceneDepth();
};

void Deferred::Hooks::BSCubeMapCamera_RenderCubemap::thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
{
	auto deferred = globals::deferred;
	auto state = globals::state;

	deferred->ReflectionsPrepasses();
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
	func(camera, a2, a3, a4, a5);
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
}

void Deferred::Hooks::Main_RenderFirstPersonView::thunk(bool a1, bool a2)
{
	auto* const state = globals::state;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	func(a1, a2);
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
}

void Deferred::Hooks::Renderer_ResetState::thunk(void* This)
{
	func(This);

	auto* const state = globals::state;
	auto* const context = globals::d3d::context;

	ID3D11Buffer* buffers[3] = { state->permutationCB->CB(), state->sharedDataCB->CB(), state->featureDataCB->CB() };
	context->PSSetConstantBuffers(4, 3, buffers);
	context->CSSetConstantBuffers(5, 2, buffers + 1);
}

void Deferred::CopyShadowData()
{
	CS_GPU_PASS("Deferred::CopyShadowData");
	auto* context = globals::d3d::context;
	if (!context)
		return;

	winrt::com_ptr<ID3D11ShaderResourceView> sourceShadow;
	context->PSGetShaderResources(4, 1, sourceShadow.put());
	ID3D11ShaderResourceView* shadowSRVs[2]{ sourceShadow.get(), nullptr };
	// Clear the previous structured view before writing it or taking a fallback.
	context->PSSetShaderResources(18, ARRAYSIZE(shadowSRVs), shadowSRVs);
	if (!shadowCopyContext || resourceContext != context ||
		resourceDevice.get() != globals::d3d::device || !perShadow || !perShadow->srv || !perShadow->uav)
		return;

	auto* copyShader = copyShadowCS.Get(
		L"Data\\Shaders\\CopyShadowDataCS.hlsl",
		{},
		"cs_5_0",
		"main",
		"Deferred::CopyShadowDataCS");
	if (!copyShader)
		return;

	constexpr std::array<UINT, 3> sourceSlots{ 0, 12, 2 };
	std::array<winrt::com_ptr<ID3D11Buffer>, 3> owners;
	std::array<ID3D11Buffer*, 3> buffers{};
	std::array<UINT, 3> first{}, count{};
	for (std::size_t index = 0; index < sourceSlots.size(); ++index) {
		shadowCopyContext->PSGetConstantBuffers1(sourceSlots[index], 1, owners[index].put(), &first[index], &count[index]);
		buffers[index] = owners[index].get();
		if (!buffers[index])
			return;
	}

	{
		D3DState::ComputeState restore(shadowCopyContext.get(), 1);
		ID3D11UnorderedAccessView* output = perShadow->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		for (UINT index = 0; index < buffers.size(); ++index) {
			if (first[index] != 0 || count[index] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)
				shadowCopyContext->CSSetConstantBuffers1(index, 1, &buffers[index], &first[index], &count[index]);
			else
				context->CSSetConstantBuffers(index, 1, &buffers[index]);
		}
		context->CSSetShader(copyShader, nullptr, 0);
		context->Dispatch(1, 1, 1);
	}

	shadowSRVs[1] = perShadow->srv.get();
	context->PSSetShaderResources(18, ARRAYSIZE(shadowSRVs), shadowSRVs);
}

void Deferred::UpdateCharacterCategoryTarget()
{
	const bool requested = globals::features::upscaling.NeedsCharacterCategoryShaders();
	if (requested == characterCategoryEnabled) {
		failedCharacterCategoryMode.reset();
		return;
	}
	if (failedCharacterCategoryMode == requested)
		return;

	globals::features::upscaling.SetNeuralSceneCategories(nullptr, nullptr, 0, 0, 0);
	auto& target = globals::game::renderer->GetRuntimeData().renderTargets[MASKS2];
	try {
		if (!target.texture || !target.SRV || !target.RTV)
			throw std::runtime_error("Deferred mask target is incomplete");
		D3D11_TEXTURE2D_DESC textureDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		target.texture->GetDesc(&textureDesc);
		target.SRV->GetDesc(&srvDesc);
		target.RTV->GetDesc(&rtvDesc);
		SetupRenderTarget(MASKS2, textureDesc, srvDesc, rtvDesc, {},
			requested ? NeuralRendering::kCharacterCategoryFormat : NeuralRendering::kVertexAoFormat,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		characterCategoryEnabled = requested;
		failedCharacterCategoryMode.reset();
	} catch (const std::exception& error) {
		// Retry after a settings transition or resource recreation, not every draw.
		failedCharacterCategoryMode = requested;
		logger::error("Deferred mask layout switch failed; retaining the previous layout: {}", error.what());
	}
}

void Deferred::CaptureCharacterCategories()
{
	auto& upscaling = globals::features::upscaling;
	upscaling.SetNeuralSceneCategories(nullptr, nullptr, 0, 0, 0);
	if (!deferredPass || !IsCharacterCategoryCaptureReady() ||
		!upscaling.IsNeuralCharacterSelectionRequested() || !globals::state->inWorld)
		return;
	auto* renderer = globals::game::renderer;
	auto* context = globals::d3d::context;
	if (!renderer || !context || !globals::d3d::device)
		return;

	const auto plan = upscaling.GetRuntimeResolutionPlan();
	const auto width = plan.engineRenderSize.x, height = plan.engineRenderSize.y;
	if (!width || !height || plan.menuContextActive || plan.loadingMenuActive)
		return;
	const auto& categories = renderer->GetRuntimeData().renderTargets[MASKS2];
	auto* depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
	if (!categories.SRV || !depth)
		return;

	try {
		// Build both copies before publishing either. They retain the exact opaque
		// mask/depth pair even when decals, first-person geometry or upscaling reuse
		// the engine attachments later in this frame.
		const auto prepare = [&](ID3D11ShaderResourceView* source, std::unique_ptr<Texture2D>& copy,
								 const char* name) -> winrt::com_ptr<ID3D11Texture2D> {
			winrt::com_ptr<ID3D11Resource> resource;
			source->GetResource(resource.put());
			auto texture = resource.as<ID3D11Texture2D>();
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (desc.Width < width || desc.Height < height || desc.ArraySize != 1 ||
				desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
				throw std::runtime_error("NR category source has an incompatible viewport or texture layout");
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			source->GetDesc(&srv);
			if (srv.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
				throw std::runtime_error("NR category source is not a single-view texture");
			if (!copy || copy->desc.Width != desc.Width || copy->desc.Height != desc.Height || copy->desc.Format != desc.Format) {
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.CPUAccessFlags = 0;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.MiscFlags = 0;
				auto replacement = std::make_unique<Texture2D>(desc, name);
				replacement->CreateSRV(srv);
				copy = std::move(replacement);
			}
			return texture;
		};
		const auto categoryTexture = prepare(categories.SRV, authoredCategories, "NR::AuthoredCategories");
		const auto depthTexture = prepare(depth, authoredDepth, "NR::AuthoredDepth");
		if (authoredCategories->desc.Width != authoredDepth->desc.Width ||
			authoredCategories->desc.Height != authoredDepth->desc.Height)
			throw std::runtime_error("NR category and depth extents differ");
		ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		winrt::com_ptr<ID3D11DepthStencilView> previousDepth;
		context->OMGetRenderTargets(std::size(targets), targets, previousDepth.put());
		const SKSE::stl::scope_exit restore([&]() noexcept {
			context->OMSetRenderTargets(std::size(targets), targets, previousDepth.get());
			for (auto* target : targets)
				if (target)
					target->Release();
		});
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->CopyResource(authoredCategories->resource.get(), categoryTexture.get());
		context->CopyResource(authoredDepth->resource.get(), depthTexture.get());
		upscaling.SetNeuralSceneCategories(authoredCategories->srv.get(), authoredDepth->srv.get(),
			globals::state->frameCount, width, height);
		categoryCopyFailureReported = false;
	} catch (const std::exception& error) {
		characterCategoryCaptureValid = false;
		if (!categoryCopyFailureReported)
			logger::warn("NR character guide capture unavailable: {}", error.what());
		categoryCopyFailureReported = true;
	}
}
