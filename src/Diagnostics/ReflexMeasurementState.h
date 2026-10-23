#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <cstdint>
#	include <mutex>
#	include <optional>
#	include <span>
#	include <stdexcept>
#	include <type_traits>
#	include <vector>

namespace CSX::Diagnostics::ReflexMeasurement
{
	inline constexpr uint32_t Schema = 1;
	inline constexpr uint32_t HistoryCapacity = 128;
	using Tick = int64_t;
	enum class Mode : uint8_t
	{
		Minimal,
		Passive,
		Queried
	};
	enum class Exit : uint8_t
	{
		Unavailable,
		Uninitialized,
		AdapterUnavailable,
		PluginUnavailable,
		FrameGeneration,
		Updated
	};
	struct Preferences
	{
		bool enabled{}, boost{}, capped{}, markers{};
		float fps = 60;
		bool operator==(const Preferences&) const = default;
	};

	/// A lease never overwrites preferences observed to differ from its last write.
	struct PreferenceLease
	{
		Preferences original{}, owned{};
		bool active{}, conflict{}, restoring{}, requestedRestored{}, acceptedRestored{};
		bool freshNativeSettings{}, wrotePreferences{}, unchangedReleased{};
		Tick restoreRequestedAt{}, acceptedAt{};
		uint64_t acceptedGeneration{};
		void Acquire(Preferences current)
		{
			if (active)
				throw std::logic_error("preference lease already active");
			*this = {};
			original = owned = current;
			active = true;
		}
		void Observe(Preferences current)
		{
			if (active && current != owned)
				conflict = true;
		}
		bool Write(Preferences current, Preferences next)
		{
			Observe(current);
			if (!active || conflict || restoring)
				return false;
			owned = next;
			wrotePreferences = true;
			return true;
		}
		bool ReleaseUnchanged(Preferences current, Tick now)
		{
			Observe(current);
			if (wrotePreferences)
				return false;
			if (conflict) {
				active = false;
				return true;
			}
			if (!active)
				return unchangedReleased;
			requestedRestored = unchangedReleased = true;
			restoreRequestedAt = now;
			active = false;
			return true;
		}
		bool Restore(Preferences current, Tick now)
		{
			Observe(current);
			if (conflict) {
				active = false;
				return false;
			}
			if (!active)
				return false;
			if (!restoring) {
				owned = original;
				restoring = requestedRestored = true;
				restoreRequestedAt = now;
			}
			return true;
		}
		void Confirm(Tick now, uint64_t generation, bool effectiveAccepted, bool freshSettings = false)
		{
			if (active && restoring && !conflict && now > restoreRequestedAt && effectiveAccepted) {
				acceptedRestored = true;
				acceptedAt = now;
				acceptedGeneration = generation;
				freshNativeSettings = freshSettings;
				active = false;
			}
		}
	};

