#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cstdint>
#	include <string_view>
#	include <thread>

namespace CSX::Diagnostics::Stutters
{
	using Tick = std::int64_t;
	enum class Boundary : unsigned
	{
		None,
		Engine,
		Present,
		Compositor,
		Count
	};
	inline constexpr unsigned kThreadCapacity = 16, kScopeDepth = 16;
	struct Context
	{
		Tick qpc = 0;
		std::uint32_t frame = 0, cell = 0;
		std::array<float, 3> position{};
		bool playerAvailable = false, loading = false, paused = false, compiling = false;
	};
	struct Phase
	{
		std::array<char, 96> name{};
		Tick begin = 0;
		std::uint32_t thread = 0;
		std::uint64_t id = 0, epoch = 0, lossVersion = 0;
		Context context;
		bool nameTruncated = false;
	};
	struct Event
	{
		Phase phase;
		Tick end = 0;
		bool gap = false, complete = false;
	};
	struct Token
	{
		Phase phase;
		unsigned slot = kThreadCapacity;
	};
	struct Batch
	{
		std::array<Phase, kThreadCapacity> active{}, overdue{};
		std::array<Phase, static_cast<unsigned>(Boundary::Count)> boundaries{};
		std::array<Event, 256> events{};
		Context context;
		unsigned count = 0;
		std::uint64_t lostUpdates = 0, droppedEvents = 0;
	};

	/** Fixed storage; hooks make one lock attempt, never allocate, and cannot throw. */
	class Recorder
	{
	public:
		void Start(Tick threshold, Tick now, Tick deadline = 0) noexcept
		{
			Stop();
			Lock();
			++epoch_;
			data_ = {};
			slots_ = {};
			losses_.store(0);
			threshold_ = threshold;
			started_ = now;
			deadline_ = deadline;
			enabled_.store(threshold > 0 && now > 0 && (!deadline || deadline > now), std::memory_order_release);
			Unlock();
		}
		void Stop() noexcept { enabled_.store(false, std::memory_order_release); }
		bool Enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
		std::uint64_t LostUpdates() const noexcept { return losses_.load(std::memory_order_relaxed); }
		std::uint64_t Epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
		Token Begin(std::uint32_t thread, std::string_view name, Tick now, Boundary boundary = Boundary::None) noexcept
		{
			if (!Enabled())
				return {};
			const auto epoch = Epoch();
			if (!thread || now <= 0 || name.empty() || boundary >= Boundary::Count) {
				++losses_;
				return {};
			}
			if (lock_.test_and_set(std::memory_order_acquire)) {
				++losses_;
				return {};
			}
			if (epoch == Epoch() && deadline_ && now >= deadline_)
				Stop();
			if (!Enabled() || epoch != Epoch() || now < started_) {
				Unlock();
				return {};
			}
			unsigned index = kThreadCapacity, free = kThreadCapacity;
			for (unsigned i = 0; i < slots_.size(); ++i) {
				Prune(i);
				if (slots_[i].owner == thread) {
					index = i;
					break;
				}
				if (!slots_[i].depth && !HasBoundary(slots_[i].owner))
					free = i;
			}
			if (index == kThreadCapacity)
				index = free;
			if (index == kThreadCapacity || slots_[index].depth == kScopeDepth) {
				++losses_;
				Unlock();
				return {};
			}
			auto& slot = slots_[index];
			slot.owner = thread;
			Phase phase{ .begin = now, .thread = thread, .id = ++nextId_, .epoch = epoch, .lossVersion = losses_.load(), .context = data_.context };
			phase.nameTruncated = name.size() >= phase.name.size();
			std::copy_n(name.begin(), std::min(name.size(), phase.name.size() - 1), phase.name.begin());
			slot.phases[slot.depth++] = phase;
			if (boundary != Boundary::None) {
				auto& previous = data_.boundaries[static_cast<unsigned>(boundary)];
				if (previous.id && previous.thread == thread && now >= previous.begin && now - previous.begin >= threshold_)
					Push({ previous, now, true, previous.lossVersion == losses_.load() });
				previous = phase;
			}
			Unlock();
			return { phase, index };
		}
		void End(const Token& token, Tick now) noexcept
		{
			if (!Valid(token) || !Enabled() || token.phase.epoch != Epoch())
				return;
			if (now < token.phase.begin || lock_.test_and_set(std::memory_order_acquire)) {
				InvalidateExit(token);
				return;
			}
			if (token.phase.epoch == Epoch() && deadline_ && now >= deadline_)
				Stop();
			if (Enabled() && token.phase.epoch == Epoch()) {
				auto& slot = slots_[token.slot];
				Prune(token.slot);
				if (slot.owner == token.phase.thread && slot.depth && slot.phases[slot.depth - 1].id == token.phase.id) {
					--slot.depth;
					if (now - token.phase.begin >= threshold_)
						Push({ token.phase, now, false, token.phase.lossVersion == losses_.load() });
				} else if (slot.owner == token.phase.thread) {
					for (unsigned i = 0; i < slot.depth; ++i)
						if (slot.phases[i].id == token.phase.id) {
							auto invalid = token;
							invalid.phase.id = slot.phases[slot.depth - 1].id;
							InvalidateExit(invalid);
							Prune(token.slot);
							break;
						}
				}
			}
			Unlock();
		}
		/** A lost exit invalidates older nested state on this slot, never another thread's scopes. */
		void InvalidateExit(const Token& token) noexcept
		{
			if (!Valid(token))
				return;
			auto& invalid = invalidThrough_[token.slot];
			auto value = invalid.load(std::memory_order_relaxed);
			while (value < token.phase.id && !invalid.compare_exchange_weak(value, token.phase.id, std::memory_order_release, std::memory_order_relaxed)) {}
			if (token.phase.epoch == Epoch() && Enabled())
				++losses_;
		}
		void PublishContext(const Context& context, std::uint64_t epoch) noexcept
		{
			if (!Enabled())
				return;
			if (lock_.test_and_set(std::memory_order_acquire)) {
				++losses_;
				return;
			}
			if (epoch == Epoch() && deadline_ && context.qpc >= deadline_)
				Stop();
			if (Enabled() && epoch == Epoch() && context.qpc >= started_)
				data_.context = context;
			Unlock();
		}
		/** Confirms overlap by capture and scope identity, including overdue outer scopes. */
		bool StillCurrent(const Phase& phase, bool boundary) noexcept
		{
			Lock();
			bool found = false;
			if (Enabled() && phase.id && phase.epoch == Epoch()) {
				if (boundary) {
					for (const auto& current : data_.boundaries) found |= current.id == phase.id;
				} else {
					for (unsigned i = 0; i < slots_.size(); ++i) {
						Prune(i);
						for (unsigned j = 0; j < slots_[i].depth; ++j) found |= slots_[i].phases[j].id == phase.id;
					}
				}
			}
			Unlock();
			return found;
		}
		Batch Drain(Tick now) noexcept
		{
			Lock();
			auto result = data_;
			for (unsigned i = 0; i < slots_.size(); ++i) {
				Prune(i);
				const auto& slot = slots_[i];
				result.active[i].thread = slot.owner;
				if (slot.depth)
					result.active[i] = slot.phases[slot.depth - 1];
				for (unsigned j = 0; j < slot.depth; ++j)
					if (now >= slot.phases[j].begin && now - slot.phases[j].begin >= threshold_)
						result.overdue[i] = slot.phases[j];
			}
			result.lostUpdates = losses_.load();
			data_.count = 0;
			Unlock();
			return result;
		}

