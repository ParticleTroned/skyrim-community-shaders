#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <atomic>
#	include <cstdint>
#	include <mutex>
#	include <vector>

namespace ReflexDiagnostics::Presentation
{
	struct PresentRecord
	{
		uint64_t session = 0, sequence = 0, entry = 0, begin = 0, end = 0, observerTicks = 0;
		uint64_t swap = 0, window = 0, foreground = 0;
		uint32_t completedFrame = 0, thread = 0, sync = 0, flags = 0;
		int32_t result = 0;
		bool returned = false, windowValid = false, visible = false, minimized = false;
	};
	/** Independent Present producers may overlap; retained evidence is never overwritten. */
	template <size_t Capacity = 32768>
	class PresentJournal
	{
	public:
		struct Snapshot
		{
			uint64_t session = 0, start = 0, deadline = 0, dropped = 0, inFlight = 0, retained = 0;
			bool active = false;
			std::vector<PresentRecord> records;
		};
		bool Start(uint64_t session, uint64_t start, uint64_t deadline)
		{
			std::scoped_lock lock(mutex_);
			if (active_ || inFlight_ || !session || deadline <= start)
				return false;
			records_.clear();
			records_.reserve(Capacity);
			session_ = session;
			start_ = start;
			deadline_ = deadline;
			dropped_ = 0;
			active_ = true;
			return true;
		}
		bool Begin(uint64_t tick, PresentRecord& record)
		{
			if (!active_)
				return false;
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock()) {
				++dropped_;
				return false;
			}
			Expire(tick);
			if (!active_)
				return false;
			if (tick < start_ || records_.size() + inFlight_ >= Capacity) {
				++dropped_;
				return false;
			}
			record.session = session_;
			record.entry = tick;
			++inFlight_;
			return true;
		}
		void Finish(PresentRecord& record)
		{
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock.owns_lock() || record.session != session_) {
				++dropped_;
			} else {
				record.sequence = records_.size() + 1;
				records_.push_back(record);
			}
			--inFlight_;
		}
		void Stop()
		{
			std::scoped_lock lock(mutex_);
			active_ = false;
		}
		bool Drained() const { return !inFlight_.load(); }
		Snapshot Read(uint64_t tick, uint64_t after, size_t limit)
		{
			std::scoped_lock lock(mutex_);
			Expire(tick);
			Snapshot result{ session_, start_, deadline_, dropped_, inFlight_, records_.size(), active_, {} };
			const auto first = static_cast<size_t>(std::min<uint64_t>(after, records_.size()));
			const auto count = std::min({ limit, size_t{ 256 }, records_.size() - first });
			result.records.assign(records_.begin() + first, records_.begin() + first + count);
			return result;
		}

	private:
		void Expire(uint64_t tick)
		{
			if (tick >= deadline_)
				active_ = false;
		}
		std::mutex mutex_;
		std::atomic_bool active_{ false };
		std::atomic_uint64_t inFlight_{ 0 }, dropped_{ 0 };
		uint64_t session_ = 0, start_ = 0, deadline_ = 0;
		std::vector<PresentRecord> records_;
	};
}
#endif
