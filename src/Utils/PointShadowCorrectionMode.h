#pragma once

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace PointShadowCorrection
{
	enum class Mode : std::uint32_t
	{
		Off = 0,
		ClipOnly = 1
	};

	inline void Validate(Mode mode)
	{
		if (mode != Mode::Off && mode != Mode::ClipOnly)
			throw std::invalid_argument("PointShadowCorrection must be 0 or 1");
	}

	inline std::string_view Name(Mode mode)
	{
		switch (mode) {
		case Mode::Off:
			return "off";
		case Mode::ClipOnly:
			return "clip_only";
		}
		throw std::invalid_argument("Invalid point-shadow correction mode");
	}

	inline Mode Parse(std::string_view name)
	{
		for (const auto mode : { Mode::Off, Mode::ClipOnly })
			if (name == Name(mode))
				return mode;
		throw std::invalid_argument("pointShadowCorrection.mode must be off or clip_only");
	}

}
