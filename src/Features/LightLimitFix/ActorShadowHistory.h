#pragma once

#include "ShadowCullingPolicy.h"
#include <mutex>

namespace LightLimitFixDetail::ShadowCulling
{
	/** Bounded actor-distance hysteresis; native handles distinguish recycled reference addresses. */
	class ActorShadowHistory
	{
	public:
		/** Keep thresholds immutable for the lifetime of snapshots sharing this history. */
		explicit ActorShadowHistory(ActorDistanceBand distances) : distances(distances) {}

		/** Share distance decisions across parts and threads without retaining actors or geometry. */
		bool Beyond(std::uint32_t handle, const std::array<double, 3>& position,
			const std::array<std::array<double, 3>, 2>& eyes, unsigned eyeCount, std::uint64_t sampledAt, std::uint32_t frame)
		{
			if (!handle)
				return false;
			double distance = std::numeric_limits<double>::infinity();
			bool valid = eyeCount && eyeCount <= eyes.size();
			for (unsigned eye = 0; valid && eye < eyeCount; ++eye) {
				for (unsigned axis = 0; axis < position.size(); ++axis)
					valid &= std::isfinite(position[axis]) && std::isfinite(eyes[eye][axis]);
				distance = std::min(distance, std::hypot(position[0] - eyes[eye][0], position[1] - eyes[eye][1], position[2] - eyes[eye][2]) - EyeDistanceMargin);
			}
			const std::lock_guard lock{ mutex };
			auto& entry = entries[handle % entries.size()];
			// Distinct frames can share a clock tick; delayed draws must not undo a newer return decision.
			if (sampledAt < entry.sampledAt || frame < entry.frame)
				return false;
			// A skipped frame cannot prove the actor stayed outside the return distance.
			const bool fresh = entry.handle && sampledAt - entry.sampledAt <= SnapshotMaxAgeMs && frame - entry.frame <= 1;
			// Keep colliding actors visible rather than evicting another actor's live return band.
			if (fresh && entry.handle != handle)
				return false;
			const bool wasCulled = fresh && entry.culled;
			const bool culled = valid && std::isfinite(distance) && distance > (wasCulled ? distances.returning : distances.cutoff);
			entry = { handle, frame, sampledAt, culled };
			return culled;
		}

	private:
		const ActorDistanceBand distances;
		struct Entry
		{
			std::uint32_t handle = 0;
			std::uint32_t frame = 0;
			std::uint64_t sampledAt = 0;
			bool culled = false;
		};
		std::array<Entry, 512> entries{};
		std::mutex mutex;
	};
}
