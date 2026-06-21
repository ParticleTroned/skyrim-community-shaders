#include "ShadowmapCascadeRasterizerFix.h"

#include "Features/VR.h"
#include "Globals.h"
#include "Utils/D3D.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace
{
	std::unordered_map<ID3D11RasterizerState*, std::array<ID3D11RasterizerState*, ShadowmapRasterizerFix::maxCascades>> biasedRasterStateLookup;
	std::unordered_map<ID3D11RasterizerState*, ID3D11RasterizerState*> originalRasterStateLookup;

	bool MatchDescriptorCandidate(void* candidate, const RE::BSShadowLight::ShadowmapDescriptorVR& descriptor)
	{
		if (!candidate)
			return false;

		return candidate == std::addressof(descriptor) ||
		       candidate == descriptor.camera[0].get() ||
		       candidate == descriptor.camera[1].get() ||
		       candidate == descriptor.shaderAccumulator[0].get() ||
		       candidate == descriptor.shaderAccumulator[1].get() ||
		       candidate == descriptor.cullingProcess;
	}

	bool MatchDescriptorArguments(void* arg1, void* arg2, const RE::BSShadowLight::ShadowmapDescriptorVR& descriptor)
	{
		return MatchDescriptorCandidate(arg1, descriptor) || MatchDescriptorCandidate(arg2, descriptor);
	}

	std::uint32_t ResolveNativeCascadeIndex(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2)
	{
		if (!light)
			return ShadowmapRasterizerFix::invalidCascade;

		auto& runtimeData = light->GetVRRuntimeData();
		const auto descriptorCount = std::min<std::uint32_t>(
			static_cast<std::uint32_t>(runtimeData.shadowmapDescriptors.size()),
			std::min(light->shadowMapCount, ShadowmapRasterizerFix::maxCascades));

		for (std::uint32_t descriptorIndex = 0; descriptorIndex < descriptorCount; descriptorIndex++) {
			const auto& descriptor = runtimeData.shadowmapDescriptors[descriptorIndex];
			if (!MatchDescriptorArguments(arg1, arg2, descriptor))
				continue;

			if (descriptor.shadowmapIndex < ShadowmapRasterizerFix::maxCascades)
				return descriptor.shadowmapIndex;

			return descriptorIndex;
		}

		return ShadowmapRasterizerFix::invalidCascade;
	}

	template <class Fn>
	void ForEachRasterStateSlot(Fn&& fn)
	{
		for (int fill = 0; fill < ShadowmapRasterizerFix::kFill; fill++) {
			for (int cull = 0; cull < ShadowmapRasterizerFix::kCull; cull++) {
				for (int depth = 0; depth < ShadowmapRasterizerFix::depthDim; depth++) {
					for (int scissor = 0; scissor < ShadowmapRasterizerFix::kScissor; scissor++) {
						fn(ShadowmapRasterizerFix::StateIndex(fill, cull, depth, scissor));
					}
				}
			}
		}
	}

	void RestoreOriginalRasterState(ID3D11DeviceContext* context)
	{
		if (!context || !ShadowmapRasterizerFix::d3dHooksInstalled)
			return;

		ID3D11RasterizerState* currentState = nullptr;
		context->RSGetState(&currentState);
		if (!currentState)
			return;

		const auto iter = originalRasterStateLookup.find(currentState);
		if (iter != originalRasterStateLookup.end()) {
			ShadowmapRasterizerFix::ID3D11DeviceContext_RSSetState::func(context, iter->second);
		}

		currentState->Release();
	}
}

void ShadowmapRasterizerFix::Install()
{
	gRasterStates = reinterpret_cast<RasterStatePtr*>(REL::RelocationID(524748, 411363).address());
	depthDim = REL::Module::IsVR() ? kVRDepth : kFlatDepth;

	auto configuredCascades = Util::GetGameSettingValue<std::int32_t>("iNumSplits:Display", Settings.at("iNumSplits:Display"));
	numCascades = static_cast<std::uint32_t>(std::clamp(configuredCascades, 1, static_cast<std::int32_t>(maxCascades)));
	currentCascade = 0;
	activeCascade = invalidCascade;
	ReleaseClonedRasterStates();
	initialized = false;

	// This function is called once per cascade to begin the updating and rendering process.
	stl::write_thunk_call<BSShadowDirectionalLight_RenderShadowmaps_RenderCascade>(REL::RelocationID(101495, 108489).address() + REL::Relocate(0xC6, 0xC6, 0xF6));
}

