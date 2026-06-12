#include "Deferred.h"

#include <DDSTextureLoader.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "ShaderCache.h"
#include "State.h"

#include "Features/DynamicCubemaps.h"
#include "Features/IBL.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/WeatherEditor.h"

#include "Hooks.h"
#include "Utils/D3D.h"

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

namespace
{
	constexpr UINT kDeferredCompositePSSRVCount = 17;
	constexpr UINT kDeferredCompositeRTVCount = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
	constexpr UINT kDeferredCompositeViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

	template <class T>
	void ReleaseCOM(T*& a_resource)
	{
		if (a_resource) {
			a_resource->Release();
			a_resource = nullptr;
		}
	}

	std::vector<std::pair<const char*, const char*>> BuildDeferredCompositeDefines(bool a_interior, bool a_metadataOnly)
	{
		std::vector<std::pair<const char*, const char*>> defines;

		if (a_interior)
			defines.push_back({ "INTERIOR", nullptr });

		if (a_metadataOnly)
			defines.push_back({ "DEFERRED_METADATA_ONLY", nullptr });

		if (!a_metadataOnly) {
			if (globals::features::dynamicCubemaps.loaded)
				defines.push_back({ "DYNAMIC_CUBEMAPS", nullptr });

			if (!a_interior && globals::features::skylighting.loaded)
				defines.push_back({ "SKYLIGHTING", nullptr });

			if (globals::features::screenSpaceGI.loaded) {
				defines.push_back({ "SSGI", nullptr });
				if (!globals::features::screenSpaceGI.HasGIResources())
					defines.push_back({ "SSGI_AO_ONLY", nullptr });
			}

			if (globals::features::ibl.loaded)
				defines.push_back({ "IBL", nullptr });
		}

		if (REL::Module::IsVR())
			defines.push_back({ "FRAMEBUFFER", nullptr });

		if (globals::features::terrainBlending.loaded)
			defines.push_back({ "TERRAIN_BLENDING", nullptr });

		return defines;
	}

