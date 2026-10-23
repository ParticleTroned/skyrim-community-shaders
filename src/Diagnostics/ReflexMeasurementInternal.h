#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Diagnostics/ReflexMeasurement.h"
#	include "Features/Upscaling/ReflexBackend.h"
#	include <functional>
#	include <nlohmann/json.hpp>
#	include <string>
#	include <thread>

namespace CSX::Diagnostics::ReflexMeasurement
{
	using json = nlohmann::json;
	inline constexpr uint32_t SamplePeriodMs = 10, PostrollMs = 2000, QueryCapacity = 4096;
	inline constexpr const char* LatencyLimitation = "No source-proven simulation-start to corresponding VR eye GPU-completion boundary. OpenVR Submit is a CPU handoff; mirror Present and GPU elapsed-work timings are not eye completion or headset display. No latency queries or diagnostic markers are issued.";
	struct Service
	{
		Journal journal;
		std::mutex commands, preferences;
		PreferenceLease lease;
		std::atomic_bool leaseActive{};
		std::atomic_bool responseWritten{};
		std::atomic_bool acquiring{};
		std::atomic_bool queryEnabled{};
		std::atomic_bool preferenceConflict{};
		std::atomic<uint64_t> controlCallsDuringCapture{};
		uint64_t captureControlCalls{};
		std::atomic<Tick> nextQueryAfter{};
		Tick queryPeriodTicks{};
		bool startPending{};
		bool captureReleased = true;
		bool rawAcknowledged = true, islPurpose{};
		size_t contiguousExport{};
		std::string owner;
		uint64_t session{};
		uint64_t captureSession{};
		std::string captureOwner;
		uint32_t requestedDurationMs{};
		size_t requestedCapacity{};
		Tick preparedAt{};
		json environment, condition;
		std::vector<Reflex::DiagnosticReadback> queries;
		std::vector<Reflex::DiagnosticReadback> queryStarted;
		CompositorCursor cursor;
		uint32_t sourceStart{}, sourceTarget{};
		uint32_t maxHistoryReturned{}, lastHistoryReturned{};
		Tick sourceStartQpc{}, sourceStopQpc{};
		bool compositorRequested{}, readbackRequested{};
		bool capturePreferenceConflict{};
		uint32_t queryPeriodMs = 1000;
		uint32_t admissionDelayMs = 2500;
		std::string workerError, termination;
		std::jthread worker;
	};
	Service& State();
	Reflex::Backend& Backend();
	Preferences CurrentPreferences();
	void ApplyPreferences(Preferences p);
	Reflex::Options EffectiveOptions(Preferences p, bool constrained);
	json OnMain(std::function<json()> fn);
	json PreferencesJson(Preferences p);
	json Timestamp(Tick value);
	json StatusJson(const Reflex::Status& status);
	json ReadbackJson(const Reflex::DiagnosticReadback& result);
	json LeaseJson();
	json Environment();
	json Export(const json& args);
	json Readiness(const json& args);
	void Run(std::stop_token token, uint32_t durationMs);
	void RequestExpiredRestoration();
	void FreezeCapture(Tick now, bool tailComplete, bool timedOut);
}
#endif
