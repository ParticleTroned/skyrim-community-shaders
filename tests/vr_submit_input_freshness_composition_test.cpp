#include "Features/Upscaling/VRSubmitInputFreshnessBoundary.h"
#include "Features/Upscaling/VRSubmitInputReusePolicy.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
	using namespace VRSubmitInputFreshnessPolicy;
	using namespace VRSubmitInputReusePolicy;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::fprintf(stderr, "%s\n", a_message);
			std::exit(EXIT_FAILURE);
		}
	}

	OuterPairBoundaryState CaptureBoundary(const vr::Texture_t* a_texture)
	{
		return {
			.token = 19,
			.compositorCycle = 7,
			.frame = 42,
			.thread = 11,
			.flags = vr::Submit_Default,
			.source = CaptureSubmitTextureIdentity(a_texture),
			.active = true,
		};
	}

	OuterBoundaryObservation Observe(
		const OuterPairBoundaryState& a_boundary, const vr::Texture_t* a_texture)
	{
		return ObserveNestedSubmit(a_boundary, a_texture, 7, 42, 11, vr::Submit_Default);
	}

	ProducerAdmission Admit(const OuterBoundaryObservation& a_observation)
	{
		ProducerAdmission admission{
			.compositorCycle = a_observation.currentCompositorCycle,
			.matchedOuterBoundaryToken = ResolveSubmitBoundaryIdentity(a_observation).matchedToken,
			.submitFrame = a_observation.currentFrame,
			.lastWorldRenderFrame = a_observation.currentFrame,
			.lastCompletedWorldRenderFrame = a_observation.currentFrame,
			.method = 2,
			.generation = 5,
			.producedEyeMask = 0x3,
			.colorSource = a_observation.nestedHandleIdentity,
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
		admission.eyes[0] = { .right = 100, .bottom = 80, .depthWidth = 100, .depthHeight = 80 };
		admission.eyes[1] = { .left = 100, .right = 200, .bottom = 80, .depthWidth = 100, .depthHeight = 80, .depthOffsetX = 100 };
		return admission;
	}

	void RequireBoundaryRejected(const OuterBoundaryObservation& a_observation,
		OuterBoundaryRejection a_expected)
	{
		Require(ResolveOuterBoundaryRejection(a_observation) == a_expected,
			"Incorrect outer rejection reason");
		const auto proof = ResolveProducerProof(Admit(a_observation));
		Require(!CanConsumePeerInputs(proof, 0) && !CanConsumePeerInputs(proof, 1),
			"Rejected outer observation authorized a peer input read");
	}
}

