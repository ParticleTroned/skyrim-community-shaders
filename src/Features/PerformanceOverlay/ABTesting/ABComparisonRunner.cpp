#include "ABComparisonRunner.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "ABComparison.h"
#	include "ABTesting.h"
#	include "Feature.h"
#	include "Globals.h"
#	include "Menu.h"
#	include "Menu/PerformanceTuningController.h"
#	include "Menu/PerformanceTuningRenderer.h"
#	include "Menu/ProfilingRenderer.h"
#	include "ShaderCache.h"
#	include "State.h"
#	include "Utils/VanityCamera.h"
#	include "Utils/RuntimeToggle.h"

#	include <algorithm>
#	include <chrono>
#	include <cmath>
#	include <map>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;
	using Clock = std::chrono::steady_clock;
	using Snapshot = std::map<std::string, json>;
	constexpr size_t MaximumPresets = 4;
	constexpr size_t MaximumSnapshotBytes = 256 * 1024;

	double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
	uint32_t Cell()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		return cell ? cell->GetFormID() : 0;
	}

	void Require(bool valid, const char* message)
	{
		if (!valid)
			throw std::runtime_error(message);
	}
	void RequireEnvironment()
	{
		if (const auto* error = PerformanceTuningRenderer::GetComparisonEnvironmentError())
			throw std::runtime_error(error);
		Require(globals::shaderCache && globals::shaderCache->IsEnabled(), "shaders must be enabled");
		Require(globals::profiler && globals::profiler->IsInitialized(), "profiler unavailable");
	}
	bool Ready(Feature* feature)
	{
		return feature && feature->loaded && globals::shaderCache && feature->IsPerformanceCostMeasurementReady() &&
		       !globals::shaderCache->IsCompiling() && !(globals::game::ui && globals::game::ui->GameIsPaused());
	}
	bool FiniteState(const json& state)
	{
		if (state.is_number_float())
			return std::isfinite(state.get<double>());
		if (state.is_structured())
			return std::ranges::all_of(state, FiniteState);
		return !state.is_discarded();
	}
	Snapshot Capture()
	{
		Snapshot result;
		for (auto* feature : Feature::GetFeatureList()) {
			if (PerformanceTuningRenderer::GetComparisonFeatureBlockReason(feature))
				continue;
			auto state = feature->CapturePerformanceCostMeasurementState();
			Require(FiniteState(state), "captured settings contain nonfinite values");
			result.emplace(feature->GetShortName(), std::move(state));
		}
		Require(!result.empty() && json(result).dump().size() <= MaximumSnapshotBytes, "comparison snapshot unavailable or too large");
		return result;
	}
	unsigned Bounded(const json& request, const char* key, unsigned fallback, unsigned maximum)
	{
		if (!request.contains(key))
			return fallback;
		const auto& value = request.at(key);
		Require(value.is_number_integer() && value >= 1 && value <= maximum, "comparison pacing is outside its supported range");
		return value.get<unsigned>();
	}
	json MetricJson(const ABComparison::Metric& metric)
	{
		return { { "valid", metric.valid }, { "pairs", metric.pairs },
			{ "baselineMs", metric.valid ? json(metric.baseline) : json(nullptr) },
			{ "changeMs", metric.valid ? json(metric.change) : json(nullptr) },
			{ "deltaMs", metric.valid ? json(metric.delta) : json(nullptr) },
			{ "uncertaintyMs", metric.valid ? json(metric.uncertainty) : json(nullptr) },
			{ "verdict", !metric.valid ? "insufficient_evidence" : metric.verdict < 0 ? "faster" :
															   metric.verdict > 0     ? "slower" :
																						"no_measurable_change" } };
	}
}

