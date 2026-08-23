#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace CSX::RenderMap
{
	inline constexpr std::uint64_t kUnknownFrame = std::numeric_limits<std::uint64_t>::max();
	inline constexpr std::size_t kMaximumScopeDepth = 32;

	enum class EventKind : std::uint16_t
	{
		kCaptureMarker,
		kGap,
		kFrameBegin,
		kFrameEnd,
		kSceneAccumulationBegin,
		kSceneAccumulationEnd,
		kEyeBegin,
		kEyeEnd,
		kObjectObserved,
		kGeometryObserved,
		kMaterialObserved,
		kRenderPassCreated,
		kRenderPassEnter,
		kRenderPassExit,
		kTechniqueBegin,
		kTechniqueEnd,
		kGeometrySetupBegin,
		kGeometrySetupEnd,
		kPipelineObjectCreated,
		kPipelineBind,
		kRenderTargetBind,
		kDepthSourceReady,
		kVisibilityCandidate,
		kVisibilityResultReady,
		kVisibilityConsumed,
		kCullDecision,
		kDraw,
		kDispatch,
		kFinishCommandList,
		kExecuteCommandList,
	};

	enum class Eye : std::uint8_t
	{
		kUnknown,
		kLeft,
		kRight,
		kBoth,
		kMono,
	};

	enum class ScopeKind : std::uint8_t
	{
		kRenderPass,
		kTechnique,
		kGeometry,
		kCommandList,
		kCount,
	};

	enum class StartResult : std::uint8_t
	{
		kStarted,
		kAlreadyCapturing,
		kInvalidBounds,
		kAllocationFailed,
	};

	enum class RecordResult : std::uint8_t
	{
		kRecorded,
		kInactive,
		kStopped,
		kEventLimit,
		kByteLimit,
		kFrameLimit,
		kTimeLimit,
	};

	enum class StopReason : std::uint8_t
	{
		kRequested,
		kFrameLimit,
		kTimeLimit,
		kEventLimit,
		kByteLimit,
		kShutdown,
		kFailure,
	};

	struct CollectorConfig
	{
		std::uint64_t captureNumericId{ 0 };
		std::uint64_t maxFrames{ 1 };
		std::uint64_t maxEvents{ 1 };
		std::uint64_t maxBytes{ 1 };
		std::chrono::nanoseconds maxDuration{ std::chrono::seconds(1) };
		std::uint8_t maxScopeDepth{ 8 };
	};

	struct FrameContext
	{
		std::uint64_t cpuFrame{ kUnknownFrame };
		std::uint64_t sceneEpoch{ kUnknownFrame };
		std::uint64_t submissionEpoch{ kUnknownFrame };
		Eye eye{ Eye::kUnknown };
		std::uint8_t eyeMask{ 0 };
	};

	struct ScopeBinding
	{
		std::uint64_t token{ 0 };
		std::uint64_t observationId{ 0 };
	};

	struct ScopeSnapshot
	{
		ScopeBinding renderPass;
		ScopeBinding technique;
		ScopeBinding geometry;
		ScopeBinding commandList;
	};

	struct EventPayload
	{
		std::uint16_t schema{ 0 };
		std::array<std::uint64_t, 6> words{};
	};

	struct EventRecord
	{
		std::uint16_t schemaMajor{ 1 };
		std::uint16_t schemaMinor{ 0 };
		EventKind kind{ EventKind::kCaptureMarker };
		std::uint16_t reserved{ 0 };
		std::uint64_t captureNumericId{ 0 };
		std::uint64_t sessionGeneration{ 0 };
		std::uint64_t sequence{ 0 };
		std::uint64_t timestampTicks{ 0 };
		std::uint64_t threadId{ 0 };
		std::uint64_t deviceContextObservationId{ 0 };
		FrameContext frame;
		ScopeSnapshot scopes;
		EventPayload payload;
	};

	static_assert(std::is_trivially_copyable_v<EventRecord>);
	static_assert(sizeof(EventRecord) <= 256);

	struct CaptureStatistics
	{
		std::uint64_t attempted{ 0 };
		std::uint64_t recorded{ 0 };
		std::uint64_t droppedStopped{ 0 };
		std::uint64_t droppedEventLimit{ 0 };
		std::uint64_t droppedByteLimit{ 0 };
		std::uint64_t droppedFrameLimit{ 0 };
		std::uint64_t droppedTimeLimit{ 0 };
		std::uint64_t scopeOverflow{ 0 };
		std::uint64_t scopeMismatch{ 0 };
	};

	struct CaptureSnapshot
	{
		CollectorConfig config;
		std::uint64_t sessionGeneration{ 0 };
		std::uint64_t clockFrequencyHz{ 0 };
		std::uint64_t startTimestampTicks{ 0 };
		std::uint64_t endTimestampTicks{ 0 };
		StopReason stopReason{ StopReason::kRequested };
		CaptureStatistics statistics;
		std::vector<EventRecord> events;
	};

	class Collector
	{
	private:
		struct Session;

	public:
		class ScopeGuard
		{
		public:
			ScopeGuard() = default;
			~ScopeGuard();
			ScopeGuard(const ScopeGuard&) = delete;
			ScopeGuard& operator=(const ScopeGuard&) = delete;
			ScopeGuard(ScopeGuard&& a_other) noexcept;
			ScopeGuard& operator=(ScopeGuard&& a_other) noexcept;

			bool IsActive() const noexcept;
			std::uint64_t Token() const noexcept;

		private:
			friend class Collector;
			ScopeGuard(
				Collector* a_owner,
				std::uint64_t a_generation,
				ScopeKind a_kind,
				std::uint64_t a_token,
				EventKind a_endKind,
				EventPayload a_endPayload) noexcept;

			void Reset() noexcept;

			Collector* owner{ nullptr };
			std::uint64_t generation{ 0 };
			ScopeKind kind{ ScopeKind::kRenderPass };
			std::uint64_t token{ 0 };
			EventKind endKind{ EventKind::kCaptureMarker };
			EventPayload endPayload;
		};

		Collector();
		~Collector();
		Collector(const Collector&) = delete;
		Collector& operator=(const Collector&) = delete;

		StartResult Start(const CollectorConfig& a_config);
		std::optional<CaptureSnapshot> Stop(StopReason a_reason = StopReason::kRequested);
		bool IsCapturing() const noexcept;
		std::uint64_t ActiveGeneration() const noexcept;

		RecordResult Record(
			EventKind a_kind,
			const EventPayload& a_payload = {},
			std::uint64_t a_deviceContextObservationId = 0) noexcept;

		ScopeGuard EnterScope(
			ScopeKind a_kind,
			std::uint64_t a_observationId,
			EventKind a_beginKind,
			EventKind a_endKind,
			const EventPayload& a_beginPayload = {},
			const EventPayload& a_endPayload = {}) noexcept;

		std::uint64_t AllocateObservationId() noexcept;
		void SetThreadFrameContext(const FrameContext& a_context) noexcept;
		FrameContext GetThreadFrameContext() const noexcept;
		ScopeSnapshot GetThreadScopes() const noexcept;

		static constexpr std::size_t EventRecordSize() noexcept { return sizeof(EventRecord); }
		static std::uint64_t ClockFrequencyHz() noexcept;

	private:
		RecordResult RecordForGeneration(
			EventKind a_kind,
			const EventPayload& a_payload,
			std::uint64_t a_deviceContextObservationId,
			std::uint64_t a_expectedGeneration) noexcept;

		void ExitScope(
			std::uint64_t a_generation,
			ScopeKind a_kind,
			std::uint64_t a_token,
			EventKind a_endKind,
			const EventPayload& a_endPayload) noexcept;

		std::atomic<std::shared_ptr<Session>> activeSession;
	};
}
