#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace NeuralRendering
{
	/** Selects character-region planning independently of NR placement A, B or C. */
	enum class RoiExecutionMode : std::uint32_t
	{
		AutomaticSingle = 0,
		Independent = 1,
		Batched = 2,
	};

	[[nodiscard]] constexpr RoiExecutionMode ClampRoiExecutionMode(std::uint32_t value) noexcept
	{
		return value <= static_cast<std::uint32_t>(RoiExecutionMode::Batched) ?
		           static_cast<RoiExecutionMode>(value) :
		           RoiExecutionMode::AutomaticSingle;
	}

	/** Legacy session overrides retain independent execution without replacing a saved choice. */
	[[nodiscard]] constexpr RoiExecutionMode ResolveRoiExecutionMode(std::uint32_t value, bool legacyMultiRoi) noexcept
	{
		const auto mode = ClampRoiExecutionMode(value);
		return mode == RoiExecutionMode::AutomaticSingle && legacyMultiRoi ? RoiExecutionMode::Independent : mode;
	}

	[[nodiscard]] constexpr std::string_view RoiExecutionModeName(RoiExecutionMode mode) noexcept
	{
		switch (mode) {
		case RoiExecutionMode::Independent:
			return "independent";
		case RoiExecutionMode::Batched:
			return "batched";
		default:
			return "automatic_single";
		}
	}

	[[nodiscard]] constexpr std::optional<RoiExecutionMode> ParseRoiExecutionMode(std::string_view name) noexcept
	{
		for (std::uint32_t value = 0; value <= static_cast<std::uint32_t>(RoiExecutionMode::Batched); ++value) {
			const auto mode = static_cast<RoiExecutionMode>(value);
			if (RoiExecutionModeName(mode) == name)
				return mode;
		}
		return std::nullopt;
	}
}
