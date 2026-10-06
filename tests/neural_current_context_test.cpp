#include "Features/Upscaling/NeuralRendering/CurrentContextExperiment.h"

#include <cstdlib>

namespace
{
	void Check(bool condition)
	{
		if (!condition)
			std::abort();
	}
}

int main()
{
	using namespace NeuralRendering;
	const UpscalingDLSS::Extent capacity{ 1009, 1121 };
	const ComputeSubrect support{ 273, 381, 99, 117 };
	const ComputeSubrect retained{ 0, 0, 1009, 1121 };
	const auto baseline = BuildRoiDescriptor(support, retained, capacity, true);
	CharacterSettings policy;
	policy.enabled = true;
	CharacterCategoryFramePolicy framePolicy;
	Check(framePolicy.Resolve(10, policy) == policy);
	auto changedExperiments = policy;
	changedExperiments.experimentalCurrentContext = true;
	changedExperiments.experimentalGpuMaskSupport = true;
	Check(framePolicy.Resolve(10, changedExperiments) == policy);
	Check(framePolicy.Resolve(11, changedExperiments) == changedExperiments);
	const auto unchanged = [&](const CharacterSettings& settings, bool jittered, bool proven) {
		auto roi = baseline;
		Check(!ApplyCurrentContextExperiment(roi, settings, jittered, proven));
		Check(roi == baseline);
	};
	unchanged(policy, true, true);
	policy.experimentalCurrentContext = true;
	unchanged(policy, false, true);
	unchanged(policy, true, false);
	for (const auto mode : { CharacterMaskTestMode::ForceZero, CharacterMaskTestMode::ForceOne,
			 CharacterMaskTestMode::ForceHalf, CharacterMaskTestMode::InvertAuthored,
			 CharacterMaskTestMode::AuthoredWithoutVisibilityDepth }) {
		auto diagnostic = policy;
		diagnostic.maskTestMode = mode;
		unchanged(diagnostic, true, true);
	}
	auto disabled = policy;
	disabled.enabled = false;
	unchanged(disabled, true, true);
	auto debug = policy;
	debug.debugView = CharacterDebugView::CharacterMask;
	unchanged(debug, true, true);

	auto current = baseline;
	Check(ApplyCurrentContextExperiment(current, policy, true, true));
	Check(current.currentContextApplied);
	Check(current.inferenceContext == BuildCharacterProviderComputeSubrect(support, capacity.width, capacity.height));
	Check(current.inferenceContext == current.ownedOutput);
	Check(current.inferenceContext.Area() < retained.Area());
	Check(current.samplingSupport == baseline.samplingSupport);
	Check(current.temporalEnvelope == baseline.temporalEnvelope && current.allocationCapacity == capacity);
	Check(GetRoiDescriptorViolation(current, current.inferenceContext, capacity).empty());
	Check(ContainsComputeSubrect(current.inferenceContext, support));
	Check(current.inferenceContext != support);
	auto unauthorized = current;
	unauthorized.currentContextApplied = false;
	Check(!GetRoiDescriptorViolation(unauthorized, unauthorized.inferenceContext, capacity).empty());
	auto invalid = current;
	invalid.temporalEnvelope = support;
	Check(!GetRoiDescriptorViolation(invalid, invalid.inferenceContext, capacity).empty());
	invalid = current;
	invalid.samplingSupport.reset();
	Check(!GetRoiDescriptorViolation(invalid, invalid.inferenceContext, capacity).empty());

	for (const auto missing : { BuildRoiDescriptor(std::nullopt, retained, capacity, true),
			 BuildRoiDescriptor(support, retained, capacity, false), RoiDescriptor{} }) {
		auto roi = missing;
		Check(!ApplyCurrentContextExperiment(roi, policy, true, true));
		Check(roi == missing);
	}
	// Current spatial padding must fit the retained proof, or baseline work stays intact.
	auto narrow = BuildRoiDescriptor(support, support, capacity, true);
	const auto narrowBefore = narrow;
	Check(!ApplyCurrentContextExperiment(narrow, policy, true, true) && narrow == narrowBefore);
	for (const ComputeSubrect edge : { ComputeSubrect{ 0, 0, 13, 19 }, ComputeSubrect{ 980, 1100, 29, 21 } }) {
		auto roi = BuildRoiDescriptor(edge, retained, capacity, true);
		const auto before = roi;
		Check(!ApplyCurrentContextExperiment(roi, policy, true, true));
		Check(roi == before);
	}
	// A clipped strip must retain its envelope, including at asymmetric eye edges.
	for (const ComputeSubrect strip : { ComputeSubrect{ 0, 416, 1, 640 }, ComputeSubrect{ 977, 416, 32, 640 },
			 ComputeSubrect{ 416, 0, 512, 1 }, ComputeSubrect{ 416, 1105, 512, 16 } }) {
		auto roi = BuildRoiDescriptor(strip, retained, capacity, true);
		const auto before = roi;
		Check(!ApplyCurrentContextExperiment(roi, policy, true, true));
		Check(roi == before);
	}
	static_assert(!QualifiedExperimentalContextGeometry(ComputeSubrect{ 0, 384, 64, 736 }));
	static_assert(!QualifiedExperimentalContextGeometry(ComputeSubrect{ 0, 0, 127, 1024 }));
	static_assert(!QualifiedExperimentalContextGeometry(ComputeSubrect{ 0, 0, 1024, 127 }));
	static_assert(QualifiedExperimentalContextGeometry(ComputeSubrect{ 0, 0, 128, 128 }));
	const UpscalingDLSS::Extent observedCapacity{ 1008, 1120 };
	const ComputeSubrect clippedSupport{ 0, 480, 1, 544 };
	Check(BuildCharacterProviderComputeSubrect(clippedSupport, 1008, 1120) == ComputeSubrect{ 0, 384, 64, 736 });
	auto clipped = BuildRoiDescriptor(clippedSupport, { 0, 0, 1008, 1120 }, observedCapacity, true);
	const auto clippedBefore = clipped;
	Check(!ApplyCurrentContextExperiment(clipped, policy, true, true) && clipped == clippedBefore);

	StableCharacterComputeSubrect history;
	(void)ResolveStableCharacterComputeSubrect(retained, capacity.width, capacity.height, history);
	const auto temporal = ResolveStableCharacterComputeSubrect(support, capacity.width, capacity.height, history);
	const auto count = history.recentCount;
	const auto cursor = history.recentCursor;
	const auto cooldown = history.framesSinceContraction;
	for (unsigned repeat = 0; repeat < 8; ++repeat) {
		auto roi = BuildRoiDescriptor(support, temporal, capacity, true);
		Check(ApplyCurrentContextExperiment(roi, policy, true, true));
		Check(roi == current);
		Check(history.recentCount == count && history.recentCursor == cursor && history.framesSinceContraction == cooldown);
	}
}