	void BindGlobalConstantBuffersForPS(ID3D11DeviceContext* a_context)
	{
		if (!a_context)
			return;

		ID3D11Buffer* sharedBuffers[2] = {
			globals::state && globals::state->sharedDataCB ? globals::state->sharedDataCB->CB() : nullptr,
			globals::state && globals::state->featureDataCB ? globals::state->featureDataCB->CB() : nullptr,
		};
		a_context->PSSetConstantBuffers(5, ARRAYSIZE(sharedBuffers), sharedBuffers);

		ID3D11Buffer* frameBuffers[2]{};
		if (auto perFrame = globals::game::perFrame.get(); perFrame && *perFrame)
			frameBuffers[0] = *perFrame;
		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			if (auto vrValues = VRValues.get())
				frameBuffers[1] = *vrValues;
		}
		a_context->PSSetConstantBuffers(12, ARRAYSIZE(frameBuffers), frameBuffers);
	}

	struct DeferredCompositePSStateBackup
	{
		explicit DeferredCompositePSStateBackup(ID3D11DeviceContext* a_context) :
			context(a_context)
		{
			if (!context)
				return;

			context->IAGetInputLayout(&inputLayout);
			context->IAGetPrimitiveTopology(&topology);
			context->VSGetShader(&vertexShader, nullptr, nullptr);
			context->HSGetShader(&hullShader, nullptr, nullptr);
			context->DSGetShader(&domainShader, nullptr, nullptr);
			context->GSGetShader(&geometryShader, nullptr, nullptr);
			context->PSGetShader(&pixelShader, nullptr, nullptr);
			context->PSGetShaderResources(0, kDeferredCompositePSSRVCount, shaderResourceViews);
			context->PSGetSamplers(0, 1, samplers);
			context->PSGetConstantBuffers(5, 2, sharedConstantBuffers);
			context->PSGetConstantBuffers(12, 2, frameConstantBuffers);
			context->OMGetRenderTargets(kDeferredCompositeRTVCount, renderTargetViews, &depthStencilView);
			context->OMGetBlendState(&blendState, blendFactor, &sampleMask);
			context->OMGetDepthStencilState(&depthStencilState, &stencilRef);
			context->RSGetState(&rasterizerState);
			context->RSGetViewports(&viewportCount, viewports);
		}

		~DeferredCompositePSStateBackup()
		{
			Restore();
			Release();
		}

		DeferredCompositePSStateBackup(const DeferredCompositePSStateBackup&) = delete;
		DeferredCompositePSStateBackup& operator=(const DeferredCompositePSStateBackup&) = delete;

		void RestoreRasterState()
		{
			if (!context)
				return;

			context->RSSetState(rasterizerState);
			context->RSSetViewports(viewportCount, viewports);
		}

		void Restore()
		{
			if (!context || restored)
				return;

			context->OMSetRenderTargets(kDeferredCompositeRTVCount, renderTargetViews, depthStencilView);
			context->OMSetBlendState(blendState, blendFactor, sampleMask);
			context->OMSetDepthStencilState(depthStencilState, stencilRef);
			RestoreRasterState();
			context->IASetInputLayout(inputLayout);
			context->IASetPrimitiveTopology(topology);
			context->VSSetShader(vertexShader, nullptr, 0);
			context->HSSetShader(hullShader, nullptr, 0);
			context->DSSetShader(domainShader, nullptr, 0);
			context->GSSetShader(geometryShader, nullptr, 0);
			context->PSSetShader(pixelShader, nullptr, 0);
			context->PSSetShaderResources(0, kDeferredCompositePSSRVCount, shaderResourceViews);
			context->PSSetSamplers(0, 1, samplers);
			context->PSSetConstantBuffers(5, 2, sharedConstantBuffers);
			context->PSSetConstantBuffers(12, 2, frameConstantBuffers);
			restored = true;
		}

		void Release()
		{
			ReleaseCOM(inputLayout);
			ReleaseCOM(vertexShader);
			ReleaseCOM(hullShader);
			ReleaseCOM(domainShader);
			ReleaseCOM(geometryShader);
			ReleaseCOM(pixelShader);
			for (auto& srv : shaderResourceViews)
				ReleaseCOM(srv);
			for (auto& sampler : samplers)
				ReleaseCOM(sampler);
			for (auto& buffer : sharedConstantBuffers)
				ReleaseCOM(buffer);
			for (auto& buffer : frameConstantBuffers)
				ReleaseCOM(buffer);
			for (auto& rtv : renderTargetViews)
				ReleaseCOM(rtv);
			ReleaseCOM(depthStencilView);
			ReleaseCOM(blendState);
			ReleaseCOM(depthStencilState);
			ReleaseCOM(rasterizerState);
		}

		ID3D11DeviceContext* context = nullptr;
		ID3D11InputLayout* inputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11VertexShader* vertexShader = nullptr;
		ID3D11HullShader* hullShader = nullptr;
		ID3D11DomainShader* domainShader = nullptr;
		ID3D11GeometryShader* geometryShader = nullptr;
		ID3D11PixelShader* pixelShader = nullptr;
		ID3D11ShaderResourceView* shaderResourceViews[kDeferredCompositePSSRVCount]{};
		ID3D11SamplerState* samplers[1]{};
		ID3D11Buffer* sharedConstantBuffers[2]{};
		ID3D11Buffer* frameConstantBuffers[2]{};
		ID3D11RenderTargetView* renderTargetViews[kDeferredCompositeRTVCount]{};
		ID3D11DepthStencilView* depthStencilView = nullptr;
		ID3D11BlendState* blendState = nullptr;
		FLOAT blendFactor[4]{};
		UINT sampleMask = 0xffffffff;
		ID3D11DepthStencilState* depthStencilState = nullptr;
		UINT stencilRef = 0;
		ID3D11RasterizerState* rasterizerState = nullptr;
		UINT viewportCount = kDeferredCompositeViewportCount;
		D3D11_VIEWPORT viewports[kDeferredCompositeViewportCount]{};
		bool restored = false;
	};
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

	auto& data = renderer->GetRuntimeData().renderTargets[target];
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, &data.texture));

	if (texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
		DX::ThrowIfFailed(device->CreateShaderResourceView(data.texture, &srvDesc, &data.SRV));

	if (texDesc.BindFlags & D3D11_BIND_RENDER_TARGET)
		DX::ThrowIfFailed(device->CreateRenderTargetView(data.texture, &rtvDesc, &data.RTV));

	if (texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(data.texture, &uavDesc, &data.UAV));
}

