#pragma once

#include "Utils/ContentHash.h"
#include "VRDepthCullingSubmissionPolicy.h"

#include <ankerl/unordered_dense.h>
#include <bitset>
#include <cstring>
#include <new>

namespace VRDepthCullingTemporalPolicy
{
	/** Short native-positive visibility grace; speculative recovery retains its separate budget. */
	class NativeVisibilityHistory
	{
	public:
		static constexpr std::uint64_t kGraceReadbacks = 8;
		static constexpr std::size_t kCapacity = kMaximumObjects;

		struct Domain
		{
			std::uintptr_t culler = 0, camera = 0, sourceView = 0, reducedTarget = 0;
			VRDepthCullingSubmissionPolicy::Scene scene{};
			std::uint64_t resourceGeneration = 0, cullingEpoch = 0, policyEpoch = 0;
			Depth::Convention convention = Depth::Convention::Standard;
			float activeWidth = 0, activeHeight = 0;
			bool operator==(const Domain&) const = default;

			bool IsValid() const
			{
				return culler && camera && sourceView && reducedTarget && resourceGeneration && cullingEpoch && policyEpoch &&
				       !(policyEpoch & 1u) && VRDepthCullingSubmissionPolicy::IsValid(scene) &&
				       VRDepthCullingSubmissionPolicy::IsSupportedRasterExtent(activeWidth, activeHeight) &&
				       (convention == Depth::Convention::Standard || convention == Depth::Convention::Reversed);
			}
		};

		struct Retention
		{
			std::bitset<kMaximumObjects> indices{};
			std::uint32_t capacityMisses = 0;
		};

		/** Reserve once before installing render hooks; failure leaves native and budgeted recovery available. */
		bool Prepare() noexcept
		{
			try {
				entries.reserve(kCapacity);
				ready = true;
				return true;
			} catch (const std::bad_alloc&) {
				return false;
			}
		}

		/** Retire visibility without releasing preallocated storage. */
		void Clear()
		{
			entries.clear();
			hasSequence = false;
		}

		/** Read unmodified GPU results before any CPU promotions; registration indices are not object identities. */
		Retention Observe(const Domain& a_domain, std::uint64_t a_sequence, std::span<const OBBTransform> a_bounds,
			std::span<const std::uint32_t> a_nativeResults)
		{
			Retention retained;
			if (!ready || !a_domain.IsValid() || a_bounds.empty() || a_bounds.size() > kCapacity || a_bounds.size() != a_nativeResults.size()) {
				Clear();
				return retained;
			}
			if (!hasSequence || domain != a_domain || a_sequence != sequence + 1)
				Clear();
			domain = a_domain;
			sequence = a_sequence;
			hasSequence = true;
			std::bitset<kMaximumObjects> admissions;
			for (std::size_t index = 0; index < a_bounds.size(); ++index) {
				const auto& bound = a_bounds[index];
				const auto previous = entries.find(bound);
				if (previous != entries.end()) {
					previous->second.seen = a_sequence;
					if (a_nativeResults[index])
						previous->second.visible = a_sequence;
					else if (a_sequence - previous->second.visible <= kGraceReadbacks)
						retained.indices.set(index);
				} else if (a_nativeResults[index]) {
					admissions.set(index);
				}
			}
			// Only this registration can consume grace; retire absent keys before admitting replacements.
			for (auto entry = entries.begin(); entry != entries.end();) {
				if (entry->second.seen != a_sequence || a_sequence - entry->second.visible > kGraceReadbacks)
					entry = entries.erase(entry);
				else
					++entry;
			}
			for (std::size_t index = 0; index < a_bounds.size(); ++index) {
				if (!admissions.test(index) || !IsFinite(a_bounds[index]))
					continue;
				if (entries.size() < kCapacity)
					entries.try_emplace(a_bounds[index], Sample{ a_sequence, a_sequence });
				else if (!entries.contains(a_bounds[index]))
					++retained.capacityMisses;
			}
			return retained;
		}

		[[nodiscard]] std::size_t Size() const { return entries.size(); }

	private:
		static bool IsFinite(const OBBTransform& a_bound)
		{
			for (const auto& row : a_bound.entry)
				for (const auto value : row)
					if (!std::isfinite(value))
						return false;
			return true;
		}

		struct Hash
		{
			std::size_t operator()(const OBBTransform& a_bound) const
			{
				return static_cast<std::size_t>(Util::ContentHash::HashBytes(&a_bound, sizeof(a_bound)).low);
			}
		};
		struct Equal
		{
			bool operator()(const OBBTransform& a_left, const OBBTransform& a_right) const
			{
				return std::memcmp(&a_left, &a_right, sizeof(a_left)) == 0;
			}
		};

		struct Sample
		{
			std::uint64_t visible = 0, seen = 0;
		};
		ankerl::unordered_dense::map<OBBTransform, Sample, Hash, Equal> entries;
		Domain domain{};
		std::uint64_t sequence = 0;
		bool ready = false, hasSequence = false;
	};
}
