#include "Diagnostics/ISLDiagnosticsInternal.h"
#include "Diagnostics/ReflexMeasurementInternal.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/DevBenchMainThreadDispatch.h"
#	include "Features/Upscaling.h"
#	include "Globals.h"
#	include "Utils/OpenVRFrameTiming.h"
#	include <chrono>
#	include <cstring>

namespace CSX::Diagnostics::ReflexMeasurement
{
	Service& State()
	{
		// Process-lifetime storage keeps admitted callbacks valid during process teardown.
		static auto* state = new Service;
		return *state;
	}
	namespace
	{
		thread_local Journal::Ticket applicationAdmission;
	}
	Journal& Records() { return State().journal; }
	Reflex::Backend& Backend() { return Upscaling::streamline.reflexBackend; }
	Journal::Ticket BeginNativeOperation()
	{
		auto& j = Records();
		if (applicationAdmission.admitted && j.mode != Mode::Minimal)
			return j.AdmitChild(applicationAdmission);
		return j.BeginTimed(Clock, true);
	}
	bool NativeTracingActive()
	{
		return Records().nativeEnabled.load(std::memory_order_acquire) ||
		       (applicationAdmission.admitted && Records().mode != Mode::Minimal);
	}
	Tick Clock() noexcept
	{
		LARGE_INTEGER value{};
		return QueryPerformanceCounter(&value) && value.QuadPart > 0 ? value.QuadPart : 0;
	}
	Preferences CurrentPreferences()
	{
		const auto& s = globals::features::upscaling.settings;
		return { s.reflexLowLatencyMode, s.reflexLowLatencyBoost, s.reflexUseFPSLimit, s.reflexUseMarkersToOptimize, s.reflexFPSLimit };
	}
	void ApplyPreferences(Preferences p)
	{
		auto& s = globals::features::upscaling.settings;
		s.reflexLowLatencyMode = p.enabled;
		s.reflexLowLatencyBoost = p.boost;
		s.reflexUseFPSLimit = p.capped;
		s.reflexUseMarkersToOptimize = p.markers;
		s.reflexFPSLimit = p.fps;
	}
	Reflex::Options EffectiveOptions(Preferences p, bool constrained)
	{
		Reflex::Options result{};
		if (!constrained) {
			result.mode = p.enabled ? p.boost ? Reflex::Mode::Boost : Reflex::Mode::On : Reflex::Mode::Off;
			result.frameLimitUs = p.capped ? static_cast<uint32_t>(std::lround(1000000.0 / std::clamp(p.fps, 20.0f, 240.0f))) : 0;
		}
		return result;
	}
	json OnMain(std::function<json()> fn)
	{
		auto result = CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(fn));
		if (result.contains("error"))
			throw std::runtime_error(result.dump());
		return result;
	}
	UpdateScope::UpdateScope(uint32_t frame, bool available)
	{
		ticket = Records().BeginTimed(Clock);
		row.frame = frame;
		row.frameAvailable = available;
		row.enter = ticket.enter;
		if (ticket.admitted) {
			previous = applicationAdmission;
			applicationAdmission = ticket;
		}
		if (ticket.admitted)
			row.thread = GetCurrentThreadId();
		Records().RecordStart(ticket, row, Records().appStarted, Records().appLimit);
	}
	UpdateScope::~UpdateScope()
	{
		if (!ticket.admitted)
			return;
		auto& s = State();
		if (s.queryEnabled.load(std::memory_order_acquire)) {
			const auto now = Clock();
			auto next = s.nextQueryAfter.load(std::memory_order_acquire);
			if (now >= next && s.nextQueryAfter.compare_exchange_strong(next, now + s.queryPeriodTicks)) {
				const auto queryTicket = Records().BeginTimed(Clock, true);
				if (queryTicket.admitted) {
					Reflex::DiagnosticReadback started;
					started.raw.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
					started.requested = started.admitted = queryTicket.enter;
					Records().RecordStart(queryTicket, started, s.queryStarted, QueryCapacity);
					auto query = Backend().ReadSleepStatus();
					query.admitted = queryTicket.enter;
					Records().End(queryTicket, query, s.queries, QueryCapacity);
				}
			}
		}
		row.exit = Clock();
		row.reason = reason;
		applicationAdmission = previous;
		Records().End(ticket, row, Records().app);
	}
	bool UpdateScope::CapturesBackend() const { return ticket.admitted && Records().mode != Mode::Minimal; }
	void UpdateScope::SetBackend(const Reflex::Status& s, uint64_t uptimeMs)
	{
		if (!CapturesBackend())
			return;
		row.backend = { true, s.pending, s.desiredValid, s.acceptedValid, static_cast<uint32_t>(s.health),
			static_cast<uint32_t>(s.operation), static_cast<uint32_t>(s.desired.mode), static_cast<uint32_t>(s.accepted.mode),
			s.desired.frameLimitUs, s.accepted.frameLimitUs, s.failures, s.nativeStatus, s.deviceGeneration, uptimeMs, s.retryAtMs };
	}
	void UpdateScope::SetBackendOutcome(const Reflex::Outcome& outcome, uint64_t uptimeMs)
	{
		SetBackend(outcome.status, uptimeMs);
		if (!CapturesBackend())
			return;
		row.backend.attemptsObserved = outcome.attemptsObserved;
		row.backend.settingsAttempted = outcome.settingsAttempted;
		row.backend.sleepAttempted = outcome.sleepAttempted;
		row.backend.sleepSkipReason = static_cast<uint32_t>(outcome.sleepSkipReason);
	}
	void ObservePreferences(Reflex::Backend& backend, bool backendUpdated)
	{
		auto& s = State();
		if (!s.leaseActive.load(std::memory_order_acquire))
			return;
		std::scoped_lock lock(s.preferences);
		s.lease.Observe(CurrentPreferences());
		s.preferenceConflict.store(s.lease.conflict, std::memory_order_release);
		if (!s.lease.restoring || s.lease.conflict || !backendUpdated)
			return;
		const bool constrained = globals::features::upscaling.IsFrameGenerationDx12PathActive();
		const auto status = backend.GetStatus();
		const auto submission = backend.TryInspectDiagnosticBinding();
		const bool fresh = submission && submission->status.deviceGeneration == status.deviceGeneration &&
		                   submission->submissionResult == NVAPI_OK && submission->submissionExit > s.lease.restoreRequestedAt;
		s.lease.Confirm(Clock(), status.deviceGeneration, status.deviceBound && status.acceptedValid && !status.pending && status.health == Reflex::Health::Healthy && status.accepted == EffectiveOptions(s.lease.original, constrained), fresh);
		s.leaseActive.store(s.lease.active, std::memory_order_release);
	}

	void RequestExpiredRestoration()
	{
		auto& s = State();
		std::unique_lock lock(s.preferences, std::try_to_lock);
		if (!lock || s.acquiring || !s.islPurpose || s.lease.restoring)
			return;
		if (!s.lease.ReleaseUnchanged(CurrentPreferences(), Clock()) && s.lease.Restore(CurrentPreferences(), Clock()))
			ApplyPreferences(s.lease.original);
		s.leaseActive = s.lease.active;
		s.preferenceConflict = s.lease.conflict;
	}

	namespace
	{
		void SampleCompositor(bool closing)
		{
			auto& s = State();
#	ifdef ENABLE_SKYRIM_VR
			if (!REL::Module::IsVR()) {
				++s.cursor.quality.sourceFailures;
				return;
			}
			bool fault{};
			auto* compositor = Util::OpenVRFrameTiming::TryResolveCompositor(&fault);
			std::array<vr::Compositor_FrameTiming, HistoryCapacity> raw{};
			const auto count = Util::OpenVRFrameTiming::TryGetFrameTimings(compositor, raw.data(), HistoryCapacity, &fault);
			s.lastHistoryReturned = count;
			s.maxHistoryReturned = std::max(s.maxHistoryReturned, count);
			const auto retrieved = Clock();
			std::array<CompositorRecord, HistoryCapacity> rows{};
			for (size_t i = 0; i < count; ++i) {
				const auto& r = raw[i];
				rows[i] = { retrieved, r.m_nFrameIndex, 0, r.m_nNumFramePresents, r.m_nNumMisPresented, r.m_nNumDroppedFrames,
					r.m_nReprojectionFlags, r.m_flSystemTimeInSeconds, r.m_flClientFrameIntervalMs, r.m_flPreSubmitGpuMs,
					r.m_flPostSubmitGpuMs, r.m_flTotalRenderGpuMs, r.m_flCompositorRenderCpuMs, r.m_flWaitGetPosesCalledMs,
					r.m_flNewPosesReadyMs, r.m_flNewFrameReadyMs, r.m_flCompositorUpdateEndMs };
				rows[i].sourceStructureSize = r.m_nSize;
				rows[i].compositorGpuMs = r.m_flCompositorRenderGpuMs;
				rows[i].compositorIdleCpuMs = r.m_flCompositorIdleCpuMs;
				rows[i].presentCallCpuMs = r.m_flPresentCallCpuMs;
				rows[i].waitForPresentCpuMs = r.m_flWaitForPresentCpuMs;
				rows[i].submitFrameMs = r.m_flSubmitFrameMs;
				rows[i].compositorUpdateStartMs = r.m_flCompositorUpdateStartMs;
				rows[i].compositorRenderStartMs = r.m_flCompositorRenderStartMs;
				static_assert(sizeof(rows[i].hmdTransform) == sizeof(r.m_HmdPose.mDeviceToAbsoluteTracking.m));
				std::memcpy(rows[i].hmdTransform.data(), r.m_HmdPose.mDeviceToAbsoluteTracking.m, sizeof(rows[i].hmdTransform));
				std::memcpy(rows[i].hmdLinearVelocity.data(), r.m_HmdPose.vVelocity.v, sizeof(rows[i].hmdLinearVelocity));
				std::memcpy(rows[i].hmdAngularVelocity.data(), r.m_HmdPose.vAngularVelocity.v, sizeof(rows[i].hmdAngularVelocity));
				rows[i].trackingResult = static_cast<int32_t>(r.m_HmdPose.eTrackingResult);
				rows[i].poseValid = r.m_HmdPose.bPoseIsValid;
				rows[i].hmdConnected = r.m_HmdPose.bDeviceIsConnected;
			}
			if (closing && count && s.cursor.seeded && !s.cursor.stopTarget) {
				s.cursor.stopTarget = rows[count - 1].frame;
				s.sourceTarget = *s.cursor.stopTarget;
				s.sourceStopQpc = retrieved;
			}
			s.cursor.Consume(std::span(rows.data(), count), [&](auto row) {
				auto& j = s.journal;
				std::scoped_lock lock(j.mutex);
				if (j.compositor.size() == j.compositor.capacity())
					++j.quality.overflow;
				else
					j.compositor.push_back(row);
			});
			if (!s.sourceStartQpc && s.cursor.seeded) {
				s.sourceStart = s.cursor.last;
				s.sourceStartQpc = retrieved;
			}
#	else
			(void)closing;
			++s.cursor.quality.sourceFailures;
#	endif
		}
	}

	void FreezeCapture(Tick now, bool tailComplete, bool timedOut)
	{
		auto& s = State();
		s.journal.FreezeWith(now, tailComplete, timedOut, [&] { s.journal.MergeStarted(s.queries, s.queryStarted); });
		s.capturePreferenceConflict = s.preferenceConflict.load(std::memory_order_acquire);
		s.captureControlCalls = s.controlCallsDuringCapture.load(std::memory_order_acquire);
		if (s.islPurpose) {
			ISL::FreezeCapture(now);
			ISL::TouchSession();
		}
	}
	void Run(std::stop_token token, uint32_t durationMs)
	{
		using namespace std::chrono;
		auto& s = State();
		const auto handshakeDeadline = steady_clock::now() + seconds(30);
		while (!s.responseWritten.load(std::memory_order_acquire) && !token.stop_requested() && steady_clock::now() < handshakeDeadline)
			std::this_thread::sleep_for(milliseconds(1));
		const auto armUntil = steady_clock::now() + milliseconds(s.admissionDelayMs);
		while (!token.stop_requested() && steady_clock::now() < armUntil)
			std::this_thread::sleep_for(milliseconds(1));
		if (token.stop_requested() || !s.responseWritten) {
			s.termination = token.stop_requested() ? "cancelled_before_admission" : "response_handshake_timeout";
			if (s.islPurpose)
				ISL::CloseCapture();
			s.journal.CloseTimed(Clock);
			FreezeCapture(Clock(), false, false);
			return;
		}
		const auto deadline = steady_clock::now() + milliseconds(durationMs);
		std::optional<steady_clock::time_point> closingAt;
		try {
			s.journal.Open(Clock());
			if (s.islPurpose)
				ISL::OpenCapture(s.journal.start);
			for (;;) {
				const auto now = steady_clock::now();
				if (token.stop_requested() || now >= deadline) {
					if (s.journal.active) {
						s.termination = token.stop_requested() ? "cancelled" : "completed";
						if (s.islPurpose)
							ISL::CloseCapture();
						s.journal.CloseTimed(Clock);
					}
				}
				const bool closing = !s.journal.active;
				if (closing && s.islPurpose)
					ISL::CloseCapture();
				if (closing && !closingAt)
					closingAt = now;
				if (s.compositorRequested)
					SampleCompositor(closing);
				const bool tail = !s.compositorRequested || s.cursor.Drained();
				const bool timedOut = closingAt && now - *closingAt >= milliseconds(PostrollMs);
				if (closing && ((tail && s.journal.Outstanding() == 0 && (!s.islPurpose || ISL::Outstanding() == 0)) || timedOut)) {
					{
						std::scoped_lock lock(s.journal.mutex);
						auto& q = s.journal.quality;
						q.sourceLost = s.cursor.quality.sourceLost;
						q.resets = s.cursor.quality.resets;
						q.invalidCompositor = s.cursor.quality.invalidCompositor;
						q.sourceFailures = s.cursor.quality.sourceFailures;
						q.invalidClocks += s.cursor.quality.invalidClocks;
					}
					FreezeCapture(Clock(), tail, timedOut);
					return;
				}
				std::this_thread::sleep_for(milliseconds(SamplePeriodMs));
			}
		} catch (const std::exception& e) {
			s.workerError = e.what();
		} catch (...) {
			s.workerError = "unknown acquisition failure";
		}
		if (s.islPurpose)
			ISL::CloseCapture();
		s.journal.CloseTimed(Clock);
		FreezeCapture(Clock(), false, false);
	}
}
#endif
