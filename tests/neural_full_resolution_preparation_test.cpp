#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

struct State
{
	uint32_t frameCount = 1;
};
namespace globals
{
	State* state = nullptr;
}

struct Upscaling
{
	enum class NeuralStereoRouteRole
	{
		Main
	};
	enum class NeuralStereoPairDisposition
	{
		Unknown,
		NormalDLSSPair
	};
	enum class NeuralStereoFallbackReason
	{
		None,
		RouteIneligible,
		MenuContext,
		GamePaused,
		TemporalSourceStale,
		FrameGeneration,
		StereoPreflightFailed
	};
	struct Settings
	{
		uint32_t frameGenerationMode = 0;
		uint64_t key = 12;
	} settings;
	struct NeuralStereoRouteSnapshot
	{
		bool valid = false;
		NeuralStereoRouteRole role = NeuralStereoRouteRole::Main;
		uint32_t frame = 0;
		uint64_t generation = 0;
		uint32_t arrangement = 0;
		uint32_t insertionPoint = 0;
		bool requested = false, eligible = false, sourceBatchEligible = false, sourceSignatureProven = false;
		bool frameGenerationActive = false, frameGenerationGatePassed = false;
		bool hardMenuBlocked = false, menuContinuityAllowed = false;
		bool pairComplete = false;
		NeuralRendering::TemporalAdmissionResult temporalAdmission{};
		NeuralStereoPairDisposition disposition = NeuralStereoPairDisposition::Unknown;
		NeuralStereoFallbackReason fallbackReason = NeuralStereoFallbackReason::None;
		uint32_t preparedEyeMask = 0, attemptedEyeMask = 0, appliedEyeMask = 0, bypassedEyeMask = 0, committedEyeMask = 0;
	};
	struct MainFinalLdrNeuralState
	{
		bool ready = false;
		uint32_t frame = std::numeric_limits<uint32_t>::max();
		uint32_t sourceWorldFrame = std::numeric_limits<uint32_t>::max();
		uint64_t generation = 0, settingsKey = 0;
		uint32_t inputWidthPerEye = 0, inputHeight = 0, outputWidthPerEye = 0, outputHeight = 0;
		NeuralStereoRouteSnapshot route{};
	} mainFinalLdrNeuralState;
	uint32_t mainFullResolutionNeuralPreparationFrame = std::numeric_limits<uint32_t>::max();
	uint64_t vrDLSSRuntimeResourceGeneration = 5;
	NeuralRendering::RenderingMode mode = NeuralRendering::RenderingMode::FullResolution;
	bool requested = true, presentation = false, hardMenu = false, transition = false, frameGeneration = false;
	bool dimensionsAvailable = true, guidePreparationSucceeds = true, guidePreparationThrows = false;
	bool guidePreparationThrowsUnknown = false;
	uint32_t guideCopies = 0, captureFrames = 0, resets = 0, publications = 0;
	NeuralStereoRouteSnapshot publishedRoute{};
	uint32_t width = 1200, height = 900;
	NeuralRendering::TemporalAdmissionInputs world{
		.worldFrameStateAvailable = true,
		.currentFrame = 1,
		.lastWorldRenderFrame = 1,
		.lastCompletedWorldRenderFrame = 1
	};
	bool IsNeuralRenderingRequested() const { return requested; }
	auto GetNeuralRenderingMode() const { return mode; }
	auto GetNeuralRenderingArrangement() const { return NeuralRendering::ResolvePipelineArrangement(mode); }
	bool IsPresentationUpscalingActive() const { return presentation; }
	bool IsNeuralRenderingInsertionTransitionBlocked() const { return transition; }
	bool IsFrameGenerationDx12PathActive() const { return frameGeneration; }
	auto BuildNeuralTemporalAdmission(NeuralStereoRouteRole, bool blocked, bool continuity) const
	{
		auto inputs = world;
		inputs.menuContextActive = blocked;
		inputs.pausedContinuityAllowed = continuity;
		return NeuralRendering::EvaluateTemporalAdmission(NeuralRendering::TemporalRoute::Main, inputs);
	}
	bool GetRuntimeFoveatedRegionDimensions(uint32_t& iw, uint32_t& ih, uint32_t& ow, uint32_t& oh) const
	{
		iw = width / 2;
		ih = height / 2;
		ow = width;
		oh = height;
		return dimensionsAvailable;
	}
	bool PrepareFullResolutionNeuralInputs(uint32_t, uint32_t, uint32_t, uint32_t, NeuralStereoRouteRole, uint32_t, uint64_t)
	{
		++guideCopies;
		if (guidePreparationThrows)
			throw std::runtime_error("guide allocation failed");
		if (guidePreparationThrowsUnknown)
			throw 1;
		return guidePreparationSucceeds;
	}
	void BeginNeuralCaptureFrame(NeuralStereoRouteRole, uint32_t) { ++captureFrames; }
	void PublishNeuralStereoRouteSnapshot(const NeuralStereoRouteSnapshot& route) noexcept
	{
		++publications;
		publishedRoute = route;
	}
	static NeuralStereoFallbackReason GetNeuralTemporalFallbackReason(const NeuralRendering::TemporalAdmissionResult&) noexcept;
	void RequestHistoryReset() { ++resets; }
	void PrepareMainFullResolutionNeuralFrame() noexcept;
};

