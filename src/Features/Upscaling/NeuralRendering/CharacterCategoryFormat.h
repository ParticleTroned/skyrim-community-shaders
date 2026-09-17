#pragma once

#include <dxgiformat.h>

namespace NeuralRendering
{
	/** Preserve 16-bit vertex AO alongside the exact character category lane. */
	inline constexpr DXGI_FORMAT kCharacterCategoryFormat = DXGI_FORMAT_R16G16_UNORM;
}
