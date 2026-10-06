#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <array>
#include <cstdint>
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
	};
	struct RendererApplyOutcome
	{
		uint32_t evaluationAttemptedFeatureSlotMask = 0, successful = 0;
		bool WasEvaluationAttempted(uint32_t slot) const { return (evaluationAttemptedFeatureSlotMask & (1u << slot)) != 0; }
		bool WasEvaluationSuccessful(uint32_t slot) const { return (successful & (1u << slot)) != 0; }
	};
	struct Renderer
	{
		uint32_t admittedEyes = 0, entries = 0;
		bool succeeds = true;
		static Renderer& Instance()
		{
			static Renderer value;
			return value;
		}
		bool Apply(const RendererApplyArgs& args, RendererApplyOutcome* outcome)
		{
			++entries;
			++admittedEyes;
			outcome->evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			if (succeeds)
				outcome->successful |= 1u << args.featureSlot;
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
	uint32_t resets = 0, composites = 0;
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
}
