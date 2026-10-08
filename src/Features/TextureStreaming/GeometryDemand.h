#pragma once
#include "Policy.h"
#include <array>
#include <cstdint>

namespace RE
{
	class BSGeometry;
	class BSLightingShaderMaterialBase;
}
namespace StreamingTextures
{
	/** Unknown, animated and special-sampling materials are ineligible. */
	RE::BSLightingShaderMaterialBase* StaticMaterial(RE::BSGeometry* geometry);
	/** Bound the largest world-space length per UV unit over every triangle. */
	double UnitsPerUV(RE::BSGeometry* geometry, RE::BSLightingShaderMaterialBase* material);
	struct DemandContext
	{
		std::array<TextureStreamingPolicy::Eye, 2> eyes{};
		std::array<std::array<float, 3>, 2> positions{}, forward{};
		unsigned eyeCount = 0;
		double mipBias = 0;
		std::uint32_t frame = UINT32_MAX;
		std::uint64_t sampledAtMs = 0;
	};
	/** Resolve both eyes against the committed renderer plan and effective shader bias. */
	double RequiredEdge(RE::BSGeometry* geometry, double unitsPerUV, const DemandContext& demand);
}
