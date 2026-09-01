#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <string_view>

int main()
{
	using NeuralRendering::PipelineArrangement;
	static_assert(
		NeuralRendering::kPipelineArrangement == PipelineArrangement::NeuralThenDlss,
		"paint must remain the Neural-then-DLSS experiment");

	static_assert(NeuralRendering::RunsAfterDlss(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::RunsBeforeDlss(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::ReplacesDlss(PipelineArrangement::DlssThenNeural));
	static_assert(NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::DlssThenNeural));

	static_assert(NeuralRendering::RunsBeforeDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::RunsAfterDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::ReplacesDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::NeuralThenDlss));

	static_assert(!NeuralRendering::RunsBeforeDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(!NeuralRendering::RunsAfterDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::ReplacesDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::NeuralReplacesDlss));

	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::DlssThenNeural)) == "dlss_then_neural");
	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::NeuralThenDlss)) == "neural_then_dlss");
	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::NeuralReplacesDlss)) == "neural_replaces_dlss");

	static_assert(NeuralRendering::IsOrderedStereoFeatureSlotPair(0u, 1u));
	static_assert(NeuralRendering::IsOrderedStereoFeatureSlotPair(2u, 3u));
	static_assert(!NeuralRendering::IsOrderedStereoFeatureSlotPair(1u, 0u));
	static_assert(!NeuralRendering::IsOrderedStereoFeatureSlotPair(0u, 2u));

	using NeuralRendering::CachedStereoPairReuse;
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, true, 0u, 0u) ==
		CachedStereoPairReuse::Reuse);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, true, 0b01u, 0u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b01u, 1u) ==
		CachedStereoPairReuse::CompleteLatchedPair);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b10u, 0u) ==
		CachedStereoPairReuse::CompleteLatchedPair);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, false, 0b01u, 1u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, false, 0u, 0u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0u, 0u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b01u, 0u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b10u, 1u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b11u, 0u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(NeuralRendering::IsSequentialFrame(42u, 43u));
	static_assert(!NeuralRendering::IsSequentialFrame(42u, 44u));

	constexpr auto perEyeStaged =
		NeuralRendering::ParseImplementationName("per_eye_staged");
	constexpr auto batchedStaged =
		NeuralRendering::ParseImplementationName("batched_staged");
	constexpr auto perEyeDirect =
		NeuralRendering::ParseImplementationName("per_eye_direct");
	constexpr auto batchedDirect =
		NeuralRendering::ParseImplementationName("batched_direct");
	static_assert(
		perEyeStaged && !perEyeStaged->batchedStereo &&
		!perEyeStaged->directCommit);
	static_assert(
		batchedStaged && batchedStaged->batchedStereo &&
		!batchedStaged->directCommit);
	static_assert(
		perEyeDirect && !perEyeDirect->batchedStereo &&
		perEyeDirect->directCommit);
	static_assert(
		batchedDirect && batchedDirect->batchedStereo &&
		batchedDirect->directCommit);
	static_assert(!NeuralRendering::ParseImplementationName("unknown"));
	return 0;
}