struct ABComparisonRunner::Impl
{
	std::map<std::string, Snapshot> presets;
	Snapshot baseline, change, expected;
	json observedSettings;
	double nextSettingsCheck = 0;
	std::vector<ABComparison::Interval> intervals;
	ABComparison::Interval preparedInterval;
	Util::VanityCameraSuppressionLease camera;
	std::string phase = "idle", failure, label;
	json lastRequest = json::object();
	unsigned repeats = 2, seconds = 4;
	double settleSeconds = 5, readySince = 0, phaseStarted = 0, deadline = 0, nextPublication = 0;
	uint32_t cell = 0, lastUpdateFrame = UINT32_MAX;
	uint64_t epoch = 0, consumed = 0, latest = 0;
	bool haveEpoch = false, vr = false, profilerWasEnabled = false, profilerOwned = false;
	bool reopenMenu = false, restored = true, superseded = false, cancelled = false, mustRestore = false, restorationApplied = false;
	bool Active() const { return phase == "settling" || phase == "measuring" || phase == "draining" || phase == "restoring"; }
	bool OwnsSettings() const { return Active() || (mustRestore && !restored && !superseded); }

	void Validate(const Snapshot& target)
	{
		for (const auto& [name, state] : target) {
			auto* feature = Feature::FindFeatureByShortName(name);
			if (const auto* error = PerformanceTuningRenderer::GetComparisonFeatureBlockReason(feature))
				throw std::runtime_error(name + ": " + error);
			if (const auto* error = feature->GetPerformanceComparisonBlockReason(state))
				throw std::runtime_error(name + ": " + error);
		}
	}

	void Apply(const Snapshot& target)
	{
		mustRestore = true;
		restored = false;
		for (const auto& [name, state] : target) {
			auto* feature = Feature::FindFeatureByShortName(name);
			Require(feature != nullptr, "feature unavailable while applying settings");
			try {
				feature->RestorePerformanceCostMeasurementState(state);
			} catch (...) {
				// Own partial writes too, so a rejected coupled setting can be rolled back.
				expected.at(name) = feature->CapturePerformanceCostMeasurementState();
				throw;
			}
			expected.at(name) = feature->CapturePerformanceCostMeasurementState();
			Require(expected.at(name) == state, "captured settings were rejected or adjusted by the feature");
		}
		observedSettings = PerformanceTuningRenderer::CaptureComparisonSettings();
		nextSettingsCheck = 0;
		readySince = 0;
		phaseStarted = Now();
	}

	void PrepareInterval()
	{
		preparedInterval = {};
		preparedInterval.variant = intervals.size() % 2 == 0 ? ABVariant::A : ABVariant::B;
		// Reserve during settling so sample growth cannot allocate inside the measured windows.
		for (auto& samples : preparedInterval.samples)
			samples.reserve(ABComparison::MaximumSamples);
	}