	private:
		struct Slot
		{
			std::uint32_t owner = 0;
			unsigned depth = 0;
			std::array<Phase, kScopeDepth> phases{};
		};
		bool Valid(const Token& token) const noexcept { return token.slot < kThreadCapacity && token.phase.id && token.phase.thread; }
		bool HasBoundary(std::uint32_t owner) const noexcept
		{
			if (!owner)
				return false;
			for (const auto& phase : data_.boundaries)
				if (phase.thread == owner)
					return true;
			return false;
		}
		void Prune(unsigned index) noexcept
		{
			auto& slot = slots_[index];
			const auto invalid = invalidThrough_[index].load(std::memory_order_acquire);
			unsigned count = 0;
			for (unsigned i = 0; i < slot.depth; ++i)
				if (slot.phases[i].id > invalid)
					slot.phases[count++] = slot.phases[i];
			slot.depth = count;
		}
		void Push(const Event& event) noexcept
		{
			if (data_.count == data_.events.size())
				++data_.droppedEvents;
			else
				data_.events[data_.count++] = event;
		}
		void Lock() noexcept
		{
			while (lock_.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
		}
		void Unlock() noexcept { lock_.clear(std::memory_order_release); }
		std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
		std::atomic_bool enabled_{ false };
		std::atomic<std::uint64_t> losses_{ 0 }, epoch_{ 0 };
		std::array<std::atomic<std::uint64_t>, kThreadCapacity> invalidThrough_{};
		std::uint64_t nextId_ = 0;
		Tick threshold_ = 0, started_ = 0, deadline_ = 0;
		std::array<Slot, kThreadCapacity> slots_{};
		Batch data_{};
	};
}
#endif
