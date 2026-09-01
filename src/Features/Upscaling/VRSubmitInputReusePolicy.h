#pragma once

#include "VRSubmitInputFreshnessPolicy.h"

#include <array>

namespace VRSubmitInputReusePolicy
{
	using namespace VRSubmitInputFreshnessPolicy;

	/** Identifies one observed eye inside a correlated outer scope, without admitting peer reads. */
	struct CurrentEyeIdentity
	{
		ProducerAdmission source{};
		std::uint64_t scopeToken = 0;
		std::uint32_t submitFlags = 0;
		std::uint32_t eye = 2;
		EyeRegion region{};

		/** Requires one observed eye, usable sources, and a correlated producer scope. */
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return scopeToken != 0 && source.compositorCycle != 0 && eye < 2 &&
			       source.submitFrame != std::numeric_limits<std::uint32_t>::max() &&
			       source.colorSource != 0 && source.depthSource != 0 &&
			       source.motionVectorSource != 0 && source.sourceWidth != 0 &&
			       source.sourceHeight != 0 && source.sourceMipLevels != 0 &&
			       source.sourceArraySize != 0 && source.sourceSampleCount != 0 &&
			       region.IsValid();
		}
	};

	/** Missing outer scope cannot exclude an in-place producer rewrite in the same cycle. */
	[[nodiscard]] constexpr CurrentEyeIdentity ResolveCurrentEyeIdentity(
		const ProducerAdmission& a_source,
		const SubmitBoundaryIdentity& a_boundary,
		std::uint32_t a_eye,
		const EyeRegion& a_region) noexcept
	{
		CurrentEyeIdentity identity{
			.source = a_source,
			.scopeToken = a_boundary.scopeToken,
			.submitFlags = a_boundary.submitFlags,
			.eye = a_eye,
			.region = a_region,
		};
		return identity.IsValid() ? identity : CurrentEyeIdentity{};
	}

	/** Retains independent eye entries while their common outer producer scope stays active. */
	[[nodiscard]] constexpr bool SharesCurrentEyeProducerScope(
		const CurrentEyeIdentity& a_latched,
		const CurrentEyeIdentity& a_current) noexcept
	{
		const auto& left = a_latched.source;
		const auto& right = a_current.source;
		return a_latched.IsValid() && a_current.IsValid() &&
		       a_latched.scopeToken == a_current.scopeToken &&
		       a_latched.submitFlags == a_current.submitFlags &&
		       left.compositorCycle == right.compositorCycle &&
		       left.submitFrame == right.submitFrame &&
		       left.lastWorldRenderFrame == right.lastWorldRenderFrame &&
		       left.lastCompletedWorldRenderFrame == right.lastCompletedWorldRenderFrame &&
		       left.method == right.method && left.generation == right.generation;
	}

	/** Equality authorizes only retrying the same eye; it never supplies a ProducerProof. */
	[[nodiscard]] constexpr bool MatchesCurrentEyeIdentity(
		const CurrentEyeIdentity& a_latched,
		const CurrentEyeIdentity& a_current) noexcept
	{
		const auto& left = a_latched.source;
		const auto& right = a_current.source;
		return SharesCurrentEyeProducerScope(a_latched, a_current) &&
		       a_latched.eye == a_current.eye &&
		       left.matchedOuterBoundaryToken == right.matchedOuterBoundaryToken &&
		       left.colorSource == right.colorSource && left.depthSource == right.depthSource &&
		       left.motionVectorSource == right.motionVectorSource &&
		       left.sourceWidth == right.sourceWidth && left.sourceHeight == right.sourceHeight &&
		       left.sourceMipLevels == right.sourceMipLevels && left.sourceArraySize == right.sourceArraySize &&
		       left.sourceFormat == right.sourceFormat && left.sourceSampleCount == right.sourceSampleCount &&
		       left.colorSpace == right.colorSpace &&
		       MatchesEyeRegion(a_latched.region, a_current.region);
	}

	/** Keeps independent eye preparations; attempted writes retire claims before they can fail. */
	struct PreparedInputs
	{
		std::array<CurrentEyeIdentity, 2> identities{};
		std::array<bool, 2> foveatedRegionEncode{};

		/** Retires claims before an attempted write can partially replace their inputs. */
		constexpr void Invalidate(std::uint32_t a_eyeMask) noexcept
		{
			for (std::uint32_t eye = 0; eye < 2; ++eye) {
				if ((a_eyeMask & (1u << eye)) != 0) {
					identities[eye] = {};
					foveatedRegionEncode[eye] = false;
				}
			}
		}

		/** Records only a successfully prepared eye without replacing its peer's claim. */
		constexpr void Record(const CurrentEyeIdentity& a_identity, bool a_foveated) noexcept
		{
			if (a_identity.IsValid()) {
				identities[a_identity.eye] = a_identity;
				foveatedRegionEncode[a_identity.eye] = a_foveated;
			}
		}

		/** Authorizes reuse only for the exact previously prepared current eye. */
		[[nodiscard]] constexpr bool Matches(const CurrentEyeIdentity& a_identity) const noexcept
		{
			return a_identity.eye < 2 &&
			       MatchesCurrentEyeIdentity(identities[a_identity.eye], a_identity);
		}
	};
}
