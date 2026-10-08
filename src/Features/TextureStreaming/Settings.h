#pragma once

#include "Policy.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace StreamingTextures
{
	/** Reject fractional, negative and overflowing mip limits before conversion. */
	inline std::uint32_t ParseMaximumMipDrop(const nlohmann::json& value)
	{
		if (!value.is_number_integer() || value < 1 || value > TextureStreamingPolicy::MaximumDrop)
			throw std::invalid_argument("MaximumMipDrop must be an integer between 1 and 3");
		return value.get<std::uint32_t>();
	}
}
