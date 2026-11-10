#pragma once

namespace GrassCollisionMode
{
	enum class Mode
	{
		Disabled,
		Basic,
		Advanced
	};

	/** @brief Stable names for runtime diagnostics. */
	constexpr const char* Name(Mode mode)
	{
		switch (mode) {
		case Mode::Basic:
			return "basic";
		case Mode::Advanced:
			return "advanced";
		default:
			return "disabled";
		}
	}

	/** @brief Select a backend without changing it during a frame or losing outgoing motion history. */
	constexpr Mode Select(Mode requested, Mode resident, bool currentDeformation, bool updatedThisFrame)
	{
		if (resident != Mode::Disabled && (updatedThisFrame || (resident != requested && currentDeformation)))
			return resident;
		return requested;
	}
}
