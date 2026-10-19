#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <atomic>
#	include <array>
#	include <cstdint>
#	include <limits>
#	include <mutex>
#	include <optional>
#	include <vector>

namespace ReflexDiagnostics
{
	struct Options
	{
		int mode = 0;
		uint32_t frameLimitUs = 0;
		bool markers = false;
	};

	struct Record
	{
		uint64_t sessionId = 0, sequence = 0, tick = 0, intervalTicks = 0;
		uint32_t frame = 0, threadId = 0;
		bool initialized = false, supported = false, featureLoaded = false, pclLoaded = false;
		bool setOptionsBound = false, sleepBound = false, getStateBound = false;
		bool markersRequested = false, capRequested = false, boostRequested = false;
		float requestedFPS = 0;
		Options requested, applied;
		std::optional<Options> resolved;
		bool appliedValid = false, blockedByFrameGeneration = false, frameGenerationQuarantined = false;
		uint64_t optionsSequence = 0;
		std::optional<int32_t> lastOptionsResult, sleepResult, stateResult;
		uint64_t sleepTicks = 0, stateTicks = 0, stateTick = 0;
		std::optional<bool> lowLatencyAvailable, latencyReportAvailable;
		std::optional<uint64_t> tokenAddress;
		bool queryState = false;
		const char* reason = "not_reached";
		bool contextAvailable = false, paused = false, menuOpen = false, compiling = false;
		std::optional<float> gameHour;
		uint32_t cellId = 0;
		std::array<float, 3> playerPosition{};
		std::optional<int32_t> driverStatus;
		std::optional<int32_t> nativeInterfaceResult;
		std::optional<bool> driverLowLatency, driverGameSleep, driverVrr, driverVsync, driverIFlip;
		std::optional<uint32_t> driverSleepUs, driverFrameGeneration;
		struct GpuTelemetry
		{
			uint32_t index = 0;
			std::optional<int32_t> clockResult, utilizationResult, pstateResult;
			std::optional<uint32_t> graphicsKHz, memoryKHz, utilizationPercent, pstate;
		};
		std::array<GpuTelemetry, 4> gpuTelemetry{};
		uint32_t gpuCount = 0;
		std::optional<int32_t> gpuEnumerationResult;
		uint64_t driverQueryTicks = 0, queryEndTick = 0, observerTicks = 0;
		uint64_t driverDevice = 0, rendererDevice = 0, streamlineDevice = 0;
		bool compositorQuery = false, compositorFault = false;
		uint64_t compositorQueryTicks = 0;
		struct Compositor
		{
			uint32_t frame = 0, presents = 0, mispresented = 0, dropped = 0, reprojection = 0;
			double systemTime = 0;
			float clientMs = 0, gpuMs = 0, preGpuMs = 0, postGpuMs = 0;
			float compositorGpuMs = 0, compositorCpuMs = 0, compositorIdleMs = 0;
			float presentMs = 0, waitPresentMs = 0, submitMs = 0;
			float waitPosesMs = 0, posesReadyMs = 0, frameReadyMs = 0;
			float compositorUpdateStartMs = 0, compositorUpdateEndMs = 0, compositorRenderStartMs = 0;
			std::array<float, 12> hmdPose{};
			bool poseValid = false;
		};
		std::optional<Compositor> compositor;
		std::array<Compositor, 16> compositorHistory{};
		uint32_t compositorCount = 0;
	};

	/** Bounded CPU journal. Sampling never waits for a DevBench reader. */
	template <std::size_t Capacity = 16384>
	class Capture
	{
	public:
		static_assert(Capacity > 0);
		struct Totals
		{
			uint64_t sleepCalls = 0, sleepFailures = 0, sleepTicks = 0, skipped = 0;
			uint64_t stateQueries = 0, stateFailures = 0, lowLatencyUnavailable = 0;
		};
		struct Snapshot
		{
			bool active = false;
			uint64_t sessionId = 0, startTick = 0, deadlineTick = 0, frequency = 0;
			uint64_t retained = 0, dropped = 0, inFlight = 0;
			bool queryState = true;
			const char* stopReason = "never_started";
			std::vector<Record> records;
			Totals totals;
		};
		/** Start refuses to replace unread evidence or an in-flight producer. */
		bool Start(uint64_t tick, uint64_t frequency, uint32_t seconds, bool queryState)
		{
			std::scoped_lock lock(mutex_);
			if (sessionId_ || inFlight_ || !frequency || seconds < 1 || seconds > 120 ||
				frequency > (UINT64_MAX - tick) / seconds || nextId_ == UINT64_MAX)
				return false;
			records_.clear();
			records_.reserve(Capacity);
			sessionId_ = ++nextId_;
			startTick_ = tick;
			deadlineTick_ = tick + frequency * seconds;
			frequency_ = frequency;
			queryState_ = queryState;
			nextQueryTick_ = tick;
			previousTick_ = 0;
			dropped_ = 0;
			totals_ = {};
			stopReason_ = "capturing";
			active_.store(true, std::memory_order_release);
			return true;
		}

