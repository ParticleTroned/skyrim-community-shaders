#pragma once

#include "VRSubmitInputFreshnessPolicy.h"

#include <cstdint>

namespace VROrdinarySaveRecovery
{
	inline constexpr uint32_t kRequiredStereoFrames = 6;

	/** Presentation proof for one ordinary save and one unchanged resource contract. */
	struct Identity
	{
		uint64_t saveToken = 0;
		uint64_t resourceKey = 0;
		uint32_t generation = 0;
		uint32_t method = 0;
		uint64_t commonResourceGeneration = 0;
		uint32_t intermediateGeneration = 0;

		bool operator==(const Identity&) const = default;
	};

	/** Accumulates complete consecutive stereo frames; no timeout authorizes release. */
	class Proof
	{
	public:
		void Reset() { *this = {}; }

		void Observe(const Identity& a_identity, uint32_t a_frame, uint64_t a_cycle,
			uint32_t a_eye, const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity& a_boundary,
			bool a_ready)
		{
			if (!a_ready || !a_identity.saveToken || !a_identity.generation ||
				!a_frame || !a_cycle || a_eye > 1 || !a_boundary.scopeToken) {
				Reset();
				return;
			}
			if (identity != a_identity || a_frame < frame || a_cycle < cycle) {
				Reset();
				identity = a_identity;
			}
			if (a_cycle != cycle || a_frame != frame) {
				if (eyeMask != 3 || a_frame != frame + 1) {
					stableFrames = 0;
					firstFrame = a_frame;
					qualifiedFrame = 0;
					qualifiedCycle = 0;
				}
				frame = a_frame;
				cycle = a_cycle;
				eyeMask = 0;
				boundary = a_boundary;
			} else if (boundary.scopeToken != a_boundary.scopeToken ||
					   boundary.submitFlags != a_boundary.submitFlags) {
				// A new outer scope may rewrite color within the same compositor cycle.
				Reset();
				return;
			}
			const auto bit = 1u << a_eye;
			if (eyeMask & bit)
				return;
			eyeMask |= bit;
			if (eyeMask != 3)
				return;
			lastCompleteFrame = a_frame;
			if (stableFrames < kRequiredStereoFrames)
				++stableFrames;
			if (!qualifiedFrame && stableFrames >= kRequiredStereoFrames) {
				qualifiedFrame = a_frame;
				qualifiedCycle = a_cycle;
			}
		}

		/** Release in the following compositor cycle, including desktop Present frame skew. */
		[[nodiscard]] bool CanResume(const Identity& a_identity, uint32_t a_completedWorldFrame, uint64_t a_cycle) const
		{
			return a_identity.saveToken && identity == a_identity && qualifiedCycle &&
			       a_cycle > qualifiedCycle && a_cycle >= cycle && a_cycle - cycle <= 1 &&
			       a_completedWorldFrame >= lastCompleteFrame && a_completedWorldFrame - lastCompleteFrame <= 1;
		}

		Identity identity{};
		VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity boundary{};
		uint32_t firstFrame = 0;
		uint32_t frame = 0;
		uint64_t cycle = 0;
		uint32_t eyeMask = 0;
		uint32_t stableFrames = 0;
		uint32_t qualifiedFrame = 0;
		uint64_t qualifiedCycle = 0;
		uint32_t lastCompleteFrame = 0;
	};
}