void ShadowmapRasterizerFix::InstallD3DHooks(ID3D11DeviceContext* context)
{
	if (!REL::Module::IsVR())
		return;

	if (!IsVRCasterBiasEnabled())
		return;

	if (!context || d3dHooksInstalled)
		return;

	stl::detour_vfunc<43, ID3D11DeviceContext_RSSetState>(context);

	d3dHooksInstalled = true;
}

bool ShadowmapRasterizerFix::IsVRCasterBiasEnabled()
{
	return REL::Module::IsVR() &&
	       globals::features::vr.settings.EnableOuterCascadeCasterBias;
}

void ShadowmapRasterizerFix::BSShadowDirectionalLight_RenderShadowmaps_RenderCascade::thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags)
{
	if (!REL::Module::IsVR()) {
		if (!gRasterStates) {
			func(light, arg1, arg2, flags);
			return;
		}

		const auto cascade = currentCascade % numCascades;
		const auto bytes = static_cast<std::size_t>(StateCount()) * sizeof(RasterStatePtr);
		if (!initialized) {
			if (cascade == 0) {
				std::memcpy(backupGameRasterStates, gRasterStates, bytes);
				numCascades = std::min(numCascades, maxCascades);
			}

			CloneRasterStates(backupGameRasterStates, cascade, flatCascadeDescriptors);

			initialized = cascade == numCascades - 1;
		}

		std::memcpy(gRasterStates, shadowmapRasterStates[cascade], bytes);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_DEPTH_BIAS);

		func(light, arg1, arg2, flags);

		if (cascade == numCascades - 1) {
			std::memcpy(gRasterStates, backupGameRasterStates, bytes);
			globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_DEPTH_BIAS);
		}

		currentCascade = (cascade + 1) % numCascades;
		return;
	}

	if (!gRasterStates) {
		func(light, arg1, arg2, flags);
		return;
	}

	if (!IsVRCasterBiasEnabled()) {
		func(light, arg1, arg2, flags);
		return;
	}

	if (!initialized)
		InitializeRasterStates();

	const auto cascade = ResolveNativeCascadeIndex(light, arg1, arg2);
	if (cascade >= numCascades) {
		func(light, arg1, arg2, flags);
		return;
	}

	{
		ScopedCascadeBias scopedCascadeBias(cascade);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_DEPTH_BIAS);
		func(light, arg1, arg2, flags);
		RestoreOriginalRasterState(globals::d3d::context);
		globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RASTER_DEPTH_BIAS);
	}
}

void ShadowmapRasterizerFix::InitializeRasterStates()
{
	ReleaseClonedRasterStates();
	const auto bytes = static_cast<std::size_t>(StateCount()) * sizeof(RasterStatePtr);
	std::memcpy(backupGameRasterStates, gRasterStates, bytes);

	if (REL::Module::IsVR()) {
		for (std::uint32_t cascade = 0; cascade < numCascades; cascade++)
			CloneRasterStates(backupGameRasterStates, cascade, vrCascadeDescriptors);
		RebuildBiasedRasterStateLookup();
	}

	initialized = true;
}

int ShadowmapRasterizerFix::StateCount()
{
	return kFill * kCull * depthDim * kScissor;
}

int ShadowmapRasterizerFix::StateIndex(int fill, int cull, int depth, int scissor)
{
	return ((fill * kCull + cull) * depthDim + depth) * kScissor + scissor;
}

void ShadowmapRasterizerFix::GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor shadowmapDesc)
{
	outputDesc.DepthBias = shadowmapDesc.rasterDepthBias;
	outputDesc.DepthBiasClamp = shadowmapDesc.rasterDepthBiasClamp;
	outputDesc.SlopeScaledDepthBias = shadowmapDesc.rasterSlopeScaleBias;
}

