#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

using std::uint32_t;
using std::uint64_t;
struct ID3D11Device {};
struct ID3D11DeviceContext {};
struct ID3D11Resource {};
struct ID3D11ShaderResourceView {};
namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* pointer = nullptr;
		com_ptr() = default;
		com_ptr(com_ptr&& other) noexcept : pointer(std::exchange(other.pointer, nullptr)) {}
		com_ptr& operator=(com_ptr&& other) noexcept
		{
			if (this != &other)
				pointer = std::exchange(other.pointer, nullptr);
			return *this;
		}
		void copy_from(T* value) { pointer = value; }
		explicit operator bool() const { return pointer != nullptr; }
	};
}
namespace globals
{
	struct State { uint32_t frameCount = 1; } stateStorage;
	State* state = &stateStorage;
}
namespace globals::d3d
{
	ID3D11Device deviceStorage;
	ID3D11DeviceContext contextStorage;
	ID3D11Device* device = &deviceStorage;
	ID3D11DeviceContext* context = &contextStorage;
}
namespace globals::game
{
	bool isVR = true;
}

namespace NeuralRendering
{
	struct RendererApplyArgs
	{
		ID3D11Device* device = globals::d3d::device;
		ID3D11DeviceContext* context = globals::d3d::context;
		ID3D11Resource* colorInput = nullptr;
		ID3D11Resource* depthGuide = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* colorOutput = nullptr;
		ID3D11ShaderResourceView* depthGuideSRV = nullptr;
		uint32_t featureSlot = 0, frameId = 1, sourceWorldFrame = 1;
		std::uint64_t generation = 1;
		InsertionPoint insertionPoint = InsertionPoint::UpscaledCenter;
		ComputeSubrect computeSubrect{};
		UpscalingDLSS::ViewportCrop viewportCrop{};
		bool modelRequired = false, pressureResolutionEnabled = false;
		bool characterVisualIsolation = false;
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
		bool startupDeferred = false;
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
		bool startupDeferred = false;
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
			outcome->startupDeferred = startupDeferred;
			outcome->evaluationAttemptedFeatureSlotMask |= 1u << args.featureSlot;
			if (startupDeferred)
				return false;
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
	struct Settings
	{
		uint64_t policy = 1;
		uint32_t neuralCharacterCropMode = 0;
	} settings;
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
	uint32_t finalizeCalls = 0;
	bool FinalizePreparedNeuralStereoInputs(Upscaling& upscaling, std::array<Upscaling::NeuralCenterDispatchResult, 2>&,
		std::array<NeuralRendering::RendererApplyArgs, 2>&, uint64_t* settingsKey = nullptr)
	{
		++finalizeCalls;
		if (settingsKey)
			*settingsKey = upscaling.settings.policy;
		return finalizeSucceeds;
	}
	uint32_t settingsKeyReads = 0;
	uint64_t BuildNeuralRenderingSettingsKey(const Upscaling::Settings& settings, bool includeCropMode)
	{
		++settingsKeyReads;
		return settings.policy + (includeCropMode ? settings.neuralCharacterCropMode : 0);
	}
#include "neural_stereo_output_targets_under_test.h"
#include "neural_finalized_dispatch_under_test.h"
#include "neural_empty_dispatch_under_test.h"
#include "neural_final_ldr_plan_under_test.h"
	void Require(bool value)
	{
		if (!value)
			throw std::runtime_error("NoWork reached the backend or changed pair delivery");
	}

