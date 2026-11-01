#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <array>
#	include <cmath>
#	include <cstddef>
#	include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kFaceDetailBins = 16;
	inline constexpr std::uint32_t kFaceDetailMaximumActors = 64;
	inline constexpr float kFaceDetailMinimumDensity = 0.5f;

	/** Normalized head bounds from one source frame; no actor pointers survive observation. */
	struct FaceDetailRegions
	{
		std::array<std::array<float, 4>, kFaceDetailMaximumActors> bounds{};
		std::uint32_t count = 0;
		bool current = false;
	};

	/** Positive separable CDF, packed identically to the optional shader constant block. */
	struct FaceDetailMapping
	{
		std::array<std::array<float, 4>, 8> knots{};
		std::uint32_t enabled = 0;
		std::array<std::uint32_t, 3> padding{};
		bool operator==(const FaceDetailMapping&) const = default;

		[[nodiscard]] float Knot(std::uint32_t axis, std::uint32_t i) const noexcept
		{
			return i == kFaceDetailBins ? 1.0f : knots[axis * 4 + i / 4][i % 4];
		}
		[[nodiscard]] float Forward(float value, std::uint32_t axis) const noexcept
		{
			if (!enabled)
				return value;
			const auto i = static_cast<std::uint32_t>(std::clamp(value * kFaceDetailBins, 0.0f, float(kFaceDetailBins - 1)));
			return std::lerp(Knot(axis, i), Knot(axis, i + 1), value * kFaceDetailBins - i);
		}
		[[nodiscard]] float Inverse(float value, std::uint32_t axis) const noexcept
		{
			if (!enabled)
				return value;
			std::uint32_t low = 0, high = kFaceDetailBins;
			while (low + 1 < high) {
				const auto middle = (low + high) / 2;
				if (Knot(axis, middle) <= value)
					low = middle;
				else
					high = middle;
			}
			return (low + (value - Knot(axis, low)) / (Knot(axis, low + 1) - Knot(axis, low))) / kFaceDetailBins;
		}
	};
	static_assert(sizeof(FaceDetailMapping) == 144);
	static_assert(offsetof(FaceDetailMapping, enabled) == 128);
}
#endif
