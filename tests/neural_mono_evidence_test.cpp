#include "Features/Upscaling/NeuralRendering/MemoryResolutionPolicy.h"
#include "Features/Upscaling/NeuralRendering/MonoExecutionEvidence.h"
#include "Features/Upscaling/NeuralRendering/PeripheralMapping.h"
#include "Features/Upscaling/NeuralRendering/SubmissionFenceSnapshot.h"

#include <array>
#include <mutex>
#include <stdexcept>

namespace NeuralRendering
{
	bool forbidLiveReads = false;
	unsigned liveReads = 0;
	void ObserveLiveRead()
	{
		if (forbidLiveReads)
			throw std::runtime_error("retained-frame evidence consulted mutable runtime state");
		++liveReads;
	}
	struct Runtime
	{
		struct Counts
		{
			unsigned creates = 2, releases = 1;
		};
		static Runtime& Instance()
		{
			ObserveLiveRead();
			static Runtime value;
			return value;
		}
		Counts GetLifecycleCounts() const { return {}; }
		unsigned GetResidentFeatureMask() const { return 1; }
		std::array<unsigned, 1> GetFeatureNetworkPresets() const { return { 2 }; }
	};
	struct MonoRenderer
	{
		struct State
		{
			std::mutex mutex;
			std::array<MonoExecutionRecord, 4> executions{};
			std::size_t executionCursor = 0;
			MonoExperiments experiments;
			std::uint32_t faceFrame = UINT32_MAX;
			FaceDetailRegions faces;
			PeripheralMapping mapping;
			struct Interop
			{
				LifetimeFenceSnapshot Snapshot() const
				{
					ObserveLiveRead();
					return {};
				}
			} interop;
			MemoryRecoveryPolicy memory;
			MemoryResolutionPolicy resolution;
			struct Reservation
			{
				std::uint64_t Bytes() const { return 1024; }
			} nativeReservation;
			std::uint64_t nativeProjectedBytes = 2048, nativeObservations = 1;
			struct Startup
			{
				bool active = true;
			} nativeStartup;
		};
		State* state_;
		nlohmann::json Diagnostics(std::uint32_t sourceFrame = UINT32_MAX) const;
	};

#include "neural_mono_evidence_under_test.h"
}

static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
{
	using namespace NeuralRendering;
	using Json = nlohmann::json;
	MonoRenderer::State state;
	MonoRenderer renderer{ &state };
	auto nativeLayout = BuildNativeEvaluationLayout({ 960, 540 }, { 640, 360 }, { 960, 540 }, { 960, 540 },
		{ 17, 11, 900, 500 }, { true, 640, 360 }, true);
	nativeLayout.jitterPixels = { 0.25f, -0.375f };
	nativeLayout.nativeControlMask = true;
	state.experiments.nativeJitter = state.experiments.nativeControlMask = true;
	state.experiments.reconstruction.faceDetailProtectionPercent = 100;
	state.executions[0] = { .frame = 37, .submitted = true, .committed = true, .layout = nativeLayout, .experiments = state.experiments, .faceDetail = { 37, 2, true, false } };
	state.executions[0].native.evaluateAttempted = state.executions[0].native.evaluateSucceeded = true;
	state.executions[0].native.evaluateResult = 1;
	state.executionCursor = 1;
	forbidLiveReads = true;
	const auto retained = renderer.Diagnostics(37);
	const auto& record = retained.at("nativeCalls").at(0);
	Require(retained.at("diagnosticScope") == "retained_frame" && retained.at("nativeCalls").size() == 1,
		"retained request did not select only its source frame");
	Require(record.at("jitterPixels") == Json::array({ 0.25f, -0.375f }) && record.at("scalingRatio") == 1.0f &&
				record.at("nativeControlMask") == true && record.at("motionVectorScale") == Json::array({ 640.0f, 360.0f }),
		"actual SDK inputs were replaced with requested experiment flags");
	Require(record.at("faceDetail").at("sourceFrame") == 37 && record.at("faceDetail").at("observedRegions") == 2 &&
				record.at("faceDetail").at("current") == true && record.at("faceDetail").at("active") == false &&
				record.at("experiments").at("faceDetailProtectionPercent") == 100,
		"requested face protection was reported as applied despite footprint rejection");
	for (const auto* key : { "experiments", "faceDetail", "fences", "memory", "lifecycle" })
		Require(!retained.contains(key), "retained execution contains an unrelated live-state snapshot");

	// A later execution has different phase, mask and admission; its state cannot rewrite frame 37.
	nativeLayout = BuildNativeEvaluationLayout({ 480, 270 }, { 640, 360 }, { 960, 540 }, {},
		{ 17, 11, 900, 500 }, { true, 640, 360 }, true);
	nativeLayout.jitterPixels = { -0.5f, 0.5f };
	state.experiments = {};
	state.experiments.nativeJitter = true;
	state.experiments.reconstruction.faceDetailProtectionPercent = 50;
	state.faceFrame = 38;
	state.faces.count = 4;
	state.faces.current = true;
	state.mapping.faceDetail.enabled = 1;
	state.executions[1] = { .frame = 38, .layout = nativeLayout, .experiments = state.experiments, .faceDetail = { 38, 4, true, true } };
	state.executionCursor = 2;
	Require(renderer.Diagnostics(37) == retained, "later runtime changes rewrote retained execution evidence");
	const auto later = renderer.Diagnostics(38).at("nativeCalls").at(0);
	Require(later.at("scalingRatio") == 2.0f && later.at("nativeControlMask") == false && later.at("faceDetail").at("active") == true,
		"a later frame did not retain its own native inputs and face admission");

	state.executions[2] = { .frame = 39, .prewarm = true, .layout = nativeLayout, .experiments = state.executions[0].experiments };
	state.executionCursor = 3;
	const auto prewarm = renderer.Diagnostics(39).at("nativeCalls").at(0);
	Require(prewarm.at("prewarm") == true && prewarm.at("faceDetail").at("sourceFrame").is_null() &&
				prewarm.at("faceDetail").at("current") == false && prewarm.at("faceDetail").at("active") == false,
		"menu prewarm inherited a current scene face observation");
	Require(renderer.Diagnostics(40).at("nativeCalls").empty(), "missing retained frame borrowed a nearby execution");
	Require(liveReads == 0, "retained requests queried live ownership or lifecycle");

	forbidLiveReads = false;
	const auto live = renderer.Diagnostics();
	Require(liveReads > 0 && live.at("diagnosticScope") == "current_state_and_recent_executions" &&
				live.at("nativeCalls").size() == 3 && live.at("faceDetail").at("sourceFrame") == 38,
		"current status lost its explicit live-state diagnostics");
	state.executions = {};
	Require(retained.at("nativeCalls").at(0).at("faceDetail").at("sourceFrame") == 37,
		"retained JSON lost its immutable source-frame snapshot after ring retirement");
}