int main()
{
	int colorResource = 0;
	int replacementResource = 0;
	vr::Texture_t texture{ &colorResource, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
	const auto boundary = CaptureBoundary(&texture);
	const auto observation = Observe(boundary, &texture);
	const auto admission = Admit(observation);
	const auto proof = ResolveProducerProof(admission);
	Require(boundary.source.descriptor == reinterpret_cast<std::uintptr_t>(&texture) &&
				boundary.source.resource == reinterpret_cast<std::uintptr_t>(&colorResource),
		"Outer extraction conflated the descriptor and resource");
	Require(CanConsumePeerInputs(proof, 0) && CanConsumePeerInputs(proof, 1),
		"Exact outer pair did not authorize both eyes");
	Require(!CanConsumePeerInputs(proof, 2), "Invalid eye authorized a peer read");

	auto copiedTexture = texture;
	const auto copiedObservation = Observe(boundary, &copiedTexture);
	Require(copiedObservation.activeTextureIdentity != copiedObservation.nestedTextureIdentity,
		"Descriptor-copy scenario did not use distinct descriptors");
	Require(MatchesProducerProof(proof, ResolveProducerProof(Admit(copiedObservation))),
		"Copied DirectX descriptor lost its exact resource proof");

	texture.handle = &replacementResource;
	const auto changedResourceObservation = Observe(boundary, &texture);
	RequireBoundaryRejected(changedResourceObservation, OuterBoundaryRejection::ResourceMismatch);
	copiedTexture.handle = &replacementResource;
	RequireBoundaryRejected(Observe(boundary, &copiedTexture), OuterBoundaryRejection::ResourceMismatch);
	texture.handle = nullptr;
	RequireBoundaryRejected(Observe(boundary, &texture), OuterBoundaryRejection::MissingResource);
	texture.handle = &colorResource;
	texture.eType = vr::TextureType_OpenGL;
	RequireBoundaryRejected(Observe(boundary, &texture), OuterBoundaryRejection::UnsupportedTextureType);
	const auto nonDirectXBoundary = CaptureBoundary(&texture);
	texture.eType = vr::TextureType_DirectX;
	RequireBoundaryRejected(Observe(nonDirectXBoundary, &texture), OuterBoundaryRejection::UnsupportedTextureType);
	RequireBoundaryRejected(Observe(boundary, nullptr), OuterBoundaryRejection::MissingDescriptor);
	RequireBoundaryRejected(Observe(CaptureBoundary(nullptr), &texture), OuterBoundaryRejection::MissingDescriptor);
	auto inactiveBoundary = boundary;
	inactiveBoundary.active = false;
	RequireBoundaryRejected(Observe(inactiveBoundary, &texture), OuterBoundaryRejection::MissingBoundary);
	texture.handle = nullptr;
	const auto missingResourceBoundary = CaptureBoundary(&texture);
	texture.handle = &colorResource;
	RequireBoundaryRejected(Observe(missingResourceBoundary, &texture), OuterBoundaryRejection::MissingResource);

	struct BoundaryCase
	{
		void (*mutate)(OuterBoundaryObservation&);
		OuterBoundaryRejection reason;
	};
	constexpr std::array boundaryCases{
		BoundaryCase{ [](auto& value) { value.expectedToken = 0; }, OuterBoundaryRejection::MissingBoundary },
		BoundaryCase{ [](auto& value) { ++value.expectedToken; }, OuterBoundaryRejection::TokenMismatch },
		BoundaryCase{ [](auto& value) { value.activeCompositorCycle = 0; }, OuterBoundaryRejection::CompositorCycleMismatch },
		BoundaryCase{ [](auto& value) { ++value.currentCompositorCycle; }, OuterBoundaryRejection::CompositorCycleMismatch },
		BoundaryCase{ [](auto& value) { ++value.currentFrame; }, OuterBoundaryRejection::FrameMismatch },
		BoundaryCase{ [](auto& value) { ++value.currentThread; }, OuterBoundaryRejection::ThreadMismatch },
		BoundaryCase{ [](auto& value) { ++value.currentFlags; }, OuterBoundaryRejection::FlagsMismatch },
	};
	for (const auto& scenario : boundaryCases) {
		auto changed = observation;
		scenario.mutate(changed);
		RequireBoundaryRejected(changed, scenario.reason);
		Require(ResolveSubmitBoundaryIdentity(changed).scopeToken == 0,
			"Mismatched outer scope authorized current-eye caching");
	}

	struct ProducerCase
	{
		void (*mutate)(ProducerAdmission&);
		ProducerRejection reason;
	};
	constexpr std::array producerCases{
		ProducerCase{ [](auto& value) { value.compositorCycle = 0; }, ProducerRejection::MissingCompositorCycle },
		ProducerCase{ [](auto& value) { value.matchedOuterBoundaryToken = 0; }, ProducerRejection::MissingOuterBoundary },
		ProducerCase{ [](auto& value) { value.producedEyeMask = 1; }, ProducerRejection::IncompleteEyePair },
		ProducerCase{ [](auto& value) { value.sourceContainsBothEyes = false; }, ProducerRejection::UnprovenStereoLayout },
		ProducerCase{ [](auto& value) { value.sourceSignatureProven = false; }, ProducerRejection::UnprovenSourceSignature },
		ProducerCase{ [](auto& value) { value.colorSource = 0; }, ProducerRejection::MissingSource },
		ProducerCase{ [](auto& value) { value.depthSource = 0; }, ProducerRejection::MissingSource },
		ProducerCase{ [](auto& value) { value.motionVectorSource = 0; }, ProducerRejection::MissingSource },
		ProducerCase{ [](auto& value) { value.submitFrame = std::numeric_limits<std::uint32_t>::max(); }, ProducerRejection::InvalidSubmitFrame },
		ProducerCase{ [](auto& value) { --value.lastWorldRenderFrame; }, ProducerRejection::WorldFrameMismatch },
		ProducerCase{ [](auto& value) { --value.lastCompletedWorldRenderFrame; }, ProducerRejection::IncompleteGuideFrame },
		ProducerCase{ [](auto& value) { value.eyes[1] = {}; }, ProducerRejection::InvalidEyeRegion },
	};
	for (const auto& scenario : producerCases) {
		auto changed = admission;
		scenario.mutate(changed);
		Require(ResolveProducerRejection(changed) == scenario.reason,
			"Incorrect producer rejection reason");
		Require(!CanConsumePeerInputs(ResolveProducerProof(changed), 0),
			"Rejected producer admission authorized a peer input read");
	}

	const auto rejectedBoundary = ResolveSubmitBoundaryIdentity(changedResourceObservation);
	auto fallbackAdmission = Admit(changedResourceObservation);
	--fallbackAdmission.lastCompletedWorldRenderFrame;
	const auto fallbackProof = ResolveProducerProof(fallbackAdmission);
	const auto left = ResolveCurrentEyeIdentity(fallbackAdmission, rejectedBoundary, 0, fallbackAdmission.eyes[0]);
	const auto right = ResolveCurrentEyeIdentity(fallbackAdmission, rejectedBoundary, 1, fallbackAdmission.eyes[1]);
	Require(!fallbackProof.IsValid() && left.IsValid() && right.IsValid(),
		"Rejected stereo proof prevented a correlated current-eye retry");
	PreparedInputs prepared;
	prepared.Record(left, false);
	Require(prepared.Matches(left) && !prepared.Matches(right), "Left eye preparation leaked to its peer");
	prepared.Record(right, true);
	Require(prepared.Matches(left) && prepared.Matches(right), "L/R/L sequence discarded an independent eye");
	Require(!CanConsumePeerInputs(fallbackProof, 0) && !CanConsumePeerInputs(fallbackProof, 1),
		"Current-eye reuse upgraded an invalid stereo proof");
	auto separateRightAdmission = admission;
	--separateRightAdmission.lastCompletedWorldRenderFrame;
	const auto separateRight = ResolveCurrentEyeIdentity(separateRightAdmission,
		ResolveSubmitBoundaryIdentity(observation), 1, separateRightAdmission.eyes[1]);
	Require(left.source.colorSource != separateRight.source.colorSource &&
				SharesCurrentEyeProducerScope(left, separateRight),
		"Independent source textures did not retain their shared outer scope");
	std::array<CurrentEyeIdentity, 2> outputs{};
	std::array<std::uint32_t, 2> requiredDispatches{};
	CurrentEyeIdentity previousOutputScope{};
	for (const auto& current : { left, separateRight, left }) {
		if (!SharesCurrentEyeProducerScope(previousOutputScope, current))
			outputs = {};
		if (!MatchesCurrentEyeIdentity(outputs[current.eye], current)) {
			++requiredDispatches[current.eye];
			outputs[current.eye] = current;
		}
		previousOutputScope = current;
	}
	Require(requiredDispatches[0] == 1 && requiredDispatches[1] == 1,
		"L(A)/R(B)/L(A) output reuse required a duplicate vendor dispatch");
	auto replacementScope = left;
	++replacementScope.scopeToken;
	Require(!SharesCurrentEyeProducerScope(left, replacementScope),
		"A replacement outer scope retained a prior output claim");

	using MutateIdentity = void (*)(CurrentEyeIdentity&);
	constexpr std::array<MutateIdentity, 20> identityChanges{
		[](auto& value) { ++value.scopeToken; },
		[](auto& value) { ++value.submitFlags; },
		[](auto& value) { ++value.source.compositorCycle; },
		[](auto& value) { ++value.source.matchedOuterBoundaryToken; },
		[](auto& value) { ++value.source.submitFrame; },
		[](auto& value) { ++value.source.lastWorldRenderFrame; },
		[](auto& value) { ++value.source.lastCompletedWorldRenderFrame; },
		[](auto& value) { ++value.source.colorSource; },
		[](auto& value) { ++value.source.depthSource; },
		[](auto& value) { ++value.source.motionVectorSource; },
		[](auto& value) { ++value.source.method; },
		[](auto& value) { ++value.source.generation; },
		[](auto& value) { ++value.source.sourceWidth; },
		[](auto& value) { ++value.source.sourceHeight; },
		[](auto& value) { ++value.source.sourceMipLevels; },
		[](auto& value) { ++value.source.sourceArraySize; },
		[](auto& value) { ++value.source.sourceFormat; },
		[](auto& value) { ++value.source.sourceSampleCount; },
		[](auto& value) { ++value.source.colorSpace; },
		[](auto& value) { ++value.region.left; },
	};
	for (const auto mutate : identityChanges) {
		auto changed = left;
		mutate(changed);
		Require(!prepared.Matches(changed), "Changed current-eye identity reused stale prepared inputs");
	}
	Require(!ResolveCurrentEyeIdentity(fallbackAdmission, {}, 0, fallbackAdmission.eyes[0]).IsValid(),
		"Unscoped submit authorized current-eye caching");

	prepared.Invalidate(1);
	Require(!prepared.Matches(left) && prepared.Matches(right),
		"Failed left-eye replacement retained its old claim or discarded the untouched peer");
	Require(!prepared.foveatedRegionEncode[0] && prepared.foveatedRegionEncode[1],
		"Failed encode retained stale encode-mode metadata");
	prepared.Record(left, false);
	prepared.Invalidate(3);
	Require(!prepared.Matches(left) && !prepared.Matches(right),
		"Failed stereo replacement retained a prepared input claim");
	prepared.Record(left, false);
	prepared.Record(right, true);
	prepared = {};
	Require(!prepared.Matches(left) && !prepared.Matches(right),
		"Reset or device-loss retirement retained prepared inputs");
	Require(!MatchesProducerProof({}, proof), "Reset producer proof remained reusable");
	return EXIT_SUCCESS;
}
