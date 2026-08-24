#include "RenderMap/Collector.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>

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

		void LatchLimit(
			std::atomic<RecordResult>& a_firstLimit,
			std::atomic_bool& a_accepting,
			RecordResult a_limit) noexcept
		{
			auto expected = RecordResult::kRecorded;
			if (a_firstLimit.compare_exchange_strong(
					expected, a_limit, std::memory_order_acq_rel, std::memory_order_acquire)) {
				a_accepting.store(false, std::memory_order_release);
			}
		}

		template <std::size_t N>
		bool CopyBounded(std::string_view a_source, std::array<char, N>& a_target) noexcept
		{
			static_assert(N > 0);
			const auto count = std::min(a_source.size(), N - 1);
			std::copy_n(a_source.data(), count, a_target.data());
			a_target[count] = '\0';
			return a_source.size() > count;
		}

		template <std::size_t N>
		std::string_view StoredString(const std::array<char, N>& a_value) noexcept
		{
			return { a_value.data(), std::char_traits<char>::length(a_value.data()) };
		}

		std::uint64_t HashShaderIdentity(const ShaderObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto* a_data, std::size_t a_size) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(a_data);
				for (std::size_t index = 0; index < a_size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(&a_input.shader, sizeof(a_input.shader));
			append(&a_input.shaderType, sizeof(a_input.shaderType));
			const auto appendString = [&](std::string_view a_value) {
				append(a_value.data(), a_value.size());
				const unsigned char separator = 0xFF;
				append(&separator, sizeof(separator));
			};
			appendString(a_input.fxpFilename);
			appendString(a_input.imageSpaceName);
			appendString(a_input.definesSuffix);
			return hash;
		}

		bool SameShaderIdentity(const ShaderObservationRecord& a_record, const ShaderObservationInput& a_input) noexcept
		{
			return !a_record.fxpFilenameTruncated && !a_record.imageSpaceNameTruncated &&
				!a_record.definesSuffixTruncated &&
				a_input.fxpFilename.size() <= kMaximumShaderNameLength &&
				a_input.imageSpaceName.size() <= kMaximumShaderNameLength &&
				a_input.definesSuffix.size() <= kMaximumShaderDefinesSuffixLength &&
				a_record.pointerEvidence == a_input.shader &&
				a_record.shaderType == a_input.shaderType &&
				StoredString(a_record.fxpFilename) == a_input.fxpFilename.substr(0, kMaximumShaderNameLength) &&
				StoredString(a_record.imageSpaceName) == a_input.imageSpaceName.substr(0, kMaximumShaderNameLength) &&
				StoredString(a_record.definesSuffix) == a_input.definesSuffix.substr(0, kMaximumShaderDefinesSuffixLength);
		}

		std::uint64_t HashStageShaderIdentity(const StageShaderObservationInput& a_input) noexcept
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			const auto append = [&](const auto* a_data, std::size_t a_size) {
				const auto* bytes = reinterpret_cast<const unsigned char*>(a_data);
				for (std::size_t index = 0; index < a_size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			append(&a_input.stage, sizeof(a_input.stage));
			append(&a_input.wrapper, sizeof(a_input.wrapper));
			append(&a_input.d3dObject, sizeof(a_input.d3dObject));
			append(&a_input.wrapperDescriptor, sizeof(a_input.wrapperDescriptor));
			append(&a_input.bytecodeSize, sizeof(a_input.bytecodeSize));
			append(a_input.bytecodeSha256.data(), a_input.bytecodeSha256.size());
			append(a_input.cachePath.data(), a_input.cachePath.size());
			return hash;
		}

		bool SameStageShaderIdentity(
			const StageShaderObservationRecord& a_record,
			const StageShaderObservationInput& a_input) noexcept
		{
			return !a_record.bytecodeSha256Truncated && !a_record.cachePathTruncated &&
				a_input.bytecodeSha256.size() <= kSha256HexLength &&
				a_input.cachePath.size() <= kMaximumShaderCachePathLength &&
				a_record.stage == a_input.stage && a_record.wrapperEvidence == a_input.wrapper &&
				a_record.pointerEvidence == a_input.d3dObject &&
				a_record.wrapperDescriptor == a_input.wrapperDescriptor &&
				a_record.bytecodeSize == a_input.bytecodeSize &&
				StoredString(a_record.bytecodeSha256) == a_input.bytecodeSha256 &&
				StoredString(a_record.cachePath) == a_input.cachePath;
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
		std::unique_ptr<ShaderObservationRecord[]> shaderObservations;
		std::unique_ptr<bool[]> shaderObservationRetired;
		std::mutex shaderObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> shaderObservationLookup;
		std::uint32_t shaderObservationCount{ 0 };
		std::unique_ptr<StageShaderObservationRecord[]> stageShaderObservations;
		std::mutex stageShaderObservationMutex;
		std::unordered_multimap<std::uint64_t, std::uint32_t> stageShaderObservationLookup;
		std::uint32_t stageShaderObservationCount{ 0 };

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
		std::atomic_uint64_t droppedShaderObservations{ 0 };
		std::atomic_uint64_t droppedStageShaderObservations{ 0 };
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
			a_config.maxScopeDepth == 0 || a_config.maxScopeDepth > kMaximumScopeDepth ||
			a_config.maxShaderObservations == 0 || a_config.maxStageShaderObservations == 0) {
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
		session->shaderObservations.reset(new (std::nothrow) ShaderObservationRecord[a_config.maxShaderObservations]);
		session->shaderObservationRetired.reset(new (std::nothrow) bool[a_config.maxShaderObservations]{});
		session->stageShaderObservations.reset(
			new (std::nothrow) StageShaderObservationRecord[a_config.maxStageShaderObservations]);
		if (!session->shaderObservations || !session->shaderObservationRetired || !session->stageShaderObservations)
			return StartResult::kAllocationFailed;
		try {
			session->shaderObservationLookup.reserve(a_config.maxShaderObservations);
			session->stageShaderObservationLookup.reserve(a_config.maxStageShaderObservations);
		} catch (...) {
			return StartResult::kAllocationFailed;
		}

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
			.droppedShaderObservations = session->droppedShaderObservations.load(std::memory_order_relaxed),
			.droppedStageShaderObservations = session->droppedStageShaderObservations.load(std::memory_order_relaxed),
		};

		const auto reserved = std::min(session->nextIndex.load(std::memory_order_acquire), session->capacity);
		snapshot.events.reserve(static_cast<std::size_t>(reserved));
		for (std::uint64_t index = 0; index < reserved; ++index) {
			const auto& slot = session->slots[static_cast<std::size_t>(index)];
			if (slot.published.load(std::memory_order_acquire))
				snapshot.events.push_back(slot.record);
		}

		snapshot.shaderObservations.reserve(session->shaderObservationCount);
		for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index)
			snapshot.shaderObservations.push_back(session->shaderObservations[index]);
		snapshot.stageShaderObservations.reserve(session->stageShaderObservationCount);
		for (std::uint32_t index = 0; index < session->stageShaderObservationCount; ++index)
			snapshot.stageShaderObservations.push_back(session->stageShaderObservations[index]);

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
			LatchLimit(session->firstLimit, session->accepting, RecordResult::kTimeLimit);
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
				LatchLimit(session->firstLimit, session->accepting, RecordResult::kFrameLimit);
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
			LatchLimit(session->firstLimit, session->accepting, session->capacityLimit);
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
		const EventPayload& a_endPayload,
		std::uint64_t a_expectedGeneration) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_kind == ScopeKind::kCount ||
			(a_expectedGeneration != 0 && session->generation != a_expectedGeneration))
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

	std::uint64_t Collector::AllocateObservationId(std::uint64_t a_expectedGeneration) noexcept
	{
		const auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) ||
			(a_expectedGeneration != 0 && session->generation != a_expectedGeneration))
			return 0;
		const auto observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
		return activeSession.load(std::memory_order_acquire) == session &&
				session->accepting.load(std::memory_order_acquire) ?
			observationId : 0;
	}

	ShaderObservationResult Collector::ObserveShader(const ShaderObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.shader == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashShaderIdentity(a_input);
		ShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->shaderObservationMutex);
			const auto [begin, end] = session->shaderObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto index = found->second;
				if (!session->shaderObservationRetired[index] &&
					SameShaderIdentity(session->shaderObservations[index], a_input)) {
					const auto& record = session->shaderObservations[index];
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->shaderObservationCount >= session->config.maxShaderObservations) {
					session->droppedShaderObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index) {
						if (session->shaderObservations[index].pointerEvidence == a_input.shader)
							pointerGeneration = std::max(pointerGeneration, session->shaderObservations[index].pointerGeneration + 1);
					}

					const auto index = session->shaderObservationCount++;
					auto& record = session->shaderObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.pointerEvidence = a_input.shader;
					record.pointerGeneration = pointerGeneration;
					record.shaderType = a_input.shaderType;
					record.fxpFilenameTruncated = CopyBounded(a_input.fxpFilename, record.fxpFilename);
					record.imageSpaceNameTruncated = CopyBounded(a_input.imageSpaceName, record.imageSpaceName);
					record.definesSuffixTruncated = CopyBounded(a_input.definesSuffix, record.definesSuffix);
					try {
						session->shaderObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, record.pointerGeneration, true };
					} catch (...) {
						--session->shaderObservationCount;
						record = {};
						session->droppedShaderObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	StageShaderObservationResult Collector::ObserveStageShader(const StageShaderObservationInput& a_input) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_input.d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		const auto identityHash = HashStageShaderIdentity(a_input);
		StageShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->stageShaderObservationMutex);
			const auto [begin, end] = session->stageShaderObservationLookup.equal_range(identityHash);
			for (auto found = begin; found != end; ++found) {
				const auto& record = session->stageShaderObservations[found->second];
				if (SameStageShaderIdentity(record, a_input)) {
					result = { record.observationId, session->generation, record.pointerGeneration, false };
					break;
				}
			}

			if (result.observationId == 0) {
				if (session->stageShaderObservationCount >= session->config.maxStageShaderObservations) {
					session->droppedStageShaderObservations.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::uint32_t pointerGeneration = 1;
					for (std::uint32_t index = 0; index < session->stageShaderObservationCount; ++index) {
						if (session->stageShaderObservations[index].pointerEvidence == a_input.d3dObject)
							pointerGeneration = std::max(
								pointerGeneration, session->stageShaderObservations[index].pointerGeneration + 1);
					}

					const auto index = session->stageShaderObservationCount++;
					auto& record = session->stageShaderObservations[index];
					record.observationId = session->nextObservationId.fetch_add(1, std::memory_order_relaxed);
					record.stage = a_input.stage;
					record.wrapperEvidence = a_input.wrapper;
					record.pointerEvidence = a_input.d3dObject;
					record.pointerGeneration = pointerGeneration;
					record.wrapperDescriptor = a_input.wrapperDescriptor;
					record.bytecodeSize = a_input.bytecodeSize;
					record.bytecodeSha256Truncated = CopyBounded(a_input.bytecodeSha256, record.bytecodeSha256);
					record.cachePathTruncated = CopyBounded(a_input.cachePath, record.cachePath);
					try {
						session->stageShaderObservationLookup.emplace(identityHash, index);
						result = { record.observationId, session->generation, record.pointerGeneration, true };
					} catch (...) {
						--session->stageShaderObservationCount;
						record = {};
						session->droppedStageShaderObservations.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
		releaseFlight();
		return result;
	}

	StageShaderObservationResult Collector::FindStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_d3dObject == 0)
			return {};

		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		const auto releaseFlight = [&] { session->inFlight.fetch_sub(1, std::memory_order_release); };
		if (!session->accepting.load(std::memory_order_acquire) || activeSession.load(std::memory_order_acquire) != session) {
			releaseFlight();
			return {};
		}

		StageShaderObservationResult result{ .sessionGeneration = session->generation };
		{
			std::lock_guard lock(session->stageShaderObservationMutex);
			for (std::uint32_t index = 0; index < session->stageShaderObservationCount; ++index) {
				const auto& record = session->stageShaderObservations[index];
				if (record.stage == a_stage && record.pointerEvidence == a_d3dObject &&
					record.observationId > result.observationId) {
					result = {
						.observationId = record.observationId,
						.sessionGeneration = session->generation,
						.pointerGeneration = record.pointerGeneration,
						.firstSeen = false,
					};
				}
			}
		}
		releaseFlight();
		return result;
	}

	void Collector::RetireShaderObservation(std::uintptr_t a_shader) noexcept
	{
		auto session = activeSession.load(std::memory_order_acquire);
		if (!session || !session->accepting.load(std::memory_order_acquire) || a_shader == 0)
			return;
		session->inFlight.fetch_add(1, std::memory_order_acq_rel);
		if (!session->accepting.load(std::memory_order_acquire) ||
			activeSession.load(std::memory_order_acquire) != session) {
			session->inFlight.fetch_sub(1, std::memory_order_release);
			return;
		}
		{
			std::lock_guard lock(session->shaderObservationMutex);
			for (std::uint32_t index = 0; index < session->shaderObservationCount; ++index) {
				if (session->shaderObservations[index].pointerEvidence == a_shader)
					session->shaderObservationRetired[index] = true;
			}
		}
		session->inFlight.fetch_sub(1, std::memory_order_release);
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