	void Start(const json& request)
	{
		RequireEnvironment();
		Require(!mustRestore || restored || superseded, "previous restoration failed; cancel_comparison retries it");
		Require(!Active() && !PerformanceTuningRenderer::HasActiveMeasurements() && !ABTestingManager::GetSingleton()->IsEnabled(), "another measurement is active");
		Require(globals::profiler->GetBoundedCaptureProgress().state != Profiler::CaptureSessionState::Running, "bounded profiler capture is active");
		const auto preset = request.at("preset").get<std::string>();
		const auto found = presets.find(preset);
		Require(found != presets.end(), "capture this preset before comparing it");
		auto original = Capture();
		auto target = original;
		auto observed = PerformanceTuningRenderer::CaptureComparisonSettings();
		if (request.at("action") == "start_setting_comparison") {
			const auto name = request.at("featureShortName").get<std::string>();
			Require(original.contains(name) && found->second.contains(name), "feature absent from one comparison state");
			const auto path = request.at("path").get<std::string>();
			Require(path.size() <= 256, "setting path too long");
			const json::json_pointer pointer(path);
			const auto& value = found->second.at(name).at(pointer);
			const auto& previous = original.at(name).at(pointer);
			Require(value.is_primitive() && !value.is_null() && value.type() == previous.type(), "setting must be a matching captured scalar");
			target.at(name).at(pointer) = value;
		} else {
			Require(original.size() == found->second.size(), "preset feature availability changed; capture it again");
			for (const auto& [name, state] : found->second) {
				Require(original.contains(name), "preset feature ownership changed; capture it again");
				target.at(name) = state;
			}
		}
		Snapshot a, b;
		for (const auto& [name, state] : original) {
			if (state != target.at(name)) {
				a.emplace(name, state);
				b.emplace(name, target.at(name));
			}
		}
		Require(!a.empty(), "captured and current settings are identical");
		Validate(a);
		Validate(b);
		const unsigned count = Bounded(request, "repeats", 2, 5);
		const unsigned duration = Bounded(request, "intervalSeconds", 4, 15);
		double settle = Bounded(request, "settleSeconds", 5, 15);
		for (const auto& [name, state] : a) {
			auto* feature = Feature::FindFeatureByShortName(name);
			Require(Ready(feature), "feature settings have not settled");
			for (bool enabled : { false, true }) {
				const double wait = feature->GetPerformanceCostMeasurementSettleSeconds(enabled);
				Require(std::isfinite(wait) && wait >= 0 && wait <= 30, "invalid feature settling requirement");
				settle = std::max(settle, wait);
			}
		}
		Require(camera.Acquire(), "vanity camera suppression unavailable");
		baseline = std::move(a);
		change = std::move(b);
		expected = baseline;
		observedSettings = std::move(observed);
		nextSettingsCheck = 0;
		intervals.clear();
		intervals.reserve(2 * count + 1);
		PrepareInterval();
		repeats = count;
		seconds = duration;
		settleSeconds = settle;
		cell = Cell();
		vr = globals::game::isVR;
		haveEpoch = false;
		epoch = consumed = latest = 0;
		failure.clear();
		cancelled = superseded = mustRestore = false;
		restored = true;
		label = request.at("action").get<std::string>() + ": " + preset;
		profilerWasEnabled = globals::profiler->IsUserEnabled();
		profilerOwned = !vr && !profilerWasEnabled;
		if (profilerOwned)
			globals::profiler->SetUserEnabled(true);
		reopenMenu = globals::menu->IsEnabled;
		phase = "settling";
		phaseStarted = Now();
		deadline = phaseStarted + (2 * repeats + 1) * (settleSeconds + seconds) + 30;
		readySince = 0;
		if (reopenMenu)
			globals::menu->CloseMenu();
	}

	void Restore()
	{
		phase = "restoring";
		phaseStarted = Now();
		readySince = 0;
		restorationApplied = false;
	}

	void ApplyRestoration()
	{
		Snapshot target;
		for (const auto& [name, original] : baseline) {
			auto* feature = Feature::FindFeatureByShortName(name);
			Require(feature != nullptr, "feature unavailable during restoration");
			const auto current = feature->CapturePerformanceCostMeasurementState();
			target.emplace(name, PerformanceTuningController::RestoreToggleChanges(original, expected.at(name), current));
		}
		Apply(target);
		restorationApplied = true;
	}

	void Finish()
	{
		if (profilerOwned && globals::profiler &&
			globals::profiler->GetBoundedCaptureProgress().state != Profiler::CaptureSessionState::Running)
			globals::profiler->SetUserEnabled(false);
		profilerOwned = false;
		camera.Release();
		phase = !failure.empty() ? "failed" : cancelled ? "cancelled" :
		                                                  "completed";
		if (reopenMenu && globals::menu && !globals::menu->IsEnabled)
			globals::menu->OpenMenu();
		reopenMenu = false;
	}

	void Stop(std::string reason, bool cancellation = false)
	{
		failure = std::move(reason);
		cancelled = cancellation;
		if (!mustRestore || superseded) {
			restored = !superseded;
			Finish();
		} else {
			Restore();
		}
	}