		/** Called only by the existing Reflex hook; failed admission leaves no lease. */
		bool Begin(uint64_t tick, Record& record)
		{
			if (!active_.load(std::memory_order_acquire))
				return false;
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock()) {
				++dropped_;
				return false;
			}
			Expire(tick);
			if (!active_)
				return false;
			if (inFlight_ != 0 || tick < startTick_) {
				++dropped_;
				return false;
			}
			record.sessionId = sessionId_;
			record.tick = tick;
			record.intervalTicks = previousTick_ ? tick - previousTick_ : 0;
			previousTick_ = tick;
			record.queryState = queryState_ && tick >= nextQueryTick_;
			if (record.queryState)
				nextQueryTick_ = tick + frequency_;
			++inFlight_;
			return true;
		}

		/** A stopped session drains its admitted sample before reset is allowed. */
		void Finish(Record& record)
		{
			// Pace from query completion even when a reader prevents retaining this sample.
			if (record.stateResult)
				nextQueryTick_.store(std::max(nextQueryTick_.load(), record.stateTick + record.stateTicks + frequency_));
			if (record.queryEndTick)
				nextQueryTick_.store(std::max(nextQueryTick_.load(), record.queryEndTick + frequency_));
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock()) {
				++dropped_;
			} else if (record.sessionId != sessionId_ || records_.size() >= Capacity) {
				++dropped_;
			} else {
				record.sequence = records_.size() + 1;
				records_.push_back(record);
				if (record.sleepResult) {
					++totals_.sleepCalls;
					totals_.sleepFailures += *record.sleepResult != 0;
					totals_.sleepTicks += record.sleepTicks;
				} else {
					++totals_.skipped;
				}
				if (record.stateResult) {
					++totals_.stateQueries;
					totals_.stateFailures += *record.stateResult != 0;
					totals_.lowLatencyUnavailable += record.lowLatencyAvailable == false;
				}
				if (records_.size() == Capacity) {
					active_ = false;
					stopReason_ = "capacity";
				}
			}
			--inFlight_;
		}

		bool Stop(uint64_t expectedSession)
		{
			std::scoped_lock lock(mutex_);
			if (!expectedSession || expectedSession != sessionId_)
				return false;
			if (active_.exchange(false))
				stopReason_ = "requested";
			return true;
		}

		bool Reset(uint64_t expectedSession)
		{
			std::scoped_lock lock(mutex_);
			if (!expectedSession || sessionId_ != expectedSession || active_ || inFlight_)
				return false;
			sessionId_ = 0;
			records_.clear();
			startTick_ = deadlineTick_ = frequency_ = 0;
			dropped_ = 0;
			totals_ = {};
			stopReason_ = "reset";
			return true;
		}

		/** Copies at most one page under the lock; JSON serialization happens afterward. */
		Snapshot Read(uint64_t tick, uint64_t after = 0, std::size_t limit = 0)
		{
			std::scoped_lock lock(mutex_);
			Expire(tick);
			Snapshot snapshot{ active_, sessionId_, startTick_, deadlineTick_, frequency_,
				records_.size(), dropped_, inFlight_, queryState_, stopReason_, {}, totals_ };
			const auto first = static_cast<std::size_t>(std::min<uint64_t>(after, records_.size()));
			const auto count = std::min({ limit, std::size_t{ 256 }, records_.size() - first });
			snapshot.records.assign(records_.begin() + first, records_.begin() + first + count);
			return snapshot;
		}

	private:
		void Expire(uint64_t tick)
		{
			if (active_ && tick >= deadlineTick_) {
				active_ = false;
				stopReason_ = "duration";
			}
		}
		std::mutex mutex_;
		std::atomic_bool active_{ false };
		std::atomic_uint64_t dropped_{ 0 }, inFlight_{ 0 };
		std::atomic_uint64_t nextQueryTick_{ 0 };
		uint64_t sessionId_ = 0, nextId_ = 0, startTick_ = 0, deadlineTick_ = 0;
		uint64_t frequency_ = 0, previousTick_ = 0;
		bool queryState_ = true;
		const char* stopReason_ = "never_started";
		std::vector<Record> records_;
		Totals totals_;
	};
}
#endif