bool IsNeuralRenderingHardMenuBlocked(const Upscaling& upscaling, const State*) { return upscaling.hardMenu; }
uint64_t BuildNeuralRenderingSettingsKey(const Upscaling::Settings& settings) { return settings.key; }
void LogWarnOnce(bool&, const char*, const std::exception&) {}

#include "neural_full_resolution_preparation_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void NextFrame(Upscaling& upscaling, State& state)
	{
		++state.frameCount;
		upscaling.world.currentFrame = state.frameCount;
		upscaling.world.lastWorldRenderFrame = state.frameCount;
		upscaling.world.lastCompletedWorldRenderFrame = state.frameCount;
	}
	void RequireNoAttemptFallback(const Upscaling& upscaling, const State& state, Upscaling::NeuralStereoFallbackReason reason)
	{
		const auto& route = upscaling.publishedRoute;
		Require(route.valid && route.role == Upscaling::NeuralStereoRouteRole::Main && route.frame == state.frameCount &&
					route.generation == std::max<uint64_t>(upscaling.vrDLSSRuntimeResourceGeneration, 1u) &&
					route.arrangement == static_cast<uint32_t>(upscaling.GetNeuralRenderingArrangement()) &&
					route.insertionPoint == static_cast<uint32_t>(NeuralRendering::InsertionPoint::FinalLdrPreUi),
			"Preparation fallback must carry the current route identity");
		Require(route.requested && !route.eligible && !route.sourceBatchEligible && !route.sourceSignatureProven &&
					!route.pairComplete && route.preparedEyeMask == 0 && route.attemptedEyeMask == 0 && route.appliedEyeMask == 0 &&
					route.bypassedEyeMask == 0 && route.committedEyeMask == 0 &&
					route.disposition == Upscaling::NeuralStereoPairDisposition::NormalDLSSPair && route.fallbackReason == reason,
			"Failed preparation must report its reason without claiming an NR attempt, bypass or commit");
		Require(route.temporalAdmission.currentFrame == state.frameCount && route.hardMenuBlocked == upscaling.hardMenu &&
					route.menuContinuityAllowed == !upscaling.hardMenu &&
					route.frameGenerationActive == (upscaling.frameGeneration || upscaling.settings.frameGenerationMode != 0) &&
					route.frameGenerationGatePassed == !route.frameGenerationActive,
			"Preparation fallback must preserve the current admission and frame-generation evidence");
	}
}