	void CheckFinalizedFinalLdrPlans()
	{
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			const uint32_t all = vr ? 3u : 1u;
			for (uint32_t empty = 0; empty <= all; ++empty) {
				for (bool batched : { false, true }) {
					Upscaling upscaling;
					upscaling.arrangement = NeuralRendering::PipelineArrangement::DlssThenNeural;
					auto& backend = NeuralRendering::Renderer::Instance();
					backend = {};
					NeuralRendering::CharacterRendering::Instance() = {};
					std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
					std::array<NeuralRendering::RendererApplyArgs, 2> args{};
					ID3D11Resource originalResource, replacementResource;
					for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
						args[eye].featureSlot = eye;
						args[eye].insertionPoint = NeuralRendering::InsertionPoint::FinalLdrPreUi;
						args[eye].computeSubrect = { 4, 5, 6, 7 };
						args[eye].viewportCrop = UpscalingDLSS::ViewportCrop::Identity(16, 16, 16, 16);
						args[eye].colorInput = &originalResource;
						results[eye].prepared = true;
						results[eye].bypassed = (empty & (1u << eye)) != 0;
					}
					finalizeCalls = 0;
					auto plan = FinalizedFinalLdrNeuralInputs::Create(upscaling, results, args, batched, Upscaling::NeuralStereoRouteRole::Main);
					Require(plan.has_value() && finalizeCalls == 1);
					static_assert(std::is_const_v<std::remove_reference_t<decltype(plan->Args())>>);
					args[0].frameId = args[0].sourceWorldFrame = 99;
					args[0].generation = 99;
					args[0].computeSubrect = {};
					args[0].viewportCrop = {};
					args[0].colorInput = &replacementResource;
					upscaling.arrangement = NeuralRendering::PipelineArrangement::NeuralThenDlss;
					Require(plan->Args()[0].frameId == 1 && plan->Args()[0].sourceWorldFrame == 1 && plan->Args()[0].generation == 1 &&
						plan->Args()[0].computeSubrect == NeuralRendering::ComputeSubrect{ 4, 5, 6, 7 } && plan->Args()[0].viewportCrop.IsIdentity() &&
						plan->Args()[0].colorInput == &originalResource);
					Require(plan->WillEvaluate() == (empty != all));
					auto movedPlan = std::move(*plan);
					Require(!plan->Evaluate().pairApplied && backend.entries == 0 && upscaling.resets == 1);
					*plan = std::move(movedPlan);
					Require(!movedPlan.Evaluate().pairApplied && backend.entries == 0 && upscaling.resets == 2);
					const auto evaluation = plan->Evaluate();
					Require(finalizeCalls == 1 && evaluation.preparedEyeMask == all && evaluation.bypassedEyeMask == empty &&
						evaluation.pairBypassed == (empty == all) && evaluation.pairApplied == (empty != all));
					Require(upscaling.outputTargets == 0 && upscaling.composites == 0);
					Require(backend.admittedEyes == ((all & ~empty) & 1u) + (((all & ~empty) >> 1u) & 1u));
					const auto entries = backend.entries;
					(void)plan->Evaluate();
					Require(backend.entries == entries && upscaling.resets == 3 && finalizeCalls == 1);
				}
			}
		}
		globals::game::isVR = false;
		for (uint32_t changed = 0; changed < 5; ++changed) {
			Upscaling upscaling;
			upscaling.arrangement = NeuralRendering::PipelineArrangement::DlssThenNeural;
			NeuralRendering::Renderer::Instance() = {};
			std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
			std::array<NeuralRendering::RendererApplyArgs, 2> args{};
			args[0].insertionPoint = NeuralRendering::InsertionPoint::FinalLdrPreUi;
			args[0].characterVisualIsolation = true;
			results[0].prepared = true;
			finalizeCalls = 0;
			settingsKeyReads = 0;
			auto plan = FinalizedFinalLdrNeuralInputs::Create(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main);
			Require(plan.has_value() && settingsKeyReads == 0);
			switch (changed) {
			case 0: ++upscaling.settings.policy; break;
			case 1: ++globals::state->frameCount; break;
			case 2: results[0].prepared = false; break;
			case 3: results[0].bypassed = true; break;
			case 4: ++upscaling.settings.neuralCharacterCropMode; break;
			}
			Require(!plan->Evaluate().pairApplied && NeuralRendering::Renderer::Instance().entries == 0 &&
				upscaling.resets == 1 && finalizeCalls == 1);
			Require(settingsKeyReads == (changed == 1 ? 0u : 1u));
			globals::state->frameCount = 1;
		}
		Upscaling upscaling;
		std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
		std::array<NeuralRendering::RendererApplyArgs, 2> args{};
		args[0].insertionPoint = NeuralRendering::InsertionPoint::FinalLdrPreUi;
		finalizeCalls = 0;
		Require(!FinalizedFinalLdrNeuralInputs::Create(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main));
		Require(finalizeCalls == 0);
		results[0].prepared = true;
		finalizeSucceeds = false;
		Require(!FinalizedFinalLdrNeuralInputs::Create(upscaling, results, args, true, Upscaling::NeuralStereoRouteRole::Main));
		Require(finalizeCalls == 1 && args[0].frameId == 1);
		finalizeSucceeds = true;
	}
}

int main()
{
	CheckFinalizedFinalLdrPlans();
	for (bool vr : { false, true })
		for (bool batched : { false, true })
			for (bool deferred : { false, true }) {
				globals::game::isVR = vr;
				Upscaling upscaling;
				auto& backend = NeuralRendering::Renderer::Instance();
				backend = {};
				backend.succeeds = false;
				backend.startupDeferred = deferred;
				std::array<Upscaling::NeuralCenterDispatchResult, 2> results{};
				std::array<NeuralRendering::RendererApplyArgs, 2> args{};
				for (uint32_t eye = 0; eye < (vr ? 2u : 1u); ++eye) {
					results[eye].prepared = true;
					args[eye].featureSlot = eye;
				}
				const auto summary = EvaluatePreparedNeuralStereo(upscaling, results, args, batched, Upscaling::NeuralStereoRouteRole::Main);
				Require(!summary.pairApplied && summary.successfulEyeMask == 0 && upscaling.composites == 0);
				Require(upscaling.resets == (deferred ? 0u : 1u));
			}
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