	void Command(const json& request)
	{
		const auto action = request.at("action").get<std::string>();
		if (action == "cancel_comparison") {
			if (Active() || (mustRestore && !restored))
				Stop({}, true);
			return;
		}
		Require(!Active(), "a comparison is already active");
		if (action == "capture_comparison_preset") {
			RequireEnvironment();
			Require(!PerformanceTuningRenderer::HasActiveMeasurements() && !ABTestingManager::GetSingleton()->IsEnabled(), "another measurement is active");
			const auto name = request.at("preset").get<std::string>();
			Require(!name.empty() && name.size() <= 64, "preset name must contain 1 to 64 bytes");
			Require(presets.contains(name) || presets.size() < MaximumPresets, "four presets are already retained; clear or replace one");
			auto snapshot = Capture();
			Validate(snapshot);
			lastRequest["capturedStates"] = snapshot;
			presets.insert_or_assign(name, std::move(snapshot));
		} else if (action == "clear_comparison_presets") {
			presets.clear();
		} else {
			Start(request);
		}
	}

	void Collect(const ProfilingRenderer::PerformanceTimingSummary& summary)
	{
		const uint64_t sourceEpoch = vr ? summary.vrTimingEpoch : summary.flatTimingEpoch;
		if (!haveEpoch) {
			epoch = sourceEpoch;
			haveEpoch = true;
		} else {
			Require(epoch == sourceEpoch, "timing source reset during comparison");
		}
		latest = vr ? summary.vrLatestFrameIndex : summary.flatPresentId;
		if (vr) {
			for (const auto& sample : summary.vrSamples) {
				if (sample.frameIndex <= consumed)
					continue;
				consumed = sample.frameIndex;
				Require(ABComparison::Add(intervals, consumed, { sample.hasFrame ? std::optional(sample.frameMs) : std::nullopt, sample.hasGpu ? std::optional(sample.gpuMs) : std::nullopt, sample.hasCpu ? std::optional(sample.cpuMs) : std::nullopt }), "comparison sample capacity exhausted");
			}
		} else {
			for (const auto& sample : summary.flatSamples) {
				if (sample.presentId <= consumed)
					continue;
				if (!sample.resolved)
					break;
				consumed = sample.presentId;
				Require(ABComparison::Add(intervals, consumed, { sample.hasFrame ? std::optional(sample.frameMs) : std::nullopt, sample.hasGpu ? std::optional(sample.gpuMs) : std::nullopt, sample.hasCpu ? std::optional(sample.cpuMs) : std::nullopt }), "comparison sample capacity exhausted");
			}
		}
	}

	bool SettingsReady(bool verify = false)
	{
		for (const auto& [name, state] : expected) {
			auto* feature = Feature::FindFeatureByShortName(name);
			if (!Ready(feature))
				return false;
			if (verify)
				Require(feature->CapturePerformanceCostMeasurementState() == state, "comparison settings changed or failed to apply");
		}
		return true;
	}

