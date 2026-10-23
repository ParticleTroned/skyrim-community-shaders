#include "Diagnostics/ISLDiagnosticsInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/RuntimeThreadAffinity.h"
#	include "BuildProvenance.h"
#	include "CSEditor/EditorWindow.h"
#	include "Diagnostics/ReflexMeasurementInternal.h"
#	include "EngineFixes/VRShadowBatch.h"
#	include "Features/InverseSquareLighting.h"
#	include "Features/LightLimitFix/ShadowCulling.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "Profiler.h"
#	include "State.h"
#	include <filesystem>

namespace CSX::Diagnostics::ISL
{
	namespace RM = ReflexMeasurement;
	namespace
	{
		json RetainedGpuTimings()
		{
			if (!CSX::Api::IsRuntimeMainThread())
				return { { "status", "UNAVAILABLE" }, { "reason", "profiler owner thread unavailable" } };
			const auto* profiler = globals::profiler;
			json rows = json::array();
			if (profiler && profiler->IsInitialized()) {
				for (const auto& timer : profiler->GetResults(false)) {
					if (!timer.hasGpu)
						continue;
					json history = json::array();
					for (uint32_t i = 0; i < timer.historyCount; ++i) {
						const auto value = timer.GetHistorySample(i);
						history.push_back(std::isfinite(value) ? json(value) : json(nullptr));
					}
					rows.push_back({ { "name", timer.name }, { "valid", timer.valid }, { "active", timer.activeGpu }, { "milliseconds", std::move(history) } });
				}
			}
			return { { "status", rows.empty() ? "UNAVAILABLE" : "RETAINED_HISTORY_ONLY" }, { "rows", std::move(rows) },
				{ "userEnabled", profiler && profiler->IsUserEnabled() }, { "historyEpoch", profiler ? json(profiler->GetHistoryEpoch()) : json(nullptr) },
				{ "capturedFrame", profiler ? json(profiler->GetCapturedFrameCount()) : json(nullptr) },
				{ "meaning", "Existing GPU pass histories copied outside capture; no timer activation, flush or readback added. No per-sample frame IDs or exact admission alignment. Raw OpenVR GPU timings are exported separately." } };
		}
	}
	json PolicyJson(Policy p)
	{
		static constexpr const char* luminance[] = { "normal", "native", "compute_discard_then_native" };
		static constexpr const char* renderer[] = { "normal", "vanilla_output", "vanilla_direct" };
		static constexpr const char* particle[] = { "normal", "scan_off", "bypass_before_lock" };
		static constexpr const char* observation[] = { "off", "counters", "timed" };
		return { { "luminanceMode", luminance[size_t(p.luminance)] }, { "rendererMode", renderer[size_t(p.renderer)] },
			{ "particleDetectionMode", particle[size_t(p.particle)] }, { "editorOverrideMode", p.editorBypass ? "bypass" : "normal" },
			{ "observationLevel", observation[size_t(p.observation)] }, { "generation", p.generation } };
	}
	json ConditionJson()
	{
		auto& d = Data();
		auto& isl = globals::features::inverseSquareLighting;
		const auto& editor = EditorWindow::GetSingleton()->lightEditor;
		const auto p = CurrentPolicy(), requested = Policy::Unpack(d.requested.load());
		const bool editorClean = !editor.IsEnabled() && !editor.HasDeferredWork();
		const bool applicable = isl.IsEnabled();
		const bool ready = globals::game::isVR && globals::state && !globals::state->IsPersistentMutationBlocked() &&
		                   (!isl.loaded || isl.IsPerformanceCostMeasurementReady()) && p == requested && editorClean && p.Valid(d.suppressed, editorClean);
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* sky = globals::game::sky;
		return { { "restoration", RestorationJson() }, { "supported", globals::game::isVR }, { "ready", ready }, { "applicable", applicable },
			{ "requested", PolicyJson(requested) }, { "effective", PolicyJson(p) }, { "requestedGeneration", requested.generation },
			{ "effectiveGeneration", p.generation }, { "appliedFrame", d.appliedFrame.load() }, { "appliedQpc", d.appliedQpc.load() },
			{ "particleDetectionPreference", globals::features::lightLimitFix.settings.EnableParticleLightsDetection }, { "startup", d.startupReceipt }, { "sessionActive", d.sessionActive.load() }, { "expired", d.expired.load() },
			{ "isl", { { "loaded", isl.loaded }, { "enabled", isl.IsEnabled() }, { "requestedEnabled", isl.settings.Enabled }, { "ready", isl.IsPerformanceCostMeasurementReady() } } },
			{ "editor", { { "enabled", editor.IsEnabled() }, { "pending", editor.HasDeferredWork() }, { "clean", editorClean } } },
			{ "shadowCulling", LightLimitFixDetail::ShadowCulling::Status(globals::features::lightLimitFix.settings.ShadowCulling) },
			{ "shadowBatch", { { "activeEnabled", VRShadowBatch::IsInstalled() }, { "configuredEnabled", globals::features::vr.settings.EnableVRShadowBatch }, { "restartRequired", VRShadowBatch::IsInstalled() != globals::features::vr.settings.EnableVRShadowBatch } } },
			{ "nrRequested", globals::features::upscaling.settings.neuralRenderingEnabled },
			{ "existingGpuTimings", RetainedGpuTimings() },
			{ "owner", RM::State().owner }, { "sessionId", RM::State().session }, { "captureId", d.capture }, { "acquiring", AcquisitionActive() },
			{ "producer", BuildProvenance::GetProducer() },
			{ "gameHour", globals::game::calendar ? json(globals::game::calendar->GetHour()) : json(nullptr) },
			{ "weather", sky && sky->currentWeather ? json(sky->currentWeather->GetFormID()) : json(nullptr) },
			{ "cell", player && player->GetParentCell() ? json(player->GetParentCell()->GetFormID()) : json(nullptr) },
			{ "pacing", RM::PreferencesJson(RM::CurrentPreferences()) },
			{ "qpc", RM::Clock() }, { "blockReason", ready ? json(nullptr) : json("VR, loaded/settled ISL, settled inactive editor and safe game state required") },
			{ "nativeLuminanceMeaning", "Native evaluation uses the current engine-light state, including normal ISL radius writes; it is not a fully vanilla scene." },
			{ "originalRadiusMeaning", "Captured runtime radius; not guaranteed final plugin-authored radius." },
			{ "frameAlignment", "QPC intervals are comparable; no exact application/compositor frame mapping. Worker frame IDs remain unknown." } };
	}
	json RestorationJson()
	{
		const auto& d = Data();
		const auto p = CurrentPolicy();
		return { { "liveRestored", p.luminance == Luminance::Normal && p.renderer == Renderer::Normal && p.particle == Particle::Normal && !p.editorBypass && p.Pack() == d.requested.load() },
			{ "conflict", d.restorationConflict }, { "expired", d.expired.load() }, { "startup", d.startupReceipt },
			{ "restartRequired", d.suppressed || VRShadowBatch::IsInstalled() != globals::features::vr.settings.EnableVRShadowBatch } };
	}
	namespace
	{
		json Caller(uintptr_t address)
		{
			if (!address)
				return nullptr;
			HMODULE module{};
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(address), &module))
				return { { "address", address }, { "module", nullptr }, { "rva", nullptr } };
			wchar_t path[32768]{};
			const auto length = GetModuleFileNameW(module, path, uint32_t(std::size(path)));
			const auto rva = address - reinterpret_cast<uintptr_t>(module);
			FreeLibrary(module);
			return { { "module", length ? json(std::filesystem::path(path).filename().string()) : json(nullptr) }, { "rva", rva } };
		}
	}
	void FreezeCapture(int64_t now)
	{
		auto& d = Data();
		CloseCapture();
		d.stop = RM::State().journal.stop;
		d.freeze = now;
		uint64_t unfinished = 0, overflow = 0, overlap = 0, uniqueOverflow = 0;
		json threads = json::array();
		d.frozenRows.clear();
		for (auto& slot : d.slots) {
			if (slot.busy.test_and_set(std::memory_order_acquire)) {
				++unfinished;
				continue;
			}
			const auto release = SKSE::stl::scope_exit([&] { slot.busy.clear(std::memory_order_release); });
			if (!slot.thread.load())
				continue;
			json counters = json::object();
			for (size_t i = 0; i < slot.totals.size(); ++i) {
				const auto& total = slot.totals[i];
				counters[MetricName(Metric(i))] = { { "count", total.count }, { "timedSamples", total.timed }, { "elapsedQpc", total.elapsed }, { "maxQpc", total.maximum } };
			}
			json callers = json::array();
			for (const auto& entry : slot.callers)
				if (entry.address)
					callers.push_back({ { "caller", Caller(entry.address) }, { "calls", entry.count } });
			threads.push_back({ { "callers", callers }, { "callerOverflow", slot.callerOverflow }, { "thread", slot.thread.load() }, { "counters", std::move(counters) }, { "candidateChecksum", slot.checksum },
				{ "candidateCount", slot.candidateCount }, { "uniqueLightAddresses", slot.uniqueCount }, { "uniqueOverflow", slot.uniqueOverflow } });
			overflow += slot.overflow + slot.callerOverflow;
			overlap += slot.overlap;
			uniqueOverflow += slot.uniqueOverflow;
			for (const auto& row : slot.rows) {
				if (row.enter < d.start || row.exit > d.stop)
					++overlap;
				d.frozenRows.push_back(row);
			}
		}
		json prune;
		{
			std::scoped_lock lock(d.pruneMutex);
			const auto& p = d.prune;
			prune = { { "armed", d.pruneArm.load() != 0 }, { "active", p.active }, { "consumed", p.consumed }, { "delayed", p.delayed }, { "cancelled", p.cancelled },
				{ "frames", p.frames }, { "dueFrame", p.consumed ? json(p.due) : json(nullptr) }, { "targetFrame", p.consumed ? json(p.target) : json(nullptr) },
				{ "executedFrame", p.executed ? json(p.executed) : json(nullptr) }, { "activationQpc", p.activated }, { "expiryQpc", p.expires } };
		}
		json priorInvocations = json::array();
		uint64_t oldGeneration = 0, unstableInvocations = 0;
		for (const auto& invocation : d.entryInvocations) {
			if (!invocation.active && !invocation.unstable)
				continue;
			oldGeneration += invocation.active && invocation.generation != d.captureGeneration;
			unstableInvocations += invocation.unstable;
			priorInvocations.push_back({ { "thread", invocation.thread }, { "generation", invocation.generation }, { "active", invocation.active },
				{ "enterQpc", invocation.enter ? json(invocation.enter) : json(nullptr) }, { "unstable", invocation.unstable } });
		}
		d.frozenHeader = { { "schema", 1 }, { "producer", BuildProvenance::GetProducer() }, { "sessionId", RM::State().captureSession },
			{ "clientId", RM::State().captureOwner }, { "captureId", d.capture }, { "generation", d.captureGeneration },
			{ "boundaries", { { "startQpc", d.start }, { "stopQpc", d.stop }, { "freezeQpc", now }, { "qpcFrequency", d.frequency } } },
			{ "preAdmissionInvocations", priorInvocations }, { "transitionObservation", "Root light invocations tracked while session active; QPC entry available in timed mode, unknown in counters, observations disabled in off mode. Publication does not imply global quiescence." },
			{ "condition", d.captureCondition }, { "threads", std::move(threads) }, { "prune", prune },
			{ "quality", { { "oldGenerationAtAdmission", oldGeneration }, { "unstableInvocationInspection", unstableInvocations }, { "unfinishedThreadSlots", unfinished }, { "overflow", overflow }, { "threadRegistrationLoss", d.threadLoss.load() },
							 { "contention", d.contention.load() }, { "transitionOrBoundaryOverlap", overlap }, { "uniqueOverflow", uniqueOverflow }, { "contaminated", d.contaminated.load() } } },
			{ "sampling", { { "timedQueryStride", 64 }, { "callerStride", 64 }, { "callers", "sampled only in timed mode" }, { "broadIntervalStride", 1 }, { "threadCapacity", ThreadCapacity }, { "recordsPerThread", d.capacity },
							  { "mode", d.captureCondition["effective"]["observationLevel"] }, { "counts", "unsampled per-thread totals; unique addresses are bounded per-thread estimates, not a global count" } } },
			{ "alignmentQuality", "same-clock QPC intervals; no proven one-to-one application/compositor mapping; unknown worker frame IDs are null" },
			{ "lateStartClassification", "UNAVAILABLE; raw compositor timing offsets and explicitly named offline proxies only" } };
		d.frozen = true;
	}
	json Export(const json& args)
	{
		auto& d = Data();
		if (!d.frozen || args.at("captureId") != d.capture || d.acknowledged)
			throw std::logic_error("frozen unacknowledged ISL capture required");
		const auto offset = args.value("offset", size_t{}), limit = args.value("limit", size_t{ 256 });
		if (!limit || limit > 256 || offset > d.frozenRows.size())
			throw std::invalid_argument("invalid export page");
		const auto end = std::min(offset + limit, d.frozenRows.size());
		auto result = d.frozenHeader;
		result["records"] = json::array();
		for (size_t i = offset; i < end; ++i) {
			const auto& row = d.frozenRows[i];
			result["records"].push_back({ { "ordinal", i }, { "metric", MetricName(row.metric) }, { "enterQpc", row.enter }, { "exitQpc", row.exit },
				{ "generation", row.generation }, { "frame", row.frame == UnknownFrame ? json(nullptr) : json(row.frame) }, { "thread", row.thread }, { "caller", Caller(row.caller) } });
		}
		if (offset == d.contiguous)
			d.contiguous = end;
		result["offset"] = offset;
		result["total"] = d.frozenRows.size();
		result["nextOffset"] = end < d.frozenRows.size() ? json(end) : json(nullptr);
		return result;
	}
}
#endif
