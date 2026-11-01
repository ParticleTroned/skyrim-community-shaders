#pragma once

#include "Utils/TextureMemoryPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace TextureStreamingPolicy
{
	constexpr std::uint64_t MiB = 1024ull * 1024;
	constexpr std::uint64_t MaximumPayloadBytes = 128 * MiB;
	constexpr std::uint64_t RestorationCacheBytes = 512 * MiB;
	constexpr std::uint64_t SystemMemoryReserveBytes = 512 * MiB;
	constexpr std::uint32_t MinimumEdge = 256;
	constexpr std::uint32_t MaximumDrop = 3;
	constexpr std::uint32_t MinimumResidentEdge = MinimumEdge >> MaximumDrop;
	constexpr double MovementHorizon = 512.0;
	constexpr std::uint64_t StableDemandMs = 3000;
	constexpr std::uint64_t HealthyWindowMs = 5000;
	constexpr std::uint64_t MaximumSampleAgeMs = 500;

	struct Eye
	{
		double renderWidth = 0, renderHeight = 0;
		double outputWidth = 0, outputHeight = 0;
		double projectionX = 0, projectionY = 0;
		double distanceToBound = 0;
	};

	/** Projected-bound texture demand; model-resolution input is independent. */
	inline double RequiredEdge(const std::array<Eye, 1>& eyes, unsigned eyeCount, double worldDiameter, double mipBias)
	{
		const auto invalid = std::numeric_limits<double>::infinity();
		if (!eyeCount || eyeCount > eyes.size() || !std::isfinite(worldDiameter) || worldDiameter <= 0 ||
			!std::isfinite(mipBias) || std::abs(mipBias) > 16)
			return invalid;
		double demand = 0;
		for (unsigned i = 0; i < eyeCount; ++i) {
			const auto& e = eyes[i];
			for (double value : { e.renderWidth, e.renderHeight, e.outputWidth, e.outputHeight, e.projectionX, e.projectionY, e.distanceToBound })
				if (!std::isfinite(value) || value <= 0)
					return invalid;
			const double scale = std::exp2(std::max(0.0, -mipBias));
			const double width = std::max(e.outputWidth, e.renderWidth * scale);
			const double height = std::max(e.outputHeight, e.renderHeight * scale);
			// One extra mip covers camera motion, trilinear filtering and rounding.
			demand = std::max(demand, std::max(width * e.projectionX, height * e.projectionY) * worldDiameter / e.distanceToBound);
		}
		return demand;
	}

	inline std::uint32_t DesiredDrop(std::uint32_t edge, std::uint32_t mips, double demand, std::uint32_t maximum, std::uint32_t shortestEdge = 0)
	{
		if (!shortestEdge)
			shortestEdge = edge;
		if (!std::isfinite(demand) || demand <= 0 || !edge || !mips)
			return 0;
		std::uint32_t drop = 0;
		while (drop < std::min(maximum, MaximumDrop) && drop + 1 < mips &&
			   (shortestEdge >> (drop + 1)) >= MinimumEdge && (edge >> (drop + 1)) >= demand)
			++drop;
		return drop;
	}

	/** A cap applies to eligible textures even nearby, preserving minimum mip dimensions. */
	inline std::uint32_t CappedDrop(std::uint32_t width, std::uint32_t height, std::uint32_t mips, std::uint32_t cap)
	{
		if (!cap)
			return 0;
		std::uint32_t drop = 0;
		while (drop < MaximumDrop && drop + 1 < mips && (std::max(width, height) >> drop) > cap &&
			   (std::min(width, height) >> (drop + 1)) >= MinimumResidentEdge)
			++drop;
		return drop;
	}

	enum class PressureChange
	{
		None,
		Entered,
		Recovered
	};

	struct Pressure
	{
		bool conserving = false;
		std::uint64_t healthySince = 0;
		std::uint64_t lastSampleMs = 0;
		/** Invalid or interrupted observations cannot authorize automatic refill. */
		PressureChange Update(std::uint64_t now, std::uint64_t sampledAt, bool valid, std::uint64_t budget, std::uint64_t usage,
			std::uint64_t reserve = Util::TextureMemoryPolicy::DefaultReserveMiB * MiB)
		{
			if (!valid || !budget || now < sampledAt || now - sampledAt > MaximumSampleAgeMs) {
				healthySince = 0;
				return PressureChange::None;
			}
			if (sampledAt < lastSampleMs || sampledAt - lastSampleMs > MaximumSampleAgeMs)
				healthySince = 0;
			lastSampleMs = sampledAt;
			const auto free = budget > usage ? budget - usage : 0;
			if (free <= reserve) {
				const bool entered = !conserving;
				conserving = true;
				healthySince = 0;
				return entered ? PressureChange::Entered : PressureChange::None;
			} else if (free >= reserve && free - reserve >= Util::TextureMemoryPolicy::RefillGapBytes) {
				if (!healthySince)
					healthySince = sampledAt;
				if (conserving && sampledAt >= healthySince && sampledAt - healthySince >= HealthyWindowMs) {
					conserving = false;
					return PressureChange::Recovered;
				}
			} else {
				healthySince = 0;
			}
			return PressureChange::None;
		}
	};
}
