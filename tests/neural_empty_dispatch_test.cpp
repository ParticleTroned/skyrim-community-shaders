#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

using std::uint32_t;
namespace globals::game
{
	bool isVR = true;
}

namespace NeuralRendering
{
	struct RendererApplyArgs
	{
		uint32_t featureSlot = 0, frameId = 1, sourceWorldFrame = 1;
		std::uint64_t generation = 1;
		bool modelRequired = false, pressureResolutionEnabled = false;
		uint32_t modelResolutionPercent = 100;
		std::optional<int> modelOutputSelection;
	};
	struct ModelResolution
	{
		static bool Required(const RendererApplyArgs& args) { return args.modelRequired || args.modelResolutionPercent != 100; }
	};
	struct RendererApplyOutcome
	{
		bool reconstructionHistoryReset = false, spatialHistoryReset = false;
		uint32_t evaluationAttemptedFeatureSlotMask = 0, successful = 0, selectedOutputFeatureSlotMask = 0;
		bool WasEvaluationAttempted(uint32_t slot) const { return (evaluationAttemptedFeatureSlotMask & (1u << slot)) != 0; }
		bool WasEvaluationSuccessful(uint32_t slot) const { return (successful & (1u << slot)) != 0; }
		bool WasOutputSelected(uint32_t slot) const { return (selectedOutputFeatureSlotMask & (1u << slot)) != 0; }
	};
	struct Renderer
	{
		uint32_t admittedEyes = 0, entries = 0, ceiling = 100, ceilingReads = 0;
		uint32_t ModelResolutionCeiling()
		{
			++ceilingReads;
			return ceiling;
		}
		bool succeeds = true, spatialChange = false;
		uint32_t selectionCompletion = 0b11u;
		static Renderer& Instance()
		{
			static Renderer value;
			return value;
		}
		bool Apply(const RendererApplyArgs& args, RendererApplyOutcome* outcome)
		{
			++entries;
			++admittedEyes;
			outcome->spatialHistoryReset = spatialChange;
			outcome->evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			if (succeeds) {
				outcome->successful |= 1u << args.featureSlot;
				if (args.modelOutputSelection && (selectionCompletion & (1u << args.featureSlot)))
					outcome->selectedOutputFeatureSlotMask |= 1u << args.featureSlot;
			}
			return succeeds;
		}
		bool ApplyStereo(const std::array<RendererApplyArgs, 2>& args, RendererApplyOutcome* outcome)
		{
			const bool left = Apply(args[0], outcome);
			const bool right = Apply(args[1], outcome);
			return left && right;
		}
		bool ApplySequentialStereo(const std::array<RendererApplyArgs, 2>& args, RendererApplyOutcome* outcome)
		{
			return ApplyStereo(args, outcome);
		}
	};
	struct CharacterRendering
	{
		uint32_t bypassed = 0, evaluated = 0;
		static CharacterRendering& Instance()
		{
			static CharacterRendering value;
			return value;
		}
		void ResolveFeature18Disposition(uint32_t, uint32_t, std::uint64_t, uint32_t,
			uint32_t attempts, uint32_t, uint32_t empty)
		{
			bypassed |= empty;
			evaluated |= attempts;
		}
	};
}

struct Upscaling
{
	enum class NeuralStereoRouteRole
	{
		Main,
		Submit
	};
	enum class NeuralPhysicalPass
	{
		Feature18
	};
	struct NeuralCenterDispatchResult
	{
		bool prepared = false, bypassed = false, applied = false, attempted = false;
	};
	NeuralRendering::PipelineArrangement arrangement = NeuralRendering::PipelineArrangement::NeuralThenDlss;
	uint32_t resets = 0, composites = 0, outputTargets = 0;
	bool targetsSucceed = true;
	bool PrepareReducedResolutionNeuralOutputTarget(uint32_t, NeuralRendering::RendererApplyArgs& args, bool fuse)
	{
		++outputTargets;
		args.modelOutputSelection = fuse ? std::optional<int>{ 1 } : std::nullopt;
		return targetsSucceed;
	}
	void RequestHistoryReset() { ++resets; }
	auto GetNeuralRenderingArrangement() const { return arrangement; }
	bool PrepareReducedResolutionNeuralOutput(uint32_t, const NeuralRendering::RendererApplyArgs&)
	{
		++composites;
		return true;
	}
	void RecordNeuralPassTelemetry(NeuralStereoRouteRole, uint32_t, NeuralPhysicalPass, bool, bool, uint32_t) {}
};

namespace
{
	struct NeuralStereoEvaluationSummary
	{
		uint32_t preparedEyeMask = 0, successfulEyeMask = 0, bypassedEyeMask = 0;
		bool pairApplied = false, pairBypassed = false;
	};
#include "neural_eye_mask_under_test.h"

	bool finalizeSucceeds = true;
	bool FinalizePreparedNeuralStereoInputs(Upscaling&, std::array<Upscaling::NeuralCenterDispatchResult, 2>&,
		std::array<NeuralRendering::RendererApplyArgs, 2>&) { return finalizeSucceeds; }
#include "neural_empty_dispatch_under_test.h"
	void Require(bool value)
	{
		if (!value)
			throw std::runtime_error("NoWork reached the backend or changed pair delivery");
	}
}

