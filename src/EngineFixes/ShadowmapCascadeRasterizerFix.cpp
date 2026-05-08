#include "ShadowmapCascadeRasterizerFix.h"

void ShadowmapRasterizerFix::Install()
{
	// This function is called once per cascade to begin the updating and rendering process.
	stl::write_thunk_call<BSShadowDirectionalLight_RenderShadowmaps_RenderCascade>(REL::RelocationID(101495, 108489).address() + REL::Relocate(0xC6, 0xC6, 0xF6));

	gRasterStates = reinterpret_cast<RasterStateArray*>(REL::RelocationID(524748, 411363).address());

	auto configuredCascades = Util::GetGameSettingValue<std::int32_t>("iNumSplits:Display", Settings.at("iNumSplits:Display"));
	numCascades = static_cast<std::uint32_t>(std::clamp(configuredCascades, 1, static_cast<std::int32_t>(maxCascades)));
	currentCascade = 0;
	initialized = false;
}

void ShadowmapRasterizerFix::BSShadowDirectionalLight_RenderShadowmaps_RenderCascade::thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags)
{
	if (!gRasterStates) {
		func(light, arg1, arg2, flags);
		return;
	}

	if (!initialized)
		InitializeRasterStates();

	const auto cascade = currentCascade % numCascades;

	{
		std::memcpy(*gRasterStates, shadowmapRasterStates[cascade], sizeof(RasterStateArray));

		struct RestoreRasterStatesGuard
		{
			~RestoreRasterStatesGuard() { ShadowmapRasterizerFix::RestoreRasterStates(); }
		} restoreRasterStates;

		func(light, arg1, arg2, flags);
	}

	currentCascade = (cascade + 1) % numCascades;
}

void ShadowmapRasterizerFix::InitializeRasterStates()
{
	std::memcpy(backupGameRasterStates, *gRasterStates, sizeof(RasterStateArray));

	for (std::uint32_t cascade = 0; cascade < numCascades; cascade++)
		CloneRasterStates(backupGameRasterStates, cascade);

	initialized = true;
}

void ShadowmapRasterizerFix::GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor shadowmapDesc)
{
	outputDesc.DepthBias = shadowmapDesc.rasterDepthBias;
	outputDesc.DepthBiasClamp = shadowmapDesc.rasterDepthBiasClamp;
	outputDesc.SlopeScaledDepthBias = shadowmapDesc.rasterSlopeScaleBias;
}

void ShadowmapRasterizerFix::CloneRasterStates(const RasterStateArray& inputArray, std::uint32_t cascade)
{
	for (int fill = 0; fill < 2; fill++) {
		for (int cull = 0; cull < 3; cull++) {
			for (int depth = 0; depth < 12; depth++) {
				for (int scissor = 0; scissor < 2; scissor++) {
					if (auto* gRasterizer = inputArray[fill][cull][depth][scissor]) {
						D3D11_RASTERIZER_DESC desc{};
						gRasterizer->GetDesc(&desc);

						GetUpdatedRasterDesc(desc, cascadeDescriptors[cascade]);

						DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&desc, &shadowmapRasterStates[cascade][fill][cull][depth][scissor]));
					}
				}
			}
		}
	}
}

void ShadowmapRasterizerFix::RestoreRasterStates()
{
	if (gRasterStates)
		std::memcpy(*gRasterStates, backupGameRasterStates, sizeof(RasterStateArray));
}
