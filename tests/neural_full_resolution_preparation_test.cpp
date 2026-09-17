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
	struct Settings
	{
		uint32_t frameGenerationMode = 0;
		uint64_t key = 12;
	} settings;
	struct NeuralStereoRouteSnapshot
	{
		bool valid = false;
		uint32_t frame = 0;
		uint64_t generation = 0;
		uint32_t arrangement = 0;
		uint32_t insertionPoint = 0;
		bool requested = false, eligible = false, sourceBatchEligible = false, sourceSignatureProven = false;
		bool frameGenerationGatePassed = false;
		NeuralRendering::TemporalAdmissionResult temporalAdmission{};
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
	uint32_t guideCopies = 0, captureFrames = 0, resets = 0;
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
	bool PrepareFullResolutionNeuralInputs(uint32_t, uint32_t, uint32_t, uint32_t)
	{
		++guideCopies;
		if (guidePreparationThrows)
			throw std::runtime_error("guide allocation failed");
		return guidePreparationSucceeds;
	}
	void BeginNeuralCaptureFrame(NeuralStereoRouteRole, uint32_t) { ++captureFrames; }
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

		for (int failure = 0; failure < 3; ++failure) {
			NextFrame(upscaling, state);
			upscaling.dimensionsAvailable = failure != 0;
			upscaling.guidePreparationSucceeds = failure != 1;
			upscaling.guidePreparationThrows = failure == 2;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			const auto copies = upscaling.guideCopies;
			upscaling.dimensionsAvailable = upscaling.guidePreparationSucceeds = true;
			upscaling.guidePreparationThrows = false;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == copies,
				"Failed preparation must wait for another fresh frame");
		}
		Require(upscaling.resets == 1, "Preparation exception must reset history");

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
		NextFrame(upscaling, state);
		upscaling.PrepareMainFullResolutionNeuralFrame();
		Require(upscaling.mainFinalLdrNeuralState.ready, "Paused fresh world remains valid");

		for (int gate = 0; gate < 6; ++gate) {
			NextFrame(upscaling, state);
			upscaling.mainFinalLdrNeuralState = {};
			upscaling.requested = gate != 0;
			upscaling.presentation = gate == 1;
			upscaling.mode = gate == 2 ? NeuralRendering::RenderingMode::Foveated : NeuralRendering::RenderingMode::FullResolution;
			upscaling.transition = gate == 3;
			upscaling.frameGeneration = gate == 4;
			upscaling.hardMenu = gate == 5;
			const auto before = upscaling.guideCopies;
			upscaling.PrepareMainFullResolutionNeuralFrame();
			Require(!upscaling.mainFinalLdrNeuralState.ready && upscaling.guideCopies == before,
				"Ineligible route must not capture guides");
		}
		std::cout << "Full-resolution preparation lifecycle passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
