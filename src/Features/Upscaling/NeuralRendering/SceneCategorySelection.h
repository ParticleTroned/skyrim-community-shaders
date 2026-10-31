#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace NeuralRendering
{
	/** Pixel-aligned scene strengths consumed after model reconstruction. */
	struct SceneCategorySelectionConstants
	{
		std::array<std::uint32_t, 4> sourceMapping{}, authoredRegion{};
		std::array<float, 4> reductions{}, options{}, depthLinearization{};
		bool operator==(const SceneCategorySelectionConstants&) const = default;
	};
	static_assert(sizeof(SceneCategorySelectionConstants) == 80);

	/** Retains the exact source-frame category/depth views until reconstruction. */
	struct SceneCategorySelection
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> categories, authoredDepth, currentDepth;
		SceneCategorySelectionConstants constants{};

		bool operator==(const SceneCategorySelection& other) const noexcept
		{
			return categories.Get() == other.categories.Get() && authoredDepth.Get() == other.authoredDepth.Get() &&
			       currentDepth.Get() == other.currentDepth.Get() && constants == other.constants;
		}
	};
}
