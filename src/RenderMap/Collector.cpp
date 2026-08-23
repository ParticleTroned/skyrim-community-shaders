#include "RenderMap/Collector.h"

#include <algorithm>
#include <functional>
#include <new>
#include <thread>

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

namespace CSX::RenderMap
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct ScopeFrame
		{
			std::uint64_t token{ 0 };
			std::uint64_t observationId{ 0 };
		};

		struct ThreadState
		{
			const Collector* owner{ nullptr };
			std::uint64_t generation{ 0 };
			FrameContext frame;
			std::array<std::array<ScopeFrame, kMaximumScopeDepth>, static_cast<std::size_t>(ScopeKind::kCount)> scopes{};
			std::array<std::uint8_t, static_cast<std::size_t>(ScopeKind::kCount)> depths{};
		};

		thread_local ThreadState threadState;
		std::atomic_uint64_t nextSessionGeneration{ 1 };

		std::uint64_t ReadClockTicks() noexcept
		{
#ifdef _WIN32
			LARGE_INTEGER value{};
			return ::QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
#else
			return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
		}

		std::uint64_t CurrentThreadId() noexcept
		{
#ifdef _WIN32
			return static_cast<std::uint64_t>(::GetCurrentThreadId());
#else
			return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
		}

		std::uint64_t DurationToTicks(std::chrono::nanoseconds a_duration) noexcept
		{
#ifdef _WIN32
			const auto frequency = Collector::ClockFrequencyHz();
			const long double seconds = static_cast<long double>(a_duration.count()) / 1'000'000'000.0L;
			const auto ticks = seconds * static_cast<long double>(frequency);
			return ticks > 0.0L ? static_cast<std::uint64_t>(ticks) : 0;
#else
			const auto ticks = std::chrono::duration_cast<Clock::duration>(a_duration).count();
			return ticks > 0 ? static_cast<std::uint64_t>(ticks) : 0;
#endif
		}

		std::size_t ScopeIndex(ScopeKind a_kind) noexcept
		{
			return static_cast<std::size_t>(a_kind);
		}

		ThreadState& SynchronizeThreadState(const Collector* a_owner, std::uint64_t a_generation) noexcept
		{
			if (threadState.owner != a_owner || threadState.generation != a_generation) {
				threadState = {};
				threadState.owner = a_owner;
				threadState.generation = a_generation;
			}
			return threadState;
		}

		ScopeSnapshot SnapshotScopes(const ThreadState& a_state) noexcept
		{
			const auto read = [&](ScopeKind a_kind) {
				const auto index = ScopeIndex(a_kind);
				if (a_state.depths[index] == 0)
					return ScopeBinding{};
				const auto& frame = a_state.scopes[index][a_state.depths[index] - 1];
				return ScopeBinding{ frame.token, frame.observationId };
			};

			return {
				.renderPass = read(ScopeKind::kRenderPass),
				.technique = read(ScopeKind::kTechnique),
				.geometry = read(ScopeKind::kGeometry),
				.commandList = read(ScopeKind::kCommandList),
			};
		}

		StopReason LimitToStopReason(RecordResult a_result, StopReason a_fallback) noexcept
		{
			switch (a_result) {
			case RecordResult::kFrameLimit:
				return StopReason::kFrameLimit;
			case RecordResult::kTimeLimit:
				return StopReason::kTimeLimit;
			case RecordResult::kEventLimit:
				return StopReason::kEventLimit;
			case RecordResult::kByteLimit:
				return StopReason::kByteLimit;
			default:
				return a_fallback;
			}
		}
	}

	struct Collector::Session
	{
		struct Slot
		{
			std::atomic_bool published{ false };
			EventRecord record;
		};

		CollectorConfig config;
		std::uint64_t generation{ 0 };
		std::uint64_t capacity{ 0 };
		RecordResult capacityLimit{ RecordResult::kEventLimit };
		std::uint64_t maxDurationTicks{ 0 };
		std::uint64_t startTimestampTicks{ 0 };
		std::unique_ptr<Slot[]> slots;

		std::atomic_bool accepting{ true };
		std::atomic_uint64_t inFlight{ 0 };
		std::atomic_uint64_t nextIndex{ 0 };
		std::atomic_uint64_t nextObservationId{ 1 };
		std::atomic_uint64_t nextScopeToken{ 1 };
		std::atomic_uint64_t firstFrame{ kUnknownFrame };
		std::atomic<RecordResult> firstLimit{ RecordResult::kRecorded };

		std::atomic_uint64_t attempted{ 0 };
		std::atomic_uint64_t recorded{ 0 };
		std::atomic_uint64_t droppedStopped{ 0 };
		std::atomic_uint64_t droppedEventLimit{ 0 };
		std::atomic_uint64_t droppedByteLimit{ 0 };
		std::atomic_uint64_t droppedFrameLimit{ 0 };
		std::atomic_uint64_t droppedTimeLimit{ 0 };
		std::atomic_uint64_t scopeOverflow{ 0 };
		std::atomic_uint64_t scopeMismatch{ 0 };
	};

	Collector::ScopeGuard::ScopeGuard(
		Collector* a_owner,
		std::uint64_t a_generation,
		ScopeKind a_kind,
		std::uint64_t a_token,
		EventKind a_endKind,
		EventPayload a_endPayload) noexcept :
		owner(a_owner),
		generation(a_generation),
		kind(a_kind),
		token(a_token),
		endKind(a_endKind),
		endPayload(a_endPayload)
	{}

	Collector::ScopeGuard::~ScopeGuard()
	{
		Reset();
	}

	Collector::ScopeGuard::ScopeGuard(ScopeGuard&& a_other) noexcept :
		owner(a_other.owner),
		generation(a_other.generation),
		kind(a_other.kind),
		token(a_other.token),
		endKind(a_other.endKind),
		endPayload(a_other.endPayload)
	{
		a_other.owner = nullptr;
		a_other.token = 0;
	}

	Collector::ScopeGuard& Collector::ScopeGuard::operator=(ScopeGuard&& a_other) noexcept
	{
		if (this != std::addressof(a_other)) {
			Reset();
			owner = a_other.owner;
			generation = a_other.generation;
			kind = a_other.kind;
			token = a_other.token;
			endKind = a_other.endKind;
			endPayload = a_other.endPayload;
			a_other.owner = nullptr;
			a_other.token = 0;
		}
		return *this;
	}

	bool Collector::ScopeGuard::IsActive() const noexcept
	{
		return owner != nullptr;
	}

	std::uint64_t Collector::ScopeGuard::Token() const noexcept
	{
		return token;
	}

	void Collector::ScopeGuard::Reset() noexcept
	{
		if (owner)
			owner->ExitScope(generation, kind, token, endKind, endPayload);
		owner = nullptr;
		token = 0;
	}

	Collector::Collector() = default;

	Collector::~Collector()
	{
		Stop(StopReason::kShutdown);
	}

	StartResult Collector::Start(const CollectorConfig& a_config)
	{
		if (a_config.maxFrames == 0 || a_config.maxEvents == 0 ||
			a_config.maxBytes < sizeof(EventRecord) || a_config.maxDuration.count() <= 0 ||
			a_config.maxScopeDepth == 0 || a_config.maxScopeDepth > kMaximumScopeDepth) {
			return StartResult::kInvalidBounds;
		}

		if (activeSession.load(std::memory_order_acquire))
			return StartResult::kAlreadyCapturing;

		const auto capacityByBytes = a_config.maxBytes / sizeof(EventRecord);
		const auto capacity = std::min(a_config.maxEvents, static_cast<std::uint64_t>(capacityByBytes));
		if (capacity == 0 || capacity > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
			return StartResult::kInvalidBounds;

		auto session = std::shared_ptr<Session>(new (std::nothrow) Session{});
		if (!session)
			return StartResult::kAllocationFailed;

		session->slots.reset(new (std::nothrow) Session::Slot[static_cast<std::size_t>(capacity)]);
		if (!session->slots)
			return StartResult::kAllocationFailed;

		session->config = a_config;
		session->generation = nextSessionGeneration.fetch_add(1, std::memory_order_relaxed);
		session->capacity = capacity;
		session->capacityLimit = a_config.maxEvents <= capacityByBytes ?
			RecordResult::kEventLimit : RecordResult::kByteLimit;
		session->maxDurationTicks = DurationToTicks(a_config.maxDuration);
		if (session->maxDurationTicks == 0)
			return StartResult::kInvalidBounds;
		session->startTimestampTicks = ReadClockTicks();

		std::shared_ptr<Session> expected;
		if (!activeSession.compare_exchange_strong(
				expected, session, std::memory_order_release, std::memory_order_acquire)) {
			return StartResult::kAlreadyCapturing;
		}

		return StartResult::kStarted;
	}

	std::optional<CaptureSnapshot> Collector::Stop(StopReason a_reason)
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session)
			return std::nullopt;

		session->accepting.store(false, std::memory_order_release);
		if (!activeSession.compare_exchange_strong(
				session, std::shared_ptr<Session>{}, std::memory_order_acq_rel, std::memory_order_acquire)) {
			return std::nullopt;
		}

		while (session->inFlight.load(std::memory_order_acquire) != 0)
			std::this_thread::yield();

		CaptureSnapshot snapshot;
		snapshot.config = session->config;
		snapshot.sessionGeneration = session->generation;
		snapshot.clockFrequencyHz = ClockFrequencyHz();
		snapshot.startTimestampTicks = session->startTimestampTicks;
		snapshot.endTimestampTicks = ReadClockTicks();
		snapshot.stopReason = LimitToStopReason(session->firstLimit.load(std::memory_order_acquire), a_reason);
		snapshot.statistics = {
			.attempted = session->attempted.load(std::memory_order_relaxed),
			.recorded = session->recorded.load(std::memory_order_relaxed),
			.droppedStopped = session->droppedStopped.load(std::memory_order_relaxed),
			.droppedEventLimit = session->droppedEventLimit.load(std::memory_order_relaxed),
			.droppedByteLimit = session->droppedByteLimit.load(std::memory_order_relaxed),
			.droppedFrameLimit = session->droppedFrameLimit.load(std::memory_order_relaxed),
			.droppedTimeLimit = session->droppedTimeLimit.load(std::memory_order_relaxed),
			.scopeOverflow = session->scopeOverflow.load(std::memory_order_relaxed),
			.scopeMismatch = session->scopeMismatch.load(std::memory_order_relaxed),
		};

		const auto reserved = std::min(session->nextIndex.load(std::memory_order_acquire), session->capacity);
		snapshot.events.reserve(static_cast<std::size_t>(reserved));
		for (std::uint64_t index = 0; index < reserved; ++index) {
			const auto& slot = session->slots[static_cast<std::size_t>(index)];
			if (slot.published.load(std::memory_order_acquire))
				snapshot.events.push_back(slot.record);
		}

		return snapshot;
	}

	bool Collector::IsCapturing() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		return session && session->accepting.load(std::memory_order_acquire);
	}

	std::uint64_t Collector::ActiveGeneration() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		return session ? session->generation : 0;
	}

	RecordResult Collector::Record(
		EventKind a_kind,
		const EventPayload& a_payload,
		std::uint64_t a_deviceContextObservationId) noexcept
	{
		return RecordForGeneration(a_kind, a_payload, a_deviceContextObservationId, 0);
	}

	RecordResult Collector::RecordForGeneration(
		EventKind a_kind,
		const EventPayload& a_payload,
		std::uint64_t a_deviceContextObservationId,
		std::uint64_t a_expectedGeneration) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session)
			return RecordResult::kInactive;
		if (a_expectedGeneration != 0 && session->generation != a_expectedGeneration)
			return RecordResult::kStopped;

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] {
			session->inFlight.fetch_sub(1, std::memory_order_release);
		};

		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			session->droppedStopped.fetch_add(1, std::memory_order_relaxed);
			releaseFlight();
			return RecordResult::kStopped;
		}

		session->attempted.fetch_add(1, std::memory_order_relaxed);
		const auto timestamp = ReadClockTicks();
		if (timestamp - session->startTimestampTicks >= session->maxDurationTicks) {
			session->droppedTimeLimit.fetch_add(1, std::memory_order_relaxed);
			auto expected = RecordResult::kRecorded;
			session->firstLimit.compare_exchange_strong(expected, RecordResult::kTimeLimit, std::memory_order_relaxed);
			releaseFlight();
			return RecordResult::kTimeLimit;
		}

		auto& state = SynchronizeThreadState(this, session->generation);
		if (state.frame.cpuFrame != kUnknownFrame) {
			auto firstFrame = session->firstFrame.load(std::memory_order_acquire);
			if (firstFrame == kUnknownFrame) {
				session->firstFrame.compare_exchange_strong(
					firstFrame, state.frame.cpuFrame, std::memory_order_acq_rel, std::memory_order_acquire);
				firstFrame = session->firstFrame.load(std::memory_order_acquire);
			}
			if (state.frame.cpuFrame >= firstFrame && state.frame.cpuFrame - firstFrame >= session->config.maxFrames) {
				session->droppedFrameLimit.fetch_add(1, std::memory_order_relaxed);
				auto expected = RecordResult::kRecorded;
				session->firstLimit.compare_exchange_strong(expected, RecordResult::kFrameLimit, std::memory_order_relaxed);
				releaseFlight();
				return RecordResult::kFrameLimit;
			}
		}

		const auto index = session->nextIndex.fetch_add(1, std::memory_order_acq_rel);
		if (index >= session->capacity) {
			if (session->capacityLimit == RecordResult::kEventLimit)
				session->droppedEventLimit.fetch_add(1, std::memory_order_relaxed);
			else
				session->droppedByteLimit.fetch_add(1, std::memory_order_relaxed);
			auto expected = RecordResult::kRecorded;
			session->firstLimit.compare_exchange_strong(expected, session->capacityLimit, std::memory_order_relaxed);
			releaseFlight();
			return session->capacityLimit;
		}

		EventRecord record;
		record.kind = a_kind;
		record.captureNumericId = session->config.captureNumericId;
		record.sessionGeneration = session->generation;
		record.sequence = index;
		record.timestampTicks = timestamp;
		record.threadId = CurrentThreadId();
		record.deviceContextObservationId = a_deviceContextObservationId;
		record.frame = state.frame;
		record.scopes = SnapshotScopes(state);
		record.payload = a_payload;

		auto& slot = session->slots[static_cast<std::size_t>(index)];
		slot.record = record;
		slot.published.store(true, std::memory_order_release);
		session->recorded.fetch_add(1, std::memory_order_relaxed);
		releaseFlight();
		return RecordResult::kRecorded;
	}

	Collector::ScopeGuard Collector::EnterScope(
		ScopeKind a_kind,
		std::uint64_t a_observationId,
		EventKind a_beginKind,
		EventKind a_endKind,
		const EventPayload& a_beginPayload,
		const EventPayload& a_endPayload) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_kind == ScopeKind::kCount)
			return {};

		auto& state = SynchronizeThreadState(this, session->generation);
		const auto index = ScopeIndex(a_kind);
		if (state.depths[index] >= session->config.maxScopeDepth) {
			session->scopeOverflow.fetch_add(1, std::memory_order_relaxed);
			return {};
		}

		const auto token = session->nextScopeToken.fetch_add(1, std::memory_order_relaxed);
		state.scopes[index][state.depths[index]++] = { token, a_observationId };
		if (RecordForGeneration(a_beginKind, a_beginPayload, 0, session->generation) != RecordResult::kRecorded) {
			--state.depths[index];
			return {};
		}

		return ScopeGuard(this, session->generation, a_kind, token, a_endKind, a_endPayload);
	}

	std::uint64_t Collector::AllocateObservationId() noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire))
			return 0;
		const auto observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
		return activeSession.load(std::memory_order_acquire) == session &&
				session->accepting.load(std::memory_order_acquire) ?
			observationId : 0;
	}

	void Collector::SetThreadFrameContext(const FrameContext& a_context) noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session)
			return;
		SynchronizeThreadState(this, session->generation).frame = a_context;
	}

	FrameContext Collector::GetThreadFrameContext() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || threadState.owner != this || threadState.generation != session->generation)
			return {};
		return threadState.frame;
	}

	ScopeSnapshot Collector::GetThreadScopes() const noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || threadState.owner != this || threadState.generation != session->generation)
			return {};
		return SnapshotScopes(threadState);
	}

	std::uint64_t Collector::ClockFrequencyHz() noexcept
	{
#ifdef _WIN32
		LARGE_INTEGER frequency{};
		return ::QueryPerformanceFrequency(&frequency) ? static_cast<std::uint64_t>(frequency.QuadPart) : 0;
#else
		return static_cast<std::uint64_t>(Clock::period::den / Clock::period::num);
#endif
	}

	void Collector::ExitScope(
		std::uint64_t a_generation,
		ScopeKind a_kind,
		std::uint64_t a_token,
		EventKind a_endKind,
		const EventPayload& a_endPayload) noexcept
	{
		if (threadState.owner != this || threadState.generation != a_generation || a_kind == ScopeKind::kCount)
			return;

		const auto index = ScopeIndex(a_kind);
		if (threadState.depths[index] == 0)
			return;

		auto& depth = threadState.depths[index];
		if (threadState.scopes[index][depth - 1].token != a_token) {
			const auto session = activeSession.load(std::memory_order_acquire);
			if (session && session->generation == a_generation)
				session->scopeMismatch.fetch_add(1, std::memory_order_relaxed);

			const auto begin = threadState.scopes[index].begin();
			const auto end = begin + depth;
			const auto found = std::find_if(begin, end, [&](const ScopeFrame& a_frame) {
				return a_frame.token == a_token;
			});
			if (found != end) {
				std::move(found + 1, end, found);
				--depth;
			}
			return;
		}

		const auto session = activeSession.load(std::memory_order_acquire);
		if (session && session->generation == a_generation && session->accepting.load(std::memory_order_acquire))
			RecordForGeneration(a_endKind, a_endPayload, 0, a_generation);

		--depth;
	}
}