	void Tick()
	{
		if (!Active() || !globals::state || lastUpdateFrame == globals::state->frameCount)
			return;
		lastUpdateFrame = globals::state->frameCount;
		const double now = Now();
		if (superseded) {
			Stop("comparison superseded by a replacement configuration");
			return;
		}
		if (phase == "restoring") {
			if (now - phaseStarted > 30) {
				failure += " Original settings did not finish restoring; cancel_comparison retries restoration.";
				Finish();
			} else if (!Util::IsRuntimeToggleBlocked(globals::state) && globals::shaderCache) {
				if (!restorationApplied)
					ApplyRestoration();
				else if (SettingsReady(true) && now - phaseStarted >= 1) {
					restored = true;
					mustRestore = false;
					Finish();
				}
			}
			return;
		}
		RequireEnvironment();
		Require(Cell() == cell && globals::game::isVR == vr, "scene changed during comparison");
		Require(!globals::menu->IsEnabled, "menu opened during comparison");
		Require(!ABTestingManager::GetSingleton()->IsEnabled() && globals::profiler->GetBoundedCaptureProgress().state != Profiler::CaptureSessionState::Running, "measurement ownership changed");
		Require(now <= deadline, "comparison deadline exceeded");
		if (!vr) {
			Require(globals::profiler->IsUserEnabled(), "profiler disabled during comparison");
			globals::profiler->RequestCapture();
		}
		const auto summary = ProfilingRenderer::CaptureFrameTimingSummary(false);
		const bool timingAvailable = vr ? summary.vrTiming && summary.vrLatestFrameIndex != 0 : summary.flatTiming && summary.flatPresentId != 0;
		if (timingAvailable)
			Collect(summary);
		const bool ready = SettingsReady() && timingAvailable;
		if (now >= nextSettingsCheck || (phase == "measuring" && now - phaseStarted >= seconds)) {
			Validate(expected);
			SettingsReady(true);
			Require(PerformanceTuningRenderer::CaptureComparisonSettings() == observedSettings, "settings changed during comparison");
			nextSettingsCheck = now + 0.25;
		}
		if (phase == "settling") {
			if (!ready) {
				readySince = 0;
				return;
			}
			if (readySince == 0)
				readySince = now;
			if (now - readySince < settleSeconds)
				return;
			Require(intervals.size() < ABComparison::MaximumIntervals, "comparison interval capacity exhausted");
			preparedInterval.first = latest + 1;
			intervals.push_back(std::move(preparedInterval));
			phase = "measuring";
			phaseStarted = now;
		} else if (phase == "measuring") {
			Require(ready, "rendering or settings became unavailable during measurement");
			if (now - phaseStarted < seconds)
				return;
			intervals.back().last = latest;
			intervals.back().closed = true;
			if (intervals.size() == 2 * repeats + 1) {
				phase = "draining";
				phaseStarted = now;
			} else {
				const auto& target = intervals.size() % 2 == 0 ? baseline : change;
				Validate(target);
				Apply(target);
				PrepareInterval();
				phase = "settling";
			}
		} else if (consumed >= intervals.back().last || now - phaseStarted >= 2) {
			Restore();
		}
	}

	json Report() const
	{
		json result{ { "phase", phase }, { "active", Active() }, { "label", label }, { "restored", restored },
			{ "ownsSettings", OwnsSettings() }, { "superseded", superseded }, { "failure", failure.empty() ? json(nullptr) : json(failure) }, { "lastRequest", lastRequest },
			{ "intervalSeconds", seconds }, { "settleSeconds", settleSeconds }, { "repeats", repeats },
			{ "presets", json::object() }, { "intervals", json::array() },
			{ "metricDefinitions", { { "frame", vr ? "completed SteamVR ClientFrameIntervalMs" : "QPC interval between accepted Presents" },
									   { "gpu", vr ? "SteamVR TotalRenderGpuMs, including compositor GPU work" : "D3D11 whole-frame GPU interval" },
									   { "cpu", vr ? "NewFrameReadyMs - NewPosesReadyMs + CompositorRenderCpuMs" : "QPC frame CPU interval excluding Present" } } },
			{ "uncertaintyDefinition", "maximum baseline drift, pair spread and two-standard-error frame noise; heuristic, not a confidence interval" } };
		for (const auto& [name, states] : presets) {
			auto names = json::array();
			for (const auto& [feature, state] : states)
				names.push_back(feature);
			result["presets"][name] = std::move(names);
		}
		for (const auto& interval : intervals)
			result["intervals"].push_back({ { "variant", interval.variant == ABVariant::A ? "A" : "B" },
				{ "firstSourceFrame", interval.first }, { "lastSourceFrame", interval.closed ? json(interval.last) : json(nullptr) },
				{ "closed", interval.closed }, { "samples", { interval.samples[0].size(), interval.samples[1].size(), interval.samples[2].size() } } });
		const bool complete = phase == "completed" && restored;
		std::array<ABComparison::Metric, 3> metrics{};
		for (size_t i = 0; i < metrics.size(); ++i) {
			if (complete)
				metrics[i] = ABComparison::Compare(intervals, i);
			result[i == 0 ? "frame" : i == 1 ? "gpu" :
											   "cpu"] = MetricJson(metrics[i]);
		}
		const auto& frame = metrics[0];
		const auto& gpu = metrics[1];
		result["headlineMetric"] = vr ? "gpu" : "frame";
		result["headroomOnly"] = !vr && frame.valid && frame.verdict == 0 && gpu.valid && gpu.verdict != 0;
		result["baselineFps"] = frame.valid && frame.baseline > 0 ? json(1000.0 / frame.baseline) : json(nullptr);
		result["changeFps"] = frame.valid && frame.change > 0 ? json(1000.0 / frame.change) : json(nullptr);
		return result;
	}
};

