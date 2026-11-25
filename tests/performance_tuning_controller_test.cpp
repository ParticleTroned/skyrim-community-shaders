#include "Menu/PerformanceQuickScan.h"
#include "Menu/PerformanceTuningController.h"

#include "Utils/FlatFrameTiming.h"
#include "Utils/RuntimeToggle.h"
#include "Utils/VRFrameTiming.h"
#include <limits>
#include <vector>

#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	struct State
	{
		uint32_t frameCount = 0;
		bool pendingPostLoadRuntimeReset = false;
		bool IsSaveLoadSafeModeActive() const { return false; }
		bool IsEngineSaveLoadActivityActive() const { return false; }
		bool IsMainOrLoadingMenuOpen() const { return false; }
	};

	struct Timer
	{
		std::string name;
		bool valid = true;
		bool hasGpu = true;
		bool hasCpu = true;
		std::uint32_t historyCount = 2;
		std::uint32_t cpuHistoryCount = 2;
		std::array<float, 2> gpu{ 1.0f, 3.0f };
		std::array<float, 2> cpu{ 0.5f, 1.5f };
		float GetHistorySample(std::uint32_t index) const { return gpu.at(index); }
		float GetCpuHistorySample(std::uint32_t index) const { return cpu.at(index); }
	};

	struct Readiness
	{
		bool enabled = false;
		bool ready = false;
		bool IsPerformanceCostMeasurementEnabled() const { return enabled; }
		bool IsPerformanceCostMeasurementReady() const { return ready; }
	};

	struct CaptureSource
	{
		bool enabled = false;
		bool reject = false;
		bool fail = false;
		std::uint64_t session = 0;
		std::uint32_t cancellations = 0;
		bool IsUserEnabled() const { return enabled; }
		void SetUserEnabled(bool value) { enabled = value; }
		bool StartBoundedCapture(std::uint32_t, bool clear, std::uint64_t& id, bool aligned)
		{
			Require(!clear && aligned, "quick scans must preserve history and align inactive frames");
			if (fail)
				throw std::runtime_error("capture rejected");
			if (reject)
				return false;
			id = ++session;
			return true;
		}
		struct Progress
		{
			std::uint64_t sessionId;
		};
		Progress GetBoundedCaptureProgress() const { return { session }; }
		void CancelBoundedCapture(std::uint64_t id)
		{
			Require(id == session || session == 0, "foreign capture was cancelled");
			++cancellations;
		}
	};

	void TestCaptureOwnership()
	{
		using PerformanceQuickScan::CaptureOwnership;
		Require(PerformanceQuickScan::Ready(Readiness{ false, false }), "inactive outdoor features must not block indoor scans");
		Require(!PerformanceQuickScan::Ready(Readiness{ true, false }), "active pending features must block capture");
		Require(PerformanceQuickScan::Ready(Readiness{ true, true }), "ready active features must allow capture");
		for (bool enabled : { false, true }) {
			CaptureSource source{ .enabled = enabled };
			CaptureOwnership<CaptureSource> capture;
			Require(capture.Start(source, 3) && source.enabled, "capture must temporarily enable profiling");
			Require(!capture.Start(source, 3), "an active owner cannot replace its own session");
			CaptureOwnership<CaptureSource> moved(std::move(capture));
			capture.Release();
			Require(source.cancellations == 0, "moved owners must not cancel the transferred capture");
			moved.Release();
			moved.Release();
			Require(source.enabled == enabled && source.cancellations == 1, "release must restore the original preference exactly once");

			source.reject = true;
			Require(!capture.Start(source, 3) && source.enabled == enabled, "rejected capture must restore profiler state");
			source.reject = false;
			source.fail = true;
			bool threw = false;
			try {
				capture.Start(source, 3);
			} catch (const std::runtime_error&) {
				threw = true;
			}
			Require(threw && source.enabled == enabled, "start exceptions must restore state and propagate the failure");
		}
		CaptureSource source;
		CaptureOwnership<CaptureSource> capture;
		Require(capture.Start(source, 3), "capture refused");
		++source.session;
		capture.Release();
		Require(source.enabled && source.cancellations == 0, "replacement captures must retain their state and ownership");
		CaptureSource reset;
		Require(capture.Start(reset, 3), "reset fixture refused");
		reset.session = 0;
		capture.Release();
		Require(!reset.enabled, "renderer reinitialization must not leak a temporary enabled preference");
	}

	void TestWholeFrameCapture()
	{
		struct Summary
		{
			bool vrTiming = true;
			std::uint64_t vrTimingEpoch = 1, flatTimingEpoch = 1, flatPresentId = 10;
			std::uint32_t vrLatestFrameIndex = 10;
			std::vector<Util::VRFrameTiming::Sample> vrSamples;
			std::vector<Util::FlatFrameTiming::Sample> flatSamples;
		} summary;
		PerformanceQuickScan::FrameTimingCapture capture;
		capture.Begin(summary);
		Util::VRFrameTiming::Sample sample;
		sample.frameIndex = 11;
		sample.frameMs = 10;
		sample.cpuMs = 2;
		sample.gpuMs = 4;
		sample.hasFrame = sample.hasCpu = sample.hasGpu = true;
		summary.vrSamples.push_back(sample);
		capture.Push(summary);
		capture.Push(summary);
		Require(capture.frame.moments.sampleWeight == 1 && capture.Mean(capture.frame) == 10,
			"repeated timing reads must not duplicate whole-frame samples");
		sample.frameIndex = 12;
		sample.frameMs = 20;
		sample.cpuMs = 4;
		sample.gpuMs = 8;
		summary.vrSamples.push_back(sample);
		capture.Push(summary);
		Require(capture.Mean(capture.frame) == 15 && capture.Mean(capture.cpu) == 3 && capture.Mean(capture.gpu) == 6,
			"whole-frame means must cover the measured window using the live panel definitions");
		auto partial = capture;
		sample.frameIndex = 13;
		sample.hasCpu = false;
		summary.vrSamples = { sample };
		partial.Push(summary);
		Require(!partial.Mean(partial.cpu) && partial.cpu.missingSamples == 1 && partial.Mean(partial.gpu),
			"invalid native samples must invalidate that metric without discarding valid independent metrics");
		sample.frameIndex = 14;
		sample.hasCpu = true;
		summary.vrSamples = { sample };
		partial = capture;
		partial.Push(summary);
		Require(!partial.Mean(partial.frame) && partial.frame.missingSamples == 1,
			"gaps in completed timing history must not silently produce a partial mean");
		++summary.vrTimingEpoch;
		capture.Push(summary);
		Require(!capture.Mean(capture.frame), "clock discontinuities must invalidate the whole-frame aggregate");
		summary.vrTiming = false;
		capture.Begin(summary);
		Util::FlatFrameTiming::Sample flat;
		flat.presentId = 11;
		flat.frameMs = 12;
		flat.cpuMs = 3;
		flat.hasFrame = flat.hasCpu = true;
		flat.resolved = false;
		summary.flatSamples.push_back(flat);
		capture.Push(summary);
		Require(!capture.Mean(capture.cpu) && !capture.Mean(capture.gpu), "unresolved desktop frames must wait for a complete matched sample");
		summary.flatSamples[0].gpuMs = 7;
		summary.flatSamples[0].hasGpu = summary.flatSamples[0].resolved = true;
		capture.Push(summary);
		Require(capture.Mean(capture.gpu) == 7 && capture.cpu.moments.sampleWeight == 1,
			"late desktop GPU queries must resolve without duplicating CPU timing");
	}

	void TestQuickScan()
	{
		using namespace PerformanceQuickScan;
		const RenderConfiguration original{ true, true, 1, -1 };
		for (const auto changed : { RenderConfiguration{ true, false, 1, -1 },
				 RenderConfiguration{ false, true, 1, -1 }, RenderConfiguration{ true, true, 2, -1 },
				 RenderConfiguration{ true, true, 1, 0 } }) {
			Controller configured;
			configured.Begin(10.0, false, original);
			Require(configured.Poll(15.0, true, original) == Action::StartCapture, "stable shader controls must allow capture");
			configured.phase = Phase::Capturing;
			Require(configured.Poll(16.0, true, changed) == Action::ConfigurationChanged,
				"rendering hotkeys and shader changes must stop a running capture");
			configured.Begin(20.0, false, original);
			Require(configured.Poll(21.0, true, changed) == Action::ConfigurationChanged,
				"changed shader controls must not survive the settling phase");
			configured.phase = Phase::Cancelled;
			Require(configured.Poll(22.0, true, changed) == Action::Wait, "terminal scans must not restart on shader changes");
		}
		Controller scan;
		scan.Begin(10.0, true);
		Require(scan.Active() && scan.Poll(15.0, true) == Action::Wait, "settling must start after the menu actually closes");
		scan.MenuClosed(16.0);
		Require(scan.Poll(18.9, true) == Action::Wait && scan.Poll(19.0, true) == Action::StartCapture, "quick scan must retain the three-second settling floor");
		Require(scan.Poll(20.5, false) == Action::Wait && scan.Poll(23.4, true) == Action::Wait, "unready rendering must restart settling");
		Require(scan.Poll(23.5, true) == Action::StartCapture, "uninterrupted readiness must allow capture");
		scan.phase = Phase::Capturing;
		Require(scan.Poll(26.0, false) == Action::Interrupted, "compilation during capture must invalidate the scan");
		Require(scan.Poll(40.0, true) == Action::TimedOut, "capture waits must be bounded");
		scan.phase = Phase::Cancelled;
		Require(!scan.Active() && scan.Poll(50.0, true) == Action::Wait, "cancelled scans must not restart");
		scan.Begin(60.0, false);
		Require(scan.Poll(63.0, true) == Action::StartCapture, "already closed menus must work without a close notification");
		scan.Begin(0, false, original);
		Require(scan.Poll(3, true, original, false) == Action::Wait && scan.Poll(8, true, original, false) == Action::Wait,
			"capture must wait for the recorder while the stable scene settles");
		Require(scan.Poll(8, true, original, true) == Action::StartCapture && scan.readySince == 0,
			"a prepared recorder must not restart completed scene settling");
		scan.Begin(0, false, original);
		Require(scan.Poll(1, true, original, true) == Action::Wait && scan.Poll(3, true, original, true) == Action::StartCapture,
			"early recorder readiness must still retain the scene's three-second settling floor");
		scan.Begin(0, false, original, true);
		Require(scan.scanCount == 3 && scan.scanIndex == 1, "a batch must start with three scans");
		for (unsigned next = 2; next <= 3; ++next) {
			scan.phase = Phase::Complete;
			Require(scan.Advance(next * 20) && scan.Active() && scan.scanIndex == next, "gaps must retain the batch's ownership");
			Require(scan.Poll(next * 20 + 2.9, true, original) == Action::Wait, "no capture before the three-second gap");
			Require(scan.Poll(next * 20 + 3, true, original) == Action::StartCapture, "settling must fit within the three-second gap");
			Require(scan.Poll(next * 20 + 3, false, original) == Action::Wait, "unready rendering extends the gap");
			Require(scan.Poll(next * 20 + 5.9, true, original) == Action::Wait, "readiness must remain stable for three seconds");
			Require(scan.Poll(next * 20 + 6, true, original) == Action::StartCapture, "extended gaps must resume when ready");
		}
		scan.phase = Phase::Complete;
		Require(!scan.Advance(100) && !scan.Active(), "three successful scans must terminate without another gap");
		scan.Begin(0, false, original, true);
		scan.phase = Phase::Cancelled;
		Require(!scan.Advance(1) && scan.Poll(20, true, original) == Action::Wait, "cancellation must not restart a batch");

		const Util::FeatureProfiling::View view{ "Example", "Example" };
		std::vector<Timer> timers{ { "Example::Pass" }, { "Example::Other" }, { "ExampleExtra::Unrelated" } };
		auto mean = [&](bool cpu = false) { return Mean(view, std::span<const Timer>(timers), cpu, 2); };
		Require(mean() == 4.0 && mean(true) == 2.0, "owned self times must sum while similarly named features remain excluded");
		timers[1].gpu = { 0.0f, 0.0f };
		Require(mean() == 2.0, "valid zero samples must remain valid");
		timers[1].historyCount = 1;
		Require(!mean() && mean(true) == 2.0, "incomplete GPU history must not become zero or invalidate independent CPU data");
		timers[1].historyCount = 2;
		timers[1].gpu[0] = std::numeric_limits<float>::quiet_NaN();
		Require(!mean(), "nonfinite samples must never become displayed measurements");
		Require(!Mean(view, std::span<const Timer>{}, false, 2), "unobserved features are unavailable rather than free");
		std::vector<Timer> neuralTimers{ { "Upscaling::DLSSNeuralRendering" } };
		const auto* upscaling = Util::FeatureProfiling::Find("Upscaling");
		const auto* neural = Util::FeatureProfiling::Find("NeuralRendering");
		Require(upscaling && neural && !Mean(*upscaling, std::span<const Timer>(neuralTimers), false, 2) &&
					Mean(*neural, std::span<const Timer>(neuralTimers), false, 2) == 2.0,
			"specific feature ownership must prevent counting neural work again as ordinary upscaling");
		const Util::FeatureProfiling::View ambiguous{ "OtherUpscaling", "Upscaling" };
		const std::vector<Timer> ordinaryTimers{ { "Upscaling::DLSS" } };
		Require(!Mean(ambiguous, std::span<const Timer>(ordinaryTimers), false, 2),
			"ambiguous equal ownership must fail closed");
		const Util::FeatureProfiling::View shared{ "Material", "", Util::FeatureProfiling::materialPasses, false };
		Require(!Mean(shared, std::span<const Timer>(timers), false, 2), "shared rendering must never be attributed as isolated feature cost");
		Require(std::string_view(shared.OwnedCoverage()).find("No separately instrumented") != std::string_view::npos,
			"shared-only coverage must not advertise GPU context excluded from the scan");
		Require(std::string_view(neural->OwnedCoverage()).find("other GPU queues") != std::string_view::npos,
			"neural scan coverage must identify omitted evaluation queues");
	}

	void TestFullScanTiming()
	{
		using namespace PerformanceTuningController;
		const auto ordinary = BatchFeatureWaits(2, 2, 2);
		const auto upscaling = BatchFeatureWaits(2, 10, 2);
		const auto skylighting = BatchFeatureWaits(5, 1, 2);
		const auto terrain = BatchFeatureWaits(2, 3, 2);
		Require(ordinary.current == 0.5 && ordinary.comparison == 0.5 && ordinary.restoration == 0.5,
			"full scans must shorten generic settling after each state change");
		Require(upscaling.comparison == 10 && skylighting.current == 5 && skylighting.restoration == 5 && terrain.comparison == 3,
			"full scans must honor temporal feature settling on both disable and restoration");
		Require(ordinary.TotalSeconds(3) == 7.5 && kFeatureBatchCooldownSeconds == 0,
			"full scans must keep six half-second sample blocks without adding a redundant inter-feature delay");
		Require(31 * ordinary.TotalSeconds(3) + upscaling.TotalSeconds(3) + skylighting.TotalSeconds(3) + terrain.TotalSeconds(3) < kFeatureBatchMaximumSeconds,
			"the current maximum feature set must nominally fit inside five minutes");
		Require(!BatchNeedsRestoration(293.9, 5) && BatchNeedsRestoration(294, 5) && BatchNeedsRestoration(299, 0.5),
			"batch deadlines must reserve temporal restoration before five minutes");
		Require(BatchFeatureWaits(std::numeric_limits<double>::quiet_NaN(), 2, 2).comparison == 3,
			"invalid timing contracts must retain conservative waits");
	}

	struct DeferredFeature
	{
		std::string GetShortName() const { return "selected"; }
		Util::RuntimeToggle enabled{ true };
		uint32_t applied = 1;
		int quality = 7;
		bool reject = false;
		bool failRestore = false;
		bool IsPerformanceToggleEnabled() const { return enabled.Get(); }
		nlohmann::json CapturePerformanceCostMeasurementState() const
		{
			return { { "Enabled", enabled.Get() }, { "Quality", quality } };
		}
		void SetPerformanceToggleEnabled(bool value)
		{
			if (!reject)
				enabled.Set(value);
		}
		void RestorePerformanceToggleState(const nlohmann::json& state)
		{
			if (failRestore)
				throw std::runtime_error("restore failed");
			enabled.Set(state.at("Enabled").get<bool>());
			quality = state.at("Quality").get<int>();
		}
	};
}

