#pragma once

#include <cstdint>
#include <limits>

namespace VRSubmitInputFreshnessPolicy
{
	enum class ProofKind : std::uint8_t
	{
		None,
		OuterPairBoundary,
	};

	enum class OuterBoundaryRejection : std::uint8_t
	{
		None,
		MissingBoundary,
		TokenMismatch,
		CompositorCycleMismatch,
		FrameMismatch,
		ThreadMismatch,
		FlagsMismatch,
		MissingDescriptor,
		UnsupportedTextureType,
		MissingResource,
		ResourceMismatch,
		Count,
	};

	enum class ProducerRejection : std::uint8_t
	{
		None,
		MissingCompositorCycle,
		MissingOuterBoundary,
		IncompleteEyePair,
		UnprovenStereoLayout,
		UnprovenSourceSignature,
		MissingSource,
		InvalidSubmitFrame,
		WorldFrameMismatch,
		IncompleteGuideFrame,
		InvalidEyeRegion,
		Count,
	};

	struct SubmitBoundaryIdentity
	{
		std::uint64_t matchedToken = 0;
		std::uint64_t scopeToken = 0;
		std::uint32_t submitFlags = 0;
	};

	struct OuterBoundaryObservation
	{
		std::uint64_t expectedToken = 0;
		std::uint64_t activeToken = 0;
		std::uint64_t activeCompositorCycle = 0;
		std::uint64_t currentCompositorCycle = 0;
		std::uint32_t activeFrame = 0;
		std::uint32_t currentFrame = 0;
		std::uint32_t activeThread = 0;
		std::uint32_t currentThread = 0;
		std::uint32_t activeFlags = 0;
		std::uint32_t currentFlags = 0;
		std::uintptr_t activeTextureIdentity = 0;
		std::uintptr_t activeHandleIdentity = 0;
		std::uintptr_t nestedTextureIdentity = 0;
		std::uintptr_t nestedHandleIdentity = 0;
		bool activeTextureIsDirectX = false;
		bool nestedTextureIsDirectX = false;
	};

	/** Rejects stale or unrelated outer scopes before examining texture identity. */
	[[nodiscard]] constexpr OuterBoundaryRejection ResolveOuterScopeRejection(
		const OuterBoundaryObservation& a_observation) noexcept
	{
		if (a_observation.expectedToken == 0 || a_observation.activeToken == 0)
			return OuterBoundaryRejection::MissingBoundary;
		if (a_observation.expectedToken != a_observation.activeToken)
			return OuterBoundaryRejection::TokenMismatch;
		if (a_observation.activeCompositorCycle == 0 ||
			a_observation.activeCompositorCycle != a_observation.currentCompositorCycle)
			return OuterBoundaryRejection::CompositorCycleMismatch;
		if (a_observation.activeFrame != a_observation.currentFrame)
			return OuterBoundaryRejection::FrameMismatch;
		if (a_observation.activeThread != a_observation.currentThread)
			return OuterBoundaryRejection::ThreadMismatch;
		if (a_observation.activeFlags != a_observation.currentFlags)
			return OuterBoundaryRejection::FlagsMismatch;
		return OuterBoundaryRejection::None;
	}

	/** Reports why the nested submit cannot use the outer pair producer proof. */
	[[nodiscard]] constexpr OuterBoundaryRejection ResolveOuterBoundaryRejection(
		const OuterBoundaryObservation& a_observation) noexcept
	{
		const auto scopeRejection = ResolveOuterScopeRejection(a_observation);
		if (scopeRejection != OuterBoundaryRejection::None)
			return scopeRejection;
		if (a_observation.activeTextureIdentity == 0 ||
			a_observation.nestedTextureIdentity == 0)
			return OuterBoundaryRejection::MissingDescriptor;
		if (!a_observation.activeTextureIsDirectX ||
			!a_observation.nestedTextureIsDirectX)
			return OuterBoundaryRejection::UnsupportedTextureType;
		if (a_observation.activeHandleIdentity == 0 ||
			a_observation.nestedHandleIdentity == 0)
			return OuterBoundaryRejection::MissingResource;
		// Descriptor copies are allowed, but descriptor reuse cannot prove that
		// the DirectX resource still belongs to the captured pair.
		if (a_observation.activeHandleIdentity != a_observation.nestedHandleIdentity)
			return OuterBoundaryRejection::ResourceMismatch;
		return OuterBoundaryRejection::None;
	}

	/** Returns the exact outer pair token only when the nested submit matches it. */
	[[nodiscard]] constexpr std::uint64_t ResolveOuterBoundaryToken(
		const OuterBoundaryObservation& a_observation) noexcept
	{
		return ResolveOuterBoundaryRejection(a_observation) ==
		               OuterBoundaryRejection::None ?
		           a_observation.activeToken :
		           0;
	}

