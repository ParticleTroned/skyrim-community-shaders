#pragma once

#include <cmath>
#include <limits>
#include <unordered_map>

namespace GrassPolicy
{
	/** @brief Own spawn times independently of renderer readiness and snapshot lifetime. */
	template <class Group>
	class FadeHistory
	{
	public:
		/** @brief Only newly generated groups begin fading; existing groups retain their age. */
		void Generated(Group group, float now, bool replace = false)
		{
			if (!std::isfinite(now))
				return;
			if (replace)
				starts.insert_or_assign(group, now);
			else
				starts.try_emplace(group, now);
		}

		/** @brief Retire removed groups before their native storage can be reused. */
		void Removed(Group group) { starts.erase(group); }

		/** @brief Reuse existing history; record unknown grass as mature without rebuilding the table. */
		float Capture(Group group)
		{
			return starts.try_emplace(group, kAlreadyPresent).first->second;
		}

		/** @brief Late discovery stays visible; native fallback and snapshots share the same spawn age. */
		float Start(Group group) const
		{
			const auto found = starts.find(group);
			return found == starts.end() ? kAlreadyPresent : found->second;
		}

	private:
		// A synthetic age avoids rounding a short fade back to zero after long sessions.
		static constexpr float kAlreadyPresent = std::numeric_limits<float>::lowest();
		std::unordered_map<Group, float> starts;
	};
}
