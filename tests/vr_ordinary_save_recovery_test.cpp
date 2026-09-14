#include "Features/Upscaling/VROrdinarySaveRecovery.h"
#include "Features/Upscaling/VRSubmitTemporalSnapshot.h"

#include <cstdio>
#include <initializer_list>

int main()
{
	using namespace VROrdinarySaveRecovery;
	int failures = 0;
	auto check = [&](bool value, const char* message) {
		if (!value) {
			std::fprintf(stderr, "%s\n", message);
			++failures;
		}
	};
	const Identity original{ 9, 13, 4, 3 };
	const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity boundary{ 21, 21, 0 };
	Proof proof;
	auto pair = [&](uint32_t frame, Identity identity = Identity{ 9, 13, 4, 3 }) {
		proof.Observe(identity, frame, frame, 0, boundary, true);
		proof.Observe(identity, frame, frame, 1, boundary, true);
	};
	for (uint32_t frame = 10; frame < 16; ++frame) {
		pair(frame);
		check(!proof.CanResume(original, frame, frame), "Cannot release within the qualifying stereo pair");
	}
	check(proof.CanResume(original, 15, 16), "Next cycle can resume before the next world render completes");
	check(proof.CanResume(original, 16, 16), "An unchanged contract resumes after six qualified frames");
	check(!proof.CanResume(original, 16, 15), "Desktop Present cannot release either eye in the qualifying cycle");
	check(!proof.CanResume(Identity{ 10, 13, 4, 3 }, 16, 16), "A newer save cannot reuse older proof");
	check(!proof.CanResume(Identity{ 9, 13, 5, 3 }, 16, 16), "A replacement generation cannot reuse proof");
	check(!proof.CanResume(original, 17, 17), "A missing next frame revokes presentation readiness");
	pair(16);
	check(proof.CanResume(original, 17, 17), "Continued coherent frames retain readiness");

	proof.Reset();
	for (uint32_t frame = 10; frame < 150; ++frame) {
		proof.Observe(original, frame, frame, 0, boundary, true);
		proof.Observe(original, frame, frame, 0, boundary, true);
	}
	check(!proof.CanResume(original, 150, 150), "Neither elapsed time nor duplicate eyes authorize release");
	check(proof.stableFrames == 0, "Missing peer eyes never count as stereo frames");

	proof.Reset();
	for (uint32_t frame = 10; frame < 16; ++frame)
		pair(frame);
	proof.Observe(original, 16, 16, 0, boundary, false);
	check(!proof.CanResume(original, 16, 16), "A resource or input failure immediately revokes readiness");
	pair(17);
	check(proof.stableFrames == 1 && !proof.CanResume(original, 18, 18), "Failure restarts the full proof");

	proof.Reset();
	for (uint32_t frame = 10; frame < 16; ++frame)
		pair(frame);
	// An unknown first-eye source exits before observation and resets admission.
	proof.Reset();
	proof.Observe(original, 16, 16, 1, boundary, true);
	check(!proof.CanResume(original, 16, 16) && proof.stableFrames == 0,
		"A peer eye cannot retain qualification after an earlier submit was rejected");
	pair(17);
	check(proof.stableFrames == 1 && !proof.qualifiedCycle,
		"Early submit failure requires six new complete stereo frames");

	proof.Reset();
	VRSubmitTemporalSnapshot::Snapshot<uint32_t> temporal;
	const VRSubmitTemporalSnapshot::Scalars scalars{
		.cameraNear = 1.0f,
		.cameraFar = 1000.0f,
		.verticalFov = 1.0f,
		.frameTimeMilliseconds = 11.0f
	};
	for (uint32_t frame = 10; frame < 16; ++frame) {
		const VRSubmitTemporalSnapshot::Key producer{
			.frame = frame,
			.generation = original.generation,
			.method = original.method,
			.inputWidth = 1000,
			.inputHeight = 1200,
			.outputWidth = 1500,
			.outputHeight = 1800,
			.compositorCycle = frame + 100u
		};
		check(temporal.Publish(producer, scalars, { 1, 2 }), "The next producer publishes one immutable stereo snapshot");
		auto dispatch = producer;
		++dispatch.frame;
		check(!temporal.Matches(dispatch) && temporal.MatchesForDispatch(dispatch),
			"Desktop Present frame skew retains the exact compositor producer and resource contract");
		for (uint32_t eye = 0; eye < 2; ++eye)
			proof.Observe(original, temporal.key.frame, dispatch.compositorCycle, eye,
				boundary, temporal.MatchesForDispatch(dispatch));
		check(!proof.CanResume(original, producer.frame, dispatch.compositorCycle),
			"Desktop frame advancement cannot release within the qualifying compositor cycle");
		++dispatch.generation;
		check(!temporal.MatchesForDispatch(dispatch), "Desktop frame skew cannot admit a different resource generation");
		dispatch = producer;
		++dispatch.compositorCycle;
		check(!temporal.MatchesForDispatch(dispatch), "A different compositor cycle cannot reuse the producer snapshot");
	}
	check(proof.qualifiedFrame == 15 && proof.qualifiedCycle == 115 && proof.CanResume(original, 15, 116),
		"Six producer frames qualify for the following cycle despite desktop Present skew");

	for (const Identity changed : { Identity{ 10, 13, 4, 3 }, Identity{ 9, 14, 4, 3 },
			 Identity{ 9, 13, 5, 3 }, Identity{ 9, 13, 4, 2 },
			 Identity{ 9, 13, 4, 3, 1, 0 }, Identity{ 9, 13, 4, 3, 0, 1 } }) {
		proof.Reset();
		for (uint32_t frame = 10; frame < 16; ++frame)
			pair(frame);
		check(!proof.CanResume(changed, 16, 16), "Every resource identity field is rechecked before release");
		pair(16, changed);
		check(proof.stableFrames == 1 && !proof.qualifiedFrame, "Identity changes restart settling");
	}

	proof.Reset();
	pair(10);
	pair(11);
	pair(13);
	check(proof.stableFrames == 1 && proof.firstFrame == 13, "Gaps restart consecutive-frame proof");
	proof.Observe(original, 14, 28, 0, boundary, true);
	proof.Observe(original, 14, 29, 1, boundary, true);
	check(proof.stableFrames == 0, "Eyes from different compositor cycles cannot pair");

	proof.Reset();
	for (uint32_t frame = 10; frame < 16; ++frame) {
		proof.Observe(original, frame, frame, 1, boundary, true);
		proof.Observe(original, frame, frame, 0, boundary, true);
	}
	check(proof.CanResume(original, 16, 16), "Either native eye order is supported");
	check(proof.stableFrames == kRequiredStereoFrames, "Diagnostics retain every complete qualification frame");
	proof.Observe({}, 16, 16, 0, boundary, true);
	check(!proof.CanResume(original, 16, 16), "An unknown or load-owned save token revokes readiness");

	for (const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity changedBoundary : {
			 VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity{ 22, 22, 0 },
			 VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity{ 21, 21, 1 } }) {
		proof.Reset();
		for (uint32_t frame = 10; frame < 16; ++frame) {
			proof.Observe(original, frame, frame, 0, boundary, true);
			proof.Observe(original, frame, frame, 1, changedBoundary, true);
		}
		check(!proof.qualifiedCycle && !proof.CanResume(original, 16, 16),
			"Different producer scopes or submit flags cannot form stereo proof within one compositor cycle");
	}

	proof.Reset();
	for (uint32_t frame = 10; frame < 16; ++frame) {
		// Split-eye textures retain one correlated scope without a shared color resource.
		const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity splitBoundary{ 0, frame + 1000, 0 };
		proof.Observe(original, frame, frame, 0, splitBoundary, true);
		proof.Observe(original, frame, frame, 1, splitBoundary, true);
	}
	check(proof.CanResume(original, 16, 16), "A new coherent split-eye source scope each frame qualifies");
	proof.Observe(original, 16, 16, 0, {}, true);
	check(!proof.CanResume(original, 16, 16), "An uncorrelated submit revokes earlier proof");
	std::printf("Ordinary-save stereo recovery: %d failures\n", failures);
	return failures ? 1 : 0;
}