	/** Separates an exact pair proof from the enclosing current-eye cache scope. */
	[[nodiscard]] constexpr SubmitBoundaryIdentity ResolveSubmitBoundaryIdentity(
		const OuterBoundaryObservation& a_observation) noexcept
	{
		return {
			.matchedToken = ResolveOuterBoundaryToken(a_observation),
			.scopeToken = ResolveOuterScopeRejection(a_observation) ==
			                      OuterBoundaryRejection::None ?
			                  a_observation.activeToken :
			                  0,
			.submitFlags = a_observation.currentFlags,
		};
	}

	struct EyeRegion
	{
		std::uint32_t subresource = 0;
		std::uint32_t left = 0;
		std::uint32_t top = 0;
		std::uint32_t right = 0;
		std::uint32_t bottom = 0;
		std::uint32_t depthWidth = 0;
		std::uint32_t depthHeight = 0;
		std::uint32_t depthOffsetX = 0;
		std::uint32_t depthOffsetY = 0;

		/** Requires nonempty color and guide regions before their inputs can be read. */
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return right > left && bottom > top && depthWidth != 0 &&
			       depthHeight != 0;
		}
	};

	/** Requires identical color subresources and guide geometry for cached input reuse. */
	[[nodiscard]] constexpr bool MatchesEyeRegion(
		const EyeRegion& a_left,
		const EyeRegion& a_right) noexcept
	{
		return a_left.subresource == a_right.subresource &&
		       a_left.left == a_right.left && a_left.top == a_right.top &&
		       a_left.right == a_right.right &&
		       a_left.bottom == a_right.bottom &&
		       a_left.depthWidth == a_right.depthWidth &&
		       a_left.depthHeight == a_right.depthHeight &&
		       a_left.depthOffsetX == a_right.depthOffsetX &&
		       a_left.depthOffsetY == a_right.depthOffsetY;
	}

	struct ProducerAdmission
	{
		std::uint64_t compositorCycle = 0;
		std::uint64_t matchedOuterBoundaryToken = 0;
		std::uint32_t submitFrame = 0;
		std::uint32_t lastWorldRenderFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t lastCompletedWorldRenderFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t method = 0;
		std::uint32_t generation = 0;
		std::uint32_t producedEyeMask = 0;
		std::uintptr_t colorSource = 0;
		std::uintptr_t depthSource = 0;
		std::uintptr_t motionVectorSource = 0;
		std::uint32_t sourceWidth = 0;
		std::uint32_t sourceHeight = 0;
		std::uint32_t sourceMipLevels = 0;
		std::uint32_t sourceArraySize = 0;
		std::uint32_t sourceFormat = 0;
		std::uint32_t sourceSampleCount = 0;
		std::uint32_t colorSpace = 0;
		EyeRegion eyes[2]{};
		bool sourceContainsBothEyes = false;
		bool sourceSignatureProven = false;
	};

	struct ProducerProof
	{
		ProofKind kind = ProofKind::None;
		std::uint64_t compositorCycle = 0;
		std::uint64_t pairProducerToken = 0;
		std::uint32_t submitFrame = 0;
		std::uint32_t sourceWorldFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t method = 0;
		std::uint32_t generation = 0;
		std::uint32_t producedEyeMask = 0;
		std::uintptr_t colorSource = 0;
		std::uintptr_t depthSource = 0;
		std::uintptr_t motionVectorSource = 0;
		std::uint32_t sourceWidth = 0;
		std::uint32_t sourceHeight = 0;
		std::uint32_t sourceMipLevels = 0;
		std::uint32_t sourceArraySize = 0;
		std::uint32_t sourceFormat = 0;
		std::uint32_t sourceSampleCount = 0;
		std::uint32_t colorSpace = 0;
		EyeRegion eyes[2]{};

		/** Requires a complete outer pair and retained source identities for both eyes. */
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return kind == ProofKind::OuterPairBoundary &&
			       compositorCycle != 0 && pairProducerToken != 0 &&
			       sourceWorldFrame !=
			           std::numeric_limits<std::uint32_t>::max() &&
			       producedEyeMask == 0x3u && colorSource != 0 &&
			       depthSource != 0 && motionVectorSource != 0 &&
			       eyes[0].IsValid() && eyes[1].IsValid();
		}
	};

	/** Reports the first missing input required to admit an exact stereo producer. */
	[[nodiscard]] constexpr ProducerRejection ResolveProducerRejection(
		const ProducerAdmission& a_admission) noexcept
	{
		if (a_admission.compositorCycle == 0)
			return ProducerRejection::MissingCompositorCycle;
		if (a_admission.matchedOuterBoundaryToken == 0)
			return ProducerRejection::MissingOuterBoundary;
		if (a_admission.producedEyeMask != 0x3u)
			return ProducerRejection::IncompleteEyePair;
		if (!a_admission.sourceContainsBothEyes)
			return ProducerRejection::UnprovenStereoLayout;
		if (!a_admission.sourceSignatureProven)
			return ProducerRejection::UnprovenSourceSignature;
		if (a_admission.colorSource == 0 || a_admission.depthSource == 0 ||
			a_admission.motionVectorSource == 0)
			return ProducerRejection::MissingSource;
		if (a_admission.submitFrame == std::numeric_limits<std::uint32_t>::max())
			return ProducerRejection::InvalidSubmitFrame;
		if (a_admission.lastWorldRenderFrame != a_admission.submitFrame)
			return ProducerRejection::WorldFrameMismatch;
		if (a_admission.lastCompletedWorldRenderFrame != a_admission.submitFrame)
			return ProducerRejection::IncompleteGuideFrame;
		if (!a_admission.eyes[0].IsValid() || !a_admission.eyes[1].IsValid())
			return ProducerRejection::InvalidEyeRegion;
		return ProducerRejection::None;
	}

	/** Admits peer reads only for an exact engine pair and current completed guide frame. */
	[[nodiscard]] constexpr ProducerProof ResolveProducerProof(
		const ProducerAdmission& a_admission) noexcept
	{
		if (ResolveProducerRejection(a_admission) != ProducerRejection::None) {
			return {};
		}

		ProducerProof proof{
			.kind = ProofKind::OuterPairBoundary,
			.compositorCycle = a_admission.compositorCycle,
			.pairProducerToken = a_admission.matchedOuterBoundaryToken,
			.submitFrame = a_admission.submitFrame,
			.sourceWorldFrame = a_admission.submitFrame,
			.method = a_admission.method,
			.generation = a_admission.generation,
			.producedEyeMask = a_admission.producedEyeMask,
			.colorSource = a_admission.colorSource,
			.depthSource = a_admission.depthSource,
			.motionVectorSource = a_admission.motionVectorSource,
			.sourceWidth = a_admission.sourceWidth,
			.sourceHeight = a_admission.sourceHeight,
			.sourceMipLevels = a_admission.sourceMipLevels,
			.sourceArraySize = a_admission.sourceArraySize,
			.sourceFormat = a_admission.sourceFormat,
			.sourceSampleCount = a_admission.sourceSampleCount,
			.colorSpace = a_admission.colorSpace,
		};
		proof.eyes[0] = a_admission.eyes[0];
		proof.eyes[1] = a_admission.eyes[1];
		return proof;
	}

	/** Rejects cached pair inputs after any producer, source, or region change. */
	[[nodiscard]] constexpr bool MatchesProducerProof(
		const ProducerProof& a_latched,
		const ProducerProof& a_current) noexcept
	{
		return a_latched.IsValid() && a_current.IsValid() &&
		       a_latched.kind == a_current.kind &&
		       a_latched.compositorCycle == a_current.compositorCycle &&
		       a_latched.pairProducerToken == a_current.pairProducerToken &&
		       a_latched.submitFrame == a_current.submitFrame &&
		       a_latched.sourceWorldFrame == a_current.sourceWorldFrame &&
		       a_latched.method == a_current.method &&
		       a_latched.generation == a_current.generation &&
		       a_latched.producedEyeMask == a_current.producedEyeMask &&
		       a_latched.colorSource == a_current.colorSource &&
		       a_latched.depthSource == a_current.depthSource &&
		       a_latched.motionVectorSource ==
		           a_current.motionVectorSource &&
		       a_latched.sourceWidth == a_current.sourceWidth &&
		       a_latched.sourceHeight == a_current.sourceHeight &&
		       a_latched.sourceMipLevels == a_current.sourceMipLevels &&
		       a_latched.sourceArraySize == a_current.sourceArraySize &&
		       a_latched.sourceFormat == a_current.sourceFormat &&
		       a_latched.sourceSampleCount == a_current.sourceSampleCount &&
		       a_latched.colorSpace == a_current.colorSpace &&
		       MatchesEyeRegion(a_latched.eyes[0], a_current.eyes[0]) &&
		       MatchesEyeRegion(a_latched.eyes[1], a_current.eyes[1]);
	}

	/** Allows a peer read only when the exact pair proof includes that eye. */
	[[nodiscard]] constexpr bool CanConsumePeerInputs(
		const ProducerProof& a_proof,
		std::uint32_t a_requestedEye) noexcept
	{
		return a_requestedEye < 2u && a_proof.IsValid() &&
		       (a_proof.producedEyeMask & (1u << (a_requestedEye ^ 1u))) != 0;
	}
}
