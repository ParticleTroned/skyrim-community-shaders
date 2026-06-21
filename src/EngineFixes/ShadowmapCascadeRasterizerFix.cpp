#include "ShadowmapCascadeRasterizerFix.h"

#include "Globals.h"
#include "Utils/D3D.h"

#include <algorithm>

namespace
{
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
}

void ShadowmapRasterizerFix::Install()
{
	gRasterStates = reinterpret_cast<RasterStatePtr*>(REL::RelocationID(524748, 411363).address());
	depthDim = REL::Module::IsVR() ? kVRDepth : kFlatDepth;

	auto configuredCascades = Util::GetGameSettingValue<std::int32_t>("iNumSplits:Display", Settings.at("iNumSplits:Display"));
	numCascades = static_cast<std::uint32_t>(std::clamp(configuredCascades, 1, static_cast<std::int32_t>(maxCascades)));
	currentCascade = 0;
	ReleaseClonedRasterStates();
	initialized = false;

	// This function is called once per cascade to begin the updating and rendering process.
	stl::write_thunk_call<BSShadowDirectionalLight_RenderShadowmaps_RenderCascade>(REL::RelocationID(101495, 108489).address() + REL::Relocate(0xC6, 0xC6, 0xF6));
}

void ShadowmapRasterizerFix::BSShadowDirectionalLight_RenderShadowmaps_RenderCascade::thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags)
{
	if (!gRasterStates) {
		func(light, arg1, arg2, flags);
		return;
	}

	const auto cascade = currentCascade % numCascades;
	const auto bytes = static_cast<std::size_t>(StateCount()) * sizeof(RasterStatePtr);

	if (!initialized) {
		if (cascade == 0) {
			std::memcpy(backupGameRasterStates, gRasterStates, bytes);
			numCascades = std::max<std::uint32_t>(1, std::min(numCascades, maxCascades));
		}

		CloneRasterStates(backupGameRasterStates, cascade);
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

void ShadowmapRasterizerFix::CloneRasterStates(RasterStatePtr* inputArray, std::uint32_t cascade)
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

			GetUpdatedRasterDesc(desc, cascadeDescriptors[cascade]);

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
}