ABComparisonRunner::ABComparisonRunner() : impl(std::make_unique<Impl>()) { publication = impl->Report(); }
ABComparisonRunner::~ABComparisonRunner() = default;
ABComparisonRunner& ABComparisonRunner::Get()
{
	static ABComparisonRunner value;
	return value;
}

nlohmann::json ABComparisonRunner::Submit(const json& request)
{
	Require(request.is_object() && request.dump().size() <= 4096, "comparison request too large");
	const auto action = request.at("action").get<std::string>();
	Require(action == "capture_comparison_preset" || action == "clear_comparison_presets" || action == "start_setting_comparison" ||
				action == "start_preset_comparison" || action == "cancel_comparison",
		"unknown comparison action");
	std::lock_guard lock(mutex);
	if (pending)
		return { { "queued", false }, { "error", "comparison command pending" } };
	auto command = request;
	command["requestId"] = ++nextRequest;
	pending = std::move(command);
	commandPending.store(true, std::memory_order_release);
	return { { "queued", true }, { "requestId", nextRequest } };
}

nlohmann::json ABComparisonRunner::Status()
{
	std::lock_guard lock(mutex);
	return publication;
}

void ABComparisonRunner::Update()
{
	if (!running.load(std::memory_order_acquire) && !commandPending.load(std::memory_order_acquire))
		return;
	std::optional<json> command;
	{
		std::lock_guard lock(mutex);
		command.swap(pending);
		commandPending.store(false, std::memory_order_release);
	}
	const bool replaced = superseded.exchange(false, std::memory_order_acq_rel);
	if (replaced && (impl->Active() || impl->mustRestore))
		impl->superseded = true;
	running.store(impl->OwnsSettings(), std::memory_order_release);
	if (command) {
		const bool wasActive = impl->Active();
		impl->lastRequest = { { "requestId", command->at("requestId") }, { "action", command->at("action") }, { "accepted", false } };
		try {
			impl->Command(*command);
			impl->lastRequest["accepted"] = true;
		} catch (const std::exception& error) {
			impl->lastRequest["error"] = error.what();
			if (!wasActive && impl->camera.IsActive())
				impl->Stop(error.what());
		}
	}
	running.store(impl->OwnsSettings(), std::memory_order_release);
	try {
		impl->Tick();
	} catch (const std::exception& error) {
		logger::warn("A/B comparison stopped: {}", error.what());
		try {
			if (impl->phase == "restoring") {
				impl->failure = std::string("Restoration failed: ") + error.what();
				impl->Finish();
			} else {
				impl->Stop(error.what());
			}
		} catch (const std::exception& restoreError) {
			impl->failure += std::string("; restoration failed: ") + restoreError.what();
			impl->Finish();
		}
	}
	running.store(impl->OwnsSettings(), std::memory_order_release);
	if (command || replaced || (impl->Active() && Now() >= impl->nextPublication) || (!impl->Active() && impl->nextPublication != 0)) {
		try {
			auto report = impl->Report();
			std::lock_guard lock(mutex);
			publication = std::move(report);
		} catch (const std::exception& error) {
			logger::error("A/B comparison status publication failed: {}", error.what());
		}
		impl->nextPublication = impl->Active() ? Now() + 0.1 : 0;
	}
}

#endif
