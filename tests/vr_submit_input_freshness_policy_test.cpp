#include "Features/Upscaling/VRSubmitInputFreshnessPolicy.h"

#include <cstdint>

namespace
{
	constexpr VRSubmitInputFreshnessPolicy::EyeRegion kLeftRegion{
		.subresource = 0,
		.left = 0,
		.top = 0,
		.right = 100,
		.bottom = 80,
		.depthWidth = 100,
		.depthHeight = 80,
		.depthOffsetX = 0,
		.depthOffsetY = 0,
	};
	constexpr VRSubmitInputFreshnessPolicy::EyeRegion kRightRegion{
		.subresource = 0,
		.left = 100,
		.top = 0,
		.right = 200,
		.bottom = 80,
		.depthWidth = 100,
		.depthHeight = 80,
		.depthOffsetX = 100,
		.depthOffsetY = 0,
	};

	constexpr VRSubmitInputFreshnessPolicy::ProducerAdmission
	MakeAdmission()
	{
		VRSubmitInputFreshnessPolicy::ProducerAdmission admission{
			.compositorCycle = 7,
			.matchedOuterBoundaryToken = 19,
			.submitFrame = 42,
			.lastWorldRenderFrame = 42,
			.lastCompletedWorldRenderFrame = 42,
			.method = 2,
			.generation = 5,
			.producedEyeMask = 0x3,
			.colorSource = 0x1000,
			.depthSource = 0x2000,
			.motionVectorSource = 0x3000,
			.sourceWidth = 200,
			.sourceHeight = 80,
			.sourceMipLevels = 1,
			.sourceArraySize = 1,
			.sourceFormat = 28,
			.sourceSampleCount = 1,
			.colorSpace = 1,
			.sourceContainsBothEyes = true,
			.sourceSignatureProven = true,
		};
		admission.eyes[0] = kLeftRegion;
		admission.eyes[1] = kRightRegion;
		return admission;
	}
}

int main()
{
	using namespace VRSubmitInputFreshnessPolicy;

	constexpr OuterBoundaryObservation boundary{
		.expectedToken = 19,
		.activeToken = 19,
		.activeCompositorCycle = 7,
		.currentCompositorCycle = 7,
		.activeFrame = 42,
		.currentFrame = 42,
		.activeThread = 11,
		.currentThread = 11,
		.activeFlags = 3,
		.currentFlags = 3,
		.activeSourceIdentity = 0x1000,
		.nestedTextureIdentity = 0x4000,
		.nestedHandleIdentity = 0x1000,
	};
	static_assert(ResolveOuterBoundaryToken(boundary) == 19);
	constexpr auto wrongCycleBoundary = [] {
		auto value = boundary;
		value.currentCompositorCycle = 8;
		return value;
	}();
	static_assert(ResolveOuterBoundaryToken(wrongCycleBoundary) == 0);
	constexpr auto wrongIdentityBoundary = [] {
		auto value = boundary;
		value.nestedHandleIdentity = 0x5000;
		return value;
	}();
	static_assert(ResolveOuterBoundaryToken(wrongIdentityBoundary) == 0);
	constexpr auto wrongThreadBoundary = [] {
		auto value = boundary;
		value.currentThread = 12;
		return value;
	}();
	static_assert(ResolveOuterBoundaryToken(wrongThreadBoundary) == 0);
	constexpr auto wrongFlagsBoundary = [] {
		auto value = boundary;
		value.currentFlags = 4;
		return value;
	}();
	static_assert(ResolveOuterBoundaryToken(wrongFlagsBoundary) == 0);

	constexpr auto admission = MakeAdmission();
	constexpr auto proof = ResolveProducerProof(admission);
	static_assert(proof.IsValid());
	static_assert(CanConsumePeerInputs(proof, 0));
	static_assert(CanConsumePeerInputs(proof, 1));
	static_assert(MatchesProducerProof(proof, proof));

	constexpr auto layoutOnly = [] {
		auto value = admission;
		value.matchedOuterBoundaryToken = 0;
		return value;
	}();
	static_assert(!ResolveProducerProof(layoutOnly).IsValid());
	constexpr auto currentEyeOnly = [] {
		auto value = admission;
		value.producedEyeMask = 0x1;
		return value;
	}();
	static_assert(!ResolveProducerProof(currentEyeOnly).IsValid());
	constexpr auto incompleteGuideFrame = [] {
		auto value = admission;
		value.lastCompletedWorldRenderFrame = 41;
		return value;
	}();
	static_assert(!ResolveProducerProof(incompleteGuideFrame).IsValid());
	constexpr auto retainedGuideFrameWithoutExplicitContinuity = [] {
		auto value = admission;
		value.lastWorldRenderFrame = 41;
		value.lastCompletedWorldRenderFrame = 41;
		return value;
	}();
	static_assert(
		!ResolveProducerProof(retainedGuideFrameWithoutExplicitContinuity)
			.IsValid());
	constexpr auto missingDepth = [] {
		auto value = admission;
		value.depthSource = 0;
		return value;
	}();
	static_assert(!ResolveProducerProof(missingDepth).IsValid());
	constexpr auto invalidPeerRegion = [] {
		auto value = admission;
		value.eyes[1].right = value.eyes[1].left;
		return value;
	}();
	static_assert(!ResolveProducerProof(invalidPeerRegion).IsValid());

	constexpr auto staleCycleProof = [] {
		auto value = proof;
		value.compositorCycle = 8;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, staleCycleProof));
	constexpr auto staleProducerProof = [] {
		auto value = proof;
		value.pairProducerToken = 20;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, staleProducerProof));
	constexpr auto staleWorldProof = [] {
		auto value = proof;
		value.sourceWorldFrame = 40;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, staleWorldProof));
	constexpr auto changedMotionVectors = [] {
		auto value = proof;
		value.motionVectorSource = 0x3001;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, changedMotionVectors));
	constexpr auto changedGeneration = [] {
		auto value = proof;
		value.generation = 6;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, changedGeneration));
	constexpr auto changedMethod = [] {
		auto value = proof;
		value.method = 3;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, changedMethod));
	constexpr auto changedRegion = [] {
		auto value = proof;
		++value.eyes[1].left;
		return value;
	}();
	static_assert(!MatchesProducerProof(proof, changedRegion));

	return 0;
}
