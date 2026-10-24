#pragma once

// Analytic mapping adapted from Yuri Grib (BeliyG3), optimizer-fps-dlss5, MIT.
// See package/SKSE/Plugins/CommunityShaders/Licenses/NeuralRendering/PeripheralCompression.txt.
#include <array>
#include <cmath>
#include <cstdint>

namespace NeuralRendering
{
	/** Normalized, symmetric source/work mapping; identical coefficients describe colour and guides. */
	struct PeripheralMapping
	{
		static constexpr std::uint32_t kVersion = 1;
		std::array<float, 2> centre{ 1.0f, 1.0f }, work{ 1.0f, 1.0f };
		std::array<float, 2> compression{ 1.0f, 1.0f }, edgeSlope{ 1.0f, 1.0f };
		bool operator==(const PeripheralMapping&) const = default;

		/** Extend endpoints monotonically beyond the image; only texture reads are clamped. */
		[[nodiscard]] float Forward(float source, std::uint32_t axis) const noexcept
		{
			const float w = work[axis], c = centre[axis], a = compression[axis];
			if (w == 1.0f)
				return source;
			if (source < 0.0f)
				return source * (edgeSlope[axis] / w);
			if (source > 1.0f)
				return 1.0f + (source - 1.0f) * (edgeSlope[axis] / w);
			const float delta = source * 2.0f - 1.0f;
			float radius = std::abs(delta);
			if (radius > c) {
				const float t = (radius - c) / (1.0f - c);
				radius = c + (w - c) * t / (a + (1.0f - a) * t);
			}
			return 0.5f + std::copysign(radius / (2.0f * w), delta);
		}

		/** Analytic inverse uses the same pixel-centre convention as Forward. */
		[[nodiscard]] float Inverse(float target, std::uint32_t axis) const noexcept
		{
			const float w = work[axis], c = centre[axis], a = compression[axis];
			if (w == 1.0f)
				return target;
			if (target < 0.0f)
				return target * (w / edgeSlope[axis]);
			if (target > 1.0f)
				return 1.0f + (target - 1.0f) * (w / edgeSlope[axis]);
			const float delta = (target * 2.0f - 1.0f) * w;
			float radius = std::abs(delta);
			if (radius > c) {
				const float y = (radius - c) / (w - c);
				const float t = a * y / (1.0f - (1.0f - a) * y);
				radius = c + (1.0f - c) * t;
			}
			return 0.5f + std::copysign(radius * 0.5f, delta);
		}
	};
	static_assert(sizeof(PeripheralMapping) == 32);
}
