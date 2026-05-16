#include "ShadowmapCascadeRasterizerFix.h"

#include "Globals.h"

#include <unordered_map>

namespace
{
	std::unordered_map<ID3D11RasterizerState*, std::array<ID3D11RasterizerState*, ShadowmapRasterizerFix::maxCascades>> biasedRasterStateLookup;
	std::unordered_map<ID3D11RasterizerState*, ID3D11RasterizerState*> originalRasterStateLookup;

	template <class Fn>
	void ForEachRasterStateSlot(Fn&& fn)
	{
		for (int fill = 0; fill < 2; fill++) {
			for (int cull = 0; cull < 3; cull++) {
				for (int depth = 0; depth < 12; depth++) {
					for (int scissor = 0; scissor < 2; scissor++) {
						fn(fill, cull, depth, scissor);
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
	// This function is called once per cascade to begin the updating and rendering process.
	stl::write_thunk_call<BSShadowDirectionalLight_RenderShadowmaps_RenderCascade>(REL::RelocationID(101495, 108489).address() + REL::Relocate(0xC6, 0xC6, 0xF6));

	gRasterStates = reinterpret_cast<RasterStateArray*>(REL::RelocationID(524748, 411363).address());

	auto configuredCascades = Util::GetGameSettingValue<std::int32_t>("iNumSplits:Display", Settings.at("iNumSplits:Display"));
	numCascades = static_cast<std::uint32_t>(std::clamp(configuredCascades, 1, static_cast<std::int32_t>(maxCascades)));
	currentCascade = 0;
	activeCascade = invalidCascade;
	ReleaseClonedRasterStates();
	initialized = false;
}

void ShadowmapRasterizerFix::InstallD3DHooks(ID3D11DeviceContext* context)
{
	if (!REL::Module::IsVR())
		return;

	if constexpr (!vrCasterBiasEnabled)
		return;

	if (!context || d3dHooksInstalled)
		return;

	stl::detour_vfunc<43, ID3D11DeviceContext_RSSetState>(context);

	d3dHooksInstalled = true;
}

void ShadowmapRasterizerFix::BSShadowDirectionalLight_RenderShadowmaps_RenderCascade::thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags)
{
	if (!REL::Module::IsVR()) {
		if (!gRasterStates) {
			func(light, arg1, arg2, flags);
			return;
		}

		const auto cascade = currentCascade % numCascades;
		if (!initialized) {
			if (cascade == 0) {
				std::memcpy(backupGameRasterStates, *gRasterStates, sizeof(RasterStateArray));
				numCascades = std::min(numCascades, maxCascades);
			}

			CloneRasterStates(*gRasterStates, cascade, flatCascadeDescriptors);

			initialized = cascade == numCascades - 1;
		}

		std::memcpy(*gRasterStates, shadowmapRasterStates[cascade], sizeof(RasterStateArray));

		func(light, arg1, arg2, flags);

		if (cascade == numCascades - 1)
			std::memcpy(*gRasterStates, backupGameRasterStates, sizeof(RasterStateArray));

		currentCascade = (cascade + 1) % numCascades;
		return;
	}

	if (!gRasterStates) {
		func(light, arg1, arg2, flags);
		return;
	}

	if (!initialized)
		InitializeRasterStates();

	const auto cascade = currentCascade % numCascades;

	{
		ScopedCascadeBias scopedCascadeBias(cascade);
		func(light, arg1, arg2, flags);
		RestoreOriginalRasterState(globals::d3d::context);
	}

	currentCascade = (cascade + 1) % numCascades;
}

void ShadowmapRasterizerFix::InitializeRasterStates()
{
	ReleaseClonedRasterStates();
	std::memcpy(backupGameRasterStates, *gRasterStates, sizeof(RasterStateArray));

	if constexpr (vrCasterBiasEnabled) {
		for (std::uint32_t cascade = 0; cascade < numCascades; cascade++)
			CloneRasterStates(backupGameRasterStates, cascade, vrCascadeDescriptors);
		RebuildBiasedRasterStateLookup();
	}

	initialized = true;
}

void ShadowmapRasterizerFix::GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor shadowmapDesc)
{
	outputDesc.DepthBias = shadowmapDesc.rasterDepthBias;
	outputDesc.DepthBiasClamp = shadowmapDesc.rasterDepthBiasClamp;
	outputDesc.SlopeScaledDepthBias = shadowmapDesc.rasterSlopeScaleBias;
}

void ShadowmapRasterizerFix::CloneRasterStates(const RasterStateArray& inputArray, std::uint32_t cascade, const std::array<ShadowMapRasterizerDescriptor, maxCascades>& descriptors)
{
	ForEachRasterStateSlot([&](int fill, int cull, int depth, int scissor) {
		auto*& clonedRaster = shadowmapRasterStates[cascade][fill][cull][depth][scissor];
		if (clonedRaster) {
			clonedRaster->Release();
			clonedRaster = nullptr;
		}

		if (auto* gRasterizer = inputArray[fill][cull][depth][scissor]) {
			D3D11_RASTERIZER_DESC desc{};
			gRasterizer->GetDesc(&desc);

			GetUpdatedRasterDesc(desc, descriptors[cascade]);

			DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&desc, &clonedRaster));
		}
	});
}

void ShadowmapRasterizerFix::ReleaseClonedRasterStates()
{
	ForEachRasterStateSlot([&](int fill, int cull, int depth, int scissor) {
		for (std::uint32_t cascade = 0; cascade < maxCascades; cascade++) {
			auto*& clonedRaster = shadowmapRasterStates[cascade][fill][cull][depth][scissor];
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

	if constexpr (!vrCasterBiasEnabled)
		return;

	ForEachRasterStateSlot([&](int fill, int cull, int depth, int scissor) {
		auto* gameRaster = backupGameRasterStates[fill][cull][depth][scissor];
		if (!gameRaster)
			return;

		auto& cascadedStates = biasedRasterStateLookup[gameRaster];
		for (std::uint32_t cascade = 0; cascade < numCascades; cascade++) {
			auto* biasedRaster = shadowmapRasterStates[cascade][fill][cull][depth][scissor];
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

	if constexpr (!vrCasterBiasEnabled)
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