void Deferred::SetupResources()
{
	auto renderer = globals::game::renderer;
	static ID3D11Device* shaderDevice = nullptr;
	if (shaderDevice != globals::d3d::device) {
		ReleaseCOM(mainCompositeCS);
		ReleaseCOM(mainCompositeInteriorCS);
		ReleaseCOM(mainCompositeMetadataCS);
		ReleaseCOM(mainCompositeMetadataInteriorCS);
		ReleaseCOM(mainCompositePS);
		ReleaseCOM(mainCompositeInteriorPS);
		ReleaseCOM(mainCompositeVS);
		ReleaseCOM(compositeColorBlendState);
		ReleaseCOM(compositeColorDepthStencilState);
		ReleaseCOM(compositeColorRasterizerState);
		delete deferredCompositeColorCopy;
		deferredCompositeColorCopy = nullptr;
		if (linearSampler) {
			linearSampler->Release();
			linearSampler = nullptr;
		}
		if (pointSampler) {
			pointSampler->Release();
			pointSampler = nullptr;
		}
		delete perShadow;
		perShadow = nullptr;
		if (copyShadowCS) {
			copyShadowCS->Release();
			copyShadowCS = nullptr;
		}
		shaderDevice = globals::d3d::device;
	}

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
		SetupRenderTarget(REFLECTANCE, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Normal + Roughness
		SetupRenderTarget(NORMALROUGHNESS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks
		SetupRenderTarget(MASKS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		// TAA Water Buffers
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_1, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	}

	{
		auto device = globals::d3d::device;

		if (!linearSampler || !pointSampler) {
			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.MinLOD = 0;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &linearSampler));

			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &pointSampler));
		}

		if (!compositeColorBlendState) {
			D3D11_BLEND_DESC blendDesc{};
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &compositeColorBlendState));
		}

		if (!compositeColorDepthStencilState) {
			D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
			depthStencilDesc.DepthEnable = false;
			depthStencilDesc.StencilEnable = false;
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, &compositeColorDepthStencilState));
		}

		if (!compositeColorRasterizerState) {
			D3D11_RASTERIZER_DESC rasterizerDesc{};
			rasterizerDesc.FillMode = D3D11_FILL_SOLID;
			rasterizerDesc.CullMode = D3D11_CULL_NONE;
			rasterizerDesc.FrontCounterClockwise = false;
			rasterizerDesc.DepthClipEnable = false;
			DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, &compositeColorRasterizerState));
		}
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
		if (!perShadow) {
			perShadow = new Buffer(sbDesc);
			srvDesc.Buffer.NumElements = numElements;
			perShadow->CreateSRV(srvDesc);
			uavDesc.Buffer.NumElements = numElements;
			perShadow->CreateUAV(uavDesc);
		}

		if (!copyShadowCS)
			copyShadowCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CopyShadowDataCS.hlsl", {}, "cs_5_0"));
	}

	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
	}
}