int main()
{
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		const uint32_t all = vr ? 3u : 1u;
		for (const auto arrangement : { NeuralRendering::PipelineArrangement::DlssThenNeural,
				 NeuralRendering::PipelineArrangement::NeuralThenDlss }) {
			for (bool batched : { false, true }) {
				for (uint32_t empty = 0; empty <= all; ++empty) {
					for (bool success : { false, true }) {
						Upscaling upscaling;
						upscaling.arrangement = arrangement;
						auto& backend = NeuralRendering::Renderer::Instance();
						backend = {};
						backend.succeeds = success;
						NeuralRendering::CharacterRendering::Instance() = {};
						std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
						std::array<NeuralRendering::RendererApplyArgs, 2> args{};
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
							args[eye].featureSlot = eye;
							results[eye].prepared = true;
							results[eye].bypassed = (empty & (1u << eye)) != 0;
						}
						const auto summary = EvaluatePreparedNeuralStereo(upscaling, results, args, batched, Upscaling::NeuralStereoRouteRole::Main);
						const auto required = all & ~empty;
						Require(backend.admittedEyes == (required & 1u) + ((required >> 1u) & 1u));
						Require(summary.pairBypassed == (required == 0));
						Require(summary.pairApplied == (required != 0 && success));
						Require(summary.bypassedEyeMask == empty);
						Require(upscaling.outputTargets == (NeuralRendering::RunsBeforeDlss(arrangement) ? backend.admittedEyes : 0u));
						Require(upscaling.composites == (success && NeuralRendering::RunsBeforeDlss(arrangement) ? backend.admittedEyes : 0u));
						Require(NeuralRendering::CharacterRendering::Instance().evaluated == required);
						if (required == 0)
							Require(backend.entries == 0 && upscaling.composites == 0 && upscaling.resets == 0);
						finalizeSucceeds = false;
						backend.entries = 0;
						(void)EvaluatePreparedNeuralStereo(upscaling, results, args, batched, Upscaling::NeuralStereoRouteRole::Main);
						Require(backend.entries == 0);
						finalizeSucceeds = true;
					}
				}
			}
		}
	}
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		const uint32_t all = vr ? 3u : 1u;
		for (uint32_t empty = 0; empty <= all; ++empty) {
			for (uint32_t modelRequired = 0; modelRequired <= all; ++modelRequired) {
				for (uint32_t selected = 0; selected <= all; ++selected) {
					for (bool success : { false, true }) {
						Upscaling upscaling;
						auto& backend = NeuralRendering::Renderer::Instance();
						backend = {};
						backend.succeeds = success;
						backend.selectionCompletion = selected;
						NeuralRendering::CharacterRendering::Instance() = {};
						std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
						std::array<NeuralRendering::RendererApplyArgs, 2> args{};
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
							args[eye].featureSlot = eye;
							args[eye].modelRequired = (modelRequired & (1u << eye)) != 0;
							results[eye].prepared = true;
							results[eye].bypassed = (empty & (1u << eye)) != 0;
						}
						const uint32_t required = all & ~empty;
						const bool fused = (required & modelRequired) != 0;
						const bool complete = !fused || (selected & required) == required;
						const auto summary = EvaluatePreparedNeuralStereo(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main);
						Require(summary.pairApplied == (required != 0 && success && complete));
						Require(summary.successfulEyeMask == (summary.pairApplied ? required : 0u));
						Require(upscaling.composites == (success && !fused ? backend.admittedEyes : 0u));
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye)
							Require(args[eye].modelOutputSelection.has_value() == (fused && (required & (1u << eye)) != 0));
						if (required) {
							upscaling.targetsSucceed = false;
							backend.entries = 0;
							(void)EvaluatePreparedNeuralStereo(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main);
							Require(backend.entries == 0);
						}
					}
				}
			}
		}
	}
	for (const bool vr : { false, true }) {
		globals::game::isVR = vr;
		const uint32_t all = vr ? 3u : 1u;
		for (const uint32_t empty : { 0u, 1u, 2u, 3u }) {
			if (empty > all)
				continue;
			for (const bool pressure : { false, true })
				for (const uint32_t ceiling : { 90u, 100u })
					for (const uint32_t requested : { 75u, 100u }) {
						Upscaling upscaling;
						auto& backend = NeuralRendering::Renderer::Instance();
						backend = {};
						backend.ceiling = ceiling;
						std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
						std::array<NeuralRendering::RendererApplyArgs, 2> args{};
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
							args[eye].featureSlot = eye;
							args[eye].pressureResolutionEnabled = pressure;
							args[eye].modelResolutionPercent = requested;
							results[eye].prepared = true;
							results[eye].bypassed = (empty & (1u << eye)) != 0;
						}
						const uint32_t required = all & ~empty;
						const bool fused = requested < 100 || (pressure && ceiling < 100);
						const auto summary = EvaluatePreparedNeuralStereo(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main);
						Require(summary.pairApplied == (required != 0));
						Require(upscaling.composites == (fused ? 0u : backend.admittedEyes));
						Require(backend.ceilingReads == (required && pressure && requested == 100 ? 1u : 0u));
						for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
							Require(args[eye].modelResolutionPercent == requested);
							Require(args[eye].modelOutputSelection.has_value() == (fused && (required & (1u << eye)) != 0));
						}
					}
		}
	}
	for (bool batched : { false, true }) {
		globals::game::isVR = true;
		Upscaling upscaling;
		auto& backend = NeuralRendering::Renderer::Instance();
		backend = {};
		backend.spatialChange = true;
		std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
		std::array<NeuralRendering::RendererApplyArgs, 2> args{};
		for (uint32_t eye = 0; eye < 2; ++eye) {
			results[eye].prepared = true;
			args[eye].featureSlot = eye;
		}
		(void)EvaluatePreparedNeuralStereo(upscaling, results, args, batched, Upscaling::NeuralStereoRouteRole::Main);
		Require(upscaling.resets == 1);
	}
}