int main()
{
	try {
		State state;
		globals::state = &state;
		Upscaling upscaling;
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == 1, "Fresh scene must prepare");
		const auto& first = upscaling.mainFinalLdrNeuralState;
		Require(first.frame == 1 && first.sourceWorldFrame == 1 && first.generation == 5 &&
					first.settingsKey == 12 && first.inputWidthPerEye == 600 && first.outputWidthPerEye == 1200,
			"Prepared identity must match the captured scene");
		Require(upscaling.publications == 0, "Successful preparation must await the actual final-LDR transaction");
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.guideCopies == 1, "Repeated producer hook must reuse preparation");
		upscaling.mainFinalLdrNeuralState = {};
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == 1,
			"Consumed frame must never prepare a framebuffer feedback pass");

		upscaling.requested = false;
		upscaling.PrepareMainFullResolutionNeuralFrame();
		upscaling.requested = true;
		++upscaling.settings.key;
		++upscaling.vrDLSSRuntimeResourceGeneration;
		upscaling.width = 1600;
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.guideCopies == 1, "Same-frame enable, resize or generation change must not retry");
		NextFrame(upscaling, state);
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == 2 &&
					upscaling.mainFinalLdrNeuralState.outputWidthPerEye == 1600 && upscaling.mainFinalLdrNeuralState.generation == 6,
			"Next fresh frame must capture the changed configuration");

		for (int failure = 0; failure < 4; ++failure) {
			NextFrame(upscaling, state);
			upscaling.dimensionsAvailable = failure != 0;
			upscaling.guidePreparationSucceeds = failure != 1;
			upscaling.guidePreparationThrows = failure == 2;
			upscaling.guidePreparationThrowsUnknown = failure == 3;
			const auto publications = upscaling.publications;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			Require(upscaling.publications == publications + 1, "Each failed preparation must publish once");
			RequireNoAttemptFallback(upscaling, state, Upscaling::NeuralStereoFallbackReason::StereoPreflightFailed);
			const auto copies = upscaling.guideCopies;
			upscaling.dimensionsAvailable = upscaling.guidePreparationSucceeds = true;
			upscaling.guidePreparationThrows = false;
			upscaling.guidePreparationThrowsUnknown = false;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == copies,
				"Failed preparation must wait for another fresh frame");
			Require(upscaling.publications == publications + 1, "Same-frame retry must not publish another route");
		}
		Require(upscaling.resets == 2, "Only preparation exceptions must reset history");

		NextFrame(upscaling, state);
		upscaling.world.gamePaused = true;
		--upscaling.world.lastWorldRenderFrame;
		--upscaling.world.lastCompletedWorldRenderFrame;
		const auto retained = upscaling.BuildNeuralTemporalAdmission(Upscaling::NeuralStereoRouteRole::Main, false, true);
		Require(retained.admitted && retained.retainedWorldFrame, "Fixture must exercise admitted retained-world continuity");
		const auto copies = upscaling.guideCopies;
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == copies,
			"Retained framebuffer must not become new scene input");
		RequireNoAttemptFallback(upscaling, state, Upscaling::NeuralStereoFallbackReason::TemporalSourceStale);
		Require(upscaling.publishedRoute.temporalAdmission.admitted &&
					upscaling.publishedRoute.temporalAdmission.sourceWorldFrame == state.frameCount - 1,
			"Retained-world refusal must preserve its actual source frame and admission");
		NextFrame(upscaling, state);
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.mainFinalLdrNeuralState.ready, "Paused fresh world remains valid");

		for (int gate = 0; gate < 8; ++gate) {
			NextFrame(upscaling, state);
			upscaling.mainFinalLdrNeuralState = {};
			upscaling.requested = gate != 0;
			upscaling.presentation = gate == 1;
			upscaling.mode = gate == 2 ? NeuralRendering::RenderingMode::Foveated : NeuralRendering::RenderingMode::FullResolution;
			upscaling.transition = gate == 3;
			upscaling.frameGeneration = gate == 4;
			upscaling.hardMenu = gate == 5;
			upscaling.settings.frameGenerationMode = gate == 6 ? 1u : 0u;
			upscaling.world.worldFrameStateAvailable = gate != 7;
			const auto before = upscaling.guideCopies;
			const auto publications = upscaling.publications;
			const auto captures = upscaling.captureFrames;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == before,
				"Ineligible route must not capture guides");
			if (gate < 3) {
				Require(upscaling.publications == publications && upscaling.captureFrames == captures,
					"Disabled or differently owned routes must not begin or publish Main preparation");
			} else {
				Require(upscaling.publications == publications + 1 && upscaling.captureFrames == captures + 1,
					"An active route refused before guide preparation must publish its capture decision");
				const auto reason = gate == 3 ? Upscaling::NeuralStereoFallbackReason::RouteIneligible :
				                    gate == 5 ? Upscaling::NeuralStereoFallbackReason::MenuContext :
				                    gate == 7 ? Upscaling::NeuralStereoFallbackReason::TemporalSourceStale :
				                                Upscaling::NeuralStereoFallbackReason::FrameGeneration;
				RequireNoAttemptFallback(upscaling, state, reason);
			}
		}
		Require(upscaling.resets == 2, "Observational fallback publication must not reset temporal histories");
		std::cout << "Full-resolution preparation lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
