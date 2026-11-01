#pragma once

#include <dxgiformat.h>

namespace NeuralRendering
{
	inline constexpr DXGI_FORMAT kVertexAoFormat = DXGI_FORMAT_R16_UNORM;

	/** Preserve 16-bit vertex AO alongside the exact character category lane. */
	inline constexpr DXGI_FORMAT kCharacterCategoryFormat = DXGI_FORMAT_R16G16_UNORM;

	/** Capture and selection shaders require scalar floating-point or normalized depth. */
	[[nodiscard]] constexpr bool IsSupportedDepthViewFormat(DXGI_FORMAT format) noexcept
	{
		return format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
		       format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS ||
		       format == DXGI_FORMAT_R32_FLOAT || format == DXGI_FORMAT_R16_UNORM;
	}
}
