#pragma once
#include "Categories.h"
#include "MaterialDiagnostics.h"
#include "Policy.h"
#include <array>
#include <cstdint>

namespace RE
{
	class BSGeometry;
	class BSLightingShaderMaterialBase;
	class NiSourceTexture;
}
namespace StreamingTextures
{
	/** Ordinary diffuse/normal slots, including native and registered PBR terrain layers; auxiliary maps stay protected. */
	bool IsStreamingSlot(const RE::BSLightingShaderMaterialBase* material, const RE::NiSourceTexture* texture);
	/** Classify supported static materials and required consent bits; unknown and animated sampling stays protected. */
	RE::BSLightingShaderMaterialBase* StaticMaterial(RE::BSGeometry* geometry, std::uint32_t* categories = nullptr
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		MaterialInspection* inspection = nullptr
#endif
	);
	struct DemandContext
	{
		static constexpr std::uint64_t MaximumAgeMs = 250;
		std::array<TextureStreamingPolicy::Eye, 2> eyes{};
		std::array<std::array<float, 3>, 2> positions{}, forward{};
		unsigned eyeCount = 0;
		double mipBias = 0;
		std::uint32_t frame = UINT32_MAX;
		std::uint64_t sampledAtMs = 0;
		/** Unknown eyes and stale snapshots cannot establish current texture demand. */
		[[nodiscard]] bool Fresh(std::uint32_t currentFrame, std::uint64_t now) const noexcept
		{
			return eyeCount && eyeCount <= eyes.size() && frame != UINT32_MAX && currentFrame - frame <= 1 &&
			       now >= sampledAtMs && now - sampledAtMs <= MaximumAgeMs;
		}
	};
	/** Estimate texture demand from projected world bounds, both eyes and effective shader bias. */
	double RequiredEdge(RE::BSGeometry* geometry, const DemandContext& demand);
}