	struct BackendContext
	{
		bool observed{}, pending{}, desiredValid{}, acceptedValid{};
		uint32_t health{}, operation{}, desiredMode{}, acceptedMode{}, desiredCapUs{}, acceptedCapUs{}, failures{};
		int32_t result{};
		uint64_t generation{}, uptimeMs{}, retryAtMs{};
		bool attemptsObserved{}, settingsAttempted{}, sleepAttempted{};
		uint32_t sleepSkipReason{};
	};
	struct AppRecord
	{
		Tick enter{}, exit{};
		uint32_t frame{}, thread{};
		bool frameAvailable{};
		Exit reason{};
		BackendContext backend{};
		uint64_t sequence{};
		bool completed{};
	};
	struct NativeRecord
	{
		Tick enter{}, exit{};
		uint64_t generation{};
		uintptr_t device{};
		uint32_t frame{};
		bool frameAvailable{}, settings{};
		int32_t result{};
		uint32_t version{}, size{}, lowLatency{}, boost{}, intervalUs{}, markers{}, minQueue{};
		Tick admitted{};
		bool inheritedAdmission{};
		Tick operationEnter{};
		uint64_t applicationSequence{}, sequence{};
		bool completed{};
	};
	struct CompositorRecord
	{
		Tick retrieved{};
		uint32_t frame{}, epoch{}, presents{}, mispresented{}, dropped{}, reprojectionFlags{};
		double systemSeconds{};
		float clientMs{}, preSubmitGpuMs{}, postSubmitGpuMs{}, totalGpuMs{}, compositorCpuMs{},
			waitGetPosesCalledMs{}, newPosesReadyMs{}, newFrameReadyMs{}, compositorUpdateEndMs{};
		uint32_t sourceStructureSize{};
		float compositorGpuMs{}, compositorIdleCpuMs{}, presentCallCpuMs{}, waitForPresentCpuMs{}, submitFrameMs{},
			compositorUpdateStartMs{}, compositorRenderStartMs{};
		std::array<float, 12> hmdTransform{};
		std::array<float, 3> hmdLinearVelocity{}, hmdAngularVelocity{};
		int32_t trackingResult{};
		bool poseValid{}, hmdConnected{};
	};
	struct Quality
	{
		uint64_t overflow{}, contention{}, unfinished{}, appGaps{}, appDuplicates{}, invalidClocks{},
			invalidFrames{}, sourceLost{}, resets{}, invalidCompositor{}, sourceFailures{}, missingCompletions{};
		bool missingTail{}, postrollTimedOut{};
		bool Complete() const
		{
			return !(overflow || contention || unfinished || appGaps || appDuplicates || invalidClocks ||
					 invalidFrames || sourceLost || resets || invalidCompositor || sourceFailures || missingCompletions || missingTail || postrollTimedOut);
		}
	};

	/// Tracks source loss independently of destination capacity and other VR observers.
	class CompositorCursor
	{
	public:
		bool seeded{};
		uint32_t last{}, latest{}, epoch{};
		double lastTime{};
		std::optional<uint32_t> stopTarget;
		Quality quality{};
		template <class Emit>
		void Consume(std::span<const CompositorRecord> history, Emit emit)
		{
			if (history.size() <= 2 || history.size() > HistoryCapacity) {
				++quality.sourceFailures;
				return;
			}
			const auto newest = history.back().frame;
			if (seeded && newest < latest) {
				++quality.resets;
				++epoch;
				seeded = false;
			}
			latest = newest;
			// OpenVR's newest two entries can still receive completion fields.
			if (newest < 2)
				return;
			const auto completed = newest - 2;
			std::array<const CompositorRecord*, HistoryCapacity> ordered{};
			size_t count{};
			for (const auto& row : history)
				if (row.frame <= completed)
					ordered[count++] = &row;
			// Separate reads can straddle a runtime advance; frame IDs define completion and order.
			std::sort(ordered.begin(), ordered.begin() + count, [](auto* a, auto* b) { return a->frame < b->frame; });
			if (!seeded) {
				if (count && ordered[count - 1]->frame == completed &&
					std::isfinite(ordered[count - 1]->systemSeconds) && ordered[count - 1]->systemSeconds > 0) {
					seeded = true;
					last = completed;
					lastTime = ordered[count - 1]->systemSeconds;
				}
				return;
			}
			for (size_t i = 0; i < count; ++i) {
				auto row = *ordered[i];
				if (i && row.frame == ordered[i - 1]->frame && row.systemSeconds != ordered[i - 1]->systemSeconds)
					++quality.invalidCompositor;
				if (row.frame <= last || row.frame > completed || (stopTarget && row.frame > *stopTarget))
					continue;
				if (row.frame > last + 1)
					quality.sourceLost += row.frame - last - 1;
				if (!std::isfinite(row.systemSeconds) || row.systemSeconds <= lastTime || row.systemSeconds <= 0)
					++quality.invalidCompositor;
				if (!std::isfinite(row.clientMs) || row.clientMs <= 0)
					++quality.invalidCompositor;
				if (row.retrieved <= 0)
					++quality.invalidClocks;
				last = row.frame;
				lastTime = row.systemSeconds;
				row.epoch = epoch;
				emit(row);
			}
		}
		void Close()
		{
			if (seeded)
				stopTarget = latest;
		}
		bool Drained() const { return stopTarget && last >= *stopTarget; }
	};