void Deferred::CopyShadowData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowData");

	auto context = globals::d3d::context;

	ID3D11UnorderedAccessView* uavs[1]{ perShadow->uav.get() };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11Buffer* buffers[3]{};
	context->PSGetConstantBuffers(0, 3, buffers);

	// Slot b12 contains the utility-shadow constants expected by CopyShadowDataCS.
	// Replace b1 with b12 and release the overwritten COM ref from PSGetConstantBuffers.
	ID3D11Buffer* utilityShadowBuffer = nullptr;
	context->PSGetConstantBuffers(12, 1, &utilityShadowBuffer);
	if (buffers[1]) {
		buffers[1]->Release();
	}
	buffers[1] = utilityShadowBuffer;

	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(copyShadowCS, nullptr, 0);

	context->Dispatch(1, 1, 1);

	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	// Release COM refs returned by PSGetConstantBuffers.
	for (auto& buffer : buffers) {
		if (buffer) {
			buffer->Release();
			buffer = nullptr;
		}
	}
	context->CSSetConstantBuffers(0, 3, buffers);

	context->CSSetShader(nullptr, nullptr, 0);

	{
		context->PSGetShaderResources(4, 1, &shadowView);

		ID3D11ShaderResourceView* srvs[2]{
			shadowView,
			perShadow->srv.get(),
		};

		context->PSSetShaderResources(18, ARRAYSIZE(srvs), srvs);

		// Release COM object to prevent memory leak
		if (shadowView)
			shadowView->Release();
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

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false, true);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

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
	if (!globals::state->inWorld)
		return;
	globals::state->UpdateSharedData(true, false);

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(setRenderTargetMode, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

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
		RE::RENDER_TARGET::kNONE
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

		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	PrepassPasses();

	OverrideBlendStates();
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	Util::BindGlobalConstantBuffersForCS(context);

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];

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
	bool ssgi_hq_spec = ssgi.IsSpecularGIActive();

	const bool submitStageSceneDomain = globals::features::upscaling.loaded && globals::features::upscaling.IsSubmitStageUpscalingActive();
	auto dispatchCount = Util::GetScreenDispatchCount(true, submitStageSceneDomain);

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

		// Compute features before this pass may bind their own private state.
		// Rebind global CS constants here so deferred composite never depends on
		// state left behind by SSGI/SSS/shadows or future feature passes.
		Util::BindGlobalConstantBuffersForCS(context);

		ID3D11ShaderResourceView* srvs[16]{
			specular.SRV,
			albedo.SRV,
			normalRoughness.SRV,
			masks.SRV,
			dynamicCubemaps.loaded || REL::Module::IsVR() ? Util::GetCurrentSceneDepthSRV(false) : nullptr,
			dynamicCubemaps.loaded ? reflectance.SRV : nullptr,
			dynamicCubemaps.loaded ? dynamicCubemaps.envTexture->srv.get() : nullptr,
			dynamicCubemaps.loaded ? dynamicCubemaps.envReflectionsTexture->srv.get() : nullptr,
			dynamicCubemaps.loaded && skylighting.loaded ? skylighting.texProbeArray->srv.get() : nullptr,
			nullptr,
			ssgi_ao,
			ssgi_hq_spec ? nullptr : ssgi_y,
			ssgi_hq_spec ? nullptr : ssgi_cocg,
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,
			ibl.IsRuntimeEnabled() && ibl.envIBLTexture ? ibl.envIBLTexture->srv.get() : nullptr,
			ibl.IsRuntimeEnabled() && ibl.skyIBLTexture ? ibl.skyIBLTexture->srv.get() : nullptr,
		};

		bool deferredCompositePSComplete = false;
		auto& upscaling = globals::features::upscaling;
		const bool deferredCompositePSRequested = upscaling.loaded && upscaling.IsDeferredCompositePSActive();
		if (!deferredCompositePSRequested && deferredCompositeColorCopy) {
			delete deferredCompositeColorCopy;
			deferredCompositeColorCopy = nullptr;
		}
		if (deferredCompositePSRequested) {
			auto* colorCopy = EnsureDeferredCompositeColorCopy(main.texture, main.SRV);
			auto metadataShader = interior ? GetComputeMainCompositeMetadataInterior() : GetComputeMainCompositeMetadata();
			auto pixelShader = interior ? GetPixelMainCompositeInterior() : GetPixelMainComposite();
			auto vertexShader = GetPixelMainCompositeVS();

			if (main.texture && main.RTV && colorCopy && colorCopy->resource && colorCopy->srv && normals.UAV && motionVectors.UAV &&
				metadataShader && pixelShader && vertexShader &&
				compositeColorBlendState && compositeColorDepthStencilState && compositeColorRasterizerState) {
				auto renderSize = Util::ConvertToDynamic(globals::state->screenSize, submitStageSceneDomain);
				UINT renderWidth = renderSize.x > 0.0f ? static_cast<UINT>(std::ceil(renderSize.x)) : 0;
				UINT renderHeight = renderSize.y > 0.0f ? static_cast<UINT>(std::ceil(renderSize.y)) : 0;

				D3D11_TEXTURE2D_DESC mainDesc{};
				main.texture->GetDesc(&mainDesc);
				renderWidth = std::min(renderWidth, mainDesc.Width);
				renderWidth = std::min(renderWidth, colorCopy->desc.Width);
				renderHeight = std::min(renderHeight, mainDesc.Height);
				renderHeight = std::min(renderHeight, colorCopy->desc.Height);

				if (renderWidth > 0 && renderHeight > 0 && main.texture != colorCopy->resource.get()) {
					DeferredCompositePSStateBackup psState(context);

					ID3D11ShaderResourceView* nullSRVs[kDeferredCompositePSSRVCount]{};
					context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
					context->PSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
					ID3D11UnorderedAccessView* nullUAVs[3]{};
					context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);
					context->OMSetRenderTargets(0, nullptr, nullptr);

					D3D11_BOX sourceBox{
						.left = 0,
						.top = 0,
						.front = 0,
						.right = renderWidth,
						.bottom = renderHeight,
						.back = 1,
					};
					context->CopySubresourceRegion(colorCopy->resource.get(), 0, 0, 0, 0, main.texture, 0, &sourceBox);

					if (dynamicCubemaps.loaded)
						context->CSSetSamplers(0, 1, &linearSampler);

					context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

					ID3D11UnorderedAccessView* metadataUAVs[3]{ nullptr, normals.UAV, motionVectors.UAV };
					context->CSSetUnorderedAccessViews(0, ARRAYSIZE(metadataUAVs), metadataUAVs, nullptr);
					context->CSSetShader(metadataShader, nullptr, 0);

					{
						TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Metadata Dispatch");
						CS_PROFILE_SCOPE("DeferredCompositeMetadata");
						context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
					}
					context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

					D3D11_VIEWPORT viewport{};
					viewport.TopLeftX = 0.0f;
					viewport.TopLeftY = 0.0f;
					viewport.Width = static_cast<float>(renderWidth);
					viewport.Height = static_cast<float>(renderHeight);
					viewport.MinDepth = 0.0f;
					viewport.MaxDepth = 1.0f;
					context->RSSetViewports(1, &viewport);

					context->IASetInputLayout(nullptr);
					context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context->VSSetShader(vertexShader, nullptr, 0);
					context->HSSetShader(nullptr, nullptr, 0);
					context->DSSetShader(nullptr, nullptr, 0);
					context->GSSetShader(nullptr, nullptr, 0);
					context->PSSetShader(pixelShader, nullptr, 0);
					context->RSSetState(compositeColorRasterizerState);
					context->OMSetBlendState(compositeColorBlendState, nullptr, 0xffffffff);
					context->OMSetDepthStencilState(compositeColorDepthStencilState, 0);

					ID3D11RenderTargetView* rtvs[1]{ main.RTV };
					context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

					const bool deferredCompositeGuarded = upscaling.GuardAAVRSRenderTarget();
					const bool deferredCompositeVrsAllowed = deferredCompositeGuarded && upscaling.ShouldUseAAVRSForDeferredComposite();
					Upscaling::ScopedAAVRSFullRateOverride aaVrsFullRateOverride(
						upscaling,
						globals::game::isVR && deferredCompositeGuarded && !deferredCompositeVrsAllowed);

					ID3D11ShaderResourceView* psSrvs[kDeferredCompositePSSRVCount]{
						srvs[0],
						srvs[1],
						srvs[2],
						srvs[3],
						srvs[4],
						srvs[5],
						srvs[6],
						srvs[7],
						srvs[8],
						srvs[9],
						srvs[10],
						srvs[11],
						srvs[12],
						srvs[13],
						srvs[14],
						srvs[15],
						colorCopy->srv.get(),
					};
					context->PSSetShaderResources(0, ARRAYSIZE(psSrvs), psSrvs);
					if (linearSampler)
						context->PSSetSamplers(0, 1, &linearSampler);
					BindGlobalConstantBuffersForPS(context);

					{
						TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Color Draw");
						CS_PROFILE_SCOPE("DeferredCompositePS");
						context->Draw(3, 0);
					}
					psState.RestoreRasterState();

					deferredCompositePSComplete = true;
				}
			}
		}

		if (!deferredCompositePSComplete) {
			if (dynamicCubemaps.loaded)
				context->CSSetSamplers(0, 1, &linearSampler);

			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			ID3D11UnorderedAccessView* uavs[3]{ main.UAV, normals.UAV, motionVectors.UAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			auto shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite();
			context->CSSetShader(shader, nullptr, 0);

			{
				TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Dispatch");
				CS_PROFILE_SCOPE("DeferredComposite");
				context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
			}
		}
	}

	if (globals::game::isVR && globals::features::vr.loaded) {
		CS_PROFILE_SCOPE("VR::StereoBlend");
		globals::features::vr.DrawStereoBlend();
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
	if (!globals::state->inWorld)
		return;

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(renderTargets, shadowState)
	GET_INSTANCE_MEMBER(stateUpdateFlags, shadowState)

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

void Deferred::ClearShaderCache()
{
	ReleaseCOM(mainCompositeCS);
	ReleaseCOM(mainCompositeInteriorCS);
	ReleaseCOM(mainCompositeMetadataCS);
	ReleaseCOM(mainCompositeMetadataInteriorCS);
	ReleaseCOM(mainCompositePS);
	ReleaseCOM(mainCompositeInteriorPS);
	ReleaseCOM(mainCompositeVS);
	ReleaseCOM(copyShadowCS);
	delete deferredCompositeColorCopy;
	deferredCompositeColorCopy = nullptr;
}

Texture2D* Deferred::EnsureDeferredCompositeColorCopy(ID3D11Texture2D* a_source, ID3D11ShaderResourceView* a_sourceSRV)
{
	if (!a_source || !a_sourceSRV)
		return nullptr;

	D3D11_TEXTURE2D_DESC sourceDesc{};
	a_source->GetDesc(&sourceDesc);
	if (sourceDesc.SampleDesc.Count != 1) {
		delete deferredCompositeColorCopy;
		deferredCompositeColorCopy = nullptr;
		return nullptr;
	}

	const bool recreate =
		!deferredCompositeColorCopy ||
		deferredCompositeColorCopy->desc.Width != sourceDesc.Width ||
		deferredCompositeColorCopy->desc.Height != sourceDesc.Height ||
		deferredCompositeColorCopy->desc.MipLevels != sourceDesc.MipLevels ||
		deferredCompositeColorCopy->desc.ArraySize != sourceDesc.ArraySize ||
		deferredCompositeColorCopy->desc.Format != sourceDesc.Format ||
		deferredCompositeColorCopy->desc.SampleDesc.Count != sourceDesc.SampleDesc.Count ||
		deferredCompositeColorCopy->desc.SampleDesc.Quality != sourceDesc.SampleDesc.Quality;

	if (recreate) {
		delete deferredCompositeColorCopy;
		deferredCompositeColorCopy = nullptr;

		D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		deferredCompositeColorCopy = new Texture2D(copyDesc, "Deferred::CompositeColorCopy");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		a_sourceSRV->GetDesc(&srvDesc);
		deferredCompositeColorCopy->CreateSRV(srvDesc);
	}

	return deferredCompositeColorCopy;
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	if (!mainCompositeCS) {
		logger::debug("Compiling DeferredCompositeCS");

		auto defines = BuildDeferredCompositeDefines(false, false);
		mainCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeInterior()
{
	if (!mainCompositeInteriorCS) {
		logger::debug("Compiling DeferredCompositeCS INTERIOR");

		auto defines = BuildDeferredCompositeDefines(true, false);
		mainCompositeInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeInteriorCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeMetadata()
{
	if (!mainCompositeMetadataCS) {
		logger::debug("Compiling DeferredCompositeCS METADATA");

		auto defines = BuildDeferredCompositeDefines(false, true);
		mainCompositeMetadataCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeMetadataCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeMetadataInterior()
{
	if (!mainCompositeMetadataInteriorCS) {
		logger::debug("Compiling DeferredCompositeCS INTERIOR METADATA");

		auto defines = BuildDeferredCompositeDefines(true, true);
		mainCompositeMetadataInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeMetadataInteriorCS;
}

ID3D11PixelShader* Deferred::GetPixelMainComposite()
{
	if (!mainCompositePS) {
		logger::debug("Compiling DeferredCompositePS");

		auto defines = BuildDeferredCompositeDefines(false, false);
		mainCompositePS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositePS.hlsl", defines, "ps_5_0"));
	}
	return mainCompositePS;
}

ID3D11PixelShader* Deferred::GetPixelMainCompositeInterior()
{
	if (!mainCompositeInteriorPS) {
		logger::debug("Compiling DeferredCompositePS INTERIOR");

		auto defines = BuildDeferredCompositeDefines(true, false);
		mainCompositeInteriorPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositePS.hlsl", defines, "ps_5_0"));
	}
	return mainCompositeInteriorPS;
}

ID3D11VertexShader* Deferred::GetPixelMainCompositeVS()
{
	if (!mainCompositeVS) {
		logger::debug("Compiling DeferredCompositeVS");

		mainCompositeVS = static_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\DeferredCompositeVS.hlsl", {}, "vs_5_0"));
	}
	return mainCompositeVS;
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
	state->lastWorldRenderFrame = state->frameCount;
	func(a1);
	state->lastCompletedWorldRenderFrame = state->frameCount;
	state->inWorld = false;
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
};

void Deferred::Hooks::Main_RenderWorld_Start::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
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

	// Deferred blended decals

	{
		const bool aaVrsFullRate = globals::features::upscaling.ShouldForceFullRateForAAVRSPhase(Upscaling::AAVRSPassPolicyReason::DecalPhase);
		Upscaling::ScopedAAVRSFullRateOverride aaVrsFullRateOverride(globals::features::upscaling, aaVrsFullRate);
		func(This, RenderFlags);
	}

	deferred->EndDeferred();

	// Copy depth from before water
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	context->CopyResource(depthCopy.texture, depth.texture);

	// After this point, water starts rendering
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
	Util::BindSharedDataConstantBuffersForCS(context);
}
