#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace FSRSharedGuidePolicy
{
	enum class Guide : std::size_t
	{
		Depth,
		MotionVectors,
		Reactive,
		Transparency,
		Count
	};
	inline constexpr auto kGuideCount = static_cast<std::size_t>(Guide::Count);

	enum class Route
	{
		Copy,
		Import,
		Direct
	};

	/** Selects a bounded import without replacing an owned, possibly in-flight generation. */
	[[nodiscard]] constexpr Route SelectRoute(bool a_enabled, bool a_eligible,
		bool a_hasRetainedSource, bool a_sameSource, bool a_importReady) noexcept
	{
		if (!a_enabled || !a_eligible)
			return Route::Copy;
		if (!a_hasRetainedSource)
			return Route::Import;
		return a_sameSource && a_importReady ? Route::Direct : Route::Copy;
	}

	struct CopyWork
	{
		uint64_t copyCalls = 0;
		uint64_t activePixels = 0;
		uint64_t avoidedPixels = 0;
		uint64_t directGuides = 0;
		uint64_t directGuidePixels = 0;
	};

	/** Accounts for color plus four guides; output copies are outside this input counter. */
	[[nodiscard]] constexpr CopyWork CountInputCopies(uint32_t a_renderWidth, uint32_t a_renderHeight,
		uint32_t a_allocationWidth, uint32_t a_allocationHeight, uint32_t a_directGuideCount) noexcept
	{
		if (!a_renderWidth || !a_renderHeight || a_renderWidth > a_allocationWidth ||
			a_renderHeight > a_allocationHeight || a_directGuideCount > kGuideCount)
			return {};
		const uint64_t activePixels = static_cast<uint64_t>(a_renderWidth) * a_renderHeight;
		const uint64_t allocationPixels = static_cast<uint64_t>(a_allocationWidth) * a_allocationHeight;
		if (allocationPixels > std::numeric_limits<uint64_t>::max() / (1u + kGuideCount))
			return {};
		const uint64_t copyCalls = 1u + kGuideCount - a_directGuideCount;
		return { copyCalls, activePixels * copyCalls,
			allocationPixels * (1u + kGuideCount) - activePixels * copyCalls,
			a_directGuideCount, activePixels * a_directGuideCount };
	}
}
