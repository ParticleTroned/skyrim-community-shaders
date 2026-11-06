#pragma once

#include "Utils/Finite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace SeasonalWeatherPolicy
{
	constexpr size_t MonthCount = 12;
	constexpr size_t CategoryCount = 4;
	constexpr size_t MaxCandidates = 128;
	constexpr size_t MaxAssignments = 128;
	constexpr float MaxMultiplier = 20.0f;
	constexpr float MinAssignmentWeight = 0.01f;
	constexpr float MaxAssignmentWeight = 100.0f;
	constexpr uint32_t ChanceTotal = 100;
	using MonthlyWeights = std::array<std::array<float, CategoryCount>, MonthCount>;

	enum class Category : uint8_t
	{
		Clear,
		Cloudy,
		Rain,
		Snow,
		Unchanged
	};

	/** Monthly relative weights in clear, cloudy, rain, snow order. */
	inline constexpr MonthlyWeights DefaultWeights{ { { 0.7f, 1.0f, 0.25f, 4.0f }, { 0.8f, 1.0f, 0.5f, 3.0f },
		{ 1.0f, 1.0f, 1.5f, 1.0f }, { 1.0f, 1.0f, 1.5f, 0.5f },
		{ 1.2f, 1.0f, 1.0f, 0.25f }, { 1.5f, 0.8f, 0.8f, 0.1f },
		{ 1.5f, 0.8f, 0.8f, 0.1f }, { 1.3f, 1.0f, 1.0f, 0.25f },
		{ 1.0f, 1.2f, 1.3f, 0.5f }, { 0.9f, 1.3f, 1.2f, 1.0f },
		{ 0.8f, 1.2f, 0.7f, 2.5f }, { 0.7f, 1.0f, 0.25f, 4.0f } } };

	/** Smoothly wraps the game calendar; invalid clocks return a neutral multiplier. */
	inline float Multiplier(const MonthlyWeights& weights, float month, Category category)
	{
		if (!Util::IsFinite(month))
			return 1.0f;
		const auto index = static_cast<size_t>(category);
		if (index >= CategoryCount)
			return 1.0f;
		month = std::fmod(month, static_cast<float>(MonthCount));
		if (month < 0)
			month += static_cast<float>(MonthCount);
		// Adding a tiny negative remainder to twelve can round up to twelve.
		if (month >= static_cast<float>(MonthCount))
			month = 0;
		const auto first = static_cast<size_t>(month);
		float blend = month - static_cast<float>(first);
		blend = blend * blend * (3.0f - 2.0f * blend);
		return std::lerp(weights[first][index], weights[(first + 1) % MonthCount][index], blend);
	}

	/** Converts relative weights to native percentages, preserving zero exclusions. */
	inline std::vector<uint32_t> Percentages(std::span<const double> weights)
	{
		if (weights.empty() || weights.size() > MaxCandidates)
			return {};
		double total = 0;
		for (const auto weight : weights) {
			if (!Util::IsFinite(weight) || weight < 0)
				return {};
			total += weight;
		}
		if (!Util::IsFinite(total) || total <= 0)
			return {};
		std::vector<uint32_t> result(weights.size());
		std::vector<double> remainder(weights.size());
		uint32_t allocated = 0;
		for (size_t i = 0; i < weights.size(); ++i) {
			const auto exact = weights[i] / total * ChanceTotal;
			result[i] = static_cast<uint32_t>(std::floor(exact));
			remainder[i] = weights[i] > 0 ? exact - result[i] : -1;
			allocated += result[i];
		}
		while (allocated < ChanceTotal) {
			const auto best = std::max_element(remainder.begin(), remainder.end());
			++result[static_cast<size_t>(best - remainder.begin())];
			*best = -1;
			++allocated;
		}
		return result;
	}
}
