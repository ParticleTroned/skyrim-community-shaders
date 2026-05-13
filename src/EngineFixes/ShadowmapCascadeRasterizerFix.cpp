#include "ShadowmapCascadeRasterizerFix.h"

#include "Globals.h"
#include "Utils/Game.h"

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

void ShadowmapRasterizerFix::InstallD3DHooks(ID3D11DeviceContext* context)
{
	if (!context || d3dHooksInstalled)
		return;

	stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(context);
	stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(context);
	stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(context);
	stl::detour_vfunc<21, ID3D11DeviceContext_DrawInstanced>(context);

	d3dHooksInstalled = true;
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
		ScopedCascadeBias scopedCascadeBias(cascade);
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

ID3D11RasterizerState* ShadowmapRasterizerFix::GetBiasedRasterState()
{
	if (!initialized || activeCascade >= numCascades || !globals::game::shadowState)
		return nullptr;

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(rasterStateFillMode, shadowState)
	GET_INSTANCE_MEMBER(rasterStateCullMode, shadowState)
	GET_INSTANCE_MEMBER(rasterStateDepthBiasMode, shadowState)
	GET_INSTANCE_MEMBER(rasterStateScissorMode, shadowState)

	if (rasterStateFillMode >= 2 || rasterStateCullMode >= 3 || rasterStateDepthBiasMode >= 12 || rasterStateScissorMode >= 2)
		return nullptr;

	return shadowmapRasterStates[activeCascade][rasterStateFillMode][rasterStateCullMode][rasterStateDepthBiasMode][rasterStateScissorMode];
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

ShadowmapRasterizerFix::ScopedBiasedRasterState::ScopedBiasedRasterState(ID3D11DeviceContext* a_context) :
	context(a_context)
{
	auto* biasedState = GetBiasedRasterState();
	if (!context || !biasedState)
		return;

	context->RSGetState(&previousState);
	if (previousState != biasedState) {
		context->RSSetState(biasedState);
		applied = true;
	}
}

ShadowmapRasterizerFix::ScopedBiasedRasterState::~ScopedBiasedRasterState()
{
	if (applied)
		context->RSSetState(previousState);

	if (previousState)
		previousState->Release();
}

void ShadowmapRasterizerFix::ID3D11DeviceContext_DrawIndexed::thunk(ID3D11DeviceContext* context, UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
{
	ScopedBiasedRasterState scopedRasterState(context);
	func(context, indexCount, startIndexLocation, baseVertexLocation);
}

void ShadowmapRasterizerFix::ID3D11DeviceContext_Draw::thunk(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertexLocation)
{
	ScopedBiasedRasterState scopedRasterState(context);
	func(context, vertexCount, startVertexLocation);
}

void ShadowmapRasterizerFix::ID3D11DeviceContext_DrawIndexedInstanced::thunk(
	ID3D11DeviceContext* context,
	UINT indexCountPerInstance,
	UINT instanceCount,
	UINT startIndexLocation,
	INT baseVertexLocation,
	UINT startInstanceLocation)
{
	ScopedBiasedRasterState scopedRasterState(context);
	func(context, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
}

void ShadowmapRasterizerFix::ID3D11DeviceContext_DrawInstanced::thunk(
	ID3D11DeviceContext* context,
	UINT vertexCountPerInstance,
	UINT instanceCount,
	UINT startVertexLocation,
	UINT startInstanceLocation)
{
	ScopedBiasedRasterState scopedRasterState(context);
	func(context, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
}
