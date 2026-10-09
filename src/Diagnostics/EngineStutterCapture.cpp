#include "Diagnostics/EngineStutterCapture.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/ServiceFoundation.h"
#	include <Windows.h>
#	include <chrono>
#	include <cmath>
#	include <deque>
#	include <mutex>
#	include <stdexcept>
#	include <string>
#	include <thread>
#	include <utility>
#	include <wct.h>
#	include <winrt/base.h>

namespace CSX::Diagnostics::Stutters
{
	using json = nlohmann::json;
	namespace
	{
		constexpr unsigned kPollMs = 10, kRepeatMs = 250, kWaitTimeoutMs = 10;
		constexpr unsigned kMaximumRecords = 4096, kRetainedRecords = 256, kMaximumRequestBytes = 8192;
		constexpr double kMinThresholdMs = 10, kMaxThresholdMs = 1000, kDefaultThresholdMs = 50;
		constexpr double kMinDurationSeconds = 1, kMaxDurationSeconds = 600, kDefaultDurationSeconds = 120;
		std::uint64_t FileTicks(FILETIME value) noexcept { return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
		struct CpuSample
		{
			std::string_view status = "unavailable";
			DWORD error = 0;
			Tick begin = 0, end = 0;
			std::uint64_t delta = 0, creation = 0;
			json Json(Tick frequency) const
			{
				const bool captured = status == "captured";
				return { { "status", status }, { "win32Error", error }, { "threadCreationFiletime", creation },
					{ "qpcBegin", begin }, { "qpcEnd", end }, { "cpuMs", captured ? json(static_cast<double>(delta) / 10000.0) : json(nullptr) },
					{ "windowMs", captured ? json(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency)) : json(nullptr) } };
			}
		};
		struct CpuProbe
		{
			DWORD thread = 0;
			winrt::handle handle;
			Tick qpc = 0;
			std::uint64_t total = 0, creation = 0;
			CpuSample Sample(DWORD id) noexcept
			{
				CpuSample sample;
				if (thread != id) {
					handle.close();
					thread = id;
					qpc = 0;
				}
				if (!handle) {
					handle.attach(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id));
					if (!handle) {
						sample.error = GetLastError();
						return sample;
					}
					qpc = 0;
				}
				FILETIME create{}, exit{}, kernel{}, user{};
				if (!GetThreadTimes(handle.get(), &create, &exit, &kernel, &user)) {
					sample.error = GetLastError();
					handle.close();
					qpc = 0;
					return sample;
				}
				if (FileTicks(exit)) {
					handle.close();
					qpc = 0;
					sample.status = "thread_exited";
					return sample;
				}
				const auto now = ReadCounter();
				const auto identity = FileTicks(create), current = FileTicks(kernel) + FileTicks(user);
				sample.creation = identity;
				sample.end = now;
				if (!now) {
					sample.status = "clock_unavailable";
					qpc = 0;
					return sample;
				}
				sample.status = "baseline";
				if (qpc && now > qpc && creation == identity && current >= total) {
					sample.status = "captured";
					sample.begin = qpc;
					sample.delta = current - total;
				}
				qpc = now;
				total = current;
				creation = identity;
				return sample;
			}
		};
		struct WaitProbe
		{
			HWCT session = nullptr;
			std::array<WAITCHAIN_NODE_INFO, WCT_MAX_NODE_COUNT> nodes{};
			DWORD count = WCT_MAX_NODE_COUNT, error = ERROR_IO_PENDING;
			BOOL cycle = FALSE;
			std::atomic_bool ready{ false };
			bool pending = false, timedOut = false, boundary = false;
			Phase phase;
			Tick begin = 0, completed = 0;
			std::uint64_t observation = 0;
			~WaitProbe()
			{
				if (session)
					CloseThreadWaitChainSession(session);
			}
			static void CALLBACK Callback(HWCT, DWORD_PTR context, DWORD status, LPDWORD count, PWAITCHAIN_NODE_INFO nodes, LPBOOL cycle) noexcept
			{
				auto& probe = *reinterpret_cast<WaitProbe*>(context);
				probe.error = status;
				probe.count = count ? std::min(*count, static_cast<DWORD>(WCT_MAX_NODE_COUNT)) : 0;
				if (nodes && nodes != probe.nodes.data())
					std::copy_n(nodes, probe.count, probe.nodes.begin());
				probe.cycle = cycle ? *cycle : FALSE;
				probe.completed = ReadCounter();
				probe.ready.store(true, std::memory_order_release);
			}
			json Begin(const Phase& observed, bool isBoundary, std::uint64_t sequence)
			{
				if (pending)
					return { { "status", "busy" }, { "pendingObservationSequence", observation } };
				if (!session)
					session = OpenThreadWaitChainSession(WCT_ASYNC_OPEN_FLAG, Callback);
				if (!session)
					return { { "status", "unavailable" }, { "win32Error", GetLastError() } };
				phase = observed;
				boundary = isBoundary;
				observation = sequence;
				begin = ReadCounter();
				count = WCT_MAX_NODE_COUNT;
				error = ERROR_IO_PENDING;
				cycle = FALSE;
				timedOut = false;
				ready.store(false, std::memory_order_relaxed);
				pending = true;
				const bool accepted = GetThreadWaitChain(session, reinterpret_cast<DWORD_PTR>(this), 0, phase.thread, &count, nodes.data(), &cycle) != FALSE;
				const auto immediateError = accepted ? ERROR_SUCCESS : GetLastError();
				if (!accepted && immediateError != ERROR_IO_PENDING) {
					error = immediateError;
					count = 0;
					completed = ReadCounter();
					ready.store(true, std::memory_order_release);
				}
				return { { "status", "pending" }, { "observationSequence", observation }, { "qpcBegin", begin } };
			}
			json Result() const
			{
				const bool valid = error == ERROR_SUCCESS || error == ERROR_MORE_DATA || error == ERROR_TOO_MANY_THREADS;
				bool complete = error == ERROR_SUCCESS && count > 0;
				json result{ { "status", valid ? "captured" : "unavailable" }, { "win32Error", error },
					{ "cycleDetected", valid ? json(cycle != FALSE) : json(nullptr) }, { "nodes", json::array() } };
				if (valid)
					for (DWORD i = 0; i < std::min(count, static_cast<DWORD>(WCT_MAX_NODE_COUNT)); ++i) {
						const auto& node = nodes[i];
						static constexpr std::array typeNames{ "invalid", "critical_section", "send_message", "mutex", "alpc", "com", "thread_wait", "process_wait", "thread", "com_activation", "unknown", "socket_io", "smb_io" };
						const auto type = static_cast<unsigned>(node.ObjectType);
						json item{ { "type", type }, { "typeName", type < typeNames.size() ? typeNames[type] : "unrecognized" }, { "status", static_cast<unsigned>(node.ObjectStatus) } };
						complete &= node.ObjectStatus != WctStatusNoAccess && node.ObjectStatus != WctStatusUnknown && node.ObjectStatus != WctStatusError &&
						            node.ObjectStatus != WctStatusPidOnly && node.ObjectStatus != WctStatusPidOnlyRpcss;
						if (node.ObjectType == WctThreadType) {
							item["processId"] = node.ThreadObject.ProcessId;
							item["threadId"] = node.ThreadObject.ThreadId;
							item["contextSwitches"] = node.ThreadObject.ContextSwitches;
							item["waitTimeRaw"] = node.ThreadObject.WaitTime;
						} else {
							item["objectNameUtf16"] = json::array();
							for (const auto character : node.LockObject.ObjectName) {
								if (!character)
									break;
								item["objectNameUtf16"].push_back(static_cast<unsigned>(character));
							}
						}
						result["nodes"].push_back(std::move(item));
					}
				result["complete"] = complete;
				return result;
			}
		};
		json ContextJson(const Context& context)
		{
			return { { "qpc", context.qpc }, { "frame", context.frame }, { "cellFormId", context.cell },
				{ "playerAvailable", context.playerAvailable }, { "position", context.position }, { "loading", context.loading },
				{ "paused", context.paused }, { "shaderCompiling", context.compiling }, { "semantics", "last_game_thread_observation_not_live_during_stall" } };
		}
	}
	Tick ReadCounter() noexcept
	{
		LARGE_INTEGER value{};
		return QueryPerformanceCounter(&value) && value.QuadPart > 0 ? value.QuadPart : 0;
	}
	struct Capture::Impl
	{
		CaptureEnvironment environment;
		Recorder& recorder;
		std::mutex commandMutex, outputMutex;
		std::jthread worker;
		std::deque<json> retained;
		std::string path, captureId;
		std::array<char, 512> failure{};
		std::string_view finishReason = "never_started";
		std::uint64_t sequence = 0, droppedEvents = 0, omittedEvents = 0;
		Tick frequency = 0, threshold = 0, deadline = 0;
		std::atomic_bool active{ false }, done{ true }, stopRequested{ false }, cleanupPending{ false };
		bool journalFinalized = false;
		winrt::handle file;
		Impl(CaptureEnvironment env, Recorder& source) : environment(std::move(env)), recorder(source) {}
		~Impl()
		{
			recorder.Stop();
			stopRequested.store(true);
			if (worker.joinable())
				worker.join();
		}
		double Milliseconds(Tick ticks) const { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency); }
		void Write(const json& value)
		{
			const auto line = value.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
			DWORD written = 0;
			if (line.size() > MAXDWORD)
				throw std::runtime_error("stutter journal record exceeds Win32 write size");
			const bool accepted = WriteFile(file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != FALSE;
			if (!accepted || written != line.size())
				throw std::runtime_error("stutter journal write failed: " + std::to_string(accepted ? ERROR_WRITE_FAULT : GetLastError()));
		}
		void Emit(json value)
		{
			value["captureId"] = captureId;
			value["sequence"] = sequence + 1;
			Write(value);
			++sequence;
			std::lock_guard lock(outputMutex);
			if (retained.size() == kRetainedRecords)
				retained.pop_front();
			retained.push_back(std::move(value));
		}
		json Record(const Phase& phase, Tick end, std::string_view kind, const Batch& batch, bool complete) const
		{
			return { { "kind", kind }, { "phase", phase.name.data() }, { "scopeId", phase.id }, { "captureEpoch", phase.epoch },
				{ "threadId", phase.thread }, { "qpcBegin", phase.begin }, { "qpcEnd", end }, { "durationMs", Milliseconds(end - phase.begin) },
				{ "phaseEvidenceComplete", complete }, { "phaseNameTruncated", phase.nameTruncated }, { "contextAtPhaseEntry", ContextJson(phase.context) },
				{ "lastGameContext", ContextJson(batch.context) }, { "lostHookUpdates", batch.lostUpdates }, { "droppedCompletedEvents", batch.droppedEvents } };
		}
		void Consume(const Batch& batch)
		{
			unsigned consumed = 0;
			for (; consumed < batch.count && sequence < kMaximumRecords - 1; ++consumed) {
				const auto& event = batch.events[consumed];
				Emit(Record(event.phase, event.end, event.gap ? "boundary_gap" : "slow_scope", batch, event.complete));
			}
			std::lock_guard lock(outputMutex);
			omittedEvents += batch.count - consumed;
			droppedEvents = batch.droppedEvents;
		}
		void PollWaits(std::array<WaitProbe, kThreadCapacity>& waits, Tick now)
		{
			for (auto& wait : waits) {
				if (!wait.pending || sequence >= kMaximumRecords - 1)
					continue;
				if (wait.ready.load(std::memory_order_acquire)) {
					auto result = wait.Result();
					result.update({ { "kind", "wait_chain_result" }, { "observationSequence", wait.observation },
						{ "scopeId", wait.phase.id }, { "threadId", wait.phase.thread }, { "qpcBegin", wait.begin }, { "qpcEnd", wait.completed }, { "qpcObserved", ReadCounter() },
						{ "timingAvailable", wait.begin > 0 && wait.completed > 0 }, { "completedAfterTimeout", wait.timedOut }, { "observationStillCurrent", recorder.StillCurrent(wait.phase, wait.boundary) } });
					Emit(std::move(result));
					wait.pending = false;
				} else if (!wait.timedOut && now - wait.begin >= frequency * kWaitTimeoutMs / 1000) {
					wait.timedOut = true;
					Emit({ { "kind", "wait_chain_timeout" }, { "observationSequence", wait.observation }, { "scopeId", wait.phase.id },
						{ "threadId", wait.phase.thread }, { "qpcBegin", wait.begin }, { "qpcEnd", now }, { "status", "timeout_pending_cleanup" } });
				}
			}
		}
		void ReportFailure(const char* message)
		{
			std::lock_guard lock(outputMutex);
			failure.fill(0);
			const std::string_view detail(message);
			std::copy_n(detail.begin(), std::min(detail.size(), failure.size() - 1), failure.begin());
			finishReason = "capture_failed";
		}
		void Run() noexcept
		{
			// Storage survives until Windows cancellation callbacks finish, after evidence is finalized.
			{
				std::array<WaitProbe, kThreadCapacity> waits;
				try {
					std::array<CpuProbe, kThreadCapacity> cpu;
					std::array<Tick, kThreadCapacity> lastSample{};
					std::array<std::uint64_t, kThreadCapacity> lastScope{};
					std::array<bool, kThreadCapacity> lastBoundary{};
					std::string_view reason;
					for (;;) {
						const auto now = ReadCounter();
						if (!now)
							throw std::runtime_error("QPC unavailable during capture");
						if (stopRequested.load()) {
							reason = "requested";
							break;
						}
						if (environment.shutdownRequested && environment.shutdownRequested()) {
							reason = "game_shutdown";
							break;
						}
						if (now >= deadline) {
							reason = "duration_limit";
							break;
						}
						if (sequence >= kMaximumRecords - 1) {
							reason = "record_limit";
							break;
						}
						auto batch = recorder.Drain(now);
						Consume(batch);
						PollWaits(waits, now);
						for (unsigned i = 0; i < batch.active.size() && sequence < kMaximumRecords - 1; ++i) {
							if (!batch.active[i].thread)
								continue;
							const auto cpuSample = cpu[i].Sample(batch.active[i].thread);
							auto phase = batch.overdue[i];
							bool boundary = false;
							if (!phase.id)
								for (const auto& candidate : batch.boundaries)
									if (candidate.thread == batch.active[i].thread && candidate.id && now >= candidate.begin && now - candidate.begin >= threshold &&
										(!phase.id || candidate.begin < phase.begin)) {
										phase = candidate;
										boundary = true;
									}
							if (!phase.id || (lastScope[i] == phase.id && lastBoundary[i] == boundary && now - lastSample[i] < frequency * kRepeatMs / 1000))
								continue;
							lastSample[i] = now;
							lastScope[i] = phase.id;
							lastBoundary[i] = boundary;
							auto event = Record(phase, now, boundary ? "missing_boundary_sample" : "active_stall_sample", batch, phase.lossVersion == batch.lostUpdates);
							event["activePhaseAtObservation"] = batch.active[i].id ? json(batch.active[i].name.data()) : json(nullptr);
							event["threadCpu"] = cpuSample.Json(frequency);
							event["waitChain"] = waits[i].Begin(phase, boundary, sequence + 1);
							event["observationStillCurrent"] = recorder.StillCurrent(phase, boundary);
							event["attribution"] = "observed_scope_and_wait_evidence_not_proven_root_cause";
							Emit(std::move(event));
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
					}
					recorder.Stop();
					const auto end = ReadCounter();
					if (!end)
						throw std::runtime_error("QPC unavailable at capture stop");
					const auto batch = recorder.Drain(end);
					Consume(batch);
					PollWaits(waits, end);
					json open = json::array(), pending = json::array();
					for (const auto& phase : batch.active)
						if (phase.id)
							open.push_back(Record(phase, end, "scope_open_at_stop", batch, phase.lossVersion == batch.lostUpdates));
					for (const auto& wait : waits)
						if (wait.pending)
							pending.push_back({ { "observationSequence", wait.observation }, { "threadId", wait.phase.thread }, { "timedOut", wait.timedOut } });
					Emit({ { "kind", "capture_stopped" }, { "reason", reason }, { "qpc", end }, { "lostHookUpdates", batch.lostUpdates },
						{ "droppedCompletedEvents", batch.droppedEvents }, { "omittedAtRecordLimit", omittedEvents }, { "openScopes", std::move(open) },
						{ "pendingWaitChains", std::move(pending) }, { "semantics", "pending_wait_results_are_not_part_of_finalized_journal" } });
					if (!FlushFileBuffers(file.get()))
						throw std::runtime_error("stutter journal flush failed: " + std::to_string(GetLastError()));
					std::lock_guard lock(outputMutex);
					finishReason = reason;
					journalFinalized = true;
				} catch (const std::exception& error) {
					recorder.Stop();
					ReportFailure(error.what());
				} catch (...) {
					recorder.Stop();
					ReportFailure("unknown stutter worker failure");
				}
				file.close();
				cleanupPending.store(true, std::memory_order_release);
				active.store(false, std::memory_order_release);
			}
			cleanupPending.store(false, std::memory_order_release);
			done.store(true, std::memory_order_release);
		}
		json Status(bool events)
		{
			std::lock_guard lock(outputMutex);
			json result{ { "schema", "csx-engine-stutters-v1" }, { "active", active.load(std::memory_order_acquire) }, { "recordingEnabled", recorder.Enabled() },
				{ "workerDone", done.load(std::memory_order_acquire) }, { "cleanupPending", cleanupPending.load(std::memory_order_acquire) },
				{ "stopRequested", stopRequested.load() }, { "journalFinalized", journalFinalized }, { "captureId", captureId }, { "journalPath", path },
				{ "failure", failure[0] ? json(failure.data()) : json(nullptr) }, { "finishReason", finishReason },
				{ "lostHookUpdates", recorder.LostUpdates() }, { "droppedCompletedEvents", droppedEvents }, { "omittedAtRecordLimit", omittedEvents },
				{ "firstRetainedSequence", retained.empty() ? json(nullptr) : retained.front().at("sequence") },
				{ "lastRetainedSequence", retained.empty() ? json(nullptr) : retained.back().at("sequence") },
				{ "retainedEventCapacity", kRetainedRecords }, { "maximumRecords", kMaximumRecords }, { "qpcFrequency", frequency }, { "recordingDeadlineQpc", deadline },
				{ "pollMs", kPollMs }, { "sampleRepeatMs", kRepeatMs }, { "thresholdMs", frequency ? Milliseconds(threshold) : 0.0 },
				{ "coverage", "SE/AE/VR engine update, CSX CPU pass scopes, DXGI Present, VR WaitGetPoses and stereo submit; no stack sampling or GPU execution attribution" },
				{ "eventSemantics", "overlapping observations, not a count of distinct stutters; missing boundaries may include loading, pause, minimization or shutdown" } };
			if (events)
				result["events"] = retained;
			return result;
		}
		json Start(const json& args)
		{
			if (!done.load(std::memory_order_acquire))
				return { { "error", "previous capture is active or finalizing; poll status before starting another" } };
			if (worker.joinable())
				worker.join();
			const auto thresholdMs = args.value("thresholdMs", kDefaultThresholdMs), duration = args.value("durationSeconds", kDefaultDurationSeconds);
			LARGE_INTEGER clock{};
			const auto now = ReadCounter();
			if (!QueryPerformanceFrequency(&clock) || clock.QuadPart <= 0 || !now)
				throw std::runtime_error("QPC unavailable");
			const auto directory = environment.logDirectory();
			if (!directory)
				throw std::runtime_error("SKSE log directory unavailable");
			auto id = Api::ServiceFoundation::NewId();
			const auto journalPath = *directory / ("CSX-EngineStutters-" + id + ".jsonl");
			const auto utf8Path = journalPath.u8string();
			std::string newPath(utf8Path.begin(), utf8Path.end());
			json header{ { "kind", "capture_started" }, { "schema", "csx-engine-stutters-v1" }, { "producer", environment.producer() },
				{ "processId", GetCurrentProcessId() }, { "vr", environment.vr }, { "qpc", now }, { "qpcFrequency", clock.QuadPart },
				{ "thresholdMs", thresholdMs }, { "durationSeconds", duration },
				{ "timingSemantics", "CPU wall duration includes waits; scope times overlap; DXGI is a desktop mirror in VR" },
				{ "limitations", "WCT cannot explain all synchronization primitives. Single-node or unavailable chains do not prove CPU work. WaitTime is preserved without assumed units. Correlate QPC/thread IDs with ETW stack/wait tracing for uninstrumented code and GPU attribution." } };
			const auto raw = CreateFileW(journalPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (raw == INVALID_HANDLE_VALUE)
				throw std::runtime_error("cannot create stutter journal: " + std::to_string(GetLastError()));
			file.attach(raw);
			{
				std::lock_guard lock(outputMutex);
				path.swap(newPath);
				captureId.swap(id);
				frequency = clock.QuadPart;
				threshold = static_cast<Tick>(thresholdMs * static_cast<double>(frequency) / 1000.0);
				deadline = now + static_cast<Tick>(duration * static_cast<double>(frequency));
				sequence = 0;
				droppedEvents = 0;
				omittedEvents = 0;
				failure.fill(0);
				retained.clear();
				finishReason = "running";
				journalFinalized = false;
			}
			try {
				Emit(std::move(header));
				stopRequested.store(false);
				done.store(false);
				active.store(true);
				recorder.Start(threshold, now, deadline);
				worker = std::jthread([this] { Run(); });
			} catch (...) {
				recorder.Stop();
				active.store(false);
				done.store(true);
				file.close();
				ReportFailure("capture startup failed");
				throw;
			}
			return Status(false);
		}
		json Dispatch(const json& args)
		{
			if (!args.is_object())
				return { { "error", "arguments must be an object" } };
			for (const auto& [key, value] : args.items()) {
				if (key != "action" && key != "thresholdMs" && key != "durationSeconds" && key != "expectedBuildId")
					return { { "error", "unknown argument: " + key } };
				if (key == "action" || key == "expectedBuildId") {
					if (!value.is_string())
						return { { "error", key + " must be a string" } };
				} else {
					if (!value.is_number())
						return { { "error", key + " must be a number" } };
					const double number = value.get<double>();
					const bool thresholdField = key == "thresholdMs";
					if (!std::isfinite(number) || number < (thresholdField ? kMinThresholdMs : kMinDurationSeconds) || number > (thresholdField ? kMaxThresholdMs : kMaxDurationSeconds))
						return { { "error", key + " is outside the supported range" } };
				}
			}
			if (environment.validateBuild)
				if (auto mismatch = environment.validateBuild(args))
					return std::move(*mismatch);
			std::lock_guard lock(commandMutex);
			const auto action = args.value("action", std::string("status"));
			if (action == "start")
				return Start(args);
			if (action == "stop") {
				recorder.Stop();
				stopRequested.store(true);
				return Status(false);
			}
			if (action == "status" || action == "events")
				return Status(action == "events");
			return { { "error", "unknown action" } };
		}
	};
	Capture::Capture(CaptureEnvironment environment) : impl_(std::make_unique<Impl>(std::move(environment), recorder)) {}
	Capture::~Capture() = default;
	json Capture::Dispatch(const json& args)
	{
		json result;
		try {
			result = impl_->Dispatch(args);
		} catch (const std::exception& error) {
			result = { { "error", error.what() } };
		}
		result["producer"] = impl_->environment.producer();
		return result;
	}
	json Capture::Request(const char* input)
	{
		json args, result;
		try {
			if (!input || !*input)
				args = json::object();
			else {
				unsigned length = 0;
				while (length <= kMaximumRequestBytes && input[length]) ++length;
				if (length > kMaximumRequestBytes)
					throw std::runtime_error("request exceeds " + std::to_string(kMaximumRequestBytes) + " bytes");
				args = json::parse(input, input + length);
			}
		} catch (const std::exception& error) {
			return { { "error", error.what() }, { "producer", impl_->environment.producer() } };
		}
		return Dispatch(args);
	}
	void Capture::Respond(const char* input, void* sink, void (*write)(void*, const char*)) noexcept
	{
		std::string response;
		constexpr auto fallback = R"({"error":"stutter diagnostic response failed","producer":null})";
		try {
			response = Request(input).dump(-1, ' ', false, json::error_handler_t::replace);
		} catch (...) {
			try {
				response = json{ { "error", "stutter diagnostic response failed" }, { "producer", impl_->environment.producer() } }.dump();
			} catch (...) {
				OutputDebugStringA("CSX: stutter response allocation or producer unavailable\n");
			}
		}
		try {
			if (write)
				write(sink, response.empty() ? fallback : response.c_str());
			else
				OutputDebugStringA("CSX: stutter response writer unavailable\n");
		} catch (...) {
			OutputDebugStringA("CSX: DevBench stutter response writer failed\n");
		}
	}
	json Capture::Descriptor()
	{
		return { { "description", "Opt-in DevBench-only stutter journal. Captures bounded CPU scopes, engine/DXGI/OpenVR boundary gaps, active stalls, thread CPU deltas and asynchronous Windows wait chains. No game-thread dispatch, thread suspension, stack sampling or GPU execution attribution. Scope and gap observations overlap. Loading/pause/compilation context and all evidence losses remain explicit. Hooks never wait or allocate. Full exclusive JSONL journal in the SKSE log directory; events retains bounded recent history. Stop requests immediate hook shutdown and returns promptly; poll workerDone/journalFinalized before restart. Wait cleanup may outlive journal finalization. Every response includes producer identity." },
			{ "inputSchema", { { "type", "object" }, { "properties", { { "action", { { "type", "string" }, { "enum", { "start", "status", "events", "stop" } }, { "default", "status" } } }, { "thresholdMs", { { "type", "number" }, { "minimum", kMinThresholdMs }, { "maximum", kMaxThresholdMs }, { "default", kDefaultThresholdMs } } }, { "durationSeconds", { { "type", "number" }, { "minimum", kMinDurationSeconds }, { "maximum", kMaxDurationSeconds }, { "default", kDefaultDurationSeconds } } }, { "expectedBuildId", { { "type", "string" } } } } }, { "additionalProperties", false } } } };
	}
}
#endif
