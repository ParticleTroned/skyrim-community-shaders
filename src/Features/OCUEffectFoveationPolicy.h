#pragma once

#include "OCUEffectFoveationAPI.h"
#include <cmath>
#include <cstdint>

namespace OCUEffectFoveation
{

	// Matches Common/OCUEffectFoveation.hlsli b10. Zero is native quality.
	struct Constants
	{
		float centers[4]{};
		float policy[4]{};  // inner radius, mid radius, strength, eye-tracked
		bool Active() const noexcept { return policy[2] > 0.0f; }
	};
	static_assert(sizeof(Constants) == 32);
	static_assert(offsetof(Constants, policy) == 16);

	// Provider and renderer frame IDs have different domains. Only compare a
	// provider publication against previously consumed provider publications.
	class FreshnessPolicy
	{
	public:
		static constexpr double MaxPublicationAgeSeconds = 0.100;

		Constants Consume(const ocu_effect_foveation::Snapshot& a_snapshot,
			std::int64_t a_nowQpc, std::int64_t a_localQpcFrequency) noexcept
		{
			using namespace ocu_effect_foveation;
			if (a_snapshot.structSize != sizeof(Snapshot) || a_snapshot.version != Version ||
				a_snapshot.shape != Shape::UVRadialHalfExtent ||
				(a_snapshot.mode != Mode::Fixed && a_snapshot.mode != Mode::EyeTracked) ||
				a_snapshot.frameId == 0 || a_snapshot.frameId <= lastConsumedFrame ||
				a_localQpcFrequency <= 0 || a_snapshot.qpcFrequency != a_localQpcFrequency ||
				a_snapshot.publicationQpc <= 0 || a_nowQpc < a_snapshot.publicationQpc)
				return {};
			const auto ageTicks = a_nowQpc - a_snapshot.publicationQpc;
			if (static_cast<double>(ageTicks) / static_cast<double>(a_localQpcFrequency) > MaxPublicationAgeSeconds)
				return {};
			if (!std::isfinite(a_snapshot.innerRadius) || !std::isfinite(a_snapshot.midRadius) ||
				a_snapshot.innerRadius < 0.1f || a_snapshot.innerRadius > 1.0f ||
				a_snapshot.midRadius < a_snapshot.innerRadius || a_snapshot.midRadius > 1.5f)
				return {};
			for (const auto& eye : a_snapshot.centerUV)
				for (float center : eye)
					if (!std::isfinite(center) || center < 0.0f || center > 1.0f)
						return {};

			lastConsumedFrame = a_snapshot.frameId;
			return { { a_snapshot.centerUV[0][0], a_snapshot.centerUV[0][1],
						 a_snapshot.centerUV[1][0], a_snapshot.centerUV[1][1] },
				{ a_snapshot.innerRadius, a_snapshot.midRadius, 1.0f, a_snapshot.mode == Mode::EyeTracked ? 1.0f : 0.0f } };
		}

	private:
		std::uint64_t lastConsumedFrame = 0;
	};

}  // namespace OCUEffectFoveation
