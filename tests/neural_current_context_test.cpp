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
	auto multi = policy;
	multi.multiRoi = true;
	unchanged(multi, true, true);
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
		Check(ApplyCurrentContextExperiment(roi, policy, true, true));
		Check(roi.inferenceContext.Fits(capacity.width, capacity.height));
		Check(ContainsComputeSubrect(roi.inferenceContext, edge));
	}

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
