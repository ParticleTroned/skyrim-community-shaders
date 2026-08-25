#pragma once

#include <cstdint>
#include <optional>

namespace Util::GenerationClaim
{
	enum class ClaimOutcome
	{
		CacheHit,
		Claimed,
		MustWait
	};

	enum class PublishOutcome
	{
		Published,
		RejectedStale,
		RejectedStaleCleanedPending
	};

	template <class Traits, class Map, class MakePending>
	std::pair<ClaimOutcome, typename Map::iterator> TryClaim(
		Map& a_map,
		const typename Map::key_type& a_key,
		std::optional<uint64_t> a_callerGeneration,
		uint64_t a_liveGeneration,
		MakePending&& a_makePending)
	{
		if (auto it = a_map.find(a_key); it != a_map.end()) {
			if (Traits::IsPending(it->second)) {
				return { ClaimOutcome::MustWait, it };
			}
			if (Traits::IsCompleted(it->second) && Traits::HasPayload(it->second)) {
				return { ClaimOutcome::CacheHit, it };
			}
		}

		auto inserted = a_map.insert_or_assign(
			a_key,
			a_makePending(a_callerGeneration.value_or(a_liveGeneration)));
		return { ClaimOutcome::Claimed, inserted.first };
	}

	template <class Traits, class Map, class MakeEntry>
	PublishOutcome TryPublish(
		Map& a_map,
		const typename Map::key_type& a_key,
		std::optional<uint64_t> a_callerGeneration,
		uint64_t a_liveGeneration,
		bool a_success,
		MakeEntry&& a_makeEntry)
	{
		if (a_callerGeneration && *a_callerGeneration != a_liveGeneration) {
			if (auto it = a_map.find(a_key);
				it != a_map.end() && Traits::IsPending(it->second) &&
				Traits::GetGeneration(it->second) == *a_callerGeneration) {
				a_map.erase(it);
				return PublishOutcome::RejectedStaleCleanedPending;
			}
			return PublishOutcome::RejectedStale;
		}

		a_map.insert_or_assign(
			a_key,
			a_makeEntry(a_callerGeneration.value_or(a_liveGeneration), a_success));
		return PublishOutcome::Published;
	}
}