	/// Preallocated journals admit without blocking render threads or allocating.
	class Journal
	{
	public:
		struct Ticket
		{
			Tick enter{};
			bool admitted{};
			uint64_t epoch{};
			bool inherited{};
			uint64_t sequence{}, parentSequence{};
		};
		mutable std::mutex mutex;
		static constexpr uint64_t AdmissionBit = uint64_t{ 1 } << 63;
		static constexpr uint64_t CountMask = 0xffffffff, EpochMask = ~(AdmissionBit | CountMask);
		std::atomic<uint64_t> contention{}, gate{};
		std::atomic<uint64_t> sequence{};
		std::atomic_bool active{};
		std::atomic_bool nativeEnabled{};
		uint64_t Outstanding() const { return gate.load(std::memory_order_acquire) & CountMask; }
		Mode mode{};
		uint64_t id{};
		Tick start{}, stop{}, freeze{};
		bool frozen{};
		Quality quality{};
		std::vector<AppRecord> app;
		std::vector<NativeRecord> native;
		std::vector<AppRecord> appStarted;
		std::vector<NativeRecord> nativeStarted;
		size_t appLimit{}, nativeLimit{};
		std::vector<CompositorRecord> compositor;
		void Start(Mode selected, size_t capacity, Tick now, bool open = true)
		{
			std::scoped_lock lock(mutex);
			if (active || Outstanding() || (!frozen && id))
				throw std::logic_error("capture still draining");
			if (capacity < 16 || capacity > 262144 || now <= 0)
				throw std::invalid_argument("invalid capacity or clock");
			if (id == 0x7fffffff)
				throw std::overflow_error("capture epoch exhausted");
			app.clear();
			native.clear();
			compositor.clear();
			appStarted.clear();
			nativeStarted.clear();
			app.reserve(capacity * 2);
			native.reserve(capacity * 4);
			compositor.reserve(capacity);
			appStarted.reserve(capacity);
			nativeStarted.reserve(capacity * 2);
			appLimit = capacity;
			nativeLimit = capacity * 2;
			quality = {};
			contention = 0;
			mode = selected;
			sequence = 0;
			start = open ? now : 0;
			stop = freeze = 0;
			frozen = false;
			++id;
			nativeEnabled = open && selected != Mode::Minimal;
			gate.store((id << 32) | (open ? AdmissionBit : 0), std::memory_order_release);
			active.store(open, std::memory_order_release);
		}
		void Open(Tick now)
		{
			std::scoped_lock lock(mutex);
			if (start || frozen || !id || now <= 0)
				throw std::logic_error("capture cannot open");
			start = now;
			nativeEnabled = mode != Mode::Minimal;
			gate.store((id << 32) | AdmissionBit, std::memory_order_release);
			active.store(true, std::memory_order_release);
		}
		Ticket Begin(Tick now, bool nativeOperation = false)
		{
			return Admit(now, nativeOperation, gate.load(std::memory_order_acquire));
		}
		Ticket Admit(Tick now, bool nativeOperation, uint64_t observed)
		{
			if (nativeOperation && !nativeEnabled.load(std::memory_order_acquire))
				return {};
			const auto epoch = observed & EpochMask;
			while ((observed & AdmissionBit) && (observed & EpochMask) == epoch) {
				if (gate.compare_exchange_weak(observed, observed + 1, std::memory_order_acq_rel))
					break;
			}
			if (!(observed & AdmissionBit) || (observed & EpochMask) != epoch)
				return {};
			return { now, true, epoch, false, ++sequence, 0 };
		}
		Ticket AdmitChild(Ticket parent)
		{
			if (!parent.admitted || mode == Mode::Minimal)
				return {};
			auto observed = gate.load(std::memory_order_acquire);
			while ((observed & EpochMask) == parent.epoch && (observed & CountMask)) {
				if (gate.compare_exchange_weak(observed, observed + 1, std::memory_order_acq_rel))
					return { parent.enter, true, parent.epoch, true, ++sequence, parent.sequence };
			}
			return {};
		}
		template <class ReadClock>
		Ticket BeginTimed(ReadClock read, bool nativeOperation = false)
		{
			const auto observed = gate.load(std::memory_order_acquire);
			if (!(observed & AdmissionBit) ||
				(nativeOperation && !nativeEnabled.load(std::memory_order_acquire)))
				return {};
			return Admit(read(), nativeOperation, observed);
		}
		template <class Row>
		void RecordStart(Ticket ticket, Row row, std::vector<Row>& destination, size_t limit)
		{
			if (!ticket.admitted)
				return;
			row.sequence = ticket.sequence;
			row.completed = false;
			std::unique_lock lock(mutex, std::try_to_lock);
			if (!lock)
				++contention;
			else if (!frozen) {
				if (destination.size() >= limit)
					++quality.overflow;
				else
					destination.push_back(row);
			}
		}
		template <class Row>
		void End(Ticket ticket, Row row, std::vector<Row>& destination, size_t limit = 0)
		{
			if (!ticket.admitted)
				return;
			row.sequence = ticket.sequence;
			row.completed = true;
			if (!limit) {
				if constexpr (std::is_same_v<Row, AppRecord>)
					limit = appLimit;
				else if constexpr (std::is_same_v<Row, NativeRecord>)
					limit = nativeLimit;
				else
					limit = destination.capacity();
			}
			std::unique_lock lock(mutex, std::try_to_lock);
			if (!lock)
				++contention;
			else if (frozen) { /* The frozen receipt already accounts for outstanding tickets. */
			} else if (destination.size() >= limit)
				++quality.overflow;
			else {
				if (row.enter <= 0 || row.exit < row.enter)
					++quality.invalidClocks;
				destination.push_back(row);
			}
			gate.fetch_sub(1, std::memory_order_release);
		}
		template <class ReadClock>
		void CloseTimed(ReadClock read)
		{
			gate.fetch_and(~AdmissionBit, std::memory_order_acq_rel);
			nativeEnabled = false;
			std::scoped_lock lock(mutex);
			active = false;
			if (!stop && id && !frozen)
				stop = read();
		}
		void Close(Tick now)
		{
			CloseTimed([now] { return now; });
		}
		template <class Row>
		void MergeStarted(std::vector<Row>& records, const std::vector<Row>& started)
		{
			std::vector<uint64_t> completed;
			completed.reserve(records.size());
			for (const auto& row : records) completed.push_back(row.sequence);
			std::sort(completed.begin(), completed.end());
			for (const auto& row : started) {
				if (std::binary_search(completed.begin(), completed.end(), row.sequence))
					continue;
				++quality.missingCompletions;
				if (records.size() == records.capacity())
					++quality.overflow;
				else
					records.push_back(row);
			}
		}
		template <class Finalize>
		void FreezeWith(Tick now, bool tailComplete, bool timedOut, Finalize finalize)
		{
			std::scoped_lock lock(mutex);
			if (active)
				throw std::logic_error("admission must close before freeze");
			if (frozen)
				return;
			MergeStarted(app, appStarted);
			MergeStarted(native, nativeStarted);
			finalize();
			quality.unfinished += Outstanding();
			if (start <= 0 || stop < start || now < stop)
				++quality.invalidClocks;
			quality.contention = contention;
			quality.missingTail = !tailComplete;
			quality.postrollTimedOut = timedOut;
			freeze = now;
			frozen = true;
			std::sort(app.begin(), app.end(), [](auto& a, auto& b) { return a.enter < b.enter; });
			for (size_t i = 0; i < app.size(); ++i) {
				if (!app[i].frameAvailable)
					++quality.invalidFrames;
				if (!i)
					continue;
				if (app[i].enter <= app[i - 1].enter)
					++quality.invalidClocks;
				if (app[i].frame == app[i - 1].frame)
					++quality.appDuplicates;
				else if (app[i].frame < app[i - 1].frame)
					++quality.invalidFrames;
				else
					quality.appGaps += app[i].frame - app[i - 1].frame - 1;
			}
		}
		void Freeze(Tick now, bool tailComplete, bool timedOut)
		{
			FreezeWith(now, tailComplete, timedOut, [] {});
		}
	};
}
#endif
