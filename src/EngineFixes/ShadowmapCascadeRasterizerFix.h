#pragma once

#include "EngineFix.h"
#include "Utils/GameSetting.h"

// Overrides the shadow cascade rasterizers to fix peter-panning and self-shadowing.
struct ShadowmapRasterizerFix : EngineFix
{
	std::string GetName() override { return "Shadowmap Cascade Rasterizer Fix"; }
	void Install() override;

	static constexpr int kFill = 2;
	static constexpr int kCull = 3;
	static constexpr int kFlatDepth = 12;
	static constexpr int kVRDepth = 13;
	static constexpr int kScissor = 2;
	static constexpr int kMaxStates = kFill * kCull * kVRDepth * kScissor;
	using RasterStatePtr = ID3D11RasterizerState*;

	static int StateCount();
	static int StateIndex(int fill, int cull, int depth, int scissor);
	static void CloneRasterStates(RasterStatePtr* inputArray, std::uint32_t cascade);
	static void ReleaseClonedRasterStates();

	static constexpr std::uint32_t maxCascades = 3;
	static inline int depthDim = kFlatDepth;
	static inline std::uint32_t numCascades = 1;
	static inline std::uint32_t currentCascade = 0;
	static inline bool initialized = false;

	static inline RasterStatePtr* gRasterStates = nullptr;
	static inline RasterStatePtr backupGameRasterStates[kMaxStates] = {};
	static inline RasterStatePtr shadowmapRasterStates[maxCascades][kMaxStates] = {};

	struct ShadowMapRasterizerDescriptor
	{
		int rasterDepthBias;
		float rasterDepthBiasClamp;
		float rasterSlopeScaleBias;
	};

	static void GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor desc);

	static constexpr int firstCascadeDepthBias = 160;
	static constexpr float firstCascadeDepthBiasClamp = 0.015f;
	static constexpr float firstCascadeSlopeScaleBias = 3.2f;

	static constexpr int secondCascadeDepthBias = 100;
	static constexpr float secondCascadeDepthBiasClamp = 0.015f;
	static constexpr float secondCascadeSlopeScaleBias = 3.8f;

	static constexpr int thirdCascadeDepthBias = 100;
	static constexpr float thirdCascadeDepthBiasClamp = 0.015f;
	static constexpr float thirdCascadeSlopeScaleBias = 3.8f;

	static constexpr ShadowMapRasterizerDescriptor cascadeDescriptors[maxCascades] = {
		{ firstCascadeDepthBias, firstCascadeDepthBiasClamp, firstCascadeSlopeScaleBias },
		{ secondCascadeDepthBias, secondCascadeDepthBiasClamp, secondCascadeSlopeScaleBias },
		{ thirdCascadeDepthBias, thirdCascadeDepthBiasClamp, thirdCascadeSlopeScaleBias }
	};

	struct BSShadowDirectionalLight_RenderShadowmaps_RenderCascade
	{
		static void thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, std::uint32_t flags);
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
