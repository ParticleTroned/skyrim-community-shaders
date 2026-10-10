#pragma once

#include "Categories.h"
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
	/** Saved settings and DevBench use the same strict optional-category boundary. */
	inline Categories ParseCategories(const nlohmann::json& value)
	{
		if (!value.is_object())
			throw std::invalid_argument("Texture categories must be an object");
		Categories result;
		for (const auto& [name, enabled] : value.items()) {
			if (!enabled.is_boolean())
				throw std::invalid_argument("Texture category values must be boolean");
			if (name == "landscapeStatics")
				result.landscapeStatics = enabled.get<bool>();
			else if (name == "alphaTestedStatics")
				result.alphaTestedStatics = enabled.get<bool>();
			else if (name == "emissiveStatics")
				result.emissiveStatics = enabled.get<bool>();
			else
				throw std::invalid_argument("Unknown texture category: " + name);
		}
		return result;
	}
	inline nlohmann::json SerializeCategories(const Categories& value)
	{
		return { { "landscapeStatics", value.landscapeStatics }, { "alphaTestedStatics", value.alphaTestedStatics },
			{ "emissiveStatics", value.emissiveStatics } };
	}
}
