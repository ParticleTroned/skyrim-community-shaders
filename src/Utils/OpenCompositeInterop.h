#pragma once

#include <cstdint>

namespace Util
{
	struct OCUExternalUpscalerState
	{
		float mipBias = 0.0f;
		float renderScale = 1.0f;
		uint32_t method = 0;
		uint32_t flags = 0;
	};

	bool TryReadOCUExternalUpscalerState(OCUExternalUpscalerState& o_state);
}