int main()
try {
	TestQuickScan();
	TestFullScanTiming();
	TestWholeFrameCapture();
	TestCaptureOwnership();
	using namespace PerformanceTuningController;
	using nlohmann::json;
	DeferredFeature feature;
	State state;
	feature.enabled.Apply(feature.applied, &state);
	std::optional<DisabledConfiguration> saved;
	Require(SetEnabled(feature, false, saved), "a deferred disable request must be accepted");
	Require(!feature.enabled.Get() && feature.applied == 1 && saved.has_value(), "pending disable must retain original settings");
	const auto original = *saved;
	Require(SetEnabled(feature, false, saved) && saved->enabledState == original.enabledState, "a repeated request must not overwrite the enabled snapshot");
	Require(!feature.enabled.Apply(feature.applied, &state), "the controller must not bypass the per-frame runtime guard");
	++state.frameCount;
	Require(feature.enabled.Apply(feature.applied, &state) && feature.applied == 0, "the disabled preference must apply on the next safe frame");
	feature.quality = 11;
	Require(SetEnabled(feature, true, saved), "restore must accept a deferred enable request");
	Require(feature.enabled.Get() && feature.applied == 0 && feature.quality == 11 && !saved, "reenabling must preserve edits made while disabled");
	++state.frameCount;
	feature.enabled.Apply(feature.applied, &state);
	Require(feature.applied == 1, "the restored preference must reach the renderer");

	feature.enabled.Set(false);
	Require(SetEnabled(feature, true, saved) && feature.quality == 11, "enable without a snapshot must retain tuning values");
	feature.reject = true;
	const auto before = feature.CapturePerformanceCostMeasurementState();
	Require(!SetEnabled(feature, false, saved), "a rejected toggle must report failure");
	Require(feature.CapturePerformanceCostMeasurementState() == before && !saved, "rejected disable must leave no snapshot or settings change");

	std::unordered_map<std::string, std::optional<DisabledConfiguration>> configurations;
	configurations["selected"] = original;
	configurations["unrelated"] = original;
	const auto beforeReplacement = feature.CapturePerformanceCostMeasurementState();
	DeferredFeature broken;
	broken.failRestore = true;
	bool rollbackThrew = false;
	try {
		ApplyTransaction(std::vector{ &feature, &broken }, configurations, [&] {
			feature.quality = 99;
			configurations.clear();
			return false; });
	} catch (const std::exception&) {
		rollbackThrew = true;
	}
	Require(rollbackThrew && feature.CapturePerformanceCostMeasurementState() == beforeReplacement &&
				configurations.at("selected")->enabledState == original.enabledState && configurations.contains("unrelated"),
		"One failed rollback must not prevent other features and toggle history from being restored");
	Require(!ApplyTransaction(std::vector{ &feature }, configurations, [&] {
		feature.quality = 33;
		configurations.erase("selected");
		return false; }, true), "rejected settings replacements report failure");
	Require(feature.CapturePerformanceCostMeasurementState() == beforeReplacement && configurations.at("selected")->enabledState == original.enabledState,
		"rejected settings replacements restore native settings and reversible toggle history");
	bool replacementThrew = false;
	try {
		ApplyTransaction(std::vector{ &feature }, configurations, [&]() -> bool {
			feature.quality = 44;
			throw std::runtime_error("replacement failed"); }, true);
	} catch (const std::runtime_error&) {
		replacementThrew = true;
	}
	Require(replacementThrew && feature.CapturePerformanceCostMeasurementState() == beforeReplacement && configurations.contains("selected"),
		"exceptions restore settings and history before propagating");
	Require(ApplyTransaction(std::vector{ &feature }, configurations, [&] {
		feature.quality = 22;
		return true; }, true), "accepted settings replacements succeed");
	Require(feature.quality == 22 && !configurations.contains("selected") && configurations.at("unrelated")->enabledState == original.enabledState,
		"a successful replacement discards only its own stale toggle history");

	const json enabled = { { "effects", { { "a", true }, { "b", false } } }, { "quality", 7 } };
	const json disabled = { { "effects", { { "a", false }, { "b", false } } }, { "quality", 7 } };
	json edited = disabled;
	edited["effects"]["b"] = true;
	edited["quality"] = 13;
	edited["newSetting"] = 2;
	const auto restored = RestoreToggleChanges(enabled, disabled, edited);
	Require(restored.at("effects").at("a") == true && restored.at("effects").at("b") == true, "mixed toggles must restore owned changes and retain user edits");
	Require(restored.at("quality") == 13 && restored.at("newSetting") == 2, "restoration must retain unrelated and new fields");
	Require(RestoreToggleChanges(enabled, disabled, disabled) == enabled, "unchanged disabled settings must restore exactly");
	Require(RestoreToggleChanges(json(3), json(0), json(5)) == 5, "an edited scalar must not be overwritten");

	Require(NextBatchAction(0, 2, false, false, false, 0) == BatchAction::StartNext, "a new batch must start its first comparison");
	Require(NextBatchAction(1, 2, true, false, false, 0) == BatchAction::Wait, "a running comparison must own the batch");
	Require(NextBatchAction(1, 2, false, true, false, 5) == BatchAction::Wait, "batch comparisons must respect cooldown");
	Require(NextBatchAction(1, 2, false, true, false, 0) == BatchAction::StartNext, "completed comparison must advance once ready");
	Require(NextBatchAction(2, 2, false, true, false, 10) == BatchAction::Finish, "the last comparison must finish without waiting for an unused cooldown");
	Require(NextBatchAction(1, 2, false, true, true, 0) == BatchAction::Fail, "failed comparison must stop the batch");
	Require(NextBatchAction(1, 2, false, false, false, 0) == BatchAction::Fail, "lost comparison state must fail closed");
	Require(NextBatchAction(0, 0, false, false, false, 0) == BatchAction::Fail, "empty batch must fail closed");
	Require(NextBatchAction(3, 2, false, true, false, 0) == BatchAction::Fail, "invalid batch index must fail closed");
	std::cout << "Quick scan, deferred toggle, restoration, rejection and batch sequencing checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
