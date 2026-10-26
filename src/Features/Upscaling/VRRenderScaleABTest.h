#pragma once

#ifdef VR_RENDERSCALE_AB_TEST_ENABLED
#	include <array>
#	include <atomic>
#	include <cstddef>
#	include <cstdint>
#	include <limits>

namespace VRRenderScaleABTest
{
	static_assert(std::atomic_uint64_t::is_always_lock_free);
	static_assert(std::atomic_uint32_t::is_always_lock_free);
	enum class Condition : std::uint32_t
	{
		Baseline,
		BypassMenuDraws,
		RestoreNativePasses,
		Both,
		Count
	};

	inline constexpr std::array<const char*, 4> kNames{
		"baseline", "bypass_menu_draws", "restore_native_passes", "both"
	};
	inline constexpr std::array<const char*, 4> kLabels{
		"A: Baseline", "B: Bypass menu-draw checks",
		"C: Restore native upsample passes", "D: Both changes"
	};

	struct Counts
	{
		std::uint64_t frames = 0;
		std::uint64_t menuHookFrames = 0;
		std::uint64_t nativeCandidates = 0;
		std::uint64_t nativeRestored = 0;
		std::uint64_t nativeReplaced = 0;
	};

	/** Session-only requests latch once per rendered frame; menus suspend overrides. */
	class State
	{
	public:
		[[nodiscard]] Condition Requested() const noexcept { return requested.load(std::memory_order_relaxed); }
		[[nodiscard]] bool NeedsFrame(std::uint32_t a_frame) const noexcept { return frame.load(std::memory_order_relaxed) != a_frame; }
		bool Request(Condition a_condition) noexcept
		{
			if (a_condition >= Condition::Count)
				return false;
			requested.store(a_condition, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] std::uint64_t SuspensionEpoch() const noexcept { return suspensionEpoch.load(); }

		/** Render-thread boundary; repeated configure calls keep the same selection. */
		void BeginFrame(std::uint32_t a_frame, bool a_eligible, std::uint64_t a_suspensionEpoch) noexcept
		{
			if (frame.load(std::memory_order_relaxed) == a_frame)
				return;
			const auto condition = Requested();
			frame.store(a_frame, std::memory_order_relaxed);
			const auto value = (static_cast<std::uint64_t>(a_frame) << 3) |
			                   (static_cast<std::uint32_t>(condition) << 1) |
			                   static_cast<std::uint64_t>(a_eligible && SuspensionEpoch() == a_suspensionEpoch);
			active.store(value);
			if (SuspensionEpoch() != a_suspensionEpoch)
				active.fetch_and(~std::uint64_t{ 1 });
			if (Eligible(a_frame))
				counts[static_cast<std::size_t>(condition)].frames.fetch_add(1, std::memory_order_relaxed);
		}

		/** Menu events revoke the override without changing the requested condition. */
		void Suspend() noexcept
		{
			suspensionEpoch.fetch_add(1);
			active.fetch_and(~std::uint64_t{ 1 });
		}

		[[nodiscard]] bool Eligible(std::uint32_t a_frame) const noexcept { return (ForFrame(a_frame) & 1) != 0; }
		[[nodiscard]] Condition Applied(std::uint32_t a_frame) const noexcept
		{
			const auto value = ForFrame(a_frame);
			return (value & 1) ? static_cast<Condition>((value >> 1) & 3) : Condition::Baseline;
		}

		/** Counts reached gameplay frames, not draws; no per-draw logging or allocation. */
		bool ObserveMenuHook(std::uint32_t a_frame) noexcept
		{
			const auto value = ForFrame(a_frame);
			if (!(value & 1))
				return false;
			const auto index = static_cast<std::size_t>((value >> 1) & 3);
			auto& counter = counts[index];
			auto previous = counter.lastMenuFrame.load(std::memory_order_relaxed);
			if (previous != a_frame && counter.lastMenuFrame.compare_exchange_strong(previous, a_frame, std::memory_order_relaxed))
				counter.menuHookFrames.fetch_add(1, std::memory_order_relaxed);
			return (index & 1) != 0;
		}

		/** Called after ordinary eligibility checks, before touching D3D state. */
		bool ObserveNativeCandidate(std::uint32_t a_frame) noexcept
		{
			const auto value = ForFrame(a_frame);
			if (!(value & 1))
				return false;
			const auto index = static_cast<std::size_t>((value >> 1) & 3);
			counts[index].nativeCandidates.fetch_add(1, std::memory_order_relaxed);
			if (!(index & 2))
				return false;
			counts[index].nativeRestored.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void ObserveNativeReplacement(std::uint32_t a_frame) noexcept
		{
			const auto value = ForFrame(a_frame);
			if (value & 1)
				counts[(value >> 1) & 3].nativeReplaced.fetch_add(1, std::memory_order_relaxed);
		}

		/** Counters are cumulative and independently sampled, not a transactional snapshot. */
		[[nodiscard]] Counts Read(Condition a_condition) const noexcept
		{
			if (a_condition >= Condition::Count)
				return {};
			const auto& counter = counts[static_cast<std::size_t>(a_condition)];
			return { counter.frames.load(std::memory_order_relaxed),
				counter.menuHookFrames.load(std::memory_order_relaxed),
				counter.nativeCandidates.load(std::memory_order_relaxed),
				counter.nativeRestored.load(std::memory_order_relaxed),
				counter.nativeReplaced.load(std::memory_order_relaxed) };
		}

	private:
		[[nodiscard]] std::uint64_t ForFrame(std::uint32_t a_frame) const noexcept
		{
			const auto value = active.load(std::memory_order_relaxed);
			return (value >> 3) == a_frame ? value : 0;
		}
		struct Counters
		{
			std::atomic_uint64_t frames{}, menuHookFrames{}, nativeCandidates{}, nativeRestored{}, nativeReplaced{};
			std::atomic_uint32_t lastMenuFrame{ std::numeric_limits<std::uint32_t>::max() };
		};
		std::atomic<Condition> requested{ Condition::Baseline };
		std::atomic_uint32_t frame{ std::numeric_limits<std::uint32_t>::max() };
		std::atomic_uint64_t active{};
		std::atomic_uint64_t suspensionEpoch{};
		std::array<Counters, 4> counts{};
	};
}
#endif
