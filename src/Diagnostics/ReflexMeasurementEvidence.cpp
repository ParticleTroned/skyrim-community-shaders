#include "Diagnostics/ReflexMeasurementInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "BuildProvenance.h"
#	include "Utils/CryptoHash.h"
#	include "Utils/StringUtils.h"
#	include "Utils/WinApi.h"
#	include <cstring>
#	include <fstream>

namespace CSX::Diagnostics::ReflexMeasurement
{
	json Timestamp(Tick value) { return value > 0 ? json(value) : json(nullptr); }
	json PreferencesJson(Preferences p)
	{
		return { { "enabled", p.enabled }, { "boost", p.boost }, { "capped", p.capped }, { "markers", p.markers }, { "fps", p.fps } };
	}
	json StatusJson(const Reflex::Status& s)
	{
		auto options = [](const Reflex::Options& p) -> json {
			return { { "mode", static_cast<int>(p.mode) }, { "minimumIntervalUs", p.frameLimitUs }, { "useMarkersToOptimize", p.useMarkersToOptimize } };
		};
		return { { "health", static_cast<int>(s.health) }, { "generation", s.deviceGeneration }, { "deviceBound", s.deviceBound },
			{ "pending", s.pending }, { "desired", s.desiredValid ? options(s.desired) : json(nullptr) },
			{ "accepted", s.acceptedValid ? options(s.accepted) : json(nullptr) }, { "nativeResult", s.nativeStatus },
			{ "operation", static_cast<int>(s.operation) }, { "failures", s.failures }, { "retryAtMs", s.retryAtMs } };
	}
	json ReadbackJson(const Reflex::DiagnosticReadback& r)
	{
		json fields = nullptr;
		if (r.completed && r.result == NVAPI_OK)
			fields = { { "bLowLatencyMode", r.raw.bLowLatencyMode }, { "bFsVrr", r.raw.bFsVrr },
				{ "bCplVsyncOn", r.raw.bCplVsyncOn }, { "sleepIntervalUs", r.raw.sleepIntervalUs }, { "bUseGameSleep", r.raw.bUseGameSleep },
				{ "bFullscreenIFlip", r.raw.bFullscreenIFlip }, { "fgMultiplier", r.raw.fgMultiplier } };
		return { { "sequence", r.sequence }, { "completed", r.completed }, { "result", r.completed ? json(r.result) : json(nullptr) },
			{ "resultOrigin", !r.completed ? "unrecorded_completion" : r.attempted ? "nvapi" :
																					 "diagnostic_precondition" },
			{ "nativeResult", r.completed && r.attempted ? json(r.result) : json(nullptr) },
			{ "nativeResultReason", r.completed && r.attempted ? json(nullptr) : json("no recorded completed native query; result may instead describe diagnostic preconditions") },
			{ "completionReason", r.completed ? json(nullptr) : json("completion not recorded before freeze; in flight or completion dropped") },
			{ "version", r.raw.version }, { "size", sizeof(r.raw) }, { "generation", r.generationKnown ? json(r.generation) : json(nullptr) },
			{ "generationReason", r.generationKnown ? json(nullptr) : json("backend lock unavailable; native device generation not sampled") },
			{ "requestedQpc", Timestamp(r.requested) }, { "enterQpc", r.attempted ? Timestamp(r.enter) : json(nullptr) }, { "exitQpc", Timestamp(r.exit) }, { "fields", fields },
			{ "admittedQpc", Timestamp(r.admitted) }, { "attempted", r.completed ? json(r.attempted) : json(nullptr) },
			{ "clockReason", "null timestamps indicate unrecorded completion, no native attempt/admission, or unavailable QPC" },
			{ "unavailableReason", r.completed && r.result == NVAPI_OK ? json(nullptr) : json("native readback incomplete, failed or unsupported") },
			{ "meaning", "corroboration only; sleepIntervalUs is latest sleep interval, not configured-cap readback" } };
	}
	json LeaseJson()
	{
		auto& s = State();
		std::unique_lock lock(s.preferences, std::try_to_lock);
		if (!lock)
			return { { "active", nullptr }, { "conflict", nullptr }, { "requestedRestored", nullptr }, { "acceptedRestored", nullptr },
				{ "reason", "preference observer busy; no fresh restoration receipt" } };
		const auto& l = s.lease;
		return { { "active", l.active }, { "wrotePreferences", l.wrotePreferences }, { "unchangedReleased", l.unchangedReleased }, { "conflict", l.conflict }, { "requestedRestored", l.requestedRestored },
			{ "acceptedRestored", l.acceptedRestored }, { "restoreRequestedQpc", Timestamp(l.restoreRequestedAt) },
			{ "acceptedQpc", Timestamp(l.acceptedAt) }, { "acceptedGeneration", l.acceptedRestored ? json(l.acceptedGeneration) : json(nullptr) },
			{ "acceptedReceiptKind", !l.acceptedRestored ? json(nullptr) : l.freshNativeSettings ? json("native_settings_accepted_after_request") :
																								   json("already_accepted_options_confirmed_after_backend_update") },
			{ "unavailableReason", "restoration timestamps/generation remain null until their corresponding receipt exists" },
			{ "original", PreferencesJson(l.original) }, { "lastOwned", PreferencesJson(l.owned) } };
	}
	namespace
	{
		json ModuleIdentity(HMODULE module)
		{
			if (!module)
				return { { "available", false }, { "reason", "module not loaded" } };
			std::array<wchar_t, 32768> path{};
			const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (!size || size >= path.size())
				return { { "available", false }, { "reason", "module path unavailable or truncated" } };
			const std::filesystem::path file(std::wstring(path.data(), size));
			json identity{ { "available", true }, { "base", reinterpret_cast<uintptr_t>(module) }, { "path", Util::PathToUtf8(file) },
				{ "hashMeaning", "on-disk inspection does not prove loaded code is unintercepted or unmodified" } };
			try {
				const auto bytes = std::filesystem::file_size(file);
				if (bytes > 512 * 1024 * 1024)
					throw std::runtime_error("module exceeds bounded inspection size");
				std::string contents(static_cast<size_t>(bytes), '\0');
				std::ifstream input(file, std::ios::binary);
				if (!input.read(contents.data(), static_cast<std::streamsize>(contents.size())))
					throw std::runtime_error("module read failed or was truncated");
				identity["size"] = bytes;
				identity["sha256"] = Util::CryptoHash::Sha256Hex(contents);
				const auto version = Util::GetDllVersion(file.wstring());
				identity["version"] = version ? json(version->string()) : json(nullptr);
				identity["versionReason"] = version ? json(nullptr) : json("file version resource unavailable");
			} catch (const std::exception& e) {
				identity["sha256"] = nullptr;
				identity["inspectionError"] = e.what();
			}
			return identity;
		}
		json FunctionIdentity(uintptr_t address)
		{
			HMODULE module{};
			if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(address), &module))
				return { { "address", address }, { "owner", nullptr }, { "reason", "unavailable or unknown/trampoline outside a loaded module" } };
			const SKSE::stl::scope_exit release([&] { FreeLibrary(module); });
			return { { "address", address }, { "owner", ModuleIdentity(module) },
				{ "reason", "address owner only; forwarding/interception inside a module is not excluded" } };
		}
		json QualityJson(const Quality& q)
		{
			return { { "complete", q.Complete() }, { "overflow", q.overflow }, { "contention", q.contention }, { "unfinished", q.unfinished },
				{ "appGaps", q.appGaps }, { "appDuplicates", q.appDuplicates }, { "invalidClocks", q.invalidClocks }, { "invalidFrames", q.invalidFrames },
				{ "sourceLost", q.sourceLost }, { "resets", q.resets }, { "invalidCompositor", q.invalidCompositor }, { "sourceFailures", q.sourceFailures },
				{ "missingCompletions", q.missingCompletions },
				{ "missingTail", q.missingTail }, { "postrollTimedOut", q.postrollTimedOut } };
		}
		json AppJson(const AppRecord& r, std::optional<Tick> previous, Tick frequency)
		{
			const bool clock = r.completed && frequency > 0 && r.enter > 0 && r.exit >= r.enter;
			const bool interval = previous && frequency > 0 && r.enter > 0 && *previous > 0 && r.enter > *previous;
			const auto& s = r.backend;
			json backend = s.observed ? json{ { "health", s.health }, { "pending", s.pending }, { "desiredValid", s.desiredValid }, { "acceptedValid", s.acceptedValid },
				{ "desiredMode", s.desiredValid ? json(s.desiredMode) : json(nullptr) }, { "acceptedMode", s.acceptedValid ? json(s.acceptedMode) : json(nullptr) },
				{ "desiredCapUs", s.desiredValid ? json(s.desiredCapUs) : json(nullptr) }, { "acceptedCapUs", s.acceptedValid ? json(s.acceptedCapUs) : json(nullptr) },
				{ "lastNativeOperation", s.operation }, { "lastNativeResult", s.result }, { "generation", s.generation }, { "failures", s.failures },
				{ "uptimeMs", s.uptimeMs }, { "retryAtMs", s.retryAtMs } } :
			                            json(nullptr);
			if (s.observed) {
				backend["attemptsObserved"] = s.attemptsObserved;
				backend["settingsAttempted"] = s.attemptsObserved ? json(s.settingsAttempted) : json(nullptr);
				backend["sleepAttempted"] = s.attemptsObserved ? json(s.sleepAttempted) : json(nullptr);
				backend["sleepSkipReason"] = s.attemptsObserved ? json(s.sleepSkipReason) : json(nullptr);
			}
			return { { "sequence", r.sequence }, { "completed", r.completed },
				{ "completionReason", r.completed ? json(nullptr) : json("completion not recorded before freeze; in flight or completion dropped") },
				{ "enterQpc", Timestamp(r.enter) }, { "exitQpc", Timestamp(r.exit) }, { "frame", r.frameAvailable ? json(r.frame) : json(nullptr) },
				{ "backend", backend }, { "backendReason", s.observed ? json(nullptr) : json("minimal mode or UpdateReflex exited before backend observation") },
				{ "clockReason", clock ? json(nullptr) : json("QPC/frequency unavailable or exit precedes entry") },
				{ "thread", r.thread }, { "exitReason", !r.completed ? json(nullptr) : json(r.reason == Exit::Updated ? "updated" : r.reason == Exit::FrameGeneration ? "frame_generation" :
																																r.reason == Exit::Uninitialized       ? "uninitialized" :
																																r.reason == Exit::AdapterUnavailable  ? "adapter_unavailable" :
																																r.reason == Exit::PluginUnavailable   ? "plugin_unavailable" :
																																										"unavailable") },
				{ "updateMs", clock ? json(1000.0 * (r.exit - r.enter) / frequency) : json(nullptr) },
				{ "updateReason", clock ? json(nullptr) : json("completion missing or invalid QPC/frequency") },
				{ "intervalMs", interval ? json(1000.0 * (r.enter - *previous) / frequency) : json(nullptr) },
				{ "intervalReason", interval ? json(nullptr) : json("first record or invalid/nonmonotonic clock") } };
		}
		json NativeJson(const NativeRecord& r, Tick frequency)
		{
			const bool clock = r.completed && frequency > 0 && r.enter > 0 && r.exit >= r.enter;
			const json submitted = r.settings ? json{ { "version", r.version }, { "size", r.size }, { "bLowLatencyMode", r.lowLatency },
				{ "bLowLatencyBoost", r.boost }, { "minimumIntervalUs", r.intervalUs }, { "bUseMarkersToOptimize", r.markers }, { "bUseMinQueueTime", r.minQueue } } :
			                                    json(nullptr);
			return { { "sequence", r.sequence }, { "applicationSequence", r.applicationSequence ? json(r.applicationSequence) : json(nullptr) }, { "completed", r.completed },
				{ "completionReason", r.completed ? json(nullptr) : json("completion not recorded before freeze; actual native entry/result unknown") },
				{ "operationEnterQpc", Timestamp(r.operationEnter) }, { "enterQpc", Timestamp(r.enter) }, { "exitQpc", Timestamp(r.exit) }, { "generation", r.generation }, { "device", r.device },
				{ "admittedQpc", Timestamp(r.admitted) }, { "clockReason", clock ? json(nullptr) : json("QPC/frequency unavailable or exit precedes entry") },
				{ "admissionSource", r.inheritedAdmission ? "application_frame" : "native_operation" },
				{ "frame", r.frameAvailable ? json(r.frame) : json(nullptr) }, { "operation", r.settings ? "settings" : "sleep" }, { "result", r.completed ? json(r.result) : json(nullptr) },
				{ "preparedArguments", submitted }, { "submitted", r.completed ? submitted : json(nullptr) },
				{ "durationMs", clock ? json(1000.0 * (r.exit - r.enter) / frequency) : json(nullptr) },
				{ "durationReason", clock ? json(nullptr) : json("completion missing or invalid QPC/frequency") } };
		}
		json Metric(float value) { return std::isfinite(value) && value >= 0 ? json(value) : json(nullptr); }
		json Positive(float value) { return std::isfinite(value) && value > 0 ? json(value) : json(nullptr); }
		json Offset(float value) { return std::isfinite(value) ? json(value) : json(nullptr); }
		json OffsetInterval(float start, float end)
		{
			if (!std::isfinite(start) || !std::isfinite(end) || (!start && !end))
				return nullptr;
			return Metric(end - start);
		}
		json CompositorJson(const CompositorRecord& r)
		{
			const bool gpuValid = std::isfinite(r.preSubmitGpuMs) && r.preSubmitGpuMs > 0 && std::isfinite(r.totalGpuMs) && r.totalGpuMs > 0;
			auto finite = [](float value) { return std::isfinite(value); };
			const bool poseValid = r.poseValid && r.hmdConnected && std::all_of(r.hmdTransform.begin(), r.hmdTransform.end(), finite) &&
			                       std::all_of(r.hmdLinearVelocity.begin(), r.hmdLinearVelocity.end(), finite) && std::all_of(r.hmdAngularVelocity.begin(), r.hmdAngularVelocity.end(), finite);
			return { { "retrievedQpc", Timestamp(r.retrieved) }, { "frame", r.frame }, { "epoch", r.epoch }, { "presents", r.presents }, { "mispresented", r.mispresented },
				{ "dropped", r.dropped }, { "reprojectionFlags", r.reprojectionFlags },
				{ "systemSeconds", std::isfinite(r.systemSeconds) && r.systemSeconds > 0 ? json(r.systemSeconds) : json(nullptr) },
				{ "clientMs", Positive(r.clientMs) }, { "preSubmitGpuMs", Positive(r.preSubmitGpuMs) }, { "postSubmitGpuMs", gpuValid ? Metric(r.postSubmitGpuMs) : json(nullptr) },
				{ "totalGpuMs", Positive(r.totalGpuMs) }, { "compositorCpuMs", Metric(r.compositorCpuMs) },
				{ "gpuTimingAvailable", gpuValid }, { "gpuTimingReason", gpuValid ? json(nullptr) : json("OpenVR GPU totals are nonfinite/nonpositive; GPU elapsed work unavailable") },
				{ "rawGpuFields", { { "preSubmitGpuMs", r.preSubmitGpuMs }, { "postSubmitGpuMs", r.postSubmitGpuMs },
									  { "totalGpuMs", r.totalGpuMs }, { "compositorGpuMs", r.compositorGpuMs } } },
				{ "hmdPose", { { "available", poseValid }, { "nativePoseValid", r.poseValid }, { "connected", r.hmdConnected }, { "trackingResult", r.trackingResult },
								 { "matrix34", poseValid ? json(r.hmdTransform) : json(nullptr) }, { "linearVelocity", poseValid ? json(r.hmdLinearVelocity) : json(nullptr) },
								 { "angularVelocity", poseValid ? json(r.hmdAngularVelocity) : json(nullptr) }, { "reason", poseValid ? json(nullptr) : json("invalid, disconnected or nonfinite OpenVR pose") } } },
				{ "waitGetPosesCalledMs", Offset(r.waitGetPosesCalledMs) }, { "newPosesReadyMs", Offset(r.newPosesReadyMs) },
				{ "newFrameReadyMs", Offset(r.newFrameReadyMs) }, { "compositorUpdateEndMs", Offset(r.compositorUpdateEndMs) },
				{ "sourceStructureSize", r.sourceStructureSize }, { "compositorGpuMs", Metric(r.compositorGpuMs) },
				{ "compositorIdleCpuMs", Metric(r.compositorIdleCpuMs) }, { "presentCallCpuMs", Metric(r.presentCallCpuMs) },
				{ "waitForPresentCpuMs", Metric(r.waitForPresentCpuMs) }, { "submitFrameMs", Metric(r.submitFrameMs) },
				{ "compositorUpdateStartMs", Offset(r.compositorUpdateStartMs) }, { "compositorRenderStartMs", Offset(r.compositorRenderStartMs) },
				{ "poseWaitMs", OffsetInterval(r.waitGetPosesCalledMs, r.newPosesReadyMs) },
				{ "renderToSubmitMs", OffsetInterval(r.newPosesReadyMs, r.newFrameReadyMs) },
				{ "nullReason", "nonfinite source, negative elapsed interval, or zero-filled offset pair; signed offsets relative to OpenVR systemSeconds are preserved" } };
		}
		json CoverageJson(const Service& s)
		{
			auto extents = [](const auto& rows) -> json {
				Tick firstEntry{}, lastExit{};
				size_t complete{};
				for (const auto& row : rows) {
					if (row.enter > 0 && (!firstEntry || row.enter < firstEntry))
						firstEntry = row.enter;
					if (row.completed) {
						++complete;
						lastExit = std::max(lastExit, row.exit);
					}
				}
				return { { "count", rows.size() }, { "completedCount", complete },
					{ "firstEntryQpc", Timestamp(firstEntry) }, { "lastExitQpc", Timestamp(lastExit) },
					{ "reason", "only completed records supply exit bounds; null native entry can denote an unfinished prepared operation" } };
			};
			Tick firstRequest{}, lastQueryExit{}, firstNativeEntry{}, lastNativeExit{};
			size_t nativeQueries{}, completeQueries{};
			for (const auto& query : s.queries) {
				if (query.requested > 0 && (!firstRequest || query.requested < firstRequest))
					firstRequest = query.requested;
				if (query.completed) {
					++completeQueries;
					lastQueryExit = std::max(lastQueryExit, query.exit);
				}
				if (!query.attempted)
					continue;
				++nativeQueries;
				if (query.enter > 0 && (!firstNativeEntry || query.enter < firstNativeEntry))
					firstNativeEntry = query.enter;
				lastNativeExit = std::max(lastNativeExit, query.exit);
			}
			const auto& rows = s.journal.compositor;
			return { { "application", extents(s.journal.app) }, { "native", extents(s.journal.native) },
				{ "readback", { { "count", s.queries.size() }, { "completedCount", completeQueries }, { "nativeAttemptCount", nativeQueries },
								  { "firstRequestQpc", Timestamp(firstRequest) }, { "lastReceiptExitQpc", Timestamp(lastQueryExit) },
								  { "firstNativeEntryQpc", Timestamp(firstNativeEntry) }, { "lastNativeExitQpc", Timestamp(lastNativeExit) },
								  { "reason", "null native extents indicate no attempted native query or unavailable clock; receipt bounds also include rejected/busy requests" } } },
				{ "compositor", { { "count", rows.size() }, { "firstFrame", rows.empty() ? json(nullptr) : json(rows.front().frame) },
									{ "lastFrame", rows.empty() ? json(nullptr) : json(rows.back().frame) },
									{ "firstSystemSeconds", rows.empty() ? json(nullptr) : json(rows.front().systemSeconds) },
									{ "lastSystemSeconds", rows.empty() ? json(nullptr) : json(rows.back().systemSeconds) },
									{ "firstRetrievalQpc", rows.empty() ? json(nullptr) : Timestamp(rows.front().retrieved) },
									{ "lastRetrievalQpc", rows.empty() ? json(nullptr) : Timestamp(rows.back().retrieved) },
									{ "reason", rows.empty() ? json("no independent completed compositor history") : json(nullptr) } } } };
		}
	}
	json Environment()
	{
		const auto inspected = Backend().TryInspectDiagnosticBinding();
		if (!inspected)
			throw std::runtime_error("native backend busy; identity inspection not acquired");
		const auto& b = *inspected;
		const auto driver = Backend().ReadDriverIdentity();
		json modules = json::object();
		for (const auto name : { "nvapi64.dll", "sl.interposer.dll", "sl.common.dll", "sl.reflex.dll", "sl.pcl.dll", "sl.dlss.dll", "nvngx_dlss.dll", "nvngx_dlssg.dll", "openvr_api.dll", "vrclient_x64.dll" })
			modules[name] = ModuleIdentity(GetModuleHandleA(name));
		return { { "schema", Schema }, { "producer", BuildProvenance::GetProducer() }, { "runtime", REL::Module::get().version().string() },
			{ "driver", { { "result", driver.result }, { "version", driver.result == NVAPI_OK ? json(driver.version) : json(nullptr) },
							{ "branch", driver.result == NVAPI_OK ? json(std::string(driver.branch, strnlen_s(driver.branch, sizeof(driver.branch)))) : json(nullptr) },
							{ "reason", driver.result == NVAPI_OK ? json(nullptr) : json("native driver identity query unavailable or failed") } } },
			{ "driverModule", modules["nvapi64.dll"] }, { "modules", modules }, { "device", b.device }, { "backend", StatusJson(b.status) },
			{ "settingsFunction", FunctionIdentity(b.settingsFunction) }, { "sleepFunction", FunctionIdentity(b.sleepFunction) },
			{ "readbackFunction", FunctionIdentity(b.readbackFunction) }, { "softwareLatency", { { "status", "UNSUPPORTED" }, { "reason", LatencyLimitation } } },
			{ "writerScope", "CSX native writers only; other DLL settings writes are not observed" } };
	}
	json Export(const json& args)
	{
		auto& s = State();
		auto& j = s.journal;
		std::scoped_lock lock(j.mutex);
		if (!j.frozen)
			throw std::logic_error("export requires a frozen capture");
		const auto offset = args.value("offset", size_t{}), limit = args.value("limit", size_t{ 256 });
		if (!limit || limit > 256)
			throw std::invalid_argument("limit must be 1..256");
		const auto total = j.app.size() + j.native.size() + j.compositor.size() + s.queries.size();
		if (offset > total)
			throw std::invalid_argument("offset exceeds frozen export");
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		json records = json::array();
		for (size_t i = offset; i < std::min(total, offset + limit); ++i) {
			auto n = i;
			if (n < j.app.size())
				records.push_back({ { "stream", "application" }, { "ordinal", n }, { "data", AppJson(j.app[n], n ? std::optional(j.app[n - 1].enter) : std::nullopt, frequency.QuadPart) } });
			else if ((n -= j.app.size()) < j.native.size())
				records.push_back({ { "stream", "native" }, { "ordinal", n }, { "data", NativeJson(j.native[n], frequency.QuadPart) } });
			else if ((n -= j.native.size()) < j.compositor.size())
				records.push_back({ { "stream", "compositor" }, { "ordinal", n }, { "data", CompositorJson(j.compositor[n]) } });
			else {
				n -= j.compositor.size();
				records.push_back({ { "stream", "readback" }, { "ordinal", n }, { "data", ReadbackJson(s.queries[n]) } });
			}
		}
		if (offset == s.contiguousExport)
			s.contiguousExport += records.size();
		return { { "captureId", j.id }, { "schema", Schema }, { "offset", offset }, { "total", total },
			{ "nextOffset", offset + records.size() < total ? json(offset + records.size()) : json(nullptr) },
			{ "counts", { { "application", j.app.size() }, { "native", j.native.size() }, { "compositor", j.compositor.size() }, { "readback", s.queries.size() } } },
			{ "records", records }, { "quality", QualityJson(j.quality) }, { "termination", s.termination }, { "workerError", s.workerError },
			{ "coverage", CoverageJson(s) },
			{ "preferencesChanged", s.capturePreferenceConflict },
			{ "controlCallsDuringCapture", s.captureControlCalls },
			{ "timingDefinitions", { { "application", "input-event delivery hook cadence and whole UpdateReflex CPU elapsed time; not hardware input sampling or simulation latency" },
									   { "clientMs", "OpenVR interval between WaitGetPoses calls" },
									   { "preSubmitGpuMs", "OpenVR elapsed scene work submitted between WaitGetPoses and second Submit; may include OS scheduling/other processes" },
									   { "totalGpuMs", "OpenVR elapsed application/compositor work from post-present until compositor work end; not a VR-eye completion timestamp" },
									   { "signedOffsets", "WaitGetPosesCalled/NewPosesReady/NewFrameReady/CompositorUpdate/RenderStart offsets are relative to systemSeconds; NewFrameReady marks the second CPU Submit" },
									   { "completionPolicy", "history is deferred by two compositor frame indices; GPU timings are independently qualified and this is not a GPU-completion fence" },
									   { "hmdPose", "OpenVR pose used by the application for this compositor frame; matrix/linear velocity use metres/metres-per-second, angular velocity radians-per-second. No hardware input-sampling timestamp is claimed" },
									   { "pendingRecords", "entry journals preserve prepared operations whose completion was not recorded; unfinished tickets and missing completions overlap and must not be summed. Prepared arguments do not prove native entry" },
									   { "backendState", "B/C record returned backend state, not a driver getter; last operation/result may refer to an earlier call. uptimeMs/retryAtMs use GetTickCount64 and are not QPC or OpenVR timestamps" },
									   { "healthCodes", { { "0", "Unavailable" }, { "1", "Ready" }, { "2", "Healthy" }, { "3", "Recovering" }, { "4", "Exhausted" }, { "5", "Unsupported" }, { "6", "InvalidDevice" }, { "7", "Failed" } } },
									   { "modeCodes", { { "0", "Off" }, { "1", "On" }, { "2", "Boost" } } },
									   { "operationCodes", { { "0", "Bind" }, { "1", "Settings" }, { "2", "Sleep" } } },
									   { "sleepSkipCodes", { { "0", "Attempted" }, { "1", "NotRequested" }, { "2", "NoFrame" }, { "3", "SameFrame" }, { "4", "Backoff" }, { "5", "BackendUnavailable" }, { "6", "SettingsPending" } } },
									   { "lateStart", "raw OpenVR timing offsets retained; SteamVR UI Late Start classification is not independently derived" },
									   { "readback", "explicitly enabled C-mode queries run at UpdateReflex exit on its calling thread and are included in whole-update/cadence observations" } } },
			{ "condition", s.condition }, { "environment", offset == 0 ? s.environment : json(nullptr) },
			{ "recording", { { "mode", j.mode == Mode::Minimal ? "A" : j.mode == Mode::Passive ? "B" :
																								 "C" },
							   { "owner", s.captureOwner }, { "sessionId", s.captureSession }, { "requestedDurationMs", s.requestedDurationMs },
							   { "requestedCapacity", s.requestedCapacity }, { "applicationCapacity", j.appLimit }, { "nativeCapacity", j.nativeLimit }, { "compositorCapacity", j.compositor.capacity() },
							   { "readbackRequested", s.readbackRequested }, { "compositorRequested", s.compositorRequested },
							   { "queryPeriodMs", s.readbackRequested ? json(s.queryPeriodMs) : json(nullptr) }, { "admissionDelayMs", s.admissionDelayMs },
							   { "requestedHistoryCapacity", HistoryCapacity }, { "maximumHistoryReturned", s.maxHistoryReturned }, { "lastHistoryReturned", s.lastHistoryReturned },
							   { "historyReason", "request capacity does not extend runtime retention; source-index gaps are accounted independently of destination drops" },
							   { "samplePeriodMs", SamplePeriodMs }, { "postrollLimitMs", PostrollMs } } },
			{ "boundaries", { { "applicationAdmissionStartQpc", Timestamp(j.start) }, { "applicationAdmissionStopQpc", Timestamp(j.stop) }, { "freezeQpc", Timestamp(j.freeze) }, { "qpcFrequency", Timestamp(frequency.QuadPart) },
								{ "compositorStartAfterFrame", s.sourceStartQpc ? json(s.sourceStart) : json(nullptr) },
								{ "compositorStopThroughFrame", s.sourceStopQpc ? json(s.sourceTarget) : json(nullptr) },
								{ "compositorStartRetrievalQpc", Timestamp(s.sourceStartQpc) }, { "compositorStopRetrievalQpc", Timestamp(s.sourceStopQpc) },
								{ "unavailableTimingReason", "capture never admitted, clock unavailable, or source not requested/available; consult termination and quality" },
								{ "clockRelationship", "QPC retrieval and OpenVR systemSeconds are separate clocks; no exact application-to-compositor-frame mapping is asserted" } } },
			{ "scope", { { "latency", "UNSUPPORTED" }, { "latencyReason", LatencyLimitation }, { "pre12Restoration", "NOT_MEASURED; requires separate matched original-Pre12 process" } } } };
	}
	json Readiness(const json& args)
	{
		auto& s = State();
		const auto inspected = Backend().TryInspectDiagnosticBinding();
		const auto b = inspected.value_or(Reflex::DiagnosticBinding{});
		const auto lease = LeaseJson();
		auto item = [](const char* status, const char* reason) { return json{ { "status", status }, { "reason", reason } }; };
		const auto& j = s.journal;
		const auto& q = j.quality;
		const bool captured = j.frozen && s.captureSession == s.session && j.start >= s.preparedAt && j.start > 0;
		bool app = captured && j.app.size() >= 2;
		for (size_t i = 0; i < j.app.size(); ++i) {
			const auto& row = j.app[i];
			app &= row.completed && row.enter > 0 && row.exit >= row.enter && row.frameAvailable &&
			       (!i || (row.enter > j.app[i - 1].enter && row.frame == j.app[i - 1].frame + 1));
		}
		const bool comp = captured && !j.compositor.empty() && !q.sourceLost && !q.sourceFailures &&
		                  !q.invalidCompositor && !q.resets && !q.missingTail;
		const bool updated = captured && std::any_of(j.app.begin(), j.app.end(), [](auto& row) { return row.completed && row.reason == Exit::Updated; });
		const bool sleeping = captured && std::any_of(j.native.begin(), j.native.end(), [](auto& row) { return row.completed && !row.settings && row.result == NVAPI_OK; });
		const bool bindingMatches = inspected && b.status.deviceBound && b.status.deviceGeneration == s.environment.at("backend").at("generation").get<uint64_t>();
		const bool generationMatches = captured && bindingMatches && b.status.deviceGeneration == s.condition.at("backend").at("generation").get<uint64_t>() &&
		                               std::all_of(j.native.begin(), j.native.end(), [&](auto& row) { return row.generation == b.status.deviceGeneration; });
		const bool sleepRequested = s.condition.contains("requested") && j.mode != Mode::Minimal &&
		                            (s.condition.at("requested").at("enabled").get<bool>() || s.condition.at("requested").at("capped").get<bool>());
		const bool captureQuality = captured && q.Complete() && !s.capturePreferenceConflict && !s.captureControlCalls;
		json result{ { "build", item("READY", "expectedBuildId and schema checked on this command") },
			{ "device", item(bindingMatches ? "READY" : inspected && b.status.health == Reflex::Health::Unsupported ? "UNSUPPORTED" :
																													  "FAILED",
							bindingMatches ? "owned native binding matches prepared generation" : "native binding unavailable, changed generation, or busy") },
			{ "captureGeneration", item(generationMatches ? "READY" : "FAILED", generationMatches ? "prepared, captured and current native generations match" : "no fresh matching capture-generation receipt") },
			{ "nativeFunctions", item(b.settingsFunction && b.sleepFunction ? "READY" : inspected ? "UNSUPPORTED" :
																									"FAILED",
									 b.settingsFunction && b.sleepFunction ? "settings and sleep resolved" : inspected ? "native entry point missing" :
																														 "backend busy; fresh native function snapshot unavailable") },
			{ "application", item(app ? "READY" : "FAILED", app ? "fresh application entries, completions and frame progression have valid QPC" : "application source missing, unfinished, or invalid") },
			{ "nativeExecution", item(!sleepRequested ? "NOT_REQUESTED" : updated && sleeping && generationMatches                ? "READY" :
																	  inspected && b.status.health == Reflex::Health::Unsupported ? "UNSUPPORTED" :
																																	"FAILED",
									 !sleepRequested ? "minimal mode or uncapped Off does not request native Sleep evidence" : updated && sleeping && generationMatches ? "fresh successful native sleep observed; execution is not pacing efficacy" :
																																										  "no matching completed successful native sleep") },
			{ "compositor", item(!s.compositorRequested ? "NOT_REQUESTED" : comp            ? "READY" :
																		REL::Module::IsVR() ? "FAILED" :
																							  "UNSUPPORTED",
								!s.compositorRequested ? "minimal observer does not collect compositor history" : comp ? "independent frame-delivery history and bounded post-roll; GPU work separately qualified" :
																														 "VR timing missing, invalid, or incomplete") },
			{ "softwareLatency", item(args.value("latency", false) ? "UNSUPPORTED" : "NOT_REQUESTED", LatencyLimitation) },
			{ "captureQuality", item(captureQuality ? "READY" : "FAILED", captureQuality ? "capture counters and control/preference ownership are complete" : "capture incomplete or acquisition overlapped control/preference changes") },
			{ "export", item(j.frozen && s.captureSession == s.session && s.captureReleased ? "READY" : "FAILED", "requires frozen capture, contiguous export, and client acknowledgement after schema/count/boundary validation") },
			{ "restoration", item(lease.at("active") == true && lease.at("conflict") == false ? "READY" : "FAILED", "uncontested preference lease required; restoration needs later native acceptance") },
			{ "capacity", { { "status", j.id && j.appLimit && j.nativeLimit ? "READY" : "FAILED" }, { "reason", "preallocated destination and entry-journal limits; quality independently accounts for drops" },
							  { "applicationCapacity", j.appLimit }, { "nativeCapacity", j.nativeLimit }, { "compositorCapacity", j.compositor.capacity() },
							  { "historyCapacity", HistoryCapacity }, { "samplePeriodMs", SamplePeriodMs }, { "mode", static_cast<int>(j.mode) } } } };

		if (!args.value("readback", false))
			result["readback"] = item("NOT_REQUESTED", "optional getter not requested");
		else if (!captured || !s.readbackRequested || s.queries.empty())
			result["readback"] = item("FAILED", "run and export a bounded C-mode acquisition before checking getter readiness");
		else {
			const auto& r = s.queries.back();
			const bool valid = r.completed && r.result == NVAPI_OK && r.attempted && r.generationKnown && inspected && r.generation == b.status.deviceGeneration && r.enter > 0 && r.exit >= r.enter &&
			                   r.raw.version == NV_GET_SLEEP_STATUS_PARAMS_VER;
			const bool unsupported = r.completed && (r.result == NVAPI_NOT_SUPPORTED || r.result == NVAPI_NO_IMPLEMENTATION || r.result == NVAPI_FUNCTION_NOT_FOUND);
			result["readback"] = item(valid ? "READY" : unsupported ? "UNSUPPORTED" :
																	  "FAILED",
				valid ? "getter returned a versioned timestamped result; corroboration only" : "getter unavailable, busy, failed, or returned invalid version/clock; backend health unchanged");
			result["readback"]["receipt"] = ReadbackJson(r);
		}
		return { { "items", result }, { "backend", inspected ? StatusJson(b.status) : json(nullptr) }, { "restoration", lease },
			{ "backendReason", inspected ? json(nullptr) : json("backend busy; no fresh snapshot") },
			{ "collectorReadinessIsEfficacy", false }, { "clientExportValidationRequired", true } };
	}
}
#endif
