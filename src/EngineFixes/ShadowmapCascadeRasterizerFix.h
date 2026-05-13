#pragma once

#include "EngineFix.h"
#include "Utils/GameSetting.h"

#include <limits>

// Applies shadowmap caster bias without mutating the shared rasterizer table at draw time.
struct ShadowmapRasterizerFix : EngineFix
{
	std::string GetName() override { return "Shadowmap Cascade Rasterizer Fix"; }
	void Install() override;

	using RasterStateArray = ID3D11RasterizerState* [2][3][12][2];

	static void InstallD3DHooks(ID3D11DeviceContext* context);
	static void InitializeRasterStates();
	static void CloneRasterStates(const RasterStateArray& inputArray, std::uint32_t cascade);
	static ID3D11RasterizerState* GetBiasedRasterState();

	static constexpr std::uint32_t maxCascades = 3;
	static constexpr std::uint32_t invalidCascade = std::numeric_limits<std::uint32_t>::max();
	static inline std::uint32_t numCascades = 0;
	static inline std::uint32_t currentCascade = 0;
	static inline std::uint32_t activeCascade = invalidCascade;
	static inline bool initialized = false;
	static inline bool d3dHooksInstalled = false;

	static inline RasterStateArray* gRasterStates = nullptr;
	static inline RasterStateArray backupGameRasterStates = {};
	static inline RasterStateArray shadowmapRasterStates[maxCascades] = {};

	// Keep close-range casters unshifted; even small first-cascade bias can make nearby meshes lose contact shadows.
	static constexpr int nearCascadeDepthBias = 0;
	static constexpr float nearCascadeDepthBiasClamp = 0.0f;
	static constexpr float nearCascadeSlopeScaleBias = 0.0f;

	// Keep a small scoped caster offset on outer cascades for the PR1690/1888 behavior without global table mutation.
	static constexpr int outerCascadeDepthBias = 32;
	static constexpr float outerCascadeDepthBiasClamp = 0.00075f;
	static constexpr float outerCascadeSlopeScaleBias = 0.35f;

	static constexpr bool casterBiasEnabled =
		nearCascadeDepthBias != 0 ||
		nearCascadeDepthBiasClamp != 0.0f ||
		nearCascadeSlopeScaleBias != 0.0f ||
		outerCascadeDepthBias != 0 ||
		outerCascadeDepthBiasClamp != 0.0f ||
		outerCascadeSlopeScaleBias != 0.0f;

	struct ShadowMapRasterizerDescriptor
	{
		int rasterDepthBias;
		float rasterDepthBiasClamp;
		float rasterSlopeScaleBias;
	};
	static void GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor desc);

	static constexpr ShadowMapRasterizerDescriptor cascadeDescriptors[maxCascades] = {
		{ nearCascadeDepthBias, nearCascadeDepthBiasClamp, nearCascadeSlopeScaleBias },
		{ outerCascadeDepthBias, outerCascadeDepthBiasClamp, outerCascadeSlopeScaleBias },
		{ outerCascadeDepthBias, outerCascadeDepthBiasClamp, outerCascadeSlopeScaleBias }
	};

	struct ScopedCascadeBias
	{
		explicit ScopedCascadeBias(std::uint32_t cascade);
		~ScopedCascadeBias();

		std::uint32_t previousCascade;
	};

	struct ScopedBiasedRasterState
	{
		explicit ScopedBiasedRasterState(ID3D11DeviceContext* context);
		~ScopedBiasedRasterState();

		ID3D11DeviceContext* context = nullptr;
		ID3D11RasterizerState* previousState = nullptr;
		bool applied = false;
	};

	struct BSShadowDirectionalLight_RenderShadowmaps_RenderCascade
	{
		static void thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* context, UINT indexCount, UINT startIndexLocation, INT baseVertexLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertexLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(
			ID3D11DeviceContext* context,
			UINT indexCountPerInstance,
			UINT instanceCount,
			UINT startIndexLocation,
			INT baseVertexLocation,
			UINT startInstanceLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawInstanced
	{
		static void thunk(
			ID3D11DeviceContext* context,
			UINT vertexCountPerInstance,
			UINT instanceCount,
			UINT startVertexLocation,
			UINT startInstanceLocation);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	std::map<std::string, Util::GameSetting> Settings{
		{ "iNumSplits:Display", { "Number of Shadow Map Cascades (INI) ",
									"Controls the number of shadow map cascades used for directional lighting. "
									"Higher values provide better shadow quality but use more GPU resources. "
									"Maximum of 3 cascades supported. ",
									REL::Relocate<uintptr_t>(0, 0, 0x1ed6350), 2, 1, 3 } },
	};
};