void ShadowmapRasterizerFix::CloneRasterStates(RasterStatePtr* inputArray, std::uint32_t cascade, const std::array<ShadowMapRasterizerDescriptor, maxCascades>& descriptors)
{
	ForEachRasterStateSlot([&](int stateIndex) {
		auto*& clonedRaster = shadowmapRasterStates[cascade][stateIndex];
		if (clonedRaster) {
			clonedRaster->Release();
			clonedRaster = nullptr;
		}

		if (auto* gRasterizer = inputArray[stateIndex]) {
			D3D11_RASTERIZER_DESC desc{};
			gRasterizer->GetDesc(&desc);

			GetUpdatedRasterDesc(desc, descriptors[cascade]);

			if (const auto hr = globals::d3d::device->CreateRasterizerState(&desc, &clonedRaster); FAILED(hr)) {
				logger::warn("ShadowmapRasterizerFix: failed to clone cascade {} rasterizer state (hr=0x{:08X}); keeping engine state", cascade, static_cast<std::uint32_t>(hr));
				clonedRaster = gRasterizer;
				clonedRaster->AddRef();
			} else {
				Util::SetResourceName(clonedRaster, "ShadowmapCascadeRasterizerFix::CascadeBias[%u][%d]", cascade, stateIndex);
			}
		}
	});
}

void ShadowmapRasterizerFix::ReleaseClonedRasterStates()
{
	ForEachRasterStateSlot([&](int stateIndex) {
		for (std::uint32_t cascade = 0; cascade < maxCascades; cascade++) {
			auto*& clonedRaster = shadowmapRasterStates[cascade][stateIndex];
			if (clonedRaster) {
				clonedRaster->Release();
				clonedRaster = nullptr;
			}
		}
	});

	biasedRasterStateLookup.clear();
	originalRasterStateLookup.clear();
}

void ShadowmapRasterizerFix::RebuildBiasedRasterStateLookup()
{
	biasedRasterStateLookup.clear();
	originalRasterStateLookup.clear();

	if (!REL::Module::IsVR())
		return;

	ForEachRasterStateSlot([&](int stateIndex) {
		auto* gameRaster = backupGameRasterStates[stateIndex];
		if (!gameRaster)
			return;

		auto& cascadedStates = biasedRasterStateLookup[gameRaster];
		for (std::uint32_t cascade = 0; cascade < numCascades; cascade++) {
			auto* biasedRaster = shadowmapRasterStates[cascade][stateIndex];
			cascadedStates[cascade] = biasedRaster;
			if (biasedRaster)
				originalRasterStateLookup[biasedRaster] = gameRaster;
		}
	});
}

ID3D11RasterizerState* ShadowmapRasterizerFix::GetBiasedRasterState(ID3D11RasterizerState* state)
{
	if (!REL::Module::IsVR())
		return nullptr;

	if (!IsVRCasterBiasEnabled())
		return nullptr;

	if (!state || !initialized || activeCascade >= numCascades)
		return nullptr;

	const auto desc = vrCascadeDescriptors[activeCascade];
	if (desc.rasterDepthBias == 0 && desc.rasterDepthBiasClamp == 0.0f && desc.rasterSlopeScaleBias == 0.0f)
		return nullptr;

	const auto iter = biasedRasterStateLookup.find(state);
	if (iter == biasedRasterStateLookup.end())
		return nullptr;

	return iter->second[activeCascade];
}

ShadowmapRasterizerFix::ScopedCascadeBias::ScopedCascadeBias(std::uint32_t cascade) :
	previousCascade(activeCascade)
{
	activeCascade = cascade;
}

ShadowmapRasterizerFix::ScopedCascadeBias::~ScopedCascadeBias()
{
	activeCascade = previousCascade;
}

void ShadowmapRasterizerFix::ID3D11DeviceContext_RSSetState::thunk(ID3D11DeviceContext* context, ID3D11RasterizerState* state)
{
	if (auto* biasedState = GetBiasedRasterState(state)) {
		state = biasedState;
	}
	func(context, state);
}
